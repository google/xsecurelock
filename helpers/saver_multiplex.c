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

#include <X11/X.h>                // for Window, CopyFromParent, CWBackPixel
#include <X11/Xlib.h>             // for XEvent, XFlush, XNextEvent, XOpenDi...
#include <signal.h>               // for signal, SIGTERM
#include <stdio.h>                // for fprintf, NULL, stderr
#include <string.h>               // for memcmp, memcpy
#include <sys/select.h>           // for select, FD_SET, FD_ZERO, fd_set

#include "../env_settings.h"      // for GetStringSetting
#include "../saver_child.h"       // for MAX_SAVERS
#include "../xscreensaver_api.h"  // for ReadWindowID
#include "monitors.h"             // for IsMonitorChangeEvent, Monitor, Sele...

volatile int sigterm = 0;

static void handle_sigterm(int unused_signo) {
  (void) unused_signo;
  sigterm = 1;
}

#define MAX_MONITORS MAX_SAVERS

static const char *saver_executable;

static Display *display;
static Monitor monitors[MAX_MONITORS];
static size_t num_monitors;
static Window windows[MAX_MONITORS];

static void WatchSavers(void) {
  for (size_t i = 0; i < num_monitors; ++i) {
    WatchSaverChild(display, windows[i], i, saver_executable, 1);
  }
}

static void SpawnSavers(Window parent) {
  XSetWindowAttributes attrs;
  attrs.background_pixel = BlackPixel(display, DefaultScreen(display));;
  for (size_t i = 0; i < num_monitors; ++i) {
    windows[i] =
        XCreateWindow(display, parent, monitors[i].x, monitors[i].y,
                      monitors[i].width, monitors[i].height, 0, CopyFromParent,
                      InputOutput, CopyFromParent, CWBackPixel, &attrs);
    XMapRaised(display, windows[i]);
  }
  // Need to flush the display so savers sure can access the window.
  XFlush(display);
  WatchSavers();
}

static void KillSavers(void) {
  for (size_t i = 0; i < num_monitors; ++i) {
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
int main() {
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

  SpawnSavers(parent);

  signal(SIGTERM, handle_sigterm);
  for (;;) {
    // We're using non-blocking X11 event handling and select() so we can
    // reliably catch SIGTERM and exit the loop. Also, SIGCHLD (screen saver
    // dies) will interrupt the select() as well and let WatchSavers() respawn
    // that saver.
    fd_set in_fds;
    FD_ZERO(&in_fds);
    FD_SET(x11_fd, &in_fds);
    select(x11_fd + 1, &in_fds, 0, 0, NULL);
    if (sigterm) {
      break;
    }
    WatchSavers();
    XEvent ev;
    while (XPending(display) && (XNextEvent(display, &ev), 1)) {
      if (IsMonitorChangeEvent(display, ev.type)) {
        Monitor new_monitors[MAX_SAVERS];
        size_t new_num_monitors =
            GetMonitors(display, parent, monitors, MAX_SAVERS);
        if (new_monitors != monitors ||
            memcmp(new_monitors, monitors, sizeof(monitors))) {
          KillSavers();
          num_monitors = new_num_monitors;
          memcpy(monitors, new_monitors, sizeof(monitors));
          SpawnSavers(parent);
        }
      }
    }
  }

  // Kill all the savers when exiting.
  KillSavers();

  return 0;
}
