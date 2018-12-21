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

#ifndef AUTH_CHILD_H
#define AUTH_CHILD_H

#include <X11/X.h>  // for Window

/*! \brief Kill the auth child.
 *
 * This can be used from a signal handler.
 */
void KillAuthChildSigHandler(void);

/*! \brief Checks whether an auth child should be running.
 *
 * \param force_auth If true, assume we want to start a new auth child.
 * \return true if an auth child is expected to be running after a call to
 *   WatchAuthChild() with this force_auth parameter.
 */
int WantAuthChild(int force_auth);

/*! \brief Starts or stops the authentication child process.
 *
 * \param w The screen saver window. Will get cleared after auth child
 *   execution.
 * \param executable What binary to spawn for authentication. No arguments will
 *   be passed.
 * \param force_auth If true, the auth child will be spawned if not already
 *   running.
 * \param stdinbuf If non-NULL, this data will be sent to stdin of the auth
 *   child.
 * \param auth_running Will be set to the status of the current auth child (i.e.
 *   true iff it is running).
 * \return true if authentication was successful, i.e. if the auth child exited
 *   with status zero.
 */
int WatchAuthChild(Window w, const char *executable, int force_auth,
                   const char *stdinbuf, int *auth_running);

#endif
