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

#include <X11/X.h>       // for CopyFromParent, etc
#include <X11/Xlib.h>    // for XEvent, etc
#include <X11/Xutil.h>   // for XLookupString
#include <locale.h>      // for NULL, setlocale, LC_CTYPE
#include <signal.h>      // for sigaction, sigemptyset, etc
#include <stdio.h>       // for fprintf, stderr
#include <stdlib.h>      // for EXIT_SUCCESS
#include <string.h>      // for strchr, strncmp
#include <sys/select.h>  // for select, FD_SET, FD_ZERO, etc
#include <sys/time.h>    // for timeval
#include <time.h>        // for nanosleep, timespec
#include <unistd.h>      // for access, X_OK

#ifdef HAVE_SCRNSAVER
#include <X11/extensions/scrnsaver.h>
#endif
#ifdef HAVE_XF86MISC
#include <X11/extensions/xf86misc.h>
#endif

#include "auth_child.h"
#include "mlock_page.h"
#include "saver_child.h"

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
const char *auth_executable = AUTH_EXECUTABLE;
//! The name of the saver child to execute, relative to HELPER_PATH.
const char *saver_executable = SAVER_EXECUTABLE;

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
int WatchChildren(enum WatchChildrenState state, const char *stdinbuf) {
  int auth_running = WantAuthChild(state == WATCH_CHILDREN_FORCE_AUTH);

  // Note: auth_running is true whenever we WANT to run authentication, or it is
  // already running. It may have recently terminated, which we will notice
  // later.
  if (auth_running) {
    // Make sure the saver is shut down to not interfere with the screen.
    WatchSaverChild(saver_executable, 0);

    // Actually start the auth child, or notice termination.
    if (WatchAuthChild(auth_executable, state == WATCH_CHILDREN_FORCE_AUTH,
                       stdinbuf, &auth_running)) {
      // Auth performed successfully. Terminate the other children.
      WatchSaverChild(saver_executable, 0);
      // Now terminate the screen lock.
      return 1;
    }
  }

  // No auth child is running, either because we don't have any running and
  // didn't start one, or because it just terminated.
  // Show the screen saver.
  if (!auth_running) {
    WatchSaverChild(saver_executable, state != WATCH_CHILDREN_SAVER_DISABLED);
  }

  // Do not terminate the screen lock.
  return 0;
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
      "  %s [auth_<authname>] [saver_<savername>]\n"
      "\n"
      "Example:\n"
      "  %s auth_pam_x11 saver_blank\n"
      "\n"
      "If an argument is not specified, the default will be used, which can\n"
      "be set in the $XSECURELOCK_AUTH and $XSECURELOCK_SAVER environment\n"
      "variables.\n"
      "\n"
      "Current default auth module: %s\n"
      "Current default saver module: %s\n"
      "\n"
      "This software is licensed under the Apache 2.0 License. Details are\n"
      "available at the following location:\n"
      "  " DOCS_PATH "/COPYING\n",
      me, me, auth_executable, saver_executable);
}

/*! \brief Load default settings from environment variables.
 *
 * These settings override what was figured out at ./configure time.
 */
void load_defaults() {
  const char *str = getenv("XSECURELOCK_AUTH");
  if (str != NULL && str[0] != 0) {
    auth_executable = str;
  }
  str = getenv("XSECURELOCK_SAVER");
  if (str != NULL && str[0] != 0) {
    saver_executable = str;
  }
}

/*! \brief Parse the command line arguments.
 *
 * This accepts saver_* or auth_* arguments, and puts them in their respective
 * global variable.
 *
 * Possible errors will be printed on stderr.
 *
 * \return true if everything is OK, false otherwise.
 */
