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

#include <errno.h>   // for errno, ECHILD, EINTR, ESRCH
#include <signal.h>  // for kill, sigaddset, sigemptyset, sigprocmask,
                     // sigsuspend, SIGCHLD, SIGTERM
#include <stdio.h>
#include <stdlib.h>  // for EXIT_SUCCESS, WEXITSTATUS, WIFEXITED, WIFSIGNALED
#include <string.h>
#include <sys/wait.h>  // for waitpid, WNOHANG
#include <unistd.h>    // for pid_t

#include "logging.h"  // for Log, LogErrno

static void HandleSIGCHLD(int unused_signo) {
  // No handling needed - we just want to interrupt select() or sigsuspend()
  // calls.
  (void)unused_signo;
}

void InitWaitPgrp(void) {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = HandleSIGCHLD;  // To interrupt select().
  if (sigaction(SIGCHLD, &sa, NULL) != 0) {
    LogErrno("sigaction(SIGCHLD)");
  }
}

pid_t ForkWithoutSigHandlers(void) {
  // Before forking, block all signals we may have handlers for.
  sigset_t oldset, set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGCHLD);
  sigemptyset(&oldset);
  if (sigprocmask(SIG_BLOCK, &set, &oldset)) {
    LogErrno("Unable to block signals");
  }
  pid_t pid = fork();
  int fork_errno = errno;
  if (pid == 0) {
    // Clear all our custom signal handlers in the subprocess.
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    if (sigaction(SIGUSR1, &sa, NULL)) {
      LogErrno("sigaction(SIGUSR1)");
    }
    if (sigaction(SIGTERM, &sa, NULL)) {
      LogErrno("sigaction(SIGTERM)");
    }
    if (sigaction(SIGCHLD, &sa, NULL)) {
      LogErrno("sigaction(SIGCHLD)");
    }
  }
  // Now we can unmask signals.
  if (sigprocmask(SIG_SETMASK, &oldset, NULL)) {
    LogErrno("Unable to restore signal mask");
  }
  errno = fork_errno;
  return pid;
}

void StartPgrp(void) {
  if (setsid() == (pid_t)-1) {
    LogErrno("setsid");
  }
  // To avoid a race condition when killing the process group after the leader
  // is already dead (which could then kill another new process group with the
  // same ID), we'll create a dummy process that never dies until we signal the
  // process group explicitly.
  pid_t pid = fork();
  if (pid == -1) {
    LogErrno("StartPgrp -> fork; expect potential race in KillPgrp");
    // We ignore this error, as everything else can still work in this case;
    // however in this case the aforementioned race condition in KillPgrp can
    // happen.
  } else if (pid == 0) {
    // Child process.
    // Just wait forever. We'll get SIGTERM'd when it's time to go.
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;  // Don't die of SIGUSR1 (saver reset).
    if (sigaction(SIGUSR1, &sa, NULL) != 0) {
      LogErrno("sigaction(SIGUSR1)");
    }
    {
      const char *args[2] = {"prgp_placeholder", NULL};
      ExecvHelper("pgrp_placeholder", args);
      sleep(2);  // Reduce log spam or other effects from failed execv.
      _exit(EXIT_FAILURE);
    }
  }
}

int ExecvHelper(const char *path, const char *const argv[]) {
  char *path_allocated = NULL;
  if (*path != '/') {
    size_t len = sizeof(HELPER_PATH) + 1 + strlen(path);
    path_allocated = malloc(len);
    if (snprintf(path_allocated, len, "%s/%s", HELPER_PATH, path) !=
        (int)len - 1) {
      Log("Unreachable code: could not format path name %s/%s", HELPER_PATH,
          path);
      return -1;
    }
    path = path_allocated;
  }
  execv(path, (char *const *)argv);
  // If we get here, we know it failed. We still log the error and free the
  // allocated path if any.
  int saved_errno = errno;
  LogErrno("execv %s", path);
  if (path_allocated) {
    free(path_allocated);
  }
  errno = saved_errno;
  return -1;
}

int KillPgrp(pid_t pid, int signo) {
  int ret = kill(-pid, signo);
  if (ret < 0 && errno == ESRCH) {
    // Note: this shouldn't happen as StartPgrp() should ensure that we never
    // get here. Remove this workaround once we made sure this really does not
    // happen. TODO(divVerent).
    LogErrno("Unable to kill process group %d - falling back to leader only",
             (int)pid);
    // Might mean the process is not a process group leader - but might also
    // mean that the process is already dead. Try killing just the process
    // then.
    ret = kill(pid, signo);
  }
  return ret;
}

int WaitPgrp(const char *name, pid_t *pid, int do_block, int already_killed,
             int *exit_status) {
  int pid_saved = *pid;
  int result = WaitProc(name, pid, do_block, already_killed, exit_status);
  if (result && !already_killed) {
    if (KillPgrp(pid_saved, SIGTERM) < 0) {
      LogErrno("KillPgrp %s", name);
    }
  }
  return result;
}

int WaitProc(const char *name, pid_t *pid, int do_block, int already_killed,
             int *exit_status) {
  sigset_t oldset, set;
  sigemptyset(&set);
  // We're blocking the signals we may have forwarding handlers for as their
  // handling reads the pid variable we are changing here.
  sigaddset(&set, SIGUSR1);
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
          // The process is already dead. Fine. Although this shouldn't happen.
          Log("%s child died without us noticing - please fix", name);
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
        }
        *exit_status = (signo > 0) ? -signo : WAIT_NONPOSITIVE_SIGNAL;
        *pid = 0;
        result = 1;
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
