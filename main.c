/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*!
 *\brief XSecureLock.
 *
 *XSecureLock is an X11 screen lock utility designed with the primary goal of
 *security.
 */

#include <X11/X.h>           // for Window, None, CopyFromParent
#include <X11/Xatom.h>       // for XA_CARDINAL, XA_ATOM
#include <X11/Xlib.h>        // for XEvent, XMapRaised, XSelectInput
#include <X11/Xcms.h>        // for XcmsFailure
#include <X11/Xutil.h>       // for XLookupString
#include <X11/cursorfont.h>  // for XC_arrow
#include <X11/keysym.h>      // for XK_BackSpace, XK_Tab, XK_o
#include <fcntl.h>           // for fcntl, FD_CLOEXEC, F_GETFD
#include <locale.h>          // for NULL, setlocale, LC_CTYPE
#include <signal.h>          // for sigaction, raise, sa_handler
#include <stdio.h>           // for printf, size_t, snprintf
#include <stdlib.h>          // for exit, system, EXIT_FAILURE
#include <string.h>          // for memset, strcmp, strncmp
#include <sys/select.h>      // for select, timeval, fd_set, FD_SET
#include <sys/time.h>        // for gettimeofday
#include <time.h>            // for nanosleep, timespec
#include <unistd.h>          // for _exit, chdir, close, execvp

#ifdef HAVE_DPMS_EXT
#include <X11/Xmd.h>  // for BOOL, CARD16
#include <X11/extensions/dpms.h>
#include <X11/extensions/dpmsconst.h>  // for DPMSModeOff, DPMSModeStandby
#endif
#ifdef HAVE_XCOMPOSITE_EXT
#include <X11/extensions/Xcomposite.h>  // for XCompositeGetOverlayWindow

#include "incompatible_compositor.xbm"  // for incompatible_compositor_bits
#endif
#ifdef HAVE_XSCREENSAVER_EXT
#include <X11/extensions/saver.h>      // for ScreenSaverNotify, ScreenSave...
#include <X11/extensions/scrnsaver.h>  // for XScreenSaverQueryExtension
#endif
#ifdef HAVE_XF86MISC_EXT
#include <X11/extensions/xf86misc.h>  // for XF86MiscSetGrabKeysState
#endif
#ifdef HAVE_XFIXES_EXT
#include <X11/extensions/Xfixes.h>      // for XFixesQueryExtension, XFixesS...
#include <X11/extensions/shapeconst.h>  // for ShapeBounding
#endif

#include "auth_child.h"     // for KillAuthChildSigHandler, Want...
#include "env_settings.h"   // for GetIntSetting, GetExecutableP...
#include "logging.h"        // for Log, LogErrno
#include "mlock_page.h"     // for MLOCK_PAGE
#include "saver_child.h"    // for WatchSaverChild, KillAllSaver...
#include "unmap_all.h"      // for ClearUnmapAllWindowsState
#include "util.h"           // for explicit_bzero
#include "version.h"        // for git_version
#include "wait_pgrp.h"      // for WaitPgrp
#include "wm_properties.h"  // for SetWMProperties

/*! \brief How often (in times per second) to watch child processes.
 *
 * This defines the minimum frequency to call WatchChildren().
 */
#define WATCH_CHILDREN_HZ 10

/*! \brief Try to reinstate grabs in regular intervals.
 *
 * This will reinstate the grabs WATCH_CHILDREN_HZ times per second. This
 * appears to be required with some XScreenSaver hacks that cause XSecureLock to
 * lose MotionNotify events, but nothing else.
 */
#undef ALWAYS_REINSTATE_GRABS

/*! \brief Try to bring the grab window to foreground in regular intervals.
 *
 * Some desktop environments have transparent OverrideRedirect notifications.
 * These do not send a VisibilityNotify to this window, as some part still
 * shines through. As a workaround, this enables raising the grab window
 * periodically.
 */
#undef AUTO_RAISE

/*! \brief Show cursor during auth.
 *
 * If enabled, a mouse cursor is shown whenever the auth_window is mapped.
 *
 * Note that proper use of this also would require a way to forward click
 * events to the auth helper, which doesn't exist yet.
 */
#undef SHOW_CURSOR_DURING_AUTH

/*! \brief Exhaustive list of all mouse related X11 events.
 *
 * These will be selected for grab. It is important that this contains all
 * pointer event types, to not let any through to other applications.
 */
#define ALL_POINTER_EVENTS                                                   \
  (ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask | \
   PointerMotionMask | Button1MotionMask | Button2MotionMask |               \
   Button3MotionMask | Button4MotionMask | Button5MotionMask |               \
   ButtonMotionMask)

//! Private (possibly containing information about the user's password) data.
//  This data is locked to RAM using mlock() to avoid leakage to disk via swap.
struct {
  // The received X event.
  XEvent ev;
  // The decoded key press.
  char buf[16];
  KeySym keysym;
  // The length of the data in buf.
  int len;
} priv;
//! The name of the auth child to execute, relative to HELPER_PATH.
const char *auth_executable;
//! The name of the saver child to execute, relative to HELPER_PATH.
const char *saver_executable;
//! The command to run once screen locking is complete.
char *const *notify_command = NULL;
#ifdef HAVE_XCOMPOSITE_EXT
//! Do not use XComposite to cover transparent notifications.
int no_composite = 0;
//! Create an almost-fullscreen sized "obscurer window" against bad compositors.
int composite_obscurer = 0;
#endif
//! If set, we can start a new login session.
int have_switch_user_command = 0;
//! If set, we try to force grabbing by "evil" means.
int force_grab = 0;
//! If set, print window info about any "conflicting" windows to stderr.
int debug_window_info = 0;
//! If nonnegative, the time in seconds till we blank the screen explicitly.
int blank_timeout = -1;
//! The DPMS state to switch the screen to when blanking.
const char *blank_dpms_state = "off";
//! Whether to reset the saver module when auth closes.
int saver_reset_on_auth_close = 0;
//! Delay we should wait before starting mapping windows to let children run.
int saver_delay_ms = 0;
//! Whetever stopping saver when screen is blanked.
int saver_stop_on_blank = 0;

//! The PID of a currently running notify command, or 0 if none is running.
pid_t notify_command_pid = 0;

//! The time when we will blank the screen.
struct timeval time_to_blank;

//! Whether the screen is currently blanked by us.
int blanked = 0;

#ifdef HAVE_DPMS_EXT
//! Whether DPMS needs to be disabled when unblanking. Set when blanking.
int must_disable_dpms = 0;
#endif

//! If set by signal handler we should wake up and prompt for auth.
static volatile sig_atomic_t signal_wakeup = 0;

void ResetBlankScreenTimer(void) {
  if (blank_timeout < 0) {
    return;
  }
  gettimeofday(&time_to_blank, NULL);
  time_to_blank.tv_sec += blank_timeout;
}

void InitBlankScreen(void) {
  if (blank_timeout < 0) {
    return;
  }
  blanked = 0;
  ResetBlankScreenTimer();
}