int parse_arguments(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], "auth_", 5)) {
      auth_executable = argv[i];
      continue;
    }
    if (!strncmp(argv[i], "saver_", 6)) {
      saver_executable = argv[i];
      continue;
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
  if (strchr(auth_executable, '.')) {
    fprintf(stderr, "Auth module name may not contain a dot.\n");
    return 0;
  }
  if (access(auth_executable, X_OK)) {
    fprintf(stderr, "Auth module must be executable.\n");
    return 0;
  }

  if (saver_executable == NULL) {
    fprintf(stderr, "Auth module has not been specified in any way.\n");
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

/*! \brief The main program.
 *
 * Usage: see usage().
 */
int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");

  // Change the current directory to HELPER_PATH so we don't need to process
  // path names.
  if (chdir(HELPER_PATH)) {
    fprintf(stderr, "chdir %s", HELPER_PATH);
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

  // Who's the root?
  Window root_window = DefaultRootWindow(display);

  // Query the initial screen size, and get notified on updates.
  XSelectInput(display, root_window, StructureNotifyMask);
  int w = DisplayWidth(display, DefaultScreen(display));
  int h = DisplayHeight(display, DefaultScreen(display));

  // Prepare some nice window attributes for a screen saver window.
  XColor Black;
  Black.pixel = BlackPixel(display, DefaultScreen(display));
  XQueryColor(display, DefaultColormap(display, DefaultScreen(display)),
              &Black);
  Pixmap bg = XCreatePixmap(display, root_window, 1, 1, 1);
  XSetWindowAttributes coverattrs;
  coverattrs.background_pixel = Black.pixel;
  coverattrs.save_under = 1;
  coverattrs.override_redirect = 1;
  coverattrs.cursor =
      XCreatePixmapCursor(display, bg, bg, &Black, &Black, 0, 0);

  // Create the two windows.
  // grab_window is the outer window which we grab input on.
  // saver_window is the "visible" window that the savers will draw on.
  // These windows are separated because XScreenSaver's savers might
  // XUngrabKeyboard on their window.
  Window grab_window = XCreateWindow(
      display, root_window, 0, 0, w, h, 0, CopyFromParent, InputOutput,
      CopyFromParent, CWOverrideRedirect | CWSaveUnder, &coverattrs);
  Window saver_window = XCreateWindow(
      display, grab_window, 0, 0, w, h, 0, CopyFromParent, InputOutput,
      CopyFromParent, CWBackPixel | CWCursor, &coverattrs);

  // Let's get notified if we lose visibility, so we can self-raise.
  XSelectInput(display, grab_window, VisibilityChangeMask | FocusChangeMask);
  XSelectInput(display, saver_window, VisibilityChangeMask);

  // Make sure we stay always on top.
  XWindowChanges coverchanges;
  coverchanges.stack_mode = Above;
  XConfigureWindow(display, grab_window, CWStackMode, &coverchanges);
  XConfigureWindow(display, saver_window, CWStackMode, &coverchanges);

  // Now provide the window ID as an environment variable (like XScreenSaver).
  char window_id_str[16];
  size_t window_id_len = snprintf(window_id_str, sizeof(window_id_str), "%lu",
                                  (unsigned long)saver_window);
  if (window_id_len <= 0 || window_id_len >= sizeof(window_id_str)) {
    fprintf(stderr, "Window ID doesn't fit into buffer.\n");
    return 1;
  }
  setenv("XSCREENSAVER_WINDOW", window_id_str, 1);

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
                      saver_window, XNFocusWindow, grab_window, NULL);
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
  XScreenSaverSelectInput(display, grab_window, ScreenSaverNotifyMask);
#endif

#ifdef HAVE_XF86MISC
  // In case keys to disable grabs are available, turn them off for the duration
  // of the lock.
  if (XF86MiscSetGrabKeysState(display, False) != MiscExtGrabStateSuccess) {
    fprintf(stderr, "Could not set grab keys state.\n");
    return 1;
  }
#endif

  // Map our windows.
  XMapWindow(display, grab_window);
  XMapWindow(display, saver_window);

  // Acquire all grabs we need. Retry in case the window manager is still
  // holding some grabs while starting XSecureLock.
  int retries;
  for (retries = 10; retries >= 0; --retries) {
    if (XGrabPointer(display, grab_window, False, ALL_POINTER_EVENTS,
                      GrabModeAsync, GrabModeAsync, None, None, CurrentTime) == GrabSuccess) {
      break;
    }
    nanosleep(&(const struct timespec){0, 100000000L}, NULL);
  }
  if (retries < 0) {
    fprintf(stderr, "Could not grab pointer.\n");
    return 1;
  }
  for (retries = 10; retries >= 0; --retries) {
    if (XGrabKeyboard(display, grab_window, False, GrabModeAsync, GrabModeAsync,
                       CurrentTime) == GrabSuccess) {
      break;
    }
    nanosleep(&(const struct timespec){0, 100000000L}, NULL);
  }
  if (retries < 0) {
    fprintf(stderr, "Could not grab keyboard.\n");
    return 1;
  }

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

  enum WatchChildrenState saver_state = WATCH_CHILDREN_NORMAL;
  int x11_fd = ConnectionNumber(display);
  for (;;) {
    // Watch children WATCH_CHILDREN_HZ times per second.
    fd_set in_fds;
    FD_ZERO(&in_fds);
    FD_SET(x11_fd, &in_fds);
    struct timeval tv;
    tv.tv_usec = 1000000 / WATCH_CHILDREN_HZ;
    tv.tv_sec = 0;
    select(x11_fd + 1, &in_fds, 0, 0, &tv);
    if (WatchChildren(saver_state, NULL)) {
      goto done;
    }

    // If something changed our cursor, change it back.
    XDefineCursor(display, saver_window, coverattrs.cursor);

#ifdef REINSTATE_GRABS
    // This really should never be needed...
    if (XGrabPointer(display, grab_window, False, ALL_POINTER_EVENTS,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
      fprintf(stderr, "Critical: cannot re-grab pointer.\n");
    }
    if (XGrabKeyboard(display, grab_window, False, GrabModeAsync, GrabModeAsync,
                      CurrentTime) != GrabSuccess) {
      fprintf(stderr, "Critical: cannot re-grab keyboard.\n");
    }
#endif

    // Handle all events.
    while (XPending(display) && (XNextEvent(display, &priv.ev), 1)) {
      if (XFilterEvent(&priv.ev, None)) {
        // If an input method ate the event, ignore it.
        continue;
      }
      switch (priv.ev.type) {
        case ConfigureNotify:
          if (priv.ev.xconfigure.window == root_window) {
            // Root window size changed. Adjust the saver_window window too!
            w = priv.ev.xconfigure.width;
            h = priv.ev.xconfigure.height;
            XMoveResizeWindow(display, grab_window, 0, 0, w, h);
            XMoveResizeWindow(display, saver_window, 0, 0, w, h);
          }
          break;
        case VisibilityNotify:
          if (priv.ev.xvisibility.state != VisibilityUnobscured) {
            // If something else shows an OverrideRedirect window, we want to
            // stay on top.
            if (priv.ev.xvisibility.window == saver_window) {
              XRaiseWindow(display, saver_window);
            } else if (priv.ev.xvisibility.window == grab_window) {
              XRaiseWindow(display, grab_window);
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
          if (WatchChildren(WATCH_CHILDREN_FORCE_AUTH, NULL)) {
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
          if (WatchChildren(WATCH_CHILDREN_FORCE_AUTH, priv.buf)) {
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
        case FocusIn:
        case FocusOut:
          if (priv.ev.xfocus.window == grab_window &&
              priv.ev.xfocus.mode == NotifyUngrab) {
            fprintf(stderr, "WARNING: lost grab, trying to grab again.\n");
            if (XGrabKeyboard(display, grab_window, False, GrabModeAsync,
                              GrabModeAsync, CurrentTime) != GrabSuccess) {
              fprintf(stderr,
                      "Critical: lost grab but cannot re-grab keyboard.\n");
            }
            if (XGrabPointer(display, grab_window, False, ALL_POINTER_EVENTS,
                             GrabModeAsync, GrabModeAsync, None, None,
                             CurrentTime) != GrabSuccess) {
              fprintf(stderr,
                      "Critical: lost grab but cannot re-grab pointer.\n");
            }
          }
          break;
        default:
#ifdef HAVE_SCRNSAVER
          // Handle screen saver notifications. If the screen is blanked anyway,
          // turn off the saver child.
          if (scrnsaver_event_base != 0 &&
              priv.ev.type == scrnsaver_event_base + ScreenSaverNotify) {
            if (((XScreenSaverNotifyEvent *)&priv.ev)->state == ScreenSaverOn) {
              saver_state = WATCH_CHILDREN_SAVER_DISABLED;
            } else {
              saver_state = WATCH_CHILDREN_NORMAL;
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
  XDestroyWindow(display, grab_window);
  XFreeCursor(display, coverattrs.cursor);
  XFreePixmap(display, bg);

  return EXIT_SUCCESS;
}
