/*
Copyright 2019 Google Inc. All rights reserved.

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

/*!
 *\brief Process group placeholder.
 *
 * Does nothing except sitting around until killed. Spawned as extra process in
 * our process groups so that we can control on our own when the process group
 * ID is reclaimed to the kernel, namely by killing the entire process group.
 * This prevents a race condition of our process group getting reclaimed before
 * we try to kill possibly remaining processes in it, after which we would
 * possibly kill something else.
 *
 * Must be a separate executable so F_CLOEXEC applies as intended.
 */

#include <unistd.h>

int main() {
  for (;;) {
    pause();
  }
  return 0;
}
