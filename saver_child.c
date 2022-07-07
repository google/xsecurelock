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

#include <signal.h>  // for sigemptyset, sigprocmask, SIG_SETMASK
#include <stdlib.h>  // for NULL, EXIT_FAILURE
#include <unistd.h>  // for pid_t, _exit, execl, fork, setsid, sleep

#include "logging.h"           // for LogErrno, Log
#include "wait_pgrp.h"         // for KillPgrp, WaitPgrp
#include "xscreensaver_api.h"  // for ExportWindowID and ExportSaverIndex

//! The PIDs of currently running saver children, or 0 if not running.
static pid_t saver_child_pid[MAX_SAVERS] = {0};

void KillAllSaverChildrenSigHandler(int signo) {
  // This is a signal handler, so we're not going to make this too
  // complicated. Just kill 'em all.
  for (int i = 0; i < MAX_SAVERS; ++i) {
    if (saver_child_pid[i] != 0) {
      KillPgrp(saver_child_pid[i], signo);
    }
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
      KillPgrp(saver_child_pid[index], SIGTERM);
    }

    int status;
    if (WaitPgrp("saver", &saver_child_pid[index], !should_be_running,
                 !should_be_running, &status)) {
      // Now is the time to remove anything the child may have displayed.
      XClearWindow(dpy, w);
    }
  }

  if (should_be_running && saver_child_pid[index] == 0) {
    pid_t pid = ForkWithoutSigHandlers();
    if (pid == -1) {
      LogErrno("fork");
    } else if (pid == 0) {
      // Child process.
      StartPgrp();
      ExportWindowID(w);
      ExportSaverIndex(index);

      {
        const char* args[3] = {
            executable,
            "-root",  // For XScreenSaver hacks, unused by our own.
            NULL};
        ExecvHelper(executable, args);
        sleep(2);  // Reduce log spam or other effects from failed execv.
        _exit(EXIT_FAILURE);
      }
    } else {
      // Parent process after successful fork.
      saver_child_pid[index] = pid;
    }
  }
}
