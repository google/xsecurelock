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

#include <X11/X.h>      // for Window, Atom, CopyFromParent, GCForegr...
#include <X11/Xatom.h>  // for XA_CARDINAL
#include <X11/Xlib.h>   // for Display, XColor, XSetWindowAttributes
#include <math.h>       // for pow, ceil, frexp, nextafter, sqrt
#include <stdio.h>      // for NULL, snprintf
#include <stdlib.h>     // for abort
#include <time.h>       // for nanosleep, timespec

#include "../env_settings.h"   // for GetIntSetting, GetDoubleSetting, GetSt...
#include "../logging.h"        // for Log
#include "../wm_properties.h"  // for SetWMProperties

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
  switch (index & 3) {
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
    default:
      // Logically impossible, but clang-analyzer needs help here.
      abort();
      break;
  }
}

int HaveCompositor(Display *display) {
  char buf[64];
  int buflen =
      snprintf(buf, sizeof(buf), "_NET_WM_CM_S%d", (int)DefaultScreen(display));
  if (buflen <= 0 || buflen >= (size_t)sizeof(buf)) {
    Log("Wow, pretty long screen number you got there");
    return 0;
  }
  Atom atom = XInternAtom(display, buf, False);
  return XGetSelectionOwner(display, atom) != None;
}

int dim_time_ms;
int wait_time_ms;
int min_fps;
double dim_alpha;

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
  *unused_dimmask = *unused_dimmask;  // Shut up clang-analyzer.
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
  // Total time of effect if we wouldn't stop after dim_alpha of fading out.
  double total_time_ms = dim_time_ms / dim_alpha;
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
      ceil(pow(1 << dimmer->pattern_power, 2) * dim_alpha);
  dimmer->super.PreCreateWindow = DitherEffectPreCreateWindow;
  dimmer->super.PostCreateWindow = DitherEffectPostCreateWindow;
  dimmer->super.DrawFrame = DitherEffectDrawFrame;
}

struct OpacityEffect {
  struct DimEffect super;

  Atom property_atom;
  double dim_color_brightness;
};

void OpacityEffectPreCreateWindow(void *unused_self, Display *unused_display,
                                  XSetWindowAttributes *dimattrs,
                                  unsigned long *dimmask) {
  (void)unused_self;
  (void)unused_display;

  dimattrs->background_pixel = dim_color.pixel;
  *dimmask |= CWBackPixel;
}

void OpacityEffectPostCreateWindow(void *self, Display *display,
                                   Window dim_window) {
  struct OpacityEffect *dimmer = self;

  long value = 0;
  XChangeProperty(display, dim_window, dimmer->property_atom, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&value, 1);
}

