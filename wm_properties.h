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

#ifndef WM_PROPERTIES_H
#define WM_PROPERTIES_H

#include <X11/Xlib.h>

/*! \brief Configures properties on the given window for easier debugging.
 *
 * \param dpy The X11 dpy.
 * \param w The window (which shouldn't be mapped yet).
 * \param res_class The class name the window should receive (becomes
 *   WM_CLASS.res_class)
 * \param res_name The window name the window should receive (becomes
 *   WM_CLASS.res_name and WM_NAME)
 * \param argc The number of arguments the main program received.
 * \param argv The arguments the main program received (becomes WM_COMMAND).
 */
void SetWMProperties(Display* dpy, Window w, const char* res_class,
                     const char* res_name, int argc, char* const* argv);

#endif