void MaybeBlankScreen(Display *display) {
  if (blank_timeout < 0 || blanked) {
    return;
  }
  struct timeval now;
  gettimeofday(&now, NULL);
  if (now.tv_sec < time_to_blank.tv_sec ||
      (now.tv_sec == time_to_blank.tv_sec &&
       now.tv_usec < time_to_blank.tv_usec)) {
    return;
  }
  // Blank timer expired - blank the screen.
  blanked = 1;
  XForceScreenSaver(display, ScreenSaverActive);
  if (!strcmp(blank_dpms_state, "on")) {
    // Just X11 blanking.
    goto done;
  }
#ifdef HAVE_DPMS_EXT
  // If we get here, we want to do DPMS blanking.
  int dummy;
  if (!DPMSQueryExtension(display, &dummy, &dummy)) {
    Log("DPMS is unavailable and XSECURELOCK_BLANK_DPMS_STATE not on");
    goto done;
  }
  CARD16 state;
  BOOL onoff;
  DPMSInfo(display, &state, &onoff);
  if (!onoff) {
    // DPMS not active by user - so we gotta force it.
    must_disable_dpms = 1;
    DPMSEnable(display);
  }
  if (!strcmp(blank_dpms_state, "standby")) {
    DPMSForceLevel(display, DPMSModeStandby);
  } else if (!strcmp(blank_dpms_state, "suspend")) {
    DPMSForceLevel(display, DPMSModeSuspend);
  } else if (!strcmp(blank_dpms_state, "off")) {
    DPMSForceLevel(display, DPMSModeOff);
  } else {
    Log("XSECURELOCK_BLANK_DPMS_STATE not in standby/suspend/off/on");
  }
#else
  Log("DPMS is not compiled in and XSECURELOCK_BLANK_DPMS_STATE not on");
#endif
done:
  // Flush the output buffer so we turn off the display now and not a few ms
  // later.
  XFlush(display);
}

void ScreenNoLongerBlanked(Display *display) {
#ifdef HAVE_DPMS_EXT
  if (must_disable_dpms) {
    DPMSDisable(display);
    must_disable_dpms = 0;
    // Flush the output buffer so we turn on the display now and not a
    // few ms later. Makes our and X11's idle timer more consistent.
    XFlush(display);
  }
#endif
  blanked = 0;
}

void UnblankScreen(Display *display) {
  if (blanked) {
    XForceScreenSaver(display, ScreenSaverReset);
    ScreenNoLongerBlanked(display);
  }
  ResetBlankScreenTimer();
}

static void HandleSIGTERM(int signo) {
  KillAllSaverChildrenSigHandler(signo);  // Dirty, but quick.
  KillAuthChildSigHandler(signo);         // More dirty.
  explicit_bzero(&priv, sizeof(priv));
  raise(signo);
}

static void HandleSIGUSR2(int unused_signo) {
  (void)unused_signo;
  signal_wakeup = 1;
}

enum WatchChildrenState {
  //! Request saver child.
  WATCH_CHILDREN_NORMAL,
  //! Request no saver to run (DPMS!).
  WATCH_CHILDREN_SAVER_DISABLED,
  //! Request auth child.
  WATCH_CHILDREN_FORCE_AUTH
};

/*! \brief Watch the child processes, and bring them into the desired state.
 *
 * If the requested state is WATCH_CHILDREN_NORMAL and neither auth nor saver
 * child are running, the saver child will be spawned.
 *
 * If the requested state is WATCH_CHILDREN_SAVER_DISABLED, a possibly running
 * saver child will be killed.
 *
 * If the requested state is WATCH_CHILDREN_FORCE_AUTH, a possibly running saver
 * child will be killed, and an auth child will be spawned.
 *
 * If the auth child was already running, the stdinbuf is sent to the auth child
 * on standard input.
 *
 * \param state The request to perform.
 * \param stdinbuf Key presses to send to the auth child, if set.
 * \return If true, authentication was successful and the program should exit.
 */
int WatchChildren(Display *dpy, Window auth_win, Window saver_win,
                  enum WatchChildrenState state, const char *stdinbuf) {
  int want_auth = WantAuthChild(state == WATCH_CHILDREN_FORCE_AUTH);
  int auth_running = 0;

  // Note: want_auth is true whenever we WANT to run authentication, or it is
  // already running. It may have recently terminated, which we will notice
  // later.
  if (want_auth) {
    // Actually start the auth child, or notice termination.
    if (WatchAuthChild(auth_win, auth_executable,
                       state == WATCH_CHILDREN_FORCE_AUTH, stdinbuf,
                       &auth_running)) {
      // Auth performed successfully. Terminate the other children.
      WatchSaverChild(dpy, saver_win, 0, saver_executable, 0);
      // Now terminate the screen lock.
      return 1;
    }

    // If we wanted auth, but it's not running, auth just terminated. Unmap the
    // auth window and poke the screensaver so that it can reset any timeouts.
    if (!auth_running) {
      XUnmapWindow(dpy, auth_win);
      if (saver_reset_on_auth_close) {
        KillAllSaverChildrenSigHandler(SIGUSR1);
      }
    }
  }

  // Show the screen saver.
  WatchSaverChild(dpy, saver_win, 0, saver_executable,
                  state != WATCH_CHILDREN_SAVER_DISABLED);

  if (auth_running) {
    // While auth is running, we never blank.
    UnblankScreen(dpy);
  } else {
    // If no auth is running, permit blanking as per timer.
    MaybeBlankScreen(dpy);
  }

  // Do not terminate the screen lock.
  return 0;
}

/*! \brief Wake up the screen saver in response to a keyboard or mouse event.
 *
 * \return If true, authentication was successful, and the program should exit.
 */
int WakeUp(Display *dpy, Window auth_win, Window saver_win,
           const char *stdinbuf) {
  return WatchChildren(dpy, auth_win, saver_win, WATCH_CHILDREN_FORCE_AUTH,
                       stdinbuf);
}

/*! \brief An X11 error handler that merely logs errors to stderr.
 *
 * This is used to prevent X11 errors from terminating XSecureLock.
 */
int JustLogErrorsHandler(Display *display, XErrorEvent *error) {
  char buf[128];
  XGetErrorText(display, error->error_code, buf, sizeof(buf));
  buf[sizeof(buf) - 1] = 0;
  Log("Got non-fatal X11 error: %s", buf);
  return 0;
}

/*! \brief An X11 error handler that does nothing at all.
 *
 * This is used for calls where we expect errors to happen.
 */
int SilentlyIgnoreErrorsHandler(Display *display, XErrorEvent *error) {
  (void)display;
  (void)error;
  return 0;
}

/*! \brief Print a version message.
 */
void Version(void) {
  printf("XSecureLock - X11 screen lock utility designed for security.\n");
  if (*git_version) {
    printf("Version: %s\n", git_version);
  } else {
    printf("Version unknown.\n");
  }
}

/*! \brief Print an usage message.
 *
 * A message is shown explaining how to use XSecureLock.
 *
 * \param me The name this executable was invoked as.
 */
void Usage(const char *me) {
  Version();
  printf(
      "\n"
      "Usage:\n"
      "  env [variables...] %s [-- command to run when locked]\n"
      "\n"
      "Environment variables you may set for XSecureLock and its modules:\n"
      "\n"
#include "env_helpstr.inc"  // IWYU pragma: keep
      "\n"
      "Configured default auth module: " AUTH_EXECUTABLE
      "\n"
      "Configured default authproto module: " AUTHPROTO_EXECUTABLE
      "\n"
      "Configured default global saver module: " GLOBAL_SAVER_EXECUTABLE
      "\n"
      "Configured default per-screen saver module: " SAVER_EXECUTABLE
      "\n"
      "\n"
      "This software is licensed under the Apache 2.0 License. Details are\n"
      "available at the following location:\n"
      "  " DOCS_PATH "/COPYING\n",
      me,
      "%s",   // For XSECURELOCK_KEY_%s_COMMAND.
      "%s");  // For XSECURELOCK_KEY_%s_COMMAND's description.
}

/*! \brief Load default settings from environment variables.
 *
 * These settings override what was figured out at ./configure time.
 */