double sRGBToLinear(double value) {
  return (value <= 0.04045) ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

double LinearTosRGB(double value) {
  return (value <= 0.0031308) ? 12.92 * value
                              : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

void OpacityEffectDrawFrame(void *self, Display *display, Window dim_window,
                            int frame, int unused_w, int unused_h) {
  struct OpacityEffect *dimmer = self;
  (void)unused_w;
  (void)unused_h;

  // Calculate the linear-space alpha we want to be fading to.
  double linear_alpha = (frame + 1) * dim_alpha / dimmer->super.frame_count;
  double linear_min = linear_alpha * dimmer->dim_color_brightness;
  double linear_max =
      linear_alpha * dimmer->dim_color_brightness + (1.0 - linear_alpha);

  // Calculate the sRGB-space alpha we thus must select to get the same color
  // range.
  double srgb_min = LinearTosRGB(linear_min);
  double srgb_max = LinearTosRGB(linear_max);
  double srgb_alpha = 1.0 - (srgb_max - srgb_min);
  // Note: this may have a different brightness level, here we're simply
  // solving for the same contrast as the "dither" mode.

  // Log("Got: [%f..%f], want: [%f..%f]",
  //     srgb_alpha * LinearTosRGB(dimmer->dim_color_brightness),
  //     srgb_alpha * LinearTosRGB(dimmer->dim_color_brightness) +
  //         (1.0 - srgb_alpha),
  //     srgb_min, srgb_max);

  // Convert to an opacity value.
  long value = nextafter(0xffffffff, 0) * srgb_alpha;
  XChangeProperty(display, dim_window, dimmer->property_atom, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&value, 1);
}

void OpacityEffectInit(struct OpacityEffect *dimmer, Display *display) {
  dimmer->property_atom = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
  dimmer->dim_color_brightness =
      sRGBToLinear(dim_color.red / 65535.0) * 0.2126 +
      sRGBToLinear(dim_color.green / 65535.0) * 0.7152 +
      sRGBToLinear(dim_color.blue / 65535.0) * 0.0722;

  // Generate the frame count and vtable.
  dimmer->super.frame_count = ceil(dim_time_ms * min_fps / 1000.0);
  dimmer->super.PreCreateWindow = OpacityEffectPreCreateWindow;
  dimmer->super.PostCreateWindow = OpacityEffectPostCreateWindow;
  dimmer->super.DrawFrame = OpacityEffectDrawFrame;
}

int main(int argc, char **argv) {
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    Log("Could not connect to $DISPLAY");
    return 1;
  }
  Window root_window = DefaultRootWindow(display);

  // Load global settings.
  dim_time_ms = GetIntSetting("XSECURELOCK_DIM_TIME_MS", 2000);
  wait_time_ms = GetIntSetting("XSECURELOCK_WAIT_TIME_MS", 5000);
  min_fps = GetIntSetting("XSECURELOCK_DIM_MIN_FPS", 30);
  dim_alpha = GetDoubleSetting("XSECURELOCK_DIM_ALPHA", 0.875);
  int have_compositor = GetIntSetting(
      "XSECURELOCK_DIM_OVERRIDE_COMPOSITOR_DETECTION", HaveCompositor(display));

  if (dim_alpha <= 0 || dim_alpha > 1) {
    Log("XSECURELOCK_DIM_ALPHA must be in ]0..1] - using default");
    dim_alpha = 0.875;
  }

  // Prepare the background color.
  Colormap colormap = DefaultColormap(display, DefaultScreen(display));
  const char *color_name = GetStringSetting("XSECURELOCK_DIM_COLOR", "black");
  XParseColor(display, colormap, color_name, &dim_color);
  if (XAllocColor(display, colormap, &dim_color)) {
    // Log("Allocated color %lu = %d %d %d", dim_color.pixel, dim_color.red,
    //     dim_color.green, dim_color.blue);
  } else {
    dim_color.pixel = BlackPixel(display, DefaultScreen(display));
    XQueryColor(display, colormap, &dim_color);
    Log("Could not allocate color or unknown color name: %s", color_name);
  }

  // Set up the filter.
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

  // Create a simple screen-filling window.
  int w = DisplayWidth(display, DefaultScreen(display));
  int h = DisplayHeight(display, DefaultScreen(display));
  XSetWindowAttributes dimattrs = {0};
  dimattrs.save_under = 1;
  dimattrs.override_redirect = 1;
  unsigned long dimmask = CWSaveUnder | CWOverrideRedirect;
  dimmer->PreCreateWindow(dimmer, display, &dimattrs, &dimmask);
  Window dim_window =
      XCreateWindow(display, root_window, 0, 0, w, h, 0, CopyFromParent,
                    InputOutput, CopyFromParent, dimmask, &dimattrs);
  // Not using the xsecurelock WM_CLASS here as this window shouldn't prevent
  // forcing grabs.
  SetWMProperties(display, dim_window, "xsecurelock-dimmer", "dim", argc, argv);
  dimmer->PostCreateWindow(dimmer, display, dim_window);

  // Precalculate the sleep time per step.
  unsigned long long sleep_time_ns =
      (dim_time_ms * 1000000ULL) / dimmer->frame_count;
  struct timespec sleep_ts;
  sleep_ts.tv_sec = sleep_time_ns / 1000000000;
  sleep_ts.tv_nsec = sleep_time_ns % 1000000000;
  XMapRaised(display, dim_window);
  int i;
  for (i = 0; i < dimmer->frame_count; ++i) {
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
  sleep_ts.tv_nsec = (wait_time_ms % 1000) * 1000000L;
  nanosleep(&sleep_ts, NULL);

  return 0;
}
