/*
Copyright 2018 Google Inc. All rights reserved.

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

#include <X11/X.h>       // for Window, CopyFromParent, CWBackPixel
#include <X11/Xlib.h>    // for XEvent, XFlush, XNextEvent, XOpenDi...
#include <signal.h>      // for signal, SIGTERM
#include <stdio.h>       // for fprintf, NULL, stderr
#include <stdlib.h>      // for setenv
#include <string.h>      // for memcmp, memcpy
#include <sys/select.h>  // for select, FD_SET, FD_ZERO, fd_set
#include <unistd.h>      // for sleep

#include "../env_settings.h"      // for GetStringSetting
#include "../saver_child.h"       // for MAX_SAVERS
#include "../wm_properties.h"     // for SetWMProperties
#include "../xscreensaver_api.h"  // for ReadWindowID
#include "monitors.h"             // for IsMonitorChangeEvent, Monitor, Sele...

volatile sig_atomic_t sigterm = 0;

static void handle_sigterm(int unused_signo) {
  (void)unused_signo;
  sigterm = 1;
}

static void handle_sigchld(int unused_signo) {
  // No handling needed - we just want to interrupt the select() in the main
  // loop.
  (void)unused_signo;
}

#define MAX_MONITORS MAX_SAVERS

static const char* saver_executable;

static Display* display;
static Monitor monitors[MAX_MONITORS];
static size_t num_monitors;
static Window windows[MAX_MONITORS];

static void WatchSavers(void) {
  size_t i;
  for (i = 0; i < num_monitors; ++i) {
    WatchSaverChild(display, windows[i], i, saver_executable, 1);
  }
}

static void SpawnSavers(Window parent, int argc, char* const* argv) {
  XSetWindowAttributes attrs;
  attrs.background_pixel = BlackPixel(display, DefaultScreen(display));
  size_t i;
  for (i = 0; i < num_monitors; ++i) {
    windows[i] =
        XCreateWindow(display, parent, monitors[i].x, monitors[i].y,
                      monitors[i].width, monitors[i].height, 0, CopyFromParent,
                      InputOutput, CopyFromParent, CWBackPixel, &attrs);
    SetWMProperties(display, windows[i], "xsecurelock",
                    "saver_multiplex_screen", argc, argv);
    XMapRaised(display, windows[i]);
  }
  // Need to flush the display so savers sure can access the window.
  XFlush(display);
  WatchSavers();
}

static void KillSavers(void) {
  size_t i;
  for (i = 0; i < num_monitors; ++i) {
    WatchSaverChild(display, windows[i], i, saver_executable, 0);
    XDestroyWindow(display, windows[i]);
  }
}

/*! \brief The main program.
 *
 * Usage: XSCREENSAVER_WINDOW=window_id ./saver_multiplex
 *
 * Spawns spearate saver subprocesses, one on each screen.
 */
int main(int argc, char** argv) {
  if (GetIntSetting("XSECURELOCK_INSIDE_SAVER_MULTIPLEX", 0)) {
    fprintf(stderr, "starting saver_multiplex inside saver_multiplex?!?\n");
    // If we die, the parent process will revive us, so let's sleep a while to
    // conserve battery and avoid log spam in this case.
    sleep(60);
    return 1;
  }
  setenv("XSECURELOCK_INSIDE_SAVER_MULTIPLEX", "1", 1);

  if ((display = XOpenDisplay(NULL)) == NULL) {
    fprintf(stderr, "could not connect to $DISPLAY\n");
    return 1;
  }
  int x11_fd = ConnectionNumber(display);

  Window parent = ReadWindowID();
  if (parent == None) {
    fprintf(stderr, "Invalid/no parent ID in XSCREENSAVER_WINDOW.\n");
    return 1;
  }

  saver_executable = GetStringSetting("XSECURELOCK_SAVER", SAVER_EXECUTABLE);

  SelectMonitorChangeEvents(display, parent);
  num_monitors = GetMonitors(display, parent, monitors, MAX_MONITORS);

  SpawnSavers(parent, argc, argv);

  // We're using non-blocking X11 event handling and pselect() so we can
  // reliably catch SIGTERM and exit the loop. Also, SIGCHLD (screen saver
  // dies) will interrupt the select() as well and let WatchSavers() respawn
  // that saver.
  sigset_t added_sigmask, old_sigmask;
  sigemptyset(&added_sigmask);
  sigaddset(&added_sigmask, SIGTERM);
  sigaddset(&added_sigmask, SIGCHLD);
  sigemptyset(&old_sigmask);
  if (sigprocmask(SIG_BLOCK, &added_sigmask, &old_sigmask) == 0) {
    // Only when we could actually block the signals, install the handlers.
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handle_sigterm;
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
      perror("sigaction(SIGTERM)");
    }
    sa.sa_handler = handle_sigchld;
    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
      perror("sigaction(SIGCHLD)");
    }
  } else {
    perror("sigprocmask failed; not installing signal handlers");
  }
  for (;;) {
    fd_set in_fds;
    FD_ZERO(&in_fds);
    FD_SET(x11_fd, &in_fds);
    pselect(x11_fd + 1, &in_fds, 0, 0, NULL, &old_sigmask);
    if (sigterm) {
      break;
    }
    WatchSavers();
    XEvent ev;
    while (XPending(display) && (XNextEvent(display, &ev), 1)) {
      if (IsMonitorChangeEvent(display, ev.type)) {
        Monitor new_monitors[MAX_SAVERS];
        size_t new_num_monitors =
            GetMonitors(display, parent, new_monitors, MAX_SAVERS);
        if (new_num_monitors != num_monitors ||
            memcmp(new_monitors, monitors, sizeof(monitors))) {
          KillSavers();
          num_monitors = new_num_monitors;
          memcpy(monitors, new_monitors, sizeof(monitors));
          SpawnSavers(parent, argc, argv);
        }
      }
    }
  }
  sigprocmask(SIG_SETMASK, &old_sigmask, NULL);

  // Kill all the savers when exiting.
  KillSavers();

  return 0;
}
