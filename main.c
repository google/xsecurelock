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

#include <X11/X.h>         // for Window, GrabModeAsync, None
#include <X11/Xatom.h>     // for XA_ATOM
#include <X11/Xlib.h>      // for XEvent, XSelectInput, XSetWin...
#include <X11/Xutil.h>     // for XLookupString
#include <errno.h>         // for ECHILD, EINTR, errno
#include <locale.h>        // for NULL, setlocale, LC_CTYPE
#include <signal.h>        // for sigaction, sigemptyset, SIGPIPE
#include <stdio.h>         // for fprintf, stderr, perror, printf
#include <stdlib.h>        // for EXIT_SUCCESS, WEXITSTATUS, exit
#include <string.h>        // for __s1_len, __s2_len, strncmp
#include <sys/select.h>    // for timeval, select, FD_SET, FD_ZERO
#include <sys/wait.h>      // for waitpid, WNOHANG
#include <time.h>          // for nanosleep, timespec
#include <unistd.h>        // for access, pid_t, X_OK, chdir
#include "auth_child.h"    // for WantAuthChild, WatchAuthChild
#include "env_settings.h"  // for GetIntSetting, GetStringSetting
#include "mlock_page.h"    // for MLOCK_PAGE
#include "saver_child.h"   // for WatchSaverChild

#ifdef HAVE_SCRNSAVER
#include <X11/extensions/saver.h>      // for ScreenSaverNotify, ScreenSave...
#include <X11/extensions/scrnsaver.h>  // for XScreenSaverNotifyEvent, XScr...
#endif
#ifdef HAVE_COMPOSITE
#include <X11/extensions/Xcomposite.h>  // for XCompositeGetOverlayWindow
#endif
#ifdef HAVE_XF86MISC
#include <X11/extensions/xf86misc.h>  // for XF86MiscSetGrabKeysState
#endif

#include "auth_child.h"     // for WantAuthChild, WatchAuthChild
#include "env_settings.h"   // for GetIntSetting, GetStringSetting
#include "mlock_page.h"     // for MLOCK_PAGE
#include "saver_child.h"    // for WatchSaverChild
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
#define REINSTATE_GRABS

/*! \brief Try to bring the grab window to foreground in regular intervals.
 *
 * Some desktop environments have transparent OverrideRedirect notifications.
 * These do not send a VisibilityNotify to this window, as some part still
 * shines through. As a workaround, this enables raising the grab window
 * periodically.
 */
#define AUTO_RAISE

/*! \brief Exhaustive list of all mouse related X11 events.
 *
 * These will be selected for grab. It is important that this contains all
 * pointer event types, to not let any through to other applications.
 */
#define ALL_POINTER_EVENTS                                                   \
  (ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask | \
   PointerMotionMask | PointerMotionHintMask | Button1MotionMask |           \
   Button2MotionMask | Button3MotionMask | Button4MotionMask |               \
   Button5MotionMask | ButtonMotionMask)

//! The name of the auth child to execute, relative to HELPER_PATH.
const char *auth_executable;
//! The name of the saver child to execute, relative to HELPER_PATH.
const char *saver_executable;
//! The command to run once screen locking is complete.
char *const *notify_command = NULL;
#ifdef HAVE_COMPOSITE
//! Do not use XComposite to cover transparent notifications.
int no_composite = 0;
#endif

//! The PID of a currently running notify command, or 0 if none is running.
pid_t notify_command_pid = 0;

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
int WatchChildren(Display *dpy, Window w, enum WatchChildrenState state,
                  const char *stdinbuf) {
  int auth_running = WantAuthChild(state == WATCH_CHILDREN_FORCE_AUTH);

  // Note: auth_running is true whenever we WANT to run authentication, or it is
  // already running. It may have recently terminated, which we will notice
  // later.
  if (auth_running) {
    // Make sure the saver is shut down to not interfere with the screen.
    WatchSaverChild(dpy, w, 0, saver_executable, 0);

    // Actually start the auth child, or notice termination.
    if (WatchAuthChild(dpy, w, auth_executable,
                       state == WATCH_CHILDREN_FORCE_AUTH, stdinbuf,
                       &auth_running)) {
      // Auth performed successfully. Terminate the other children.
      WatchSaverChild(dpy, w, 0, saver_executable, 0);
      // Now terminate the screen lock.
      return 1;
    }
  }

  // No auth child is running, either because we don't have any running and
  // didn't start one, or because it just terminated.
  // Show the screen saver.
  if (!auth_running) {
    WatchSaverChild(dpy, w, 0, saver_executable,
                    state != WATCH_CHILDREN_SAVER_DISABLED);
  }

  // Do not terminate the screen lock.
  return 0;
}

