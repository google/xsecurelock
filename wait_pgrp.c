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
#include <signal.h>    // for kill, sigaddset, sigemptyset, sigprocmask,
                       // sigsuspend, SIGCHLD, SIGTERM
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
  sigset_t oldset, set;
  sigemptyset(&set);
  // We're blocking the signal we may have a forwarding handler for as their
  // handling reads the pid variable we are changing here.
  sigaddset(&set, SIGTERM);
  // If we want to wait for a process to die, we must also block SIGCHLD
  // so we can reliably wait for another child in case waitpid returned 0.
  // Why can't we just use 0 instead of WNOHANG? Because then we can't block
  // above signal handlers anymore, which use the pid variable.
  if (do_block) {
    sigaddset(&set, SIGCHLD);
  }
  sigemptyset(&oldset);
  if (sigprocmask(SIG_BLOCK, &set, &oldset)) {
    LogErrno("Unable to block signals");
  }
  int result = -1;
  while (result == -1) {
    int status;
    pid_t gotpid = waitpid(*pid, &status, WNOHANG);
    if (gotpid < 0) {
      switch (errno) {
        case ECHILD:
          // The process is already dead. Fine.
          *exit_status = WAIT_ALREADY_DEAD;
          *pid = 0;
          result = 1;
          break;
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
          result = 1;
        }
      } else if (WIFEXITED(status)) {
        *exit_status = WEXITSTATUS(status);
        if (*exit_status != EXIT_SUCCESS) {
          Log("%s child failed with status %d", name, *exit_status);
        }
        *pid = 0;
        result = 1;
      }
      // Otherwise it was suspended or whatever. We need to keep waiting.
    } else if (gotpid != 0) {
      Log("Unexpectedly woke up for PID %d", (int)*pid);
    } else if (do_block) {
      // Block for SIGCHLD, then waitpid again.
      sigsuspend(&oldset);
    } else {
      result = 0;  // Child still lives.
    }
  }
  if (sigprocmask(SIG_SETMASK, &oldset, NULL)) {
    LogErrno("Unable to restore signal mask");
  }
  return result;
}
