/*
Copyright 2014 Google Inc. All rights reserved.

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

#ifndef SAVER_CHILD_H
#define SAVER_CHILD_H

#include <X11/X.h>     // for Window
#include <X11/Xlib.h>  // for Display

#define MAX_SAVERS 16

/*! \brief Kill all saver children.
 *
 * This can be used from a signal handler.
 */
void KillAllSaverChildrenSigHandler(void);

/*! \brief Starts or stops the screen saver child process.
 *
 * \param dpy The X11 display.
 * \param w The screen saver window. Will get cleared after saver child
 *   execution.
 * \param index The index of the saver to maintain (0 <= index < MAX_SAVERS).
 * \param executable What binary to spawn for screen saving. No arguments will
 *   be passed.
 * \param should_be_running If true, the saver child is started if not running
 *   yet; if alse, the saver child will be terminated.
 */
void WatchSaverChild(Display* dpy, Window w, int index, const char* executable,
                     int should_be_running);

#endif