/*! \brief Wake up the screen saver in response to a keyboard or mouse event.
 *
 * \return If true, authentication was successful, and the program should exit.
 */
int WakeUp(Display *dpy, Window w, const char *stdinbuf) {
  return WatchChildren(dpy, w, WATCH_CHILDREN_FORCE_AUTH, stdinbuf);
}

/*! \brief An X11 error handler that merely logs errors to stderr.
 *
 * This is used to prevent X11 errors from terminating XSecureLock.
 */
int IgnoreErrorsHandler(Display *display, XErrorEvent *error) {
  char buf[128];
  XGetErrorText(display, error->error_code, buf, sizeof(buf));
  buf[sizeof(buf) - 1] = 0;
  fprintf(stderr, "Got non-fatal X11 error: %s.\n", buf);
  return 0;
}

/*! \brief Print an usage message.
 *
 * A message is shown explaining how to use XSecureLock.
 *
 * \param me The name this executable was invoked as.
 */
void usage(const char *me) {
  printf(
      "Usage:\n"
      "  env [variables...] %s [-- command to run when locked]\n"
      "\n"
      "Environment variables:\n"
      "  XSECURELOCK_AUTH=<auth module>\n"
      "  XSECURELOCK_FONT=<x11 font name>\n"
#ifdef HAVE_COMPOSITE
      "  XSECURELOCK_NO_COMPOSITE=<0|1>\n"
#endif
      "  XSECURELOCK_PAM_SERVICE=<PAM service name>\n"
      "  XSECURELOCK_SAVER=<saver module>\n"
      "  XSECURELOCK_WANT_FIRST_KEYPRESS=<0|1>\n"
      "\n"
      "Default auth module: " AUTH_EXECUTABLE
      "\n"
      "Default global saver module: " GLOBAL_SAVER_EXECUTABLE
      "\n"
      "Default per-screen saver module: " SAVER_EXECUTABLE
      "\n"
      "\n"
      "This software is licensed under the Apache 2.0 License. Details are\n"
      "available at the following location:\n"
      "  " DOCS_PATH "/COPYING\n",
      me);
}

/*! \brief Load default settings from environment variables.
 *
 * These settings override what was figured out at ./configure time.
 */
void load_defaults() {
  auth_executable = GetStringSetting("XSECURELOCK_AUTH", AUTH_EXECUTABLE);
  saver_executable =
      GetStringSetting("XSECURELOCK_GLOBAL_SAVER", GLOBAL_SAVER_EXECUTABLE);
#ifdef HAVE_COMPOSITE
  no_composite = GetIntSetting("XSECURELOCK_NO_COMPOSITE", 0);
#endif
}

/*! \brief Parse the command line arguments.
 *
 * This accepts saver_* or auth_* arguments, and puts them in their respective
 * global variable.
 *
 * This is DEPRECATED - use the XSECURELOCK_SAVER and XSECURELOCK_AUTH
 * environment variables instead!
 *
 * Possible errors will be printed on stderr.
 *
 * \return true if everything is OK, false otherwise.
 */
