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
 *\brief Screen dimmer.
 *
 *A simple tool to dim the screen, then wait a little so a screen locker can
 *take over.
 *
 *Sample usage:
 *  xset s 300 2
 *  xss-lock -n dim-screen -l xsecurelock
 */

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../env_settings.h"
#include "../wm_properties.h"

// Get the entry of value index of the Bayer matrix for n = 2^power.
void Bayer(int index, int power, int *x, int *y) {
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
  switch (index % 4) {
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

int HaveCompositor(Display *display) {
  char buf[64];
  snprintf(buf, sizeof(buf), "_NET_WM_CM_S%d", (int)DefaultScreen(display));
  buf[sizeof(buf) - 1] = 0;
  Atom atom = XInternAtom(display, buf, False);
  return XGetSelectionOwner(display, atom) != None;
}

int dim_time_ms;
int wait_time_ms;
int min_fps;

XColor dim_color;

struct DimEffect {
  void (*PreCreateWindow)(void *self, Display *display,
                          XSetWindowAttributes *dimattrs,
                          unsigned long *dimmask);
  void (*PostCreateWindow)(void *self, Display *display, Window dim_window);
  void (*DrawFrame)(void *self, Display *display, Window dim_window, int frame,
                    int w, int h);

  int frame_count;
};

struct DitherEffect {
  struct DimEffect super;
  int pattern_power;

  Pixmap pattern;
  XGCValues gc_values;
  GC dim_gc, pattern_gc;
};

void DitherEffectPreCreateWindow(void *unused_self, Display *unused_display,
                                 XSetWindowAttributes *unused_dimattrs,
                                 unsigned long *unused_dimmask) {
  (void)unused_self;
  (void)unused_display;
  (void)unused_dimattrs;
  (void)unused_dimmask;
}

void DitherEffectPostCreateWindow(void *self, Display *display,
                                  Window dim_window) {
  struct DitherEffect *dimmer = self;

  // Create a pixmap to define the pattern we want to set as the window shape.
  dimmer->gc_values.foreground = 0;
  dimmer->pattern =
      XCreatePixmap(display, dim_window, 1 << dimmer->pattern_power,
                    1 << dimmer->pattern_power, 1);
  dimmer->pattern_gc =
      XCreateGC(display, dimmer->pattern, GCForeground, &dimmer->gc_values);
  XFillRectangle(display, dimmer->pattern, dimmer->pattern_gc, 0, 0,
                 1 << dimmer->pattern_power, 1 << dimmer->pattern_power);
  XSetForeground(display, dimmer->pattern_gc, 1);

  // Create a pixmap to define the shape of the screen-filling window (which
  // will increase over time).
  dimmer->gc_values.fill_style = FillStippled;
  dimmer->gc_values.foreground = dim_color.pixel;
  dimmer->gc_values.stipple = dimmer->pattern;
  dimmer->dim_gc =
      XCreateGC(display, dim_window, GCFillStyle | GCForeground | GCStipple,
                &dimmer->gc_values);
}

void DitherEffectDrawFrame(void *self, Display *display, Window dim_window,
                           int frame, int w, int h) {
  struct DitherEffect *dimmer = self;

  int x, y;
  Bayer(frame, dimmer->pattern_power, &x, &y);
  XDrawPoint(display, dimmer->pattern, dimmer->pattern_gc, x, y);
  // Draw the pattern on the window.
  XChangeGC(display, dimmer->dim_gc, GCStipple, &dimmer->gc_values);
  XFillRectangle(display, dim_window, dimmer->dim_gc, 0, 0, w, h);
}

void DitherEffectInit(struct DitherEffect *dimmer, Display *unused_display) {
  (void)unused_display;

  // Ensure dimming at least at a defined frame rate.
  dimmer->pattern_power = 3;
  // Total time of effect if we wouldn't stop after 7/8 of fading out.
  double total_time_ms = dim_time_ms * 8.0 / 7.0;
  // Minimum "total" frame count of the animation.
  double total_frames_min = total_time_ms / 1000.0 * min_fps;
  // This actually computes ceil(log2(sqrt(total_frames_min))) but cannot fail.
  frexp(sqrt(total_frames_min), &dimmer->pattern_power);
  // Clip extreme/unsupported values.
  if (dimmer->pattern_power < 2) {
    dimmer->pattern_power = 2;
  }
  if (dimmer->pattern_power > 8) {
    dimmer->pattern_power = 8;
  }
  // Generate the frame count and vtable.
  dimmer->super.frame_count =
      7 << (2 * dimmer->pattern_power - 3);  // i.e. 7/8 of the pixels.
  dimmer->super.PreCreateWindow = DitherEffectPreCreateWindow;
  dimmer->super.PostCreateWindow = DitherEffectPostCreateWindow;
  dimmer->super.DrawFrame = DitherEffectDrawFrame;
}

struct OpacityEffect {
  struct DimEffect super;

  Atom property_atom;
};

void OpacityEffectPreCreateWindow(void *unused_self, Display *unused_display,
                                  XSetWindowAttributes *dimattrs,
                                  unsigned long *dimmask) {
  (void)unused_self;
  (void)unused_display;

  dimattrs->background_pixel = dim_color.pixel;
  *dimmask |= CWBackPixel;
}

#define MIN_OPACITY 0x00000000
#define MAX_OPACITY 0x637982ca  // 1/8 transparent.
// sRGB conversion in bc: obase=16; (2^32-1) * (1.055*e(l(1/8)/2.4) - 0.055)

void OpacityEffectPostCreateWindow(void *self, Display *display,
                                   Window dim_window) {
  struct OpacityEffect *dimmer = self;

  long value = MIN_OPACITY;
  XChangeProperty(display, dim_window, dimmer->property_atom, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&value, 1);
}

void OpacityEffectDrawFrame(void *self, Display *display, Window dim_window,
                            int frame, int unused_w, int unused_h) {
  struct OpacityEffect *dimmer = self;
  (void)unused_w;
  (void)unused_h;

  long value = MAX_OPACITY - (MAX_OPACITY - MIN_OPACITY) /
                                 dimmer->super.frame_count *
                                 (dimmer->super.frame_count - frame - 1);
  XChangeProperty(display, dim_window, dimmer->property_atom, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&value, 1);
}

void OpacityEffectInit(struct OpacityEffect *dimmer, Display *display) {
  dimmer->property_atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);

  // Generate the frame count and vtable.
  dimmer->super.frame_count = dim_time_ms * min_fps / 1000;
  dimmer->super.PreCreateWindow = OpacityEffectPreCreateWindow;
  dimmer->super.PostCreateWindow = OpacityEffectPostCreateWindow;
  dimmer->super.DrawFrame = OpacityEffectDrawFrame;
}

int main(int argc, char **argv) {
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Could not connect to $DISPLAY.\n");
    return 1;
  }
  Window root_window = DefaultRootWindow(display);

  // Load global settings.
  dim_time_ms = GetIntSetting("XSECURELOCK_DIM_TIME_MS", 2000);
  wait_time_ms = GetIntSetting("XSECURELOCK_WAIT_TIME_MS", 5000);
  min_fps = GetIntSetting("XSECURELOCK_DIM_MIN_FPS", 30);
  int have_compositor = GetIntSetting(
      "XSECURELOCK_DIM_OVERRIDE_COMPOSITOR_DETECTION", HaveCompositor(display));

  struct DitherEffect dither_dimmer;
  struct OpacityEffect opacity_dimmer;
  struct DimEffect *dimmer;
  if (have_compositor) {
    OpacityEffectInit(&opacity_dimmer, display);
    dimmer = &opacity_dimmer.super;
  } else {
    DitherEffectInit(&dither_dimmer, display);
    dimmer = &dither_dimmer.super;
  }

  // Prepare the background color.
  dim_color.pixel = BlackPixel(display, DefaultScreen(display));
  XQueryColor(display, DefaultColormap(display, DefaultScreen(display)),
              &dim_color);

  // Create a simple screen-filling window.
  int w = DisplayWidth(display, DefaultScreen(display));
  int h = DisplayHeight(display, DefaultScreen(display));
  XSetWindowAttributes dimattrs;
  dimattrs.save_under = 1;
  dimattrs.override_redirect = 1;
  unsigned long dimmask = CWSaveUnder | CWOverrideRedirect;
  dimmer->PreCreateWindow(dimmer, display, &dimattrs, &dimmask);
  Window dim_window =
      XCreateWindow(display, root_window, 0, 0, w, h, 0, CopyFromParent,
                    InputOutput, CopyFromParent, dimmask, &dimattrs);
  SetWMProperties(display, dim_window, "xsecurelock", "dim", argc, argv);
  dimmer->PostCreateWindow(dimmer, display, dim_window);

  // Precalculate the sleep time per step.
  unsigned long long sleep_time_ns =
      (dim_time_ms * 1000000ULL) / dimmer->frame_count;
  struct timespec sleep_ts;
  sleep_ts.tv_sec = sleep_time_ns / 1000000000;
  sleep_ts.tv_nsec = sleep_time_ns % 1000000000;
  XMapRaised(display, dim_window);
  for (int i = 0; i < dimmer->frame_count; ++i) {
    // Advance the dim pattern by one step.
    dimmer->DrawFrame(dimmer, display, dim_window, i, w, h);
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