void LoadDefaults() {
  auth_executable =
      GetExecutablePathSetting("XSECURELOCK_AUTH", AUTH_EXECUTABLE, 1);
  saver_executable = GetExecutablePathSetting("XSECURELOCK_GLOBAL_SAVER",
                                              GLOBAL_SAVER_EXECUTABLE, 0);
#ifdef HAVE_XCOMPOSITE_EXT
  no_composite = GetIntSetting("XSECURELOCK_NO_COMPOSITE", 0);
  composite_obscurer = GetIntSetting("XSECURELOCK_COMPOSITE_OBSCURER", 1);
#endif
  have_switch_user_command =
      *GetStringSetting("XSECURELOCK_SWITCH_USER_COMMAND", "");
  force_grab = GetIntSetting("XSECURELOCK_FORCE_GRAB", 0);
  debug_window_info = GetIntSetting("XSECURELOCK_DEBUG_WINDOW_INFO", 0);
  blank_timeout = GetIntSetting("XSECURELOCK_BLANK_TIMEOUT", 600);
  blank_dpms_state = GetStringSetting("XSECURELOCK_BLANK_DPMS_STATE", "off");
  saver_reset_on_auth_close =
      GetIntSetting("XSECURELOCK_SAVER_RESET_ON_AUTH_CLOSE", 0);
  saver_delay_ms = GetIntSetting("XSECURELOCK_SAVER_DELAY_MS", 0);
  saver_stop_on_blank = GetIntSetting("XSECURELOCK_SAVER_STOP_ON_BLANK", 1);
}

/*! \brief Parse the command line arguments, or exit in case of failure.
 *
 * This accepts saver_* or auth_* arguments, and puts them in their respective
 * global variable.
 *
 * This is DEPRECATED - use the XSECURELOCK_SAVER and XSECURELOCK_AUTH
 * environment variables instead!
 *
 * Possible errors will be printed on stderr.
 */
void ParseArgumentsOrExit(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], "auth_", 5)) {
      Log("Setting auth child name from command line is DEPRECATED. Use "
          "the XSECURELOCK_AUTH environment variable instead");
      auth_executable = argv[i];
      continue;
    }
    if (!strncmp(argv[i], "saver_", 6)) {
      Log("Setting saver child name from command line is DEPRECATED. Use "
          "the XSECURELOCK_SAVER environment variable instead");
      saver_executable = argv[i];
      continue;
    }
    if (!strcmp(argv[i], "--")) {
      notify_command = argv + i + 1;
      break;
    }
    if (!strcmp(argv[i], "--help")) {
      Usage(argv[0]);
      exit(0);
    }
    if (!strcmp(argv[i], "--version")) {
      Version();
      exit(0);
    }
    // If we get here, the argument is unrecognized. Exit, then.
    Log("Unrecognized argument: %s", argv[i]);
    Usage(argv[0]);
    exit(0);
  }
}

/*! \brief Check the settings.
 *
 * This tests whether the selected auth and saver executables are valid and
 * actually executable. Failure of this could lead to an un-unlockable screen,
 * and we sure don't want that.
 *
 * Possible errors will be printed on stderr.
 *
 * \return true if everything is OK, false otherwise.
 */
int CheckSettings() {
  // Flawfinder note: the access() calls here are not security relevant and just
  // prevent accidentally running with a nonexisting saver or auth executable as
  // that could make the system un-unlockable.

  if (auth_executable == NULL) {
    Log("Auth module has not been specified in any way");
    return 0;
  }
  if (saver_executable == NULL) {
    Log("Saver module has not been specified in any way");
    return 0;
  }
  return 1;
}

/*! \brief Print some debug info about a window.
 *
 * Only enabled if debug_window_info is set.
 *
 * Spammy.
 */
void DebugDumpWindowInfo(Window w) {
  if (!debug_window_info) {
    return;
  }
  char buf[128];
  // Note: process has to be backgrounded (&) because we may be within
  // XGrabServer.
  int buflen =
      snprintf(buf, sizeof(buf),
               "{ xwininfo -all -id %lu; xprop -id %lu; } >&2 &", w, w);
  if (buflen <= 0 || (size_t)buflen >= sizeof(buf)) {
    Log("Wow, pretty large integers you got there");
    return;
  }
  system(buf);
}

/*! \brief Raise a window if necessary.
 *
 * Does not cause any events if the window is already on the top.

 * \param display The X11 display.
 * \param w The window to raise.
 * \param silent Whether to output something if we can't detect what is wrong.
 * \param force Whether to always raise our window, even if we can't find what
 *   covers us. Set this only if confident that there is something overlapping
 *   us, like in response to a negative VisibilityNotify.
 */
void MaybeRaiseWindow(Display *display, Window w, int silent, int force) {
  int need_raise = force;
  Window root, parent;
  Window *children, *siblings;
  unsigned int nchildren, nsiblings;
  if (XQueryTree(display, w, &root, &parent, &children, &nchildren)) {
    XFree(children);
    Window grandparent;
    if (!XQueryTree(display, parent, &root, &grandparent, &siblings,
                    &nsiblings)) {
      Log("XQueryTree failed on the parent");
      siblings = NULL;
      nsiblings = 0;
    }
  } else {
    Log("XQueryTree failed on self");
    siblings = NULL;
    nsiblings = 0;
  }
  if (nsiblings == 0) {
    Log("No siblings found");
  } else {
    if (w == siblings[nsiblings - 1]) {
      // But we _are_ on top...?
      if (force && !silent) {
        // We have evidence of something covering us, but cannot locate it.
        Log("MaybeRaiseWindow miss: something obscured my window %lu but I "
            "can't find it",
            w);
      }
    } else {
      // We found what's covering us.
      Log("MaybeRaiseWindow hit: window %lu was above my window %lu",
          siblings[nsiblings - 1], w);
      DebugDumpWindowInfo(siblings[nsiblings - 1]);
      need_raise = 1;
    }
  }
  XFree(siblings);
  if (need_raise) {
    XRaiseWindow(display, w);
  }
}

typedef struct {
  Display *display;
  Window root_window;
  Cursor cursor;
  int silent;
} AcquireGrabsState;

int TryAcquireGrabs(Window w, void *state_voidp) {
  AcquireGrabsState *state = state_voidp;
  int ok = 1;
  if (XGrabPointer(state->display, state->root_window, False,
                   ALL_POINTER_EVENTS, GrabModeAsync, GrabModeAsync, None,
                   state->cursor, CurrentTime) != GrabSuccess) {
    if (!state->silent) {
      Log("Critical: cannot grab pointer");
    }
    ok = 0;
  }
  if (XGrabKeyboard(state->display, state->root_window, False, GrabModeAsync,
                    GrabModeAsync, CurrentTime) != GrabSuccess) {
    if (!state->silent) {
      Log("Critical: cannot grab keyboard");
    }
    ok = 0;
  }
  if (w != None) {
    Log("Unmapped window %lu to force grabbing, which %s", w,
        ok ? "succeeded" : "didn't help");
    if (ok) {
      DebugDumpWindowInfo(w);
    }
  }
  return ok;
}

/*! \brief Acquire all necessary grabs to lock the screen.
 *
 * \param display The X11 display.
 * \param root_window The root window.
 * \param silent Do not log errors.
 * \param force Try extra hard (1), or even harder (2). The latter mode will
 * very likely interfere strongly with window managers. \return true if grabbing
 * succeeded, false otherwise.
 */
