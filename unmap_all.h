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

#ifndef UNMAP_ALL_H
#define UNMAP_ALL_H

#include <X11/X.h>     // for Window
#include <X11/Xlib.h>  // for Display

typedef struct {
  Display *display;
  Window root_window;

  // The window list; None windows should be skipped when iterating.
  Window *windows;
  unsigned int n_windows;
} UnmapAllWindowsState;

/*! \brief Stores the list of all mapped application windows in the state.
 *
 * Note that windows might be created after this has been called, so you
 * typically want to grab the server first.
 *
 * \return true if all is fine, false if a non-ignored window matching my own
 *   window class was found, which should indicate that another instance is
 *   already running.
 */
int InitUnmapAllWindowsState(UnmapAllWindowsState *state, Display *display,
                              Window root_window, const Window *ignored_windows,
                              unsigned int n_ignored_windows,
                              const char *my_res_class, const char *my_res_name,
                              int include_frame);

/*! \brief Unmaps all windows, and stores them in the state.
 *
 * Must be used on the state filled by ListAllWindows.
 */
void UnmapAllWindows(UnmapAllWindowsState *state);

/*! \brief Remaps all windows from the state.
 *
 * Must be used on the state filled by ListAllWindows.
 */
void RemapAllWindows(UnmapAllWindowsState *state);

/*! \brief Clears the UnmapAllWindowsState when done, and returns resources to
 * X11.
 */
void ClearUnmapAllWindowsState(UnmapAllWindowsState *state);

#endif
