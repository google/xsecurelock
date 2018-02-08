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

#ifndef MONITORS_H
#define MONITORS_H

#include <stdlib.h>
#include <X11/Xlib.h>

typedef struct {
  int x, y, width, height;
} Monitor;

/*! \brief Queries the current monitor configuration.
 *
 * Note: out_monitors will be zero padded and sorted in some deterministic order
 * so memcmp can be used to check if the monitor configuration has actually
 * changed.
 *
 * \param dpy The current display.
 * \param w The window this application intends to draw in.
 * \param out_monitors A pointer to an array that will receive the monitor
 *   configuration (in coordinates relative and clipped to the window w.
 * \param max_monitors The size of the array.
 * \return The number of monitors returned in the array.
 */
size_t GetMonitors(Display* dpy, Window w, Monitor* out_monitors,
                   size_t max_monitors);

/*! \brief Enable receiving monitor change events for the given display at w.
 */
void SelectMonitorChangeEvents(Display *dpy, Window w);

/*! \brief Returns the event type that indicates a change to the monitor
 *    configuration.
 *
 * \param dpy The current display.
 * \param type The received event type.
 *
 * \returns 1 if the received event is a monitor change event and GetMonitors
 *   should be called, or 0 otherwise.
 */
int IsMonitorChangeEvent(Display* dpy, int type);

#endif