int AcquireGrabs(Display *display, Window root_window, Window *ignored_windows,
                 unsigned int n_ignored_windows, Cursor cursor, int silent,
                 int force) {
  AcquireGrabsState grab_state;
  grab_state.display = display;
  grab_state.root_window = root_window;
  grab_state.cursor = cursor;
  grab_state.silent = silent;

  if (!force) {
    // Easy case.
    return TryAcquireGrabs(None, &grab_state);
  }

  XGrabServer(display);  // Critical section.
  UnmapAllWindowsState unmap_state;
  int ok;
  if (InitUnmapAllWindowsState(&unmap_state, display, root_window,
                               ignored_windows, n_ignored_windows,
                               "xsecurelock", NULL, force > 1)) {
    Log("Trying to force grabbing by unmapping all windows. BAD HACK");
    ok = UnmapAllWindows(&unmap_state, TryAcquireGrabs, &grab_state);
    RemapAllWindows(&unmap_state);
  } else {
    Log("Found XSecureLock to be already running, not forcing");
    ok = TryAcquireGrabs(None, &grab_state);
  }
  ClearUnmapAllWindowsState(&unmap_state);
  XUngrabServer(display);

  // Always flush the display after this to ensure the server is only
  // grabbed for as long as needed, and to make absolutely sure that
  // remapping did happen.
  XFlush(display);

  return ok;
}

/*! \brief Tell xss-lock or others that we're done locking.
 *
 * This enables xss-lock to delay going to sleep until the screen is actually
 * locked - useful to prevent information leaks after wakeup.
 *
 * \param fd The file descriptor of the X11 connection that we shouldn't close.
 */
void NotifyOfLock(int xss_sleep_lock_fd) {
  if (xss_sleep_lock_fd != -1) {
    if (close(xss_sleep_lock_fd) != 0) {
      LogErrno("close(XSS_SLEEP_LOCK_FD)");
    }
  }
  if (notify_command != NULL && *notify_command != NULL) {
    pid_t pid = ForkWithoutSigHandlers();
    if (pid == -1) {
      LogErrno("fork");
    } else if (pid == 0) {
      // Child process.
      execvp(notify_command[0], notify_command);
      LogErrno("execvp");
      _exit(EXIT_FAILURE);
    } else {
      // Parent process after successful fork.
      notify_command_pid = pid;
    }
  }
}

int CheckLockingEffectiveness() {
  // When this variable is set, all checks in here are still evaluated but we
  // try locking anyway.
  int error_status = 0;
  const char *error_string = "Will not lock";
  if (GetIntSetting("XSECURELOCK_DEBUG_ALLOW_LOCKING_IF_INEFFECTIVE", 0)) {
    error_status = 1;
    error_string = "Locking anyway";
  }

  // Do not try "locking" a Wayland session. Although everything we do appears
  // to work on XWayland, our grab will only affect X11 and not Wayland
  // clients, and therefore the lock will not be effective. If you need to get
  // around this check for testing, just unset the WAYLAND_DISPLAY environment
  // variable before starting XSecureLock. But really, this won't be secure in
  // any way...
  if (*GetStringSetting("WAYLAND_DISPLAY", "")) {
    Log("Wayland detected. This would only lock the X11 part of your session. "
        "%s",
        error_string);
    return error_status;
  }

  // Inside a VNC session, we better don't lock, as users might think it locked
  // their client when it actually only locked the remote.
  if (*GetStringSetting("VNCDESKTOP", "")) {
    Log("VNC detected. This would only lock your remote session. %s",
        error_string);
    return error_status;
  }

  // Inside a Chrome Remote Desktop session, we better don't lock, as users
  // might think it locked their client when it actually only locked the remote.
  if (*GetStringSetting("CHROME_REMOTE_DESKTOP_SESSION", "")) {
    Log("Chrome Remote Desktop detected. This would only lock your remote "
        "session. %s",
        error_string);
    return error_status;
  }

  return 1;
}

/*! \brief The main program.
 *
 * Usage: see Usage().
 */
int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");

  int xss_sleep_lock_fd = GetIntSetting("XSS_SLEEP_LOCK_FD", -1);
  if (xss_sleep_lock_fd != -1) {
    // Children processes should not inherit the sleep lock
    // Failures are not critical, systemd will ignore the lock
    // when InhibitDelayMaxSec is reached
    int flags = fcntl(xss_sleep_lock_fd, F_GETFD);
    if (flags == -1) {
      LogErrno("fcntl(XSS_SLEEP_LOCK_FD, F_GETFD)");
    } else {
      flags |= FD_CLOEXEC;
      int status = fcntl(xss_sleep_lock_fd, F_SETFD, flags);
      if (status == -1) {
        LogErrno("fcntl(XSS_SLEEP_LOCK_FD, F_SETFD, %#x)", flags);
      }
    }
  }

  // Switch to the root directory to not hold on to any directory descriptors
  // (just in case you started xsecurelock from a directory you want to unmount
  // later).
  if (chdir("/")) {
    Log("Could not switch to the root directory");
    return 1;
  }

  // Test if HELPER_PATH is accessible; if not, we will likely have a problem.
  if (access(HELPER_PATH "/", X_OK)) {
    Log("Could not access directory %s", HELPER_PATH);
    return 1;
  }

  // Parse and verify arguments.
  LoadDefaults();
  ParseArgumentsOrExit(argc, argv);
  if (!CheckSettings()) {
    Usage(argv[0]);
    return 1;
  }

  // Check if we are in a lockable session.
  if (!CheckLockingEffectiveness()) {
    return 1;
  }

  // Connect to X11.
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    Log("Could not connect to $DISPLAY");
    return 1;
  }

  // TODO(divVerent): Support that?
  if (ScreenCount(display) != 1) {
    Log("Warning: 'Zaphod' configurations are not supported at this point. "
        "Only locking the default screen.\n");
  }

  // My windows.
  Window my_windows[4];
  unsigned int n_my_windows = 0;

  // Who's the root?
  Window root_window = DefaultRootWindow(display);

  // Query the initial screen size, and get notified on updates. Also we're
  // going to grab on the root window, so FocusOut events about losing the grab
  // will appear there.
  XSelectInput(display, root_window, StructureNotifyMask | FocusChangeMask);
  int w = DisplayWidth(display, DefaultScreen(display));
  int h = DisplayHeight(display, DefaultScreen(display));
#ifdef DEBUG_EVENTS
  Log("DisplayWidthHeight %d %d", w, h);
#endif

  // Prepare some nice window attributes for a screen saver window.
  XColor black;
  black.pixel = BlackPixel(display, DefaultScreen(display));
  XQueryColor(display, DefaultColormap(display, DefaultScreen(display)),
              &black);

  XColor xcolor_background;
  XColor dummy;
  int status = XAllocNamedColor(
      display, DefaultColormap(display, DefaultScreen(display)),
      GetStringSetting("XSECURELOCK_BACKGROUND_COLOR", "black"),
      &xcolor_background, &dummy);
  unsigned long background_pixel = black.pixel;
  if (status != XcmsFailure) {
    background_pixel = xcolor_background.pixel;
  }

  Pixmap bg = XCreateBitmapFromData(display, root_window, "\0", 1, 1);
  Cursor default_cursor = XCreateFontCursor(display, XC_arrow);
  Cursor transparent_cursor =
      XCreatePixmapCursor(display, bg, bg, &black, &black, 0, 0);
  XSetWindowAttributes coverattrs = {0};
  coverattrs.background_pixel = background_pixel;
  coverattrs.save_under = 1;
  coverattrs.override_redirect = 1;
  coverattrs.cursor = transparent_cursor;

  Window parent_window = root_window;

