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
#include <signal.h>    // for kill, SIGTERM, sigemptyset, sigprocmask
#include <stdlib.h>    // for NULL, exit, EXIT_FAILURE, EXIT_SUCCESS
#include <sys/wait.h>  // for WEXITSTATUS, WIFEXITED, WIFSIGNALED
#include <unistd.h>    // for pid_t, execl, fork, setsid

#include "logging.h"           // for Log, LogErrno
#include "wait_pgrp.h"         // for WaitPgrp
#include "xscreensaver_api.h"  // for ExportWindowID

//! The PIDs of currently running saver children, or 0 if not running.
static pid_t saver_child_pid[MAX_SAVERS] = {0};

void KillAllSaverChildrenSigHandler(void) {
  // This is a signal handler, so we're not going to make this too
  // complicated. Just kill 'em all.
  for (int i = 0; i < MAX_SAVERS; ++i) {
    if (saver_child_pid[i] != 0) {
      KillPgrp(saver_child_pid[i]);
    }
    saver_child_pid[i] = 0;
  }
}

void WatchSaverChild(Display* dpy, Window w, int index, const char* executable,
                     int should_be_running) {
  if (index < 0 || index >= MAX_SAVERS) {
    Log("Saver index out of range: !(0 <= %d < %d)", index, MAX_SAVERS);
    return;
  }

  if (saver_child_pid[index] != 0) {
    if (!should_be_running) {
      KillPgrp(saver_child_pid[index]);
    }

    int status;
    if (WaitPgrp("saver", saver_child_pid[index], !should_be_running,
                 !should_be_running, &status)) {
      if (should_be_running) {
        // Try taking its process group with it. Should normally not do
        // anything.
        KillPgrp(saver_child_pid[index]);
      }

      // Clean up.
      saver_child_pid[index] = 0;

      // Now is the time to remove anything the child may have displayed.
      XClearWindow(dpy, w);
    }
  }

  if (should_be_running && saver_child_pid[index] == 0) {
    pid_t pid = fork();
    if (pid == -1) {
      LogErrno("fork");
    } else if (pid == 0) {
      // Child process.
      setsid();

      // saver_multiplex may call this with blocked signals, but let's not have
      // the child process inherit that.
      sigset_t no_blocked_signals;
      sigemptyset(&no_blocked_signals);
      sigprocmask(SIG_SETMASK, &no_blocked_signals, NULL);

      ExportWindowID(w);
      execl(executable,  // Path to binary.
            executable,  // argv[0].
            "-root",     // argv[1]; for XScreenSaver hacks, unused by our own.
            NULL);
      LogErrno("execl");
      sleep(2);  // Reduce log spam or other effects from failed execl.
      _exit(EXIT_FAILURE);
    } else {
      // Parent process after successful fork.
      saver_child_pid[index] = pid;
    }
  }
}
