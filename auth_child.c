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

#include <errno.h>     // for ECHILD, EINTR, errno
#include <signal.h>    // for kill, SIGTERM
#include <stdio.h>     // for perror, fprintf, stderr
#include <stdlib.h>    // for NULL, exit, EXIT_FAILURE, etc
#include <string.h>    // for strlen
#include <sys/wait.h>  // for waitpid, WEXITSTATUS, etc
#include <unistd.h>    // for close, pid_t, ssize_t, dup2, etc

//! The PID of a currently running saver child, or 0 if none is running.
static pid_t auth_child_pid = 0;

//! If auth_child_pid != 0, the FD which connects to stdin of the auth child.
static int auth_child_fd = 0;

/*! \brief Return whether the wake-up keypress should be part of the password.
 *
 * Sending the wake-up keypress to the auth child is usually a bad idea because
 * many people use "any" key, not their password's, to wake up the screen saver.
 * Also, when using a blanking screen saver, one can't easily distinguish a
 * locked screen from a turned-off screen, and may thus accidentally start
 * entering the password into a web browser or similar "bad" place.
 *
 * However, it was requested by a user, so why not add it. Usage:
 *
 * XSECURELOCK_WANT_FIRST_KEYPRESS=1 xsecurelock
 */
static int WantFirstKeypress() {
  const char *var = getenv("XSECURELOCK_WANT_FIRST_KEYPRESS");
  return var != NULL && !strcmp(var, "1");
}

int WantAuthChild(int force_auth) {
  if (force_auth) {
    return 1;
  }
  return (auth_child_pid != 0);
}

int WatchAuthChild(const char *executable, int force_auth, const char *stdinbuf,
                   int *auth_running) {
  if (auth_child_pid != 0) {
    // Check if auth child returned.
    int status;
    pid_t pid = waitpid(auth_child_pid, &status, WNOHANG);
    if (pid < 0) {
      switch (errno) {
        case ECHILD:
          // The process is dead. Fine.
          kill(-auth_child_pid, SIGTERM);
          auth_child_pid = 0;
          close(auth_child_fd);
          // The auth child failed. That's ok. Just carry on.
          // This will eventually bring back the saver child.
          break;
        case EINTR:
          // Waitpid was interrupted. Fine, assume it's still running.
          break;
        default:
          // Assume the child still lives. Shouldn't ever happen.
          perror("waitpid");
          break;
      }
    } else if (pid == auth_child_pid) {
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
        // Otherwise, the auth child failed. That's the intended behavior in
        // case of e.g. a wrong password, so don't log this. Just carry on.
        // This will eventually bring back the saver child.
      }
      // Otherwise, it was suspended or whatever. We need to keep waiting.
    } else if (pid == 0) {
      // We're still alive.
    } else {
      fprintf(stderr, "Unexpectedly woke up for PID %d.\n", (int)pid);
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
        perror("execl");
        exit(EXIT_FAILURE);
      } else {
        // Parent process after successful fork.
        close(pc[0]);
        auth_child_fd = pc[1];
        auth_child_pid = pid;

        if (!WantFirstKeypress()) {
          // The auth child has just been started. Do not send any keystrokes to
          // it immediately.
          stdinbuf = NULL;
        }
      }
    }
  }

  // Report whether the auth child is running.
  *auth_running = (auth_child_pid != 0);

  // Send the provided keyboard buffer to stdin.
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
