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

/*!
 * \brief Screen dimmer.
 *
 * A simple tool to dim the screen, then wait a little so a screen locker can
 * take over.
 *
 * Sample usage:
 *   xset s 300 2
 *   xss-lock -n dim-screen -l xsecurelock
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../env_settings.h"
#include "../wm_properties.h"

#define PATTERN_SIZE 8

int main(int argc, char** argv) {
  int dim_time_ms = GetIntSetting("XSECURELOCK_DIM_TIME_MS", 2000);
  int wait_time_ms = GetIntSetting("XSECURELOCK_WAIT_TIME_MS", 5000);

  Display* display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Could not connect to $DISPLAY.\n");
    return 1;
  }
  Window root_window = DefaultRootWindow(display);

  // Create a simple screen-filling window.
  int w = DisplayWidth(display, DefaultScreen(display));
  int h = DisplayHeight(display, DefaultScreen(display));
  XColor black;
  black.pixel = BlackPixel(display, DefaultScreen(display));
  XQueryColor(display, DefaultColormap(display, DefaultScreen(display)),
              &black);
  XSetWindowAttributes dimattrs;
  dimattrs.save_under = 1;
  dimattrs.override_redirect = 1;
  Window dim_window = XCreateWindow(
      display, root_window, 0, 0, w, h, 0, CopyFromParent, InputOutput,
      CopyFromParent, CWSaveUnder | CWOverrideRedirect, &dimattrs);
  SetWMProperties(display, dim_window, "xsecurelock", "dim", argc, argv);

  // Create a pixmap to define the pattern we want to set as the window shape.
  XGCValues gc_values;
  gc_values.foreground = 0;
  Pixmap pattern =
      XCreatePixmap(display, dim_window, PATTERN_SIZE, PATTERN_SIZE, 1);
  GC pattern_gc = XCreateGC(display, pattern, GCForeground, &gc_values);
  XFillRectangle(display, pattern, pattern_gc, 0, 0, PATTERN_SIZE,
                 PATTERN_SIZE);
  XSetForeground(display, pattern_gc, 1);

  // Create a pixmap to define the shape of the screen-filling window (which
  // will increase over time).
  gc_values.fill_style = FillStippled;
  gc_values.stipple = pattern;
  GC dim_gc =
      XCreateGC(display, dim_window, GCFillStyle | GCStipple, &gc_values);

  // Define a random order to draw the pixels.
  int coords[2 * PATTERN_SIZE * PATTERN_SIZE];
  for (int y = 0; y < PATTERN_SIZE; ++y) {
    for (int x = 0; x < PATTERN_SIZE; ++x) {
      coords[2 * (y * PATTERN_SIZE + x)] = x;
      coords[2 * (y * PATTERN_SIZE + x) + 1] = y;
    }
  }
  srand(time(NULL));
  for (int i = 0; i < PATTERN_SIZE * PATTERN_SIZE; ++i) {
    int j = rand() % (PATTERN_SIZE * PATTERN_SIZE - i) + i;  // In [i, n-1].
    int h = coords[2 * i];
    coords[2 * i] = coords[2 * j];
    coords[2 * j] = h;
    h = coords[2 * i + 1];
    coords[2 * i + 1] = coords[2 * j + 1];
    coords[2 * j + 1] = h;
  }

  // Precalculate the sleep time per step.
  unsigned long long sleep_time_ns =
      (dim_time_ms * 1000000ULL) / (PATTERN_SIZE * PATTERN_SIZE - 2);
  struct timespec sleep_ts;
  sleep_ts.tv_sec = sleep_time_ns / 1000000000;
  sleep_ts.tv_nsec = sleep_time_ns % 1000000000;
  for (int i = 0; i < PATTERN_SIZE * PATTERN_SIZE - 1; ++i) {
    // Sleep a while (except for the first iteration).
    if (i != 0) {
      nanosleep(&sleep_ts, NULL);
    }
    // Advance the dim pattern by one step.
    int x = coords[2 * i];
    int y = coords[2 * i + 1];
    XDrawPoint(display, pattern, pattern_gc, x, y);
    // Draw the pattern on the window.
    XFillRectangle(display, dim_window, dim_gc, 0, 0, w, h);
    // In the first iteration, map our window.
    if (i == 0) {
      XMapRaised(display, dim_window);
    }
    // Draw it!
    XFlush(display);
  }

  // Wait a bit at the end (to hand over to the screen locker without
  // flickering).
  sleep_ts.tv_sec = wait_time_ms / 1000;
  sleep_ts.tv_nsec = (sleep_time_ns % 1000) * 1000000L;
  nanosleep(&sleep_ts, NULL);

  return 0;
}
