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

#include <stdbool.h>

// Returns true if an auth child is either currently running, or should be
// started (because force_auth is true).
bool WantAuthChild(bool force_auth);

// Starts the authentication child process if force_auth is set and no auth
// child is running yet. Communicates stdinbuf to said auth child.
// Returns true if authentication was successful (i.e. the auth child exited
// with status zero).
//
// executable: executable to spawn for authentication.
// force_auth: if set, the auth child will be started if not running yet.
// stdinbuf: if set, this data is written to stdin of the auth child.
// auth_running: will be set to the status of the current auth child on return.
bool WatchAuthChild(const char* executable, bool force_auth,
                    const char* stdinbuf, bool* auth_running);

#endif