#ifdef HAVE_XCOMPOSITE_EXT
  int composite_event_base, composite_error_base, composite_major_version = 0,
                                                  composite_minor_version = 0;
  int have_xcomposite_ext =
      XCompositeQueryExtension(display, &composite_event_base,
                               &composite_error_base) &&
      // Require at least XComposite 0.3.
      XCompositeQueryVersion(display, &composite_major_version,
                             &composite_minor_version) &&
      (composite_major_version >= 1 || composite_minor_version >= 3);
  if (!have_xcomposite_ext) {
    Log("XComposite extension not detected");
  }
  if (have_xcomposite_ext && no_composite) {
    Log("XComposite extension detected but disabled by user");
    have_xcomposite_ext = 0;
  }
  Window composite_window = None, obscurer_window = None;
  if (have_xcomposite_ext) {
    composite_window = XCompositeGetOverlayWindow(display, root_window);
    // Some compositers may unmap or shape the overlay window - undo that, just
    // in case.
    XMapRaised(display, composite_window);
#ifdef HAVE_XFIXES_EXT
    int xfixes_event_base, xfixes_error_base;
    if (XFixesQueryExtension(display, &xfixes_event_base, &xfixes_error_base)) {
      XFixesSetWindowShapeRegion(display, composite_window, ShapeBounding,  //
                                 0, 0, 0);
    }
#endif
    parent_window = composite_window;

    if (composite_obscurer) {
      // Also create an "obscurer window" that we don't actually use but that
      // covers almost everything in case the composite window temporarily does
      // not work (e.g. in case the compositor hides the COW).  We are making
      // the obscurer window actually white, so issues like this become visible
      // but harmless. The window isn't full-sized to avoid compositors turning
      // off themselves in response to a full-screen window, but nevertheless
      // this is kept opt-in for now until shown reliable.
      XSetWindowAttributes obscurerattrs = coverattrs;
      obscurerattrs.background_pixmap = XCreatePixmapFromBitmapData(
          display, root_window, incompatible_compositor_bits,
          incompatible_compositor_width, incompatible_compositor_height,
          BlackPixel(display, DefaultScreen(display)),
          WhitePixel(display, DefaultScreen(display)),
          DefaultDepth(display, DefaultScreen(display)));
      obscurer_window = XCreateWindow(
          display, root_window, 1, 1, w - 2, h - 2, 0, CopyFromParent,
          InputOutput, CopyFromParent,
          CWBackPixmap | CWSaveUnder | CWOverrideRedirect | CWCursor,
          &obscurerattrs);
      SetWMProperties(display, obscurer_window, "xsecurelock", "obscurer", argc,
                      argv);
      my_windows[n_my_windows++] = obscurer_window;
    }
  }
#endif

  // Create the windows.
  // background_window is the outer window which exists for security reasons (in
  // case a subprocess may turn its window transparent or something).
  // saver_window is the "visible" window that the saver child will draw on.
  // auth_window is a window exclusively used by the auth child. It will only be
  // mapped during auth, and hidden otherwise. These windows are separated
  // because XScreenSaver's savers might XUngrabKeyboard on their window.
  Window background_window = XCreateWindow(
      display, parent_window, 0, 0, w, h, 0, CopyFromParent, InputOutput,
      CopyFromParent, CWBackPixel | CWSaveUnder | CWOverrideRedirect | CWCursor,
      &coverattrs);
  SetWMProperties(display, background_window, "xsecurelock", "background", argc,
                  argv);
  my_windows[n_my_windows++] = background_window;
  Window saver_window =
      XCreateWindow(display, background_window, 0, 0, w, h, 0, CopyFromParent,
                    InputOutput, CopyFromParent, CWBackPixel, &coverattrs);
  SetWMProperties(display, saver_window, "xsecurelock", "saver", argc, argv);
  my_windows[n_my_windows++] = saver_window;

  Window auth_window =
      XCreateWindow(display, background_window, 0, 0, w, h, 0, CopyFromParent,
                    InputOutput, CopyFromParent, CWBackPixel, &coverattrs);
  SetWMProperties(display, auth_window, "xsecurelock", "auth", argc, argv);
  my_windows[n_my_windows++] = auth_window;

// Let's get notified if we lose visibility, so we can self-raise.
#ifdef HAVE_XCOMPOSITE_EXT
  if (composite_window != None) {
    XSelectInput(display, composite_window,
                 StructureNotifyMask | VisibilityChangeMask);
  }
  if (obscurer_window != None) {
    XSelectInput(display, obscurer_window,
                 StructureNotifyMask | VisibilityChangeMask);
  }
#endif
  XSelectInput(display, background_window,
               StructureNotifyMask | VisibilityChangeMask);
  XSelectInput(display, saver_window, StructureNotifyMask);
  XSelectInput(display, auth_window,
               StructureNotifyMask | VisibilityChangeMask);

  // Make sure we stay always on top.
  XWindowChanges coverchanges;
  coverchanges.stack_mode = Above;
  XConfigureWindow(display, background_window, CWStackMode, &coverchanges);
  XConfigureWindow(display, auth_window, CWStackMode, &coverchanges);

  // We're OverrideRedirect anyway, but setting this hint may help compositors
  // leave our window alone.
  Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
  Atom fullscreen_atom =
      XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
  XChangeProperty(display, background_window, state_atom, XA_ATOM, 32,
                  PropModeReplace, (const unsigned char *)&fullscreen_atom, 1);

  // Bypass compositing, just in case.
  Atom dont_composite_atom =
      XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False);
  long dont_composite = 1;
  XChangeProperty(display, background_window, dont_composite_atom, XA_CARDINAL,
                  32, PropModeReplace, (const unsigned char *)&dont_composite,
                  1);
#ifdef HAVE_XCOMPOSITE_EXT
  if (composite_window != None) {
    // Also set this property on the Composite Overlay Window, just in case a
    // compositor were to try compositing it (xcompmgr does, but doesn't know
    // this property anyway).
    XChangeProperty(display, composite_window, dont_composite_atom, XA_CARDINAL,
                    32, PropModeReplace, (const unsigned char *)&dont_composite,
                    1);
  }
// Note: NOT setting this on the obscurer window, as this is a fallback and
// actually should be composited to make sure the compositor never draws
// anything "interesting".
#endif

  // Initialize XInput so we can get multibyte key events.
  XIM xim = XOpenIM(display, NULL, NULL, NULL);
  if (xim == NULL) {
    Log("XOpenIM failed. Assuming Latin-1 encoding");
  }
  XIC xic = NULL;
  if (xim != NULL) {
    // As we're OverrideRedirect and grabbing input, we can't use any fancy IMs.
    // Therefore, if we can't get a requirement-less IM, we won't use XIM at
    // all.
    int input_styles[4] = {
        XIMPreeditNothing | XIMStatusNothing,  // Status might be invisible.
        XIMPreeditNothing | XIMStatusNone,     // Maybe a compose key.
        XIMPreeditNone | XIMStatusNothing,     // Status might be invisible.
        XIMPreeditNone | XIMStatusNone         // Standard handling.
    };
    for (size_t i = 0; i < sizeof(input_styles) / sizeof(input_styles[0]);
         ++i) {
      // Note: we draw XIM stuff in auth_window so it's above the saver/auth
      // child. However, we receive events for the grab window.
      xic = XCreateIC(xim, XNInputStyle, input_styles[i], XNClientWindow,
                      auth_window, NULL);
      if (xic != NULL) {
        break;
      }
    }
    if (xic == NULL) {
      Log("XCreateIC failed. Assuming Latin-1 encoding");
    }
  }

