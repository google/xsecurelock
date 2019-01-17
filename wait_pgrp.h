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

#ifndef WAIT_PGRP_H
#define WAIT_PGRP_H

#include <limits.h>  // for INT_MIN
#include <unistd.h>  // for pid_t

#define WAIT_ALREADY_DEAD INT_MIN
#define WAIT_NONPOSITIVE_SIGNAL (INT_MIN + 1)

int KillPgrp(pid_t pid);
int WaitPgrp(const char *name, pid_t pid, int do_block, int already_killed,
             int *exit_status);

#endif
