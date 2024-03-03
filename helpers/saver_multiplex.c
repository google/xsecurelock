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
#include "../logging.h"           // for Log, LogErrno
#include "../saver_child.h"       // for MAX_SAVERS
#include "../wait_pgrp.h"         // for InitWaitPgrp
#include "../wm_properties.h"     // for SetWMProperties
#include "../xscreensaver_api.h"  // for ReadWindowID
#include "monitors.h"             // for IsMonitorChangeEvent, Monitor, Sele...

static void HandleSIGUSR1(int signo) {
  KillAllSaverChildrenSigHandler(signo);  // Dirty, but quick.
}

static void HandleSIGTERM(int signo) {
  KillAllSaverChildrenSigHandler(signo);  // Dirty, but quick.
  raise(signo);                           // Destroys windows we created anyway.
}

#define MAX_MONITORS MAX_SAVERS

static const char* saver_executable;

static Display* display;
static Monitor monitors[MAX_MONITORS];
static size_t num_monitors;
static Window windows[MAX_MONITORS];

static void WatchSavers(void) {
  for (size_t i = 0; i < num_monitors; ++i) {
    WatchSaverChild(display, windows[i], i, saver_executable, 1);
  }
}

static void SpawnSavers(Window parent, int argc, char* const* argv) {
  for (size_t i = 0; i < num_monitors; ++i) {
    windows[i] =
        XCreateWindow(display, parent, monitors[i].x, monitors[i].y,
                      monitors[i].width, monitors[i].height, 0, CopyFromParent,
                      InputOutput, CopyFromParent, 0, NULL);
    SetWMProperties(display, windows[i], "xsecurelock",
                    "saver_multiplex_screen", argc, argv);
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
int main(int argc, char** argv) {
  if (GetIntSetting("XSECURELOCK_INSIDE_SAVER_MULTIPLEX", 0)) {
    Log("Starting saver_multiplex inside saver_multiplex?!?");
    // If we die, the parent process will revive us, so let's sleep a while to
    // conserve battery and avoid log spam in this case.
    sleep(60);
    return 1;
  }
  setenv("XSECURELOCK_INSIDE_SAVER_MULTIPLEX", "1", 1);

  if ((display = XOpenDisplay(NULL)) == NULL) {
    Log("Could not connect to $DISPLAY");
    return 1;
  }
  int x11_fd = ConnectionNumber(display);

  Window parent = ReadWindowID();
  if (parent == None) {
    Log("Invalid/no parent ID in XSCREENSAVER_WINDOW");
    return 1;
  }

  saver_executable =
      GetExecutablePathSetting("XSECURELOCK_SAVER", SAVER_EXECUTABLE, 0);

  SelectMonitorChangeEvents(display, parent);
  num_monitors = GetMonitors(display, parent, monitors, MAX_MONITORS);

  SpawnSavers(parent, argc, argv);

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = HandleSIGUSR1;  // To kill children.
  if (sigaction(SIGUSR1, &sa, NULL) != 0) {
    LogErrno("sigaction(SIGUSR1)");
  }
  sa.sa_flags = SA_RESETHAND;     // It re-raises to suicide.
  sa.sa_handler = HandleSIGTERM;  // To kill children.
  if (sigaction(SIGTERM, &sa, NULL) != 0) {
    LogErrno("sigaction(SIGTERM)");
  }

  InitWaitPgrp();

  for (;;) {
    fd_set in_fds;
    FD_ZERO(&in_fds);
    FD_SET(x11_fd, &in_fds);
    select(x11_fd + 1, &in_fds, 0, 0, NULL);
    WatchSavers();
    XEvent ev;
    while (XPending(display) && (XNextEvent(display, &ev), 1)) {
      if (IsMonitorChangeEvent(display, ev.type)) {
        Monitor new_monitors[MAX_SAVERS];
        size_t new_num_monitors =
            GetMonitors(display, parent, new_monitors, MAX_SAVERS);
        if (new_num_monitors != num_monitors ||
            memcmp(new_monitors, monitors, sizeof(monitors)) != 0) {
          KillSavers();
          num_monitors = new_num_monitors;
          memcpy(monitors, new_monitors, sizeof(monitors));
          SpawnSavers(parent, argc, argv);
        }
      }
    }
  }

  return 0;
}
