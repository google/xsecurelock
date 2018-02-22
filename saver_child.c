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

#include "saver_child.h"

#include <errno.h>     // for ECHILD, EINTR, errno
#include <signal.h>    // for kill, SIGTERM
#include <stdio.h>     // for perror, fprintf, NULL, etc
#include <stdlib.h>    // for exit, EXIT_FAILURE, etc
#include <sys/wait.h>  // for WEXITSTATUS, waitpid, etc
#include <unistd.h>    // for pid_t, execl, fork, setsid

#include "xscreensaver_api.h"

//! The PIDs of currently running saver children, or 0 if not running.
static pid_t saver_child_pid[MAX_SAVERS] = {0};

void WatchSaverChild(Display* dpy, Window w, int index, const char* executable,
                     int should_be_running) {
  if (index < 0 || index >= MAX_SAVERS) {
    fprintf(stderr, "Saver index out of range: !(0 <= %d < %d).", index,
            MAX_SAVERS);
    return;
  }

  if (saver_child_pid[index] != 0) {
    if (!should_be_running) {
      // Kill the whole process group.
      kill(saver_child_pid[index], SIGTERM);
      kill(-saver_child_pid[index], SIGTERM);
    }
    do {
      int status;
      pid_t pid = waitpid(saver_child_pid[index], &status,
                          should_be_running ? WNOHANG : 0);
      if (pid < 0) {
        switch (errno) {
          case ECHILD:
            // The process is dead. Fine.
            if (should_be_running) {
              kill(-saver_child_pid[index], SIGTERM);
            }
            saver_child_pid[index] = 0;
            XClearWindow(dpy, w);
            break;
          case EINTR:
            // Waitpid was interrupted. Need to retry.
            break;
          default:
            // Assume the child still lives. Shouldn't ever happen.
            perror("waitpid");
            break;
        }
      } else if (pid == saver_child_pid[index]) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
          // Auth child exited.
          if (should_be_running) {
            // To be sure, let's also kill its process group before we restart
            // it (no need to do this if we already did above).
            kill(-saver_child_pid[index], SIGTERM);
          }
          saver_child_pid[index] = 0;
          if (WIFSIGNALED(status) &&
              (should_be_running || WTERMSIG(status) != SIGTERM)) {
            fprintf(stderr, "Saver child killed by signal %d.\n",
                    WTERMSIG(status));
          }
          if (WIFEXITED(status) && WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "Saver child failed with status %d.\n",
                    WEXITSTATUS(status));
          }
          // Now is the time to remove anything the child may have displayed.
          XClearWindow(dpy, w);
        }
        // Otherwise it was suspended or whatever. We need to keep waiting.
      } else if (pid != 0) {
        fprintf(stderr, "Unexpectedly woke up for PID %d.\n", (int)pid);
      } else if (!should_be_running) {
        fprintf(stderr, "Unexpectedly woke up for PID 0 despite no WNOHANG.\n");
      }
      // Otherwise, we're still alive.
    } while (!should_be_running && saver_child_pid[index] != 0);
  }

  if (should_be_running && saver_child_pid[index] == 0) {
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
    } else if (pid == 0) {
      // Child process.
      setsid();

      // saver_multiplex may call this with blocked signals, but let's not have
      // the child process inherit that.
      sigset_t no_blocked_signals;
      sigemptyset(&no_blocked_signals);
      sigprocmask(SIG_SETMASK, &no_blocked_signals, NULL);

      ExportWindowID(w);
      execl(executable, executable, NULL);  // Flawfinder: ignore
      perror("execl");
      exit(EXIT_FAILURE);
    } else {
      // Parent process after successful fork.
      saver_child_pid[index] = pid;
    }
  }
}