int parse_arguments(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], "auth_", 5)) {
      fprintf(stderr,
              "Setting auth child name from command line is DEPRECATED.\nUse "
              "the XSECURELOCK_AUTH environment variable instead.\n");
      auth_executable = argv[i];
      continue;
    }
    if (!strncmp(argv[i], "saver_", 6)) {
      fprintf(stderr,
              "Setting saver child name from command line is DEPRECATED.\nUse "
              "the XSECURELOCK_SAVER environment variable instead.\n");
      saver_executable = argv[i];
      continue;
    }
    if (!strcmp(argv[i], "--")) {
      notify_command = argv + i + 1;
      break;
    }
    // If we get here, the argument is unrecognized. Exit, then.
    fprintf(stderr, "Unrecognized argument: %s.\n", argv[i]);
    return 0;
  }
  return 1;
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
int check_settings() {
  if (auth_executable == NULL) {
    fprintf(stderr, "Auth module has not been specified in any way.\n");
    return 0;
  }
  if (strncmp(auth_executable, "auth_", 5)) {
    fprintf(stderr, "Auth module name must start with auth_.\n");
    return 0;
  }
  if (strchr(auth_executable, '.')) {
    fprintf(stderr, "Auth module name may not contain a dot.\n");
    return 0;
  }
  if (access(auth_executable, X_OK)) {
    fprintf(stderr, "Auth module must be executable.\n");
    return 0;
  }

  if (saver_executable == NULL) {
    fprintf(stderr, "Saver module has not been specified in any way.\n");
    return 0;
  }
  if (strncmp(saver_executable, "saver_", 6)) {
    fprintf(stderr, "Saver module name must start with saver_.\n");
    return 0;
  }
  if (strchr(saver_executable, '.')) {
    fprintf(stderr, "Saver module name may not contain a dot.\n");
    return 0;
  }
  if (access(saver_executable, X_OK)) {
    fprintf(stderr, "Saver module must be executable.\n");
    return 0;
  }

  return 1;
}

/*! \brief Raise a window if necessary.
 *
 * Does not cause any events if the window is already on the top.
 */
void MaybeRaiseWindow(Display *display, Window w) {
  Window root, parent, grandparent;
  Window *children, *siblings;
  unsigned int nchildren, nsiblings;
  if (!XQueryTree(display, w, &root, &parent, &children, &nchildren)) {
    fprintf(stderr, "XQueryTree failed.\n");
    return;
  }
  XFree(children);
  if (!XQueryTree(display, parent, &root, &grandparent, &siblings,
                  &nsiblings)) {
    fprintf(stderr, "XQueryTree failed.\n");
    return;
  }
  if (nsiblings == 0) {
    fprintf(stderr, "My parent window has no children.\n");
    XFree(siblings);
    return;
  }
  if (w == siblings[nsiblings - 1]) {
    // Already on top. Good!
  } else {
    // Need to bring myself to the top first.
    XRaiseWindow(display, w);
  }
  XFree(siblings);
}

/*! \brief Tell xss-lock or others that we're done locking.
 *
 * This enables xss-lock to delay going to sleep until the screen is actually
 * locked - useful to prevent information leaks after wakeup.
 *
 * \param fd The file descriptor of the X11 connection that we shouldn't close.
 */
void NotifyOfLock(int x11_fd) {
  int fd = GetIntSetting("XSS_SLEEP_LOCK_FD", -1);
  if (fd == x11_fd) {
    fprintf(stderr,
            "XSS_SLEEP_LOCK_FD matches DISPLAY - what?!? We're probably "
            "inhibiting sleep now.\n");
  } else if (fd != -1) {
    if (close(fd) != 0) {
      perror("close(XSS_SLEEP_LOCK_FD)");
    }
  }
  if (notify_command != NULL && *notify_command != NULL) {
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
    } else if (pid == 0) {
      // Child process.
      execvp(notify_command[0], notify_command);
      perror("execvp");
      exit(EXIT_FAILURE);
    } else {
      // Parent process after successful fork.
      notify_command_pid = pid;
    }
  }
}

/*! \brief The main program.
 *
 * Usage: see usage().
 */
int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");

  // Change the current directory to HELPER_PATH so we don't need to process
  // path names.
  if (chdir(HELPER_PATH)) {
    fprintf(stderr, "Could not switch to directory %s.\n", HELPER_PATH);
    return 1;
  }

  // Parse and verify arguments.
  load_defaults();
  if (!parse_arguments(argc, argv)) {
    usage(argv[0]);
    return 1;
  }
  if (!check_settings()) {
    usage(argv[0]);
    return 1;
  }

  // Connect to X11.
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Could not connect to $DISPLAY.\n");
    return 1;
  }

  // TODO(divVerent): Support that?
  if (ScreenCount(display) != 1) {
    fprintf(stderr,
            "Warning: 'Zaphod' configurations are not supported at this point. "
            "Only locking the default screen.\n");
  }

  // Who's the root?
  Window root_window = DefaultRootWindow(display);

  // Query the initial screen size, and get notified on updates.
  XSelectInput(display, root_window, StructureNotifyMask);
  int w = DisplayWidth(display, DefaultScreen(display));
  int h = DisplayHeight(display, DefaultScreen(display));