#ifdef HAVE_XSCREENSAVER_EXT
  // If we support the screen saver extension, that'd be good.
  int scrnsaver_event_base, scrnsaver_error_base;
  if (!XScreenSaverQueryExtension(display, &scrnsaver_event_base,
                                  &scrnsaver_error_base)) {
    scrnsaver_event_base = 0;
    scrnsaver_error_base = 0;
  }
  XScreenSaverSelectInput(display, background_window, ScreenSaverNotifyMask);
#endif

#ifdef HAVE_XF86MISC_EXT
  // In case keys to disable grabs are available, turn them off for the duration
  // of the lock.
  if (XF86MiscSetGrabKeysState(display, False) != MiscExtGrabStateSuccess) {
    Log("Could not set grab keys state");
    return EXIT_FAILURE;
  }
#endif

  // Acquire all grabs we need. Retry in case the window manager is still
  // holding some grabs while starting XSecureLock.
  int last_normal_attempt = force_grab ? 1 : 0;
  Window previous_focused_window = None;
  int previous_revert_focus_to = RevertToNone;
  int retries = 10;
  for (; retries >= 0; --retries) {
    if (AcquireGrabs(display, root_window, my_windows, n_my_windows,
                     transparent_cursor,
                     /*silent=*/retries > last_normal_attempt,
                     /*force=*/retries < last_normal_attempt)) {
      break;
    }
    if (previous_focused_window == None) {
      // When retrying for the first time, try to change the X11 input focus.
      // This may close context menus and thereby allow us to grab.
      XGetInputFocus(display, &previous_focused_window,
                     &previous_revert_focus_to);
      // We don't have any window mapped yet, but it should be a safe bet to set
      // focus to the root window (most WMs allow for an easy way out from that
      // predicament in case our restore logic fails).
      XSetInputFocus(display, PointerRoot, RevertToPointerRoot, CurrentTime);
      // Make this happen instantly.
      XFlush(display);
    }
    nanosleep(&(const struct timespec){0, 100000000L}, NULL);
  }
  if (retries < 0) {
    Log("Failed to grab. Giving up.");
    return EXIT_FAILURE;
  }

  if (MLOCK_PAGE(&priv, sizeof(priv)) < 0) {
    LogErrno("mlock");
    return EXIT_FAILURE;
  }

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = SIG_IGN;  // Don't die if auth child closes stdin.
  if (sigaction(SIGPIPE, &sa, NULL) != 0) {
    LogErrno("sigaction(SIGPIPE)");
  }
  sa.sa_handler = HandleSIGUSR2; // For remote wakeups by system events.
  if (sigaction(SIGUSR2, &sa, NULL) != 0) {
    LogErrno("sigaction(SIGUSR2)");
  }
  sa.sa_flags = SA_RESETHAND;     // It re-raises to suicide.
  sa.sa_handler = HandleSIGTERM;  // To kill children.
  if (sigaction(SIGTERM, &sa, NULL) != 0) {
    LogErrno("sigaction(SIGTERM)");
  }

  InitWaitPgrp();

  // Need to flush the display so savers sure can access the window.
  XFlush(display);

  // Figure out the initial Xss saver state. This gets updated by event.
  enum WatchChildrenState xss_requested_saver_state = WATCH_CHILDREN_NORMAL;
#ifdef HAVE_XSCREENSAVER_EXT
  if (scrnsaver_event_base != 0) {
    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    XScreenSaverQueryInfo(display, root_window, info);
    if (info->state == ScreenSaverOn && info->kind == ScreenSaverBlanked && saver_stop_on_blank) {
      xss_requested_saver_state = WATCH_CHILDREN_SAVER_DISABLED;
    }
    XFree(info);
  }
#endif

  InitBlankScreen();

  XFlush(display);
  if (WatchChildren(display, auth_window, saver_window, xss_requested_saver_state,
                    NULL)) {
    goto done;
  }

  // Wait for children to initialize.
  struct timespec sleep_ts;
  sleep_ts.tv_sec = saver_delay_ms / 1000;
  sleep_ts.tv_nsec = (saver_delay_ms % 1000) * 1000000L;
  nanosleep(&sleep_ts, NULL);

  // Map our windows.
  // This is done after grabbing so failure to grab does not blank the screen
  // yet, thereby "confirming" the screen lock.
  XMapRaised(display, saver_window);
  XMapRaised(display, background_window);
  XClearWindow(display, background_window);  // Workaround for bad drivers.
  XRaiseWindow(display, auth_window);  // Don't map here.

#ifdef HAVE_XCOMPOSITE_EXT
  if (obscurer_window != None) {
    // Map the obscurer window last so it should never become visible.
    XMapRaised(display, obscurer_window);
  }
