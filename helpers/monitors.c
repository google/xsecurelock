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

#include "monitors.h"

#include <X11/Xlib.h>  // for XWindowAttributes, Display, XGetW...
#include <stdio.h>     // for fprintf, stderr
#include <stdlib.h>    // for qsort
#include <string.h>    // for memcmp, memset

#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>  // for XRRCrtcInfo, XRROutputInfo, XRRSc...
#include <X11/extensions/randr.h>   // for RRNotify, RRCrtcChangeNotifyMask
#if RANDR_MAJOR > 1 || (RANDR_MAJOR == 1 && RANDR_MINOR >= 5)
#define HAVE_XRANDR15
#endif
#endif

#include "../env_settings.h"  // for GetIntSetting

#ifdef HAVE_XRANDR
static Display* initialized_for = NULL;
static int have_xrandr12;
#ifdef HAVE_XRANDR15
static int have_xrandr15;
#endif
static int event_base;
static int error_base;

static int MaybeInitXRandR(Display* dpy) {
  if (dpy != initialized_for) {
    have_xrandr12 = 0;
#ifdef HAVE_XRANDR15
    have_xrandr15 = 0;
#endif
    if (XRRQueryExtension(dpy, &event_base, &error_base)) {
      int major, minor;
      if (XRRQueryVersion(dpy, &major, &minor)) {
        // XRandR before 1.2 can't connect multiple screens to one, so the
        // default root window size tracking is sufficient for that.
        if (major > 1 || (major == 1 && minor >= 2)) {
          if (!GetIntSetting("XSECURELOCK_NO_XRANDR", 0)) {
            have_xrandr12 = 1;
          }
        }
#ifdef HAVE_XRANDR15
        if (major > 1 || (major == 1 && minor >= 5)) {
          if (!GetIntSetting("XSECURELOCK_NO_XRANDR15", 0)) {
            have_xrandr15 = 1;
          }
        }
#endif
      }
    }
    initialized_for = dpy;
  }
  return have_xrandr12;
}
#endif

#define CLAMP(x, mi, ma) ((x) < (mi) ? (mi) : (x) > (ma) ? (ma) : (x))

static int CompareMonitors(const void* a, const void* b) {
  return memcmp(a, b, sizeof(Monitor));
}

static int IntervalsOverlap(int astart, int asize, int bstart, int bsize) {
  // Compute exclusive bounds.
  int aend = astart + asize;
  int bend = bstart + bsize;
  // If one interval starts at or after the other, there's no overlap.
  if (astart >= bend || bstart >= aend) {
    return 0;
  }
  // Otherwise, there must be an overlap.
  return 1;
}

static void AddMonitor(Monitor* out_monitors, size_t* num_monitors,
                       size_t max_monitors, int x, int y, int w, int h) {
#ifdef DEBUG_EVENTS
  fprintf(stderr, "try %d %d %d %d\n", x, y, w, h);
#endif
  // Too many monitors? Stop collecting them.
  if (*num_monitors >= max_monitors) {
#ifdef DEBUG_EVENTS
    fprintf(stderr, "skip (too many)\n");
#endif
    return;
  }
  // Skip empty "monitors".
  if (w <= 0 || h <= 0) {
#ifdef DEBUG_EVENTS
    fprintf(stderr, "skip (zero)\n");
#endif
    return;
  }
  // Skip overlapping "monitors" (typically in cloned display setups).
  size_t i;
  for (i = 0; i < *num_monitors; ++i) {
    if (IntervalsOverlap(x, w, out_monitors[i].x, out_monitors[i].width) &&
        IntervalsOverlap(y, h, out_monitors[i].y, out_monitors[i].height)) {
#ifdef DEBUG_EVENTS
      fprintf(stderr, "skip (overlap with %d)\n", (int)i);
#endif
      return;
    }
  }
#ifdef DEBUG_EVENTS
  fprintf(stderr, "monitor %d = %d %d %d %d\n", (int)*num_monitors, x, y, w, h);
#endif
  out_monitors[*num_monitors].x = x;
  out_monitors[*num_monitors].y = y;
  out_monitors[*num_monitors].width = w;
  out_monitors[*num_monitors].height = h;
  ++*num_monitors;
}

#ifdef HAVE_XRANDR
static int GetMonitorsXRandR12(Display* dpy, Window w, int wx, int wy, int ww,
                               int wh, Monitor* out_monitors,
                               size_t* out_num_monitors, size_t max_monitors) {
  XRRScreenResources* screenres = XRRGetScreenResources(dpy, w);
  if (screenres == NULL) {
    return 0;
  }
  int i;
  for (i = 0; i < screenres->noutput; ++i) {
    XRROutputInfo* output =
        XRRGetOutputInfo(dpy, screenres, screenres->outputs[i]);
    if (output == NULL) {
      continue;
    }
    if (output->connection == RR_Connected) {
      // NOTE: If an output has multiple Crtcs (i.e. if the screen is
      // cloned), we only look at the first. Let's assume that the center
      // of that one should always be onscreen anyway (even though they
      // may not be, as cloned displays can have different panning
      // settings).
      RRCrtc crtc =
          (output->crtc ? output->crtc : output->ncrtc ? output->crtcs[0] : 0);
      XRRCrtcInfo* info = (crtc ? XRRGetCrtcInfo(dpy, screenres, crtc) : 0);
      if (info != NULL) {
        int x = CLAMP(info->x, wx, wx + ww) - wx;
        int y = CLAMP(info->y, wy, wy + wh) - wy;
        int w = CLAMP(info->x + (int)info->width, wx + x, wx + ww) - (wx + x);
        int h = CLAMP(info->y + (int)info->height, wy + y, wy + wh) - (wy + y);
        AddMonitor(out_monitors, out_num_monitors, max_monitors, x, y, w, h);
        XRRFreeCrtcInfo(info);
      }
      XRRFreeOutputInfo(output);
    }
  }
  XRRFreeScreenResources(screenres);
  return *out_num_monitors != 0;
}

