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

#define _POSIX_C_SOURCE 200112L

#include "saver_child.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t saver_child_pid = 0;

void WatchSaverChild(const char* executable, bool should_be_running) {
  if (!should_be_running && saver_child_pid != 0) {
    // Kill the whole process group.
    kill(-saver_child_pid, SIGTERM);
    while (saver_child_pid) {
      int status;
      pid_t pid = waitpid(saver_child_pid, &status, 0);
      if (pid < 0) {
        switch (errno) {
          case ECHILD:
            // The process is dead. Fine.
            saver_child_pid = 0;
            break;
          case EINTR:
            // Waitpid was interrupted. Need to retry.
            break;
          default:
            // Assume the child still lives. Shouldn't ever happen.
            perror("waitpid");
            break;
        }
      } else {
        if (WIFEXITED(status)) {
          // The process did exit.
          if (WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "Saver child failed with status %d.\n",
                    WEXITSTATUS(status));
          }
          saver_child_pid = 0;
        }
        // Otherwise it was suspended or whatever. We need to keep waiting.
      }
    }
  }

  if (should_be_running && saver_child_pid == 0) {
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
    } else if (pid == 0) {
      // Child process.
      setsid();
      execl(executable, executable, NULL);
      perror("execvp");
      exit(EXIT_FAILURE);
    } else {
      // Parent process after successful fork.
      saver_child_pid = pid;
    }
  }
}
