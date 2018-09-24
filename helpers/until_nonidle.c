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

#include <X11/X.h>     // for Window
#include <X11/Xlib.h>  // for Display, XOpenDisplay, Default...
#include <errno.h>     // for ECHILD, EINTR, errno
#include <signal.h>    // for kill, SIGTERM
#include <stdint.h>    // for uint64_t
#include <stdlib.h>    // for exit, size_t, EXIT_SUCCESS
#include <string.h>    // for NULL, memcpy, strcmp, strcspn
#include <sys/time.h>  // for gettimeofday, timeval
#include <sys/wait.h>  // for waitpid, WEXITSTATUS, WIFEXITED
#include <time.h>      // for nanosleep, timespec
#include <unistd.h>    // for execvp, fork, setsid, pid_t

#ifdef HAVE_XSCREENSAVER_EXT
#include <X11/extensions/scrnsaver.h>  // for XScreenSaverAllocInfo, XScreen...
#endif

#ifdef HAVE_XSYNC_EXT
#include <X11/extensions/sync.h>       // for XSyncSystemCounter, XSyncListS...
#include <X11/extensions/syncconst.h>  // for XSyncValue
#endif

#include "../env_settings.h"  // for GetIntSetting, GetStringSetting
#include "../logging.h"       // for Log, LogErrno

#ifdef HAVE_XSCREENSAVER_EXT
int have_xscreensaver_ext;
XScreenSaverInfo *saver_info;
#endif

#ifdef HAVE_XSYNC_EXT
int have_xsync_ext;
int num_xsync_counters;
XSyncSystemCounter *xsync_counters;
#endif

#define MAX_TIMERS 16
size_t num_timers;
const char *timers[MAX_TIMERS];

uint64_t GetIdleTimeForSingleTimer(Display *display, Window w,
                                   const char *timer) {
  if (*timer == 0) {
#ifdef HAVE_XSCREENSAVER_EXT
    if (have_xscreensaver_ext) {
      XScreenSaverQueryInfo(display, w, saver_info);
      return saver_info->idle;
    }
#endif
  } else {
#ifdef HAVE_XSYNC_EXT
    if (have_xsync_ext) {
      int i;
      for (i = 0; i < num_xsync_counters; ++i) {
        if (!strcmp(timer,
                    xsync_counters[i].name)) {  // I know this is inefficient.
          XSyncValue value;
          XSyncQueryCounter(display, xsync_counters[i].counter, &value);
          return (((uint64_t)XSyncValueHigh32(value)) << 32) |
                 (uint64_t)XSyncValueLow32(value);
        }
      }
    }
#endif
  }
  Log("Timer \"%s\" not supported", timer);
  (void)display;
  (void)w;
  return (uint64_t)-1;
}

uint64_t GetIdleTime(Display *display, Window w, const char *timers) {
  uint64_t min_idle_time = (uint64_t)-1;
  for (;;) {
    size_t len = strcspn(timers, ",");
    if (timers[len] == 0) {  // End of string.
      uint64_t this_idle_time = GetIdleTimeForSingleTimer(display, w, timers);
      if (this_idle_time < min_idle_time) {
        min_idle_time = this_idle_time;
      }
      return min_idle_time;
    }
    char this_timer[64];
    if (len < sizeof(this_timer)) {
      memcpy(this_timer, timers, len);
      this_timer[len] = 0;
      uint64_t this_idle_time =
          GetIdleTimeForSingleTimer(display, w, this_timer);
      if (this_idle_time < min_idle_time) {
        min_idle_time = this_idle_time;
      }
    } else {
      Log("Too long timer name - skipping: %s", timers);
    }
    timers += len + 1;
  }
}

int main(int argc, char **argv) {
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
  const char *timers = GetStringSetting("XSECURELOCK_IDLE_TIMERS",
#ifdef HAVE_XSCREENSAVER_EXT
                                        ""
#else
                                        "IDLETIME"
#endif
  );

  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    Log("Could not connect to $DISPLAY.");
    return 1;
  }
  Window root_window = DefaultRootWindow(display);

  // Initialize the extensions.
#ifdef HAVE_XSCREENSAVER_EXT
  have_xscreensaver_ext = 0;
  int scrnsaver_event_base, scrnsaver_error_base;
  if (XScreenSaverQueryExtension(display, &scrnsaver_event_base,
                                 &scrnsaver_error_base)) {
    have_xscreensaver_ext = 1;
    saver_info = XScreenSaverAllocInfo();
  }
#endif
#ifdef HAVE_XSYNC_EXT
  have_xsync_ext = 0;
  int sync_event_base, sync_error_base;
  if (XSyncQueryExtension(display, &sync_event_base, &sync_error_base)) {
    have_xsync_ext = 1;
    xsync_counters = XSyncListSystemCounters(display, &num_xsync_counters);
  }
#endif

  // Capture the initial idle time.
  uint64_t prev_idle = GetIdleTime(display, root_window, timers);
  if (prev_idle == (uint64_t)-1) {
    Log("Could not initialize idle timers. Bailing out.");
    return 1;
  }

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

    uint64_t cur_idle = GetIdleTime(display, root_window, timers);
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