#endif
  XFlush(display);

  // Prevent X11 errors from killing XSecureLock. Instead, just keep going.
  XSetErrorHandler(JustLogErrorsHandler);

  int x11_fd = ConnectionNumber(display);

  if (x11_fd == xss_sleep_lock_fd && xss_sleep_lock_fd != -1) {
    Log("XSS_SLEEP_LOCK_FD matches DISPLAY - what?!? We're probably "
        "inhibiting sleep now");
    xss_sleep_lock_fd = -1;
  }

  int background_window_mapped = 0, background_window_visible = 0,
      auth_window_mapped = 0, saver_window_mapped = 0,
      need_to_reinstate_grabs = 0, xss_lock_notified = 0;
  for (;;) {
    // Watch children WATCH_CHILDREN_HZ times per second.
    fd_set in_fds;
    memset(&in_fds, 0, sizeof(in_fds));  // For clang-analyzer.
    FD_ZERO(&in_fds);
    FD_SET(x11_fd, &in_fds);
    struct timeval tv;
    tv.tv_usec = 1000000 / WATCH_CHILDREN_HZ;
    tv.tv_sec = 0;
    select(x11_fd + 1, &in_fds, 0, 0, &tv);

    // Make sure to shut down the saver when blanked. Saves power.
    enum WatchChildrenState requested_saver_state =
      (saver_stop_on_blank && blanked) ? WATCH_CHILDREN_SAVER_DISABLED : xss_requested_saver_state;

    // Now check status of our children.
    if (WatchChildren(display, auth_window, saver_window, requested_saver_state,
                      NULL)) {
      goto done;
    }

    // If something changed our cursor, change it back.
    XUndefineCursor(display, saver_window);

#ifdef ALWAYS_REINSTATE_GRABS
    // This really should never be needed...
    (void)need_to_reinstate_grabs;
    need_to_reinstate_grabs = 1;
#endif
    if (need_to_reinstate_grabs) {
      need_to_reinstate_grabs = 0;
      if (!AcquireGrabs(display, root_window, my_windows, n_my_windows,
                        transparent_cursor, 0, 0)) {
        Log("Critical: could not reacquire grabs. The screen is now UNLOCKED! "
            "Trying again next frame.");
        need_to_reinstate_grabs = 1;
      }
    }

#ifdef AUTO_RAISE
    if (auth_window_mapped) {
      MaybeRaiseWindow(display, auth_window, 0, 0);
    }
    MaybeRaiseWindow(display, background_window, 0, 0);
#ifdef HAVE_XCOMPOSITE_EXT
    if (obscurer_window != None) {
      MaybeRaiseWindow(display, obscurer_window, 1, 0);
    }
#endif
#endif

    // Take care of zombies.
    if (notify_command_pid != 0) {
      int status;
      WaitProc("notify", &notify_command_pid, 0, 0, &status);
      // Otherwise, we're still alive. Re-check next time.
    }

    if (signal_wakeup) {
      // A signal was received to request a wakeup. Clear the flag
      // and proceed to auth. Technically this check involves a race
      // condition between check and set since we don't block signals,
      // but this should not make a difference since screen wakeup is
      // idempotent.
      signal_wakeup = 0;
#ifdef DEBUG_EVENTS
      Log("WakeUp on signal");
#endif
      UnblankScreen(display);
      if (WakeUp(display, auth_window, saver_window, NULL)) {
        goto done;
      }
    }

    // Handle all events.
    while (XPending(display) && (XNextEvent(display, &priv.ev), 1)) {
      if (XFilterEvent(&priv.ev, None)) {
        // If an input method ate the event, ignore it.
        continue;
      }
      switch (priv.ev.type) {
        case ConfigureNotify:
#ifdef DEBUG_EVENTS
          Log("ConfigureNotify %lu %d %d",
              (unsigned long)priv.ev.xconfigure.window,
              priv.ev.xconfigure.width, priv.ev.xconfigure.height);
#endif
          if (priv.ev.xconfigure.window == root_window) {
            // Root window size changed. Adjust the saver_window window too!
            w = priv.ev.xconfigure.width;
            h = priv.ev.xconfigure.height;
#ifdef DEBUG_EVENTS
            Log("DisplayWidthHeight %d %d", w, h);
#endif
#ifdef HAVE_XCOMPOSITE_EXT
            if (obscurer_window != None) {
              XMoveResizeWindow(display, obscurer_window, 1, 1, w - 2, h - 2);
            }
#endif
            XMoveResizeWindow(display, background_window, 0, 0, w, h);
            XClearWindow(display,
                         background_window);  // Workaround for bad drivers.
            XMoveResizeWindow(display, saver_window, 0, 0, w, h);
            // Just in case - ConfigureNotify might also be sent for raising
          }
          // Also, whatever window has been reconfigured, should also be raised
          // to make sure.
          if (auth_window_mapped && priv.ev.xconfigure.window == auth_window) {
            MaybeRaiseWindow(display, auth_window, 0, 0);
          } else if (priv.ev.xconfigure.window == background_window) {
            MaybeRaiseWindow(display, background_window, 0, 0);
            XClearWindow(display,
                         background_window);  // Workaround for bad drivers.
#ifdef HAVE_XCOMPOSITE_EXT
          } else if (obscurer_window != None &&
                     priv.ev.xconfigure.window == obscurer_window) {
            MaybeRaiseWindow(display, obscurer_window, 1, 0);
#endif
          }
          break;
        case VisibilityNotify:
#ifdef DEBUG_EVENTS
          Log("VisibilityNotify %lu %d",
              (unsigned long)priv.ev.xvisibility.window,
              priv.ev.xvisibility.state);
#endif
          if (priv.ev.xvisibility.state == VisibilityUnobscured) {
            if (priv.ev.xvisibility.window == background_window) {
              background_window_visible = 1;
            }
          } else {
            // If something else shows an OverrideRedirect window, we want to
            // stay on top.
            if (auth_window_mapped &&
                priv.ev.xvisibility.window == auth_window) {
              Log("Someone overlapped the auth window. Undoing that");
              MaybeRaiseWindow(display, auth_window, 0, 1);
            } else if (priv.ev.xvisibility.window == background_window) {
              background_window_visible = 0;
              Log("Someone overlapped the background window. Undoing that");
              MaybeRaiseWindow(display, background_window, 0, 1);
              XClearWindow(display,
                           background_window);  // Workaround for bad drivers.
#ifdef HAVE_XCOMPOSITE_EXT
            } else if (obscurer_window != None &&
                       priv.ev.xvisibility.window == obscurer_window) {
              // Not logging this as our own composite overlay window causes
              // this to happen too; keeping this there anyway so we self-raise
              // if something is wrong with the COW and something else overlaps
              // us.
              MaybeRaiseWindow(display, obscurer_window, 1, 1);
            } else if (composite_window != None &&
                       priv.ev.xvisibility.window == composite_window) {
              Log("Someone overlapped the composite overlay window window. "
                  "Undoing that");
              // Note: MaybeRaiseWindow isn't valid here, as the COW has the
              // root as parent without being a proper child of it. Let's just
              // raise the COW unconditionally.
              XRaiseWindow(display, composite_window);
#endif
            } else {
              Log("Received unexpected VisibilityNotify for window %lu",
                  priv.ev.xvisibility.window);
            }
          }
          break;
        case MotionNotify:
        case ButtonPress:
          // Mouse events launch the auth child.
          ScreenNoLongerBlanked(display);
          if (WakeUp(display, auth_window, saver_window, NULL)) {
            goto done;
          }
          break;
        case KeyPress: {
          // Keyboard events launch the auth child.
          ScreenNoLongerBlanked(display);
          Status status = XLookupNone;
          int have_key = 1;
          int do_wake_up = 1;
          priv.keysym = NoSymbol;
          if (xic) {
            // This uses the current locale.
            priv.len =
                XmbLookupString(xic, &priv.ev.xkey, priv.buf,
                                sizeof(priv.buf) - 1, &priv.keysym, &status);
            if (priv.len <= 0) {
              // Error or no output. Fine.
              have_key = 0;
            } else if (status != XLookupChars && status != XLookupBoth) {
              // Got nothing new.
              have_key = 0;
            }
          } else {
            // This is always Latin-1. Sorry.
            priv.len = XLookupString(&priv.ev.xkey, priv.buf,
                                     sizeof(priv.buf) - 1, &priv.keysym, NULL);
            if (priv.len <= 0) {
              // Error or no output. Fine.
              have_key = 0;
            }
          }
          if (have_key && (size_t)priv.len >= sizeof(priv.buf)) {
            // Detect possible overruns. This should be unreachable.
            Log("Received invalid length from XLookupString: %d", priv.len);
            have_key = 0;
          }
          if (priv.keysym == XK_Tab && (priv.ev.xkey.state & ControlMask)) {
            // Map Ctrl-Tab to Ctrl-S (switch layout). We remap this one
            // because not all layouts have a key for S.
            priv.buf[0] = '\023';
            priv.buf[1] = 0;
          } else if (priv.keysym == XK_BackSpace &&
                     (priv.ev.xkey.state & ControlMask)) {
            // Map Ctrl-Backspace to Ctrl-U (clear entry line).
            priv.buf[0] = '\025';
            priv.buf[1] = 0;
          } else if (have_switch_user_command &&
                     (priv.keysym == XK_o || priv.keysym == XK_0) &&
                     (((priv.ev.xkey.state & ControlMask) &&
                       (priv.ev.xkey.state & Mod1Mask)) ||
                      (priv.ev.xkey.state & Mod4Mask))) {
            // Switch to greeter on Ctrl-Alt-O or Win-O.
            system("eval \"$XSECURELOCK_SWITCH_USER_COMMAND\" &");
            // And send a Ctrl-U (clear entry line).
            priv.buf[0] = '\025';
            priv.buf[1] = 0;
          } else if (have_key) {
            // Map all newline-like things to newlines.
            if (priv.len == 1 && priv.buf[0] == '\r') {
              priv.buf[0] = '\n';
            }
            priv.buf[priv.len] = 0;
          } else {
            // No new bytes. Fine.
            priv.buf[0] = 0;
            // We do check if something external wants to handle this key,
            // though.
            const char *keyname = XKeysymToString(priv.keysym);
            if (keyname != NULL) {
              char buf[64];
              int buflen = snprintf(buf, sizeof(buf),
                                    "XSECURELOCK_KEY_%s_COMMAND", keyname);
              if (buflen <= 0 || (size_t)buflen >= sizeof(buf)) {
                Log("Wow, pretty long keysym names you got there");
              } else {
                const char *command = GetStringSetting(buf, "");
                if (*command) {
                  buflen = snprintf(buf, sizeof(buf),
                                    "eval \"$XSECURELOCK_KEY_%s_COMMAND\" &",
                                    keyname);
                  if (buflen <= 0 || (size_t)buflen >= sizeof(buf)) {
                    Log("Wow, pretty long keysym names you got there");
                  } else {
                    system(buf);
                    do_wake_up = 0;
                  }
                }
              }
            }
          }
          // Now if so desired, wake up the login prompt, and check its
          // status.
          int authenticated =
              do_wake_up ? WakeUp(display, auth_window, saver_window, priv.buf)
                         : 0;
          // Clear out keypress data immediately.
          explicit_bzero(&priv, sizeof(priv));
          if (authenticated) {
            goto done;
          }
        } break;
        case KeyRelease:
        case ButtonRelease:
          // Known to wake up screen blanking.
          ScreenNoLongerBlanked(display);
          break;
        case MappingNotify:
        case EnterNotify:
        case LeaveNotify:
          // Ignored.
          break;
        case MapNotify:
#ifdef DEBUG_EVENTS
          Log("MapNotify %lu", (unsigned long)priv.ev.xmap.window);
#endif
          if (priv.ev.xmap.window == auth_window) {
            auth_window_mapped = 1;
#ifdef SHOW_CURSOR_DURING_AUTH
            // Actually ShowCursor...
            XGrabPointer(display, root_window, False, ALL_POINTER_EVENTS,
                         GrabModeAsync, GrabModeAsync, None, default_cursor,
                         CurrentTime);
#endif
          } else if (priv.ev.xmap.window == saver_window) {
            saver_window_mapped = 1;
          } else if (priv.ev.xmap.window == background_window) {
            background_window_mapped = 1;
          }
          break;
        case UnmapNotify:
#ifdef DEBUG_EVENTS
          Log("UnmapNotify %lu", (unsigned long)priv.ev.xmap.window);
#endif
          if (priv.ev.xmap.window == auth_window) {
            auth_window_mapped = 0;
#ifdef SHOW_CURSOR_DURING_AUTH
            // Actually HideCursor...
            XGrabPointer(display, root_window, False, ALL_POINTER_EVENTS,
                         GrabModeAsync, GrabModeAsync, None, transparent_cursor,
                         CurrentTime);
#endif
          } else if (priv.ev.xmap.window == saver_window) {
            // This should never happen, but let's handle it anyway.
            Log("Someone unmapped the saver window. Undoing that");
            saver_window_mapped = 0;
            XMapWindow(display, saver_window);
          } else if (priv.ev.xmap.window == background_window) {
            // This should never happen, but let's handle it anyway.
            Log("Someone unmapped the background window. Undoing that");
            background_window_mapped = 0;
            XMapRaised(display, background_window);
            XClearWindow(display,
                         background_window);  // Workaround for bad drivers.
#ifdef HAVE_XCOMPOSITE_EXT
          } else if (obscurer_window != None &&
                     priv.ev.xmap.window == obscurer_window) {
            // This should never happen, but let's handle it anyway.
            Log("Someone unmapped the obscurer window. Undoing that");
            XMapRaised(display, obscurer_window);
          } else if (composite_window != None &&
                     priv.ev.xmap.window == composite_window) {
            // This should never happen, but let's handle it anyway.
            // Compton might do this when --unredir-if-possible is set and a
            // fullscreen game launches while the screen is locked.
            Log("Someone unmapped the composite overlay window. Undoing "
                "that");
            XMapRaised(display, composite_window);
#endif
          } else if (priv.ev.xmap.window == root_window) {
            // This should never happen, but let's handle it anyway.
            Log("Someone unmapped the root window?!? Undoing that");
            XMapRaised(display, root_window);
          }
          break;
        case FocusIn:
        case FocusOut:
#ifdef DEBUG_EVENTS
          Log("Focus%d %lu", priv.ev.xfocus.mode,
              (unsigned long)priv.ev.xfocus.window);
#endif
          if (priv.ev.xfocus.window == root_window &&
              priv.ev.xfocus.mode == NotifyUngrab) {
            // Not logging this - this is a normal occurrence if invoking the
            // screen lock from a key combination, as the press event may
            // launch xsecurelock while the release event releases a passive
            // grab. We still immediately try to reacquire grabs here, though.
            if (!AcquireGrabs(display, root_window, my_windows, n_my_windows,
                              transparent_cursor, 0, 0)) {
              Log("Critical: could not reacquire grabs after NotifyUngrab. "
                  "The screen is now UNLOCKED! Trying again next frame.");
              need_to_reinstate_grabs = 1;
            }
          }
          break;
        case ClientMessage: {
          if (priv.ev.xclient.window == root_window) {
            // ClientMessage on root window is used by the EWMH spec. No need to
            // spam about those. As we want to watch the root window size,
            // we must keep selecting StructureNotifyMask there.
            break;
          }
          // Those cause spam below, so let's log them separately to get some
          // details.
          const char *message_type =
              XGetAtomName(display, priv.ev.xclient.message_type);
          Log("Received unexpected ClientMessage event %s on window %lu",
              message_type == NULL ? "(null)" : message_type,
              priv.ev.xclient.window);
          break;
        }
        default:
#ifdef DEBUG_EVENTS
          Log("Event%d %lu", priv.ev.type, (unsigned long)priv.ev.xany.window);
#endif
#ifdef HAVE_XSCREENSAVER_EXT
          // Handle screen saver notifications. If the screen is blanked
          // anyway, turn off the saver child.
          if (scrnsaver_event_base != 0 &&
              priv.ev.type == scrnsaver_event_base + ScreenSaverNotify) {
            XScreenSaverNotifyEvent *xss_ev =
                (XScreenSaverNotifyEvent *)&priv.ev;
            if (xss_ev->state == ScreenSaverOn) {
              xss_requested_saver_state = WATCH_CHILDREN_SAVER_DISABLED;
            } else {
              xss_requested_saver_state = WATCH_CHILDREN_NORMAL;
            }
            break;
          }
#endif
          Log("Received unexpected event %d", priv.ev.type);
          break;
      }
      if (background_window_mapped && background_window_visible &&
          saver_window_mapped && !xss_lock_notified) {
        NotifyOfLock(xss_sleep_lock_fd);
        xss_lock_notified = 1;
      }
    }
  }

done:
  // Make sure no DPMS changes persist.
  UnblankScreen(display);

  if (previous_focused_window != None) {
    XSetErrorHandler(SilentlyIgnoreErrorsHandler);
    XSetInputFocus(display, previous_focused_window, previous_revert_focus_to,
                   CurrentTime);
    XSetErrorHandler(JustLogErrorsHandler);
  }

  // Wipe the password.
  explicit_bzero(&priv, sizeof(priv));

  // Free our resources, and exit.
#ifdef HAVE_XCOMPOSITE_EXT
  if (obscurer_window != None) {
    // Destroy the obscurer window first so it should never become visible.
    XDestroyWindow(display, obscurer_window);
  }
  if (composite_window != None) {
    XCompositeReleaseOverlayWindow(display, composite_window);
  }
#endif
  XDestroyWindow(display, auth_window);
  XDestroyWindow(display, saver_window);
  XDestroyWindow(display, background_window);

  XFreeCursor(display, transparent_cursor);
  XFreeCursor(display, default_cursor);
  XFreePixmap(display, bg);

  XCloseDisplay(display);

  return EXIT_SUCCESS;
}
