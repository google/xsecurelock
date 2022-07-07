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

#ifndef XSCREENSAVER_API_H
#define XSCREENSAVER_API_H

#include <X11/X.h>  // for Window

/*! \brief Export the given window ID to the environment for a saver/auth child.
 *
 * This simply sets $XSCREENSAVER_WINDOW.
 *
 * \param w The window the child should draw on.
 */
void ExportWindowID(Window w);

/*! \brief Export the given saver index to the environment for a saver/auth child.
 *
 * This simply sets $XSCREENSAVER_SAVER_INDEX.
 *
 * \param index The index of the saver.
 */
void ExportSaverIndex(int index);

/*! \brief Reads the window ID to draw on from the environment.
 *
 * This simply reads $XSCREENSAVER_WINDOW.
 */
Window ReadWindowID(void);

#endif
