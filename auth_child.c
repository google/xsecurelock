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

#include "auth_child.h"

#include <signal.h>    // for kill, SIGTERM
#include <stdio.h>     // for perror, fprintf, stderr
#include <stdlib.h>    // for NULL, exit, EXIT_FAILURE, etc
#include <string.h>    // for strlen
#include <sys/wait.h>  // for waitpid, WEXITSTATUS, etc
#include <unistd.h>    // for close, pid_t, ssize_t, dup2, etc

pid_t auth_child_pid = 0;
int auth_child_fd = 0;

int WantAuthChild(int force_auth) {
  if (force_auth) {
    return 1;
  }
  return (auth_child_pid != 0);
}

int WatchAuthChild(const char* executable, int force_auth, const char* stdinbuf,
                   int* auth_running) {
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
          *auth_running = 0;
          return 1;
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
        // The auth child has just been started. Do not send any keystrokes to
        // it immediately, as users prefer pressing any key in the screen saver
        // to simply start the auth child, not begin password input!
        stdinbuf = NULL;
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

  return 0;
}
