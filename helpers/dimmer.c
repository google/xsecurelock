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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../env_settings.h"
#include "../wm_properties.h"

// Get the entry of value index of the Bayer matrix for n = 2^power.
void Bayer(int index, int power, int* x, int* y) {
  // M_1 = [1].
  if (power == 0) {
    *x = 0;
    *y = 0;
    return;
  }
  // M_{2n} = [[4Mn 4M_n+2] [4M_n+3 4M_n+1]]
  int subx, suby;
  Bayer(index >> 2, power - 1, &subx, &suby);
  int n = 1 << (power - 1);
  switch(index % 4) {
    case 0:
      *x = subx;
      *y = suby;
      break;
    case 1:
      *x = subx + n;
      *y = suby + n;
      break;
    case 2:
      *x = subx + n;
      *y = suby;
      break;
    case 3:
      *x = subx;
      *y = suby + n;
      break;
  }
}

int main(int argc, char** argv) {
  int dim_time_ms = GetIntSetting("XSECURELOCK_DIM_TIME_MS", 2000);
  int wait_time_ms = GetIntSetting("XSECURELOCK_WAIT_TIME_MS", 5000);
  int min_fps = GetIntSetting("XSECURELOCK_DIM_MIN_FPS", 30);

  // Ensure dimming at least at a defined frame rate.
  int pattern_power = 3;
  // Total time of effect if we wouldn't stop after 7/8 of fading out.
  double total_time_ms = dim_time_ms * 8.0 / 7.0;
  // Minimum "total" frame count of the animation.
  double total_frames_min = total_time_ms / 1000.0 * min_fps;
  // This actually computes ceil(log2(sqrt(total_frames_min))) but cannot fail.
  frexp(sqrt(total_frames_min), &pattern_power);
  // Clip extreme/unsupported values.
  if (pattern_power < 2) {
    pattern_power = 2;
  }
  if (pattern_power > 8) {
    pattern_power = 8;
  }

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
  Pixmap pattern = XCreatePixmap(display, dim_window, 1 << pattern_power,
                                 1 << pattern_power, 1);
  GC pattern_gc = XCreateGC(display, pattern, GCForeground, &gc_values);
  XFillRectangle(display, pattern, pattern_gc, 0, 0, 1 << pattern_power,
                 1 << pattern_power);
  XSetForeground(display, pattern_gc, 1);

  // Create a pixmap to define the shape of the screen-filling window (which
  // will increase over time).
  gc_values.fill_style = FillStippled;
  gc_values.stipple = pattern;
  GC dim_gc =
      XCreateGC(display, dim_window, GCFillStyle | GCStipple, &gc_values);

  // Precalculate the sleep time per step.
  int coord_count = 7 << (2 * pattern_power - 3);  // i.e. 7/8 of the pixels.
  unsigned long long sleep_time_ns = (dim_time_ms * 1000000ULL) / coord_count;
  struct timespec sleep_ts;
  sleep_ts.tv_sec = sleep_time_ns / 1000000000;
  sleep_ts.tv_nsec = sleep_time_ns % 1000000000;
  for (int i = 0; i < coord_count; ++i) {
    // Advance the dim pattern by one step.
    int x, y;
    Bayer(i, pattern_power, &x, &y);
    XDrawPoint(display, pattern, pattern_gc, x, y);
    // Draw the pattern on the window.
    XChangeGC(display, dim_gc, GCStipple, &gc_values);
    XFillRectangle(display, dim_window, dim_gc, 0, 0, w, h);
    // In the first iteration, map our window.
    if (i == 0) {
      XMapRaised(display, dim_window);
    }
    // Draw it!
    XFlush(display);
    // Sleep a while. Yes, even at the end now - we want the user to see this
    // after all.
    nanosleep(&sleep_ts, NULL);
  }

  // Wait a bit at the end (to hand over to the screen locker without
  // flickering).
  sleep_ts.tv_sec = wait_time_ms / 1000;
  sleep_ts.tv_nsec = (sleep_time_ns % 1000) * 1000000L;
  nanosleep(&sleep_ts, NULL);

  return 0;
}
