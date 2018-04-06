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

/*!
 * \brief Screen dimmer helper.
 *
 * A simple tool to run a tool to dim the screen, and - depending on which comes
 * first:
 * - On leaving idle status, kill the dimming tool and exit with success status.
 * - On dimming tool exiting, exit with error status.
 *
 * Sample usage:
 *   until_nonidle dim-screen || xsecurelock
 */

#ifdef HAVE_SCRNSAVER
#error This tool can only be compiled with the Screen Saver extension.
#endif

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/saver.h>
#include <X11/extensions/scrnsaver.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../env_settings.h"
#include "../logging.h"

XScreenSaverInfo* saver_info;

unsigned long GetIdleTime(Display *display, Window w) {
  // TODO(divVerent): Add Xsync timer support as well.
  XScreenSaverQueryInfo(display, w, saver_info);
  return saver_info->idle;
}

int main(int argc, char** argv) {
  if (argc <= 1) {
    Log("Usage: %s program args... - runs the given program until non-idle",
        argv[0]);
    Log("Meant to be used with dimming tools, like: %s dimmer || xsecurelock",
        argv[0]);
    Log("Returns 0 when no longer idle, and 1 when still idle");
    return 1;
  }

  int dim_time_ms = GetIntSetting("XSECURELOCK_DIM_TIME_MS", 2000);
  int wait_time_ms = GetIntSetting("XSECURELOCK_WAIT_TIME_MS", 5000);

  Display* display = XOpenDisplay(NULL);
  if (display == NULL) {
    Log("Could not connect to $DISPLAY.");
    return 1;
  }
  Window root_window = DefaultRootWindow(display);

  // Initialize the extension.
  int scrnsaver_event_base, scrnsaver_error_base;
  if (!XScreenSaverQueryExtension(display, &scrnsaver_event_base,
                                  &scrnsaver_error_base)) {
    Log("No Screen Saver extension detected - cannot proceed");
    return 1;
  }
  saver_info = XScreenSaverAllocInfo();

  // Capture the initial idle time.
  unsigned long prev_idle = GetIdleTime(display, root_window);

  // Start the subprocess.
  pid_t childpid = fork();
  if (childpid == -1) {
    LogErrno("fork");
    return 1;
  }
  if (childpid == 0) {
    // Child process.
    setsid();
    execvp(argv[1], argv + 1);
    LogErrno("execl");
    exit(1);
  }

  // Parent process.
  struct timeval start_time;
  gettimeofday(&start_time, NULL);
  int still_idle = 1;
  while (childpid != 0) {
    nanosleep(&(const struct timespec){0, 10000000L}, NULL);  // 10ms.

    unsigned long cur_idle = GetIdleTime(display, root_window);
    still_idle = cur_idle >= prev_idle;
    prev_idle = cur_idle;

    // Also exit when both dim and wait time expire. This allows using
    // xss-lock's dim-screen.sh without changes.
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    int active_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 +
                    (current_time.tv_usec - start_time.tv_usec) / 1000;
    int should_be_running =
        still_idle && (active_ms <= dim_time_ms + wait_time_ms);

    if (!should_be_running) {
      // Kill the whole process group.
      kill(childpid, SIGTERM);
      kill(-childpid, SIGTERM);
    }
    do {
      int status;
      pid_t pid = waitpid(childpid, &status, should_be_running ? WNOHANG : 0);
      if (pid < 0) {
        switch (errno) {
          case ECHILD:
            // The process is dead. Fine.
            if (should_be_running) {
              kill(-childpid, SIGTERM);
            }
            childpid = 0;
            break;
          case EINTR:
            // Waitpid was interrupted. Need to retry.
            break;
          default:
            // Assume the child still lives. Shouldn't ever happen.
            LogErrno("waitpid");
            break;
        }
      } else if (pid == childpid) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
          // Auth child exited.
          if (should_be_running) {
            // To be sure, let's also kill its process group before we finish
            // (no need to do this if we already did above).
            kill(-childpid, SIGTERM);
          }
          childpid = 0;
          if (WIFSIGNALED(status) &&
              (should_be_running || WTERMSIG(status) != SIGTERM)) {
            Log("Dimmer child killed by signal %d", WTERMSIG(status));
          }
          if (WIFEXITED(status) && WEXITSTATUS(status) != EXIT_SUCCESS) {
            Log("Dimmer child failed with status %d", WEXITSTATUS(status));
          }
        }
        // Otherwise it was suspended or whatever. We need to keep waiting.
      } else if (pid != 0) {
        Log("Unexpectedly woke up for PID %d", (int)pid);
      } else if (!should_be_running) {
        Log("Unexpectedly woke up for PID 0 despite no WNOHANG");
      }
      // Otherwise, we're still alive.
    } while (!should_be_running && childpid != 0);
  }

  // This is the point where we can exit.
  return still_idle ? 1   // Dimmer exited - now it's time to lock.
                    : 0;  // No longer idle - don't lock.
}