#ifdef HAVE_XRANDR15
static int GetMonitorsXRandR15(Display* dpy, Window w, int wx, int wy, int ww,
                               int wh, Monitor* out_monitors,
                               size_t* out_num_monitors, size_t max_monitors) {
  if (!have_xrandr15) {
    return 0;
  }
  int num_rrmonitors;
  XRRMonitorInfo* rrmonitors = XRRGetMonitors(dpy, w, 1, &num_rrmonitors);
  if (rrmonitors == NULL) {
    return 0;
  }
  int i;
  for (i = 0; i < num_rrmonitors; ++i) {
    XRRMonitorInfo* info = &rrmonitors[i];
    int x = CLAMP(info->x, wx, wx + ww) - wx;
    int y = CLAMP(info->y, wy, wy + wh) - wy;
    int w = CLAMP(info->x + info->width, wx + x, wx + ww) - (wx + x);
    int h = CLAMP(info->y + info->height, wy + y, wy + wh) - (wy + y);
    AddMonitor(out_monitors, out_num_monitors, max_monitors, x, y, w, h);
  }
  XRRFreeMonitors(rrmonitors);
  return *out_num_monitors != 0;
}
#endif

static int GetMonitorsXRandR(Display* dpy, Window w,
                             const XWindowAttributes* xwa,
                             Monitor* out_monitors, size_t* out_num_monitors,
                             size_t max_monitors) {
  if (!MaybeInitXRandR(dpy)) {
    return 0;
  }

  // Translate to absolute coordinates so we can compare them to XRandR data.
  int wx, wy;
  Window child;
  if (!XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), xwa->x, xwa->y,
                             &wx, &wy, &child)) {
    fprintf(stderr, "XTranslateCoordinates failed.\n");
    wx = xwa->x;
    wy = xwa->y;
  }

#ifdef HAVE_XRANDR15
  if (GetMonitorsXRandR15(dpy, w, wx, wy, xwa->width, xwa->height, out_monitors,
                          out_num_monitors, max_monitors)) {
    return 1;
  }
#endif

  return GetMonitorsXRandR12(dpy, w, wx, wy, xwa->width, xwa->height,
                             out_monitors, out_num_monitors, max_monitors);
}
#endif

static void GetMonitorsGuess(const XWindowAttributes* xwa,
                             Monitor* out_monitors, size_t* out_num_monitors,
                             size_t max_monitors) {
  // XRandR-less dummy fallback.
  size_t guessed_monitors = CLAMP((size_t)(xwa->width * 9 + xwa->height * 8) /
                                      (size_t)(xwa->height * 16),  //
                                  1, max_monitors);
  size_t i;
  for (i = 0; i < guessed_monitors; ++i) {
    int x = xwa->width * i / guessed_monitors;
    int y = 0;
    int w = (xwa->width * (i + 1) / guessed_monitors) -
            (xwa->width * i / guessed_monitors);
    int h = xwa->height;
    AddMonitor(out_monitors, out_num_monitors, max_monitors, x, y, w, h);
  }
}

size_t GetMonitors(Display* dpy, Window w, Monitor* out_monitors,
                   size_t max_monitors) {
  if (max_monitors < 1) {
    return 0;
  }

  size_t num_monitors = 0;

  // As outputs will be relative to the window, we have to query its attributes.
  XWindowAttributes xwa;
  XGetWindowAttributes(dpy, w, &xwa);

#ifdef HAVE_XRANDR
  if (!GetMonitorsXRandR(dpy, w, &xwa, out_monitors, &num_monitors,
                         max_monitors))
#endif
    GetMonitorsGuess(&xwa, out_monitors, &num_monitors, max_monitors);

  // Sort the monitors in some deterministic order.
  qsort(out_monitors, num_monitors, sizeof(*out_monitors), CompareMonitors);

  // Fill the rest with zeros.
  if (num_monitors < max_monitors) {
    memset(out_monitors + num_monitors, 0,
           (max_monitors - num_monitors) * sizeof(*out_monitors));
  }

  return num_monitors;
}

void SelectMonitorChangeEvents(Display* dpy, Window w) {
#ifdef HAVE_XRANDR
  if (MaybeInitXRandR(dpy)) {
    XRRSelectInput(dpy, w,
                   RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask |
                       RROutputChangeNotifyMask);
  }
#else
  (void)dpy;
  (void)w;
#endif
}

int IsMonitorChangeEvent(Display* dpy, int type) {
#ifdef HAVE_XRANDR
  if (MaybeInitXRandR(dpy)) {
    switch (type - event_base) {
      case RRScreenChangeNotify:
      case RRNotify + RRNotify_CrtcChange:
      case RRNotify + RRNotify_OutputChange:
        return 1;
      default:
        return 0;
    }
  }
#else
  (void)dpy;
  (void)type;
#endif

  // XRandR-less dummy fallback.
  return 0;
}
