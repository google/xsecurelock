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

#include "wait_pgrp.h"

#include <errno.h>     // for errno, ECHILD, EINTR, ESRCH
#include <signal.h>    // for kill, SIGTERM
#include <stdlib.h>    // for EXIT_SUCCESS, WEXITSTATUS, WIFEXITED, WIFSIGNALED
#include <sys/wait.h>  // for waitpid, WNOHANG
#include <unistd.h>    // for pid_t

#include "logging.h"  // for Log, LogErrno

int KillPgrp(pid_t pid) {
  int ret = kill(-pid, SIGTERM);
  if (ret < 0 && errno == ESRCH) {
    // Might mean the process is not a process group leader - but might also
    // mean that the process is already dead. Try killing just the process then.
    ret = kill(pid, SIGTERM);
  }
  return ret;
}

int WaitPgrp(const char *name, pid_t *pid, int do_block, int already_killed,
             int *exit_status) {
  for (;;) {
    int status;
    pid_t gotpid = waitpid(*pid, &status, do_block ? 0 : WNOHANG);
    if (gotpid < 0) {
      switch (errno) {
        case ECHILD:
          // The process is already dead. Fine.
          *exit_status = WAIT_ALREADY_DEAD;
          *pid = 0;
          return 1;
        case EINTR:
          // Waitpid was interrupted. Need to retry.
          break;
        default:
          // Assume the child still lives. Shouldn't ever happen.
          LogErrno("%s child could not be waited upon", name);
          break;
      }
    } else if (gotpid == *pid) {
      if (WIFSIGNALED(status)) {
        int signo = WTERMSIG(status);
        if (!already_killed || signo != SIGTERM) {
          Log("%s child killed by signal %d", name, signo);
          *exit_status = (signo > 0) ? -signo : WAIT_NONPOSITIVE_SIGNAL;
          *pid = 0;
          return 1;
        }
      }
      if (WIFEXITED(status)) {
        *exit_status = WEXITSTATUS(status);
        if (*exit_status != EXIT_SUCCESS) {
          Log("%s child failed with status %d", name, *exit_status);
        }
        *pid = 0;
        return 1;
      }
      // Otherwise it was suspended or whatever. We need to keep waiting.
    } else if (gotpid != 0) {
      Log("Unexpectedly woke up for PID %d", (int)*pid);
    } else if (do_block) {
      Log("Unexpectedly woke up for PID 0 despite no WNOHANG");
    } else {
      return 0;  // Child still lives.
    }
  }
}