#ifdef DEBUG_EVENTS
  fprintf(stderr, "DisplayWidthHeight %d %d\n", w, h);
#endif

  // Prepare some nice window attributes for a screen saver window.
  XColor black;
  black.pixel = BlackPixel(display, DefaultScreen(display));
  XQueryColor(display, DefaultColormap(display, DefaultScreen(display)),
              &black);
  Pixmap bg = XCreateBitmapFromData(display, root_window, "\0", 1, 1);
  XSetWindowAttributes coverattrs;
  coverattrs.background_pixel = black.pixel;
  coverattrs.save_under = 1;
  coverattrs.override_redirect = 1;
  coverattrs.cursor =
      XCreatePixmapCursor(display, bg, bg, &black, &black, 0, 0);

  Window parent_window = root_window;

#ifdef HAVE_COMPOSITE
  int composite_event_base, composite_error_base, composite_major_version = 0,
                                                  composite_minor_version = 0;
  int have_composite =
      XCompositeQueryExtension(display, &composite_event_base,
                               &composite_error_base) &&
      // Require at least XComposite 0.3.
      XCompositeQueryVersion(display, &composite_major_version,
                             &composite_minor_version) &&
      (composite_major_version >= 1 || composite_minor_version >= 3);
  if (!have_composite) {
    fprintf(stderr, "XComposite extension not detected.\n");
  }
  if (have_composite && no_composite) {
    fprintf(stderr, "XComposite extension detected but disabled by user.\n");
    have_composite = 0;
  }
  Window composite_window;
  if (have_composite) {
    composite_window = XCompositeGetOverlayWindow(display, root_window);
    parent_window = composite_window;
  }
#endif

  // Create the two windows.
  // background_window is the outer window which exists for security reasons (in
  // case a subprocess may turn its window transparent or something).
  // saver_window is the "visible" window that the saver and auth children will
  // draw on. These windows are separated because XScreenSaver's savers might
  // XUngrabKeyboard on their window.
  Window background_window = XCreateWindow(
      display, parent_window, 0, 0, w, h, 0, CopyFromParent, InputOutput,
      CopyFromParent, CWBackPixel | CWSaveUnder | CWOverrideRedirect | CWCursor,
      &coverattrs);
  SetWMProperties(display, background_window, "xsecurelock", "background", argc,
                  argv);
  Window saver_window =
      XCreateWindow(display, background_window, 0, 0, w, h, 0, CopyFromParent,
                    InputOutput, CopyFromParent, CWBackPixel, &coverattrs);
  SetWMProperties(display, saver_window, "xsecurelock", "saver", argc, argv);

  // Let's get notified if we lose visibility, so we can self-raise.
  XSelectInput(display, parent_window, StructureNotifyMask | FocusChangeMask);
  XSelectInput(display, background_window,
               StructureNotifyMask | VisibilityChangeMask | FocusChangeMask);
  XSelectInput(display, saver_window,
               StructureNotifyMask | VisibilityChangeMask);

  // Make sure we stay always on top.
  XWindowChanges coverchanges;
  coverchanges.stack_mode = Above;
  XConfigureWindow(display, background_window, CWStackMode, &coverchanges);
  XConfigureWindow(display, saver_window, CWStackMode, &coverchanges);

  // We're OverrideRedirect anyway, but setting this hint may help compositors
  // leave our window alone.
  Atom state_atom = XInternAtom(display, "_NET_WM_STATE", False);
  Atom fullscreen_atom =
      XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
  XChangeProperty(display, background_window, state_atom, XA_ATOM, 32,
                  PropModeReplace, (const unsigned char *)&fullscreen_atom, 1);

  // Initialize XInput so we can get multibyte key events.
  XIM xim = XOpenIM(display, NULL, NULL, NULL);
  if (xim == NULL) {
    fprintf(stderr, "XOpenIM failed. Assuming Latin-1 encoding.\n");
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
    size_t i;
    for (i = 0; i < sizeof(input_styles) / sizeof(input_styles[0]); ++i) {
      // Note: we draw XIM stuff in saver_window so it's above the saver/auth
      // child. However, we receive events for the grab window.
      xic = XCreateIC(xim, XNInputStyle, input_styles[i], XNClientWindow,
                      saver_window, XNFocusWindow, background_window, NULL);
      if (xic != NULL) {
        break;
      }
    }
    if (xic == NULL) {
      fprintf(stderr, "XCreateIC failed. Assuming Latin-1 encoding.\n");
    }
  }

