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

#include "auth_child.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t auth_child_pid = 0;
int auth_child_fd = 0;

bool WantAuthChild(bool force_auth) {
  if (force_auth) {
    return true;
  }
  return (auth_child_pid != 0);
}

bool WatchAuthChild(const char* executable, bool force_auth,
                    const char* stdinbuf, bool* auth_running) {
  if (auth_child_pid != 0) {
    // Check if auth child returned.
    int status;
    pid_t pid = waitpid(auth_child_pid, &status, WNOHANG);
    if (pid == auth_child_pid) {
      if (WIFEXITED(status)) {
        // Auth child exited.
        // To be sure, let's also kill its process group.
        kill(-auth_child_pid, SIGTERM);
        auth_child_pid = 0;
        close(auth_child_fd);
        // If auth child exited with success status, stop the screen saver.
        if (WEXITSTATUS(status) == EXIT_SUCCESS) {
          *auth_running = false;
          return true;
        }
        // Otherwise, the auth child failed. That's ok. Just carry on.
        // This will eventually bring back the saver child.
      }
    }
  }

  if (force_auth && auth_child_pid == 0) {
    // Start auth child.
    int pc[2];
    if (pipe(pc)) {
      perror("pipe");
    } else {
      pid_t pid = fork();
      if (pid == -1) {
        perror("fork");
      } else if (pid == 0) {
        // Child process.
        setsid();
        dup2(pc[0], 0);
        close(pc[0]);
        close(pc[1]);
        execl(executable, executable, NULL);
        perror("execvp");
        exit(EXIT_FAILURE);
      } else {
        // Parent process after successful fork.
        auth_child_pid = pid;
        auth_child_fd = pc[1];
      }
    }
  }

  *auth_running = (auth_child_pid != 0);

  if (stdinbuf != NULL && stdinbuf[0] != 0) {
    if (auth_child_pid != 0) {
      ssize_t to_write = (ssize_t)strlen(stdinbuf);
      ssize_t written = write(auth_child_fd, stdinbuf, to_write);
      if (written < 0) {
        perror("Failed to send all data to the auth child");
      } else if (written != to_write) {
        fprintf(stderr, "Failed to send all data to the auth child.\n");
      }
    } else {
      fprintf(stderr, "No auth child. Can't send key events...\n");
    }
  }

  return false;
}
