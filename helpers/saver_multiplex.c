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

#include <X11/X.h>                // for Window, GCBackground, etc
#include <X11/Xlib.h>             // for XDrawString, XGCValues, etc
#include <signal.h>
#include <stdio.h>                // for fprintf, stderr, NULL, etc
#include <stdlib.h>               // for free, getenv, calloc, exit, etc
#include <string.h>               // for strlen

#ifdef HAVE_XKB
#include <X11/XKBlib.h>
#endif

#include "../saver_child.h"
#include "../xscreensaver_api.h"
#include "monitors.h"

volatile int sigterm = 0;

static void handle_sigterm(int) {
  sigterm = 1;
}

#define MAX_MONITORS MAX_SAVERS

static Display *display;
static Monitor monitors[MAX_MONITORS];
static size_t num_monitors;
static Window windows[MAX_MONITORS];

static void SpawnSavers(Window parent) {
  XSetWindowAttributes attrs;
  coverattrs.background_pixel = BlackPixel(display, DefaultScreen(display));;
  for (size_t i = 0; i < num_monitors; ++i) {
    windows[i] = XCreateWindow(display, parent, monitors[i].x, monitors[i].y,
                               monitors[i].width, monitors[i].height, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixel | CWCursor, &attrs);
    XMapWindow(display, windows[i]);
    WatchSaverChild(display, windows[i], i, saver_executable, 1);
  }
}

static void KillSavers() {
  for (size_t i = 0; i < num_monitors; ++i) {
    WatchSaverChild(display, windows[i], i, saver_executable, 0);
    XDestroyWindow(windows[i]);
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

  window = ReadWindowID();
  if (window == None) {
    fprintf(stderr, "Invalid/no window ID in XSCREENSAVER_WINDOW.\n");
    return 1;
  }

  SelectMonitorChangeEvents(display, window);

  num_monitors = GetMonitors(display, window, monitors, MAX_MONITORS);
  SpawnSavers(window);

  signal(SIGTERM, handle_sigterm);
  for (;;) {
    // We're using non-blocking X11 event handling and select() so we can
    // reliably catch SIGTERM and exit the loop.
    fd_set in_fds;
    FD_ZERO(&in_fds);
    FD_SET(x11_fd, &in_fds);
    select(x11_fd + 1, &in_fds, 0, 0, NULL);
    if (sigterm) {
      break;
    }
    XEvent ev;
    while (XPending(display) && (XNextEvent(display, &priv.ev), 1)) {
      if (IsMonitorChangeEvent(ev)) {
        Monitor new_monitors[MAX_SAVERS];
        size_t new_num_monitors =
            GetMonitors(display, window, monitors, MAX_SAVERS);
        if (new_monitors != monitors ||
            memcmp(new_monitors, monitors, sizeof(monitors))) {
          KillSavers();
          num_monitors = new_num_monitors;
          memcpy(monitors, new_monitors, sizeof(monitors));
          SpawnSavers(window);
        }
      }
    }
  }

  // Kill all the savers when exiting.
  KillSavers();

  return 0;
}
