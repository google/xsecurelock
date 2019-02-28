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

#include <stdlib.h>  // for NULL, EXIT_FAILURE
#include <string.h>  // for strlen
#include <unistd.h>  // for close, _exit, dup2, execl, fork, pipe

#include "env_settings.h"      // for GetIntSetting
#include "logging.h"           // for LogErrno, Log
#include "wait_pgrp.h"         // for KillPgrp, WaitPgrp
#include "xscreensaver_api.h"  // for ExportWindowID

//! The PID of a currently running saver child, or 0 if none is running.
static pid_t auth_child_pid = 0;

//! If auth_child_pid != 0, the FD which connects to stdin of the auth child.
static int auth_child_fd = 0;

void KillAuthChildSigHandler(int signo) {
  // This is a signal handler, so we're not going to make this too complicated.
  // Just kill it.
  if (auth_child_pid != 0) {
    KillPgrp(auth_child_pid, signo);
  }
}

/*! \brief Return whether the wake-up keypress should be discarded and not be
 * sent to the auth child.
 *
 * Sending the wake-up keypress to the auth child is usually a bad idea because
 * many people use "any" key, not their password's, to wake up the screen saver.
 * Also, when using a blanking screen saver, one can't easily distinguish a
 * locked screen from a turned-off screen, and may thus accidentally start
 * entering the password into a web browser or similar "bad" place.
 *
 * However, it was requested by a user, so why not add it. Usage:
 *
 * XSECURELOCK_DISCARD_FIRST_KEYPRESS=0 xsecurelock
 */
static int DiscardFirstKeypress() {
  return GetIntSetting("XSECURELOCK_DISCARD_FIRST_KEYPRESS",
                       !GetIntSetting("XSECURELOCK_WANT_FIRST_KEYPRESS", 0));
}

int WantAuthChild(int force_auth) {
  if (force_auth) {
    return 1;
  }
  return (auth_child_pid != 0);
}

/*! \brief Return whether buf contains exclusively control characters.
 *
 * Because there is no portable way of doing this (other than relying on wchar
 * routines that are nowhere else exercised in the main program), I'll just
 * match precisely those that ASCII defines as control codes - 00 to 1f as well
 * as 7f (DEL).
 *
 * We do this so we do not forward control keys to the auth child when just
 * waking it up (e.g. because the user tried to unlock the screen with ESC or
 * ENTER).
 *
 * \param buf The string to check.
 * \return 1 if buf contains at least one non-control character, and 0
 *   otherwise.
 */
static int ContainsNonControl(const char *buf) {
  while (*buf) {
    // Note: this almost isprint but not quite - isprint returns false on
    // high bytes in UTF-8 locales but we do want to forward anything UTF-8.
    // An alternative could be walking the string with multibyte functions and
    // using iswprint - but I'd rather not do that anywhere security critical.
    if (*buf < '\000' || (*buf > '\037' && *buf != '\177')) {
      return 1;
    }
    ++buf;
  }
  return 0;
}

int WatchAuthChild(Window w, const char *executable, int force_auth,
                   const char *stdinbuf, int *auth_running) {
  if (auth_child_pid != 0) {
    // Check if auth child returned.
    int status;
    if (WaitPgrp("auth", &auth_child_pid, 0, 0, &status)) {
      // Clean up.
      close(auth_child_fd);

      // Handle success; this will exit the screen lock.
      if (status == 0) {
        *auth_running = 0;
        return 1;
      }

      // To handle failure, we just fall through, as we may want to immediately
      // launch a new auth child and send it a keypress.
    }
  }

  if (force_auth && auth_child_pid == 0) {
    // Start auth child.
    int pc[2];
    if (pipe(pc)) {
      LogErrno("pipe");
    } else {
      pid_t pid = fork();
      if (pid == -1) {
        LogErrno("fork");
      } else if (pid == 0) {
        // Child process.
        StartPgrp();
        ExportWindowID(w);
        close(pc[1]);
        if (pc[0] != 0) {
          if (dup2(pc[0], 0) == -1) {
            LogErrno("dup2");
            _exit(EXIT_FAILURE);
          }
          close(pc[0]);
        }
        execl(executable,  // Path to binary.
              executable,  // argv[0].
              NULL);
        LogErrno("execl");
        sleep(2);  // Reduce log spam or other effects from failed execl.
        _exit(EXIT_FAILURE);
      } else {
        // Parent process after successful fork.
        close(pc[0]);
        auth_child_fd = pc[1];
        auth_child_pid = pid;

        if (stdinbuf != NULL &&
            (DiscardFirstKeypress() || !ContainsNonControl(stdinbuf))) {
          // The auth child has just been started. Do not send any keystrokes to
          // it immediately. Exception: when the user requested different
          // behavior by XSECURELOCK_DISCARD_FIRST_KEYPRESS=0 and there is a
          // printable character.
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
        LogErrno("Failed to send all data to the auth child");
      } else if (written != to_write) {
        Log("Failed to send all data to the auth child");
      }
    } else {
      Log("No auth child. Can't send key events");
    }
  }

  return 0;
}
