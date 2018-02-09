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

#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif

#ifdef HAVE_XRANDR
static Display* initialized_for = NULL;
static int supported;
static int event_base;
static int error_base;

static int MaybeInitXRandR(Display* dpy) {
  if (dpy != initialized_for) {
    supported = 0;
    if (XRRQueryExtension(dpy, &event_base, &error_base)) {
      int major, minor;
      if (XRRQueryVersion(dpy, &major, &minor)) {
        // XRandR before 1.2 can't connect multiple screens to one, so the
        // default root window size tracking is sufficient for that.
        if (major > 1 || (major == 1 && minor >= 2)) {
          supported = 1;
        }
      }
    }
    initialized_for = dpy;
  }
  return supported;
}
#endif

#define CLAMP(x, mi, ma) ((x) < (mi) ? (mi) : (x) > (ma) ? (ma) : (x))

static int compare_monitors(const void* a, const void* b) {
  return memcmp(a, b, sizeof(Monitor));
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
  if (MaybeInitXRandR(dpy)) {
    // Translate to absolute coordinates so we can compare them to XRandR data.
    int wx, wy;
    Window child;
    if (!XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), xwa.x, xwa.y,
                               &wx, &wy, &child)) {
      fprintf(stderr, "XTranslateCoordinates failed.\n");
      wx = xwa.x;
      wy = xwa.y;
    }

    XRRScreenResources* screenres = XRRGetScreenResources(dpy, w);
    if (screenres != NULL) {
      for (int k = 0; k < screenres->noutput; ++k) {
        XRROutputInfo* output =
            XRRGetOutputInfo(dpy, screenres, screenres->outputs[k]);
        if (output != NULL && output->connection == RR_Connected) {
          // NOTE: If an output has multiple Crtcs (i.e. if the screen is
          // cloned), we only look at the first. Let's assume that the center of
          // that one should always be onscreen anyway (even though they may not
          // be, as cloned displays can have different panning settings).
          RRCrtc crtc = (output->crtc ? output->crtc
                                      : output->ncrtc ? output->crtcs[0] : 0);
          XRRCrtcInfo* info = (crtc ? XRRGetCrtcInfo(dpy, screenres, crtc) : 0);
          if (info != NULL) {
            int x = CLAMP(info->x, wx, wx + xwa.width) - wx;
            int y = CLAMP(info->y, wy, wy + xwa.height) - wy;
            int w = CLAMP(info->x + (int)info->width, wx + x, wx + xwa.width) -
                    (wx + x);
            int h =
                CLAMP(info->y + (int)info->height, wy + y, wy + xwa.height) -
                (wy + y);
            if (w <= 0 || h <= 0) {
              continue;
            }
            if (num_monitors < max_monitors) {
              out_monitors[num_monitors].x = x;
              out_monitors[num_monitors].y = y;
              out_monitors[num_monitors].width = w;
              out_monitors[num_monitors].height = h;
              ++num_monitors;
            }
            XRRFreeCrtcInfo(info);
          }
          XRRFreeOutputInfo(output);
        }
      }
      XRRFreeScreenResources(screenres);
    }
  }
#endif

  // If we got no monitor info, try to guess based on size.
  if (num_monitors == 0) {
    // XRandR-less dummy fallback.
    size_t guessed_monitors = CLAMP((size_t)(xwa.width * 9 + xwa.height * 8) /
                                        (size_t)(xwa.height * 16),  //
                                    1, max_monitors);
    for (num_monitors = 0; num_monitors < guessed_monitors; ++num_monitors) {
      out_monitors[num_monitors].x =
          xwa.width * num_monitors / guessed_monitors;
      out_monitors[num_monitors].y = 0;
      out_monitors[num_monitors].width =
          (xwa.width * (num_monitors + 1) / guessed_monitors) -
          (xwa.width * num_monitors / guessed_monitors);
      out_monitors[num_monitors].height = xwa.height;
    }
  }

  // Sort the monitors in some deterministic order.
  qsort(out_monitors, num_monitors, sizeof(*out_monitors), compare_monitors);

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
#endif

  // XRandR-less dummy fallback.
  return 0;
}