#ifdef HAVE_SCRNSAVER
  // If we support the screen saver extension, that'd be good.
  int scrnsaver_event_base, scrnsaver_error_base;
  if (!XScreenSaverQueryExtension(display, &scrnsaver_event_base,
                                  &scrnsaver_error_base)) {
    scrnsaver_event_base = 0;
    scrnsaver_error_base = 0;
  }
  XScreenSaverSelectInput(display, background_window, ScreenSaverNotifyMask);
#endif

#ifdef HAVE_XF86MISC
  // In case keys to disable grabs are available, turn them off for the duration
  // of the lock.
  if (XF86MiscSetGrabKeysState(display, False) != MiscExtGrabStateSuccess) {
    fprintf(stderr, "Could not set grab keys state.\n");
    return 1;
  }
#endif

  // Acquire all grabs we need. Retry in case the window manager is still
  // holding some grabs while starting XSecureLock.
  int retries;
  for (retries = 10; retries >= 0; --retries) {
    if (XGrabPointer(display, parent_window, False, ALL_POINTER_EVENTS,
                     GrabModeAsync, GrabModeAsync, None, coverattrs.cursor,
                     CurrentTime) == GrabSuccess) {
      break;
    }
    nanosleep(&(const struct timespec){0, 100000000L}, NULL);
  }
  if (retries < 0) {
    fprintf(stderr, "Could not grab pointer.\n");
    return 1;
  }
  for (retries = 10; retries >= 0; --retries) {
    if (XGrabKeyboard(display, parent_window, False, GrabModeAsync,
                      GrabModeAsync, CurrentTime) == GrabSuccess) {
      break;
    }
    nanosleep(&(const struct timespec){0, 100000000L}, NULL);
  }
  if (retries < 0) {
    fprintf(stderr, "Could not grab keyboard.\n");
    return 1;
  }

  // Map our windows.
  // This is done after grabbing so failure to grab does not blank the screen
  // yet, thereby "confirming" the screen lock.
  XMapRaised(display, background_window);
  XMapRaised(display, saver_window);

  // Private (possibly containing information about the user's password) data.
  // This data is locked to RAM using mlock() to avoid leakage to disk via swap.
  struct {
    // The received X event.
    XEvent ev;
    // The decoded key press.
    char buf[16];
    // The length of the data in buf.
    int len;
  } priv;
  if (MLOCK_PAGE(&priv, sizeof(priv)) < 0) {
    perror("mlock");
    return 1;
  }

  // Prevent X11 errors from killing XSecureLock. Instead, just keep going.
  XSetErrorHandler(IgnoreErrorsHandler);

  // Ignore SIGPIPE, as the auth child is explicitly allowed to close its
  // standard input file descriptor.
  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGPIPE, &sa, NULL);

  // Need to flush the display so savers sure can access the window.
  XFlush(display);

  enum WatchChildrenState requested_saver_state = WATCH_CHILDREN_NORMAL;
  int x11_fd = ConnectionNumber(display);
  int background_window_mapped = 0, saver_window_mapped = 0,
      xss_lock_notified = 0;
  for (;;) {
    // Watch children WATCH_CHILDREN_HZ times per second.
    fd_set in_fds;
    FD_ZERO(&in_fds);
    FD_SET(x11_fd, &in_fds);
    struct timeval tv;
    tv.tv_usec = 1000000 / WATCH_CHILDREN_HZ;
    tv.tv_sec = 0;
    select(x11_fd + 1, &in_fds, 0, 0, &tv);
    if (WatchChildren(display, saver_window, requested_saver_state, NULL)) {
      goto done;
    }

    // If something changed our cursor, change it back.
    XUndefineCursor(display, saver_window);

#ifdef REINSTATE_GRABS
    // This really should never be needed...
    if (XGrabPointer(display, parent_window, False, ALL_POINTER_EVENTS,
                     GrabModeAsync, GrabModeAsync, None, coverattrs.cursor,
                     CurrentTime) != GrabSuccess) {
      fprintf(stderr, "Critical: cannot re-grab pointer.\n");
    }
    if (XGrabKeyboard(display, parent_window, False, GrabModeAsync,
                      GrabModeAsync, CurrentTime) != GrabSuccess) {
      fprintf(stderr, "Critical: cannot re-grab keyboard.\n");
    }
#endif

#ifdef AUTO_RAISE
    MaybeRaiseWindow(display, saver_window);
    MaybeRaiseWindow(display, background_window);
#endif

    // Take care of zombies.
    // TODO(divVerent): Refactor waitpid handling as callers mostly do the same
    // handling anyway.
    if (notify_command_pid != 0) {
      int status;
      pid_t pid = waitpid(notify_command_pid, &status, WNOHANG);
      if (pid < 0) {
        switch (errno) {
          case ECHILD:
            // The process is dead. Fine.
            notify_command_pid = 0;
            break;
          case EINTR:
            // Waitpid was interrupted. Fine, assume it's still running.
            break;
          default:
            // Assume the child still lives. Shouldn't ever happen.
            perror("waitpid");
            break;
        }
      } else if (pid == notify_command_pid) {
        if (WIFEXITED(status)) {
          // Done notifying.
          if (WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "Notification command failed with status %d.\n",
                    WEXITSTATUS(status));
          }
          notify_command_pid = 0;
        }
        // Otherwise it was suspended or whatever. We need to keep waiting.
      } else if (pid == 0) {
        // We're still alive.
      } else {
        fprintf(stderr, "Unexpectedly woke up for PID %d.\n", (int)pid);
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
          fprintf(stderr, "ConfigureNotify %lu %d %d\n",
                  (unsigned long)priv.ev.xconfigure.window,
                  priv.ev.xconfigure.width, priv.ev.xconfigure.height);
#endif
          if (priv.ev.xconfigure.window == root_window) {
            // Root window size changed. Adjust the saver_window window too!
            w = priv.ev.xconfigure.width;
            h = priv.ev.xconfigure.height;
#ifdef DEBUG_EVENTS
            fprintf(stderr, "DisplayWidthHeight %d %d\n", w, h);
#endif
            XMoveResizeWindow(display, background_window, 0, 0, w, h);
            XMoveResizeWindow(display, saver_window, 0, 0, w, h);
            // Just in case - ConfigureNotify might also be sent for raising
          }
          // Also, whatever window has been reconfigured, should also be raised
          // to make sure.
          if (priv.ev.xconfigure.window == saver_window) {
            MaybeRaiseWindow(display, saver_window);
          } else if (priv.ev.xconfigure.window == background_window) {
            MaybeRaiseWindow(display, background_window);
          }
          break;
        case VisibilityNotify:
#ifdef DEBUG_EVENTS
          fprintf(stderr, "VisibilityNotify %lu %d\n",
                  (unsigned long)priv.ev.xvisibility.window,
                  priv.ev.xvisibility.state);
#endif
          if (priv.ev.xvisibility.state != VisibilityUnobscured) {
            // If something else shows an OverrideRedirect window, we want to
            // stay on top.
            if (priv.ev.xvisibility.window == saver_window) {
              XRaiseWindow(display, saver_window);
            } else if (priv.ev.xvisibility.window == background_window) {
              XRaiseWindow(display, background_window);
            } else {
              fprintf(stderr,
                      "Received unexpected VisibilityNotify for window %d.\n",
                      (int)priv.ev.xvisibility.window);
            }
          }
          break;
        case MotionNotify:
        case ButtonPress:
          // Mouse events launch the auth child.
          if (WakeUp(display, saver_window, NULL)) {
            goto done;
          }
          break;
        case KeyPress: {
          // Keyboard events launch the auth child.
          Status status = XLookupNone;
          int have_key = 1;
          if (xic) {
            // This uses the current locale.
            priv.len = XmbLookupString(xic, &priv.ev.xkey, priv.buf,
                                       sizeof(priv.buf) - 1, NULL, &status);
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
                                     sizeof(priv.buf) - 1, NULL, NULL);
            if (priv.len <= 0) {
              // Error or no output. Fine.
              have_key = 0;
            }
          }
          if (have_key && (size_t)priv.len >= sizeof(priv.buf)) {
            // Detect possible overruns. This should be unreachable.
            fprintf(stderr, "Received invalid length from XLookupString: %d\n",
                    priv.len);
            have_key = 0;
          }
          if (have_key) {
            // Map all newline-like things to newlines.
            if (priv.len == 1 && priv.buf[0] == '\r') {
              priv.buf[0] = '\n';
            }
            priv.buf[priv.len] = 0;
          } else {
            // No new bytes. Fine.
            priv.buf[0] = 0;
          }
          // In any case, the saver will be activated.
          if (WakeUp(display, saver_window, priv.buf)) {
            goto done;
          }
        } break;
        case KeyRelease:
        case ButtonRelease:
        case MappingNotify:
        case EnterNotify:
        case LeaveNotify:
          // Ignored.
          break;
        case MapNotify:
#ifdef DEBUG_EVENTS
          fprintf(stderr, "MapNotify %lu\n",
                  (unsigned long)priv.ev.xmap.window);
#endif
          if (priv.ev.xmap.window == background_window) {
            background_window_mapped = 1;
          } else if (priv.ev.xmap.window == saver_window) {
            saver_window_mapped = 1;
          }
          if (background_window_mapped && saver_window_mapped &&
              !xss_lock_notified) {
            NotifyOfLock(x11_fd);
            xss_lock_notified = 1;
          }
          break;
        case UnmapNotify:
#ifdef DEBUG_EVENTS
          fprintf(stderr, "UnmapNotify %lu\n",
                  (unsigned long)priv.ev.xmap.window);
#endif
          if (priv.ev.xmap.window == background_window) {
            background_window_mapped = 0;
          } else if (priv.ev.xmap.window == saver_window) {
            saver_window_mapped = 0;
          }
          break;
        case FocusIn:
        case FocusOut:
#ifdef DEBUG_EVENTS
          fprintf(stderr, "Focus%d %lu\n", priv.ev.xfocus.mode,
                  (unsigned long)priv.ev.xfocus.window);
#endif
          if (priv.ev.xfocus.window == parent_window &&
              priv.ev.xfocus.mode == NotifyUngrab) {
            fprintf(stderr, "WARNING: lost grab, trying to grab again.\n");
            if (XGrabKeyboard(display, parent_window, False, GrabModeAsync,
                              GrabModeAsync, CurrentTime) != GrabSuccess) {
              fprintf(stderr,
                      "Critical: lost grab but cannot re-grab keyboard.\n");
            }
            if (XGrabPointer(display, parent_window, False, ALL_POINTER_EVENTS,
                             GrabModeAsync, GrabModeAsync, None, None,
                             CurrentTime) != GrabSuccess) {
              fprintf(stderr,
                      "Critical: lost grab but cannot re-grab pointer.\n");
            }
          }
          break;
        default:
#ifdef DEBUG_EVENTS
          fprintf(stderr, "Event%d %lu\n", priv.ev.type,
                  (unsigned long)priv.ev.xany.window);
#endif
#ifdef HAVE_SCRNSAVER
          // Handle screen saver notifications. If the screen is blanked anyway,
          // turn off the saver child.
          if (scrnsaver_event_base != 0 &&
              priv.ev.type == scrnsaver_event_base + ScreenSaverNotify) {
            if (((XScreenSaverNotifyEvent *)&priv.ev)->state == ScreenSaverOn) {
              requested_saver_state = WATCH_CHILDREN_SAVER_DISABLED;
            } else {
              requested_saver_state = WATCH_CHILDREN_NORMAL;
            }
            break;
          }
#endif
          fprintf(stderr, "Received unexpected event %d.\n", priv.ev.type);
          break;
      }
    }
  }

done:
  // Free our resources, and exit.
  XDestroyWindow(display, saver_window);
  XDestroyWindow(display, background_window);
#ifdef HAVE_COMPOSITE
  if (have_composite) {
    XCompositeReleaseOverlayWindow(display, composite_window);
  }
#endif
  XFreeCursor(display, coverattrs.cursor);
  XFreePixmap(display, bg);

  return EXIT_SUCCESS;
}
