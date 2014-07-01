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

/*! \brief Starts or stops the screen saver child process.
 *
 * \param executable What binary to spawn for screen saving. No arguments will
 *   be passed.
 * \param should_be_running If true, the saver child is started if not running
 *   yet; if alse, the saver child will be terminated.
 */
void WatchSaverChild(const char *executable, int should_be_running);

#endif
