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

#ifndef WAIT_PGRP_H
#define WAIT_PGRP_H

#include <limits.h>  // for INT_MIN
#include <unistd.h>  // for pid_t

#define WAIT_ALREADY_DEAD INT_MIN
#define WAIT_NONPOSITIVE_SIGNAL (INT_MIN + 1)

/*! \brief Initializes WaitPgrp.
 *
 * Actually just installs an empty SIGCHLD handler so select(), sigsuspend()
 * etc. get interrupted by the signal.
 */
void InitWaitPgrp(void);

/*! \brief Fork a subprocess, but do not inherit our signal handlers.
 *
 * Otherwise behaves exactly like fork().
 */
pid_t ForkWithoutSigHandlers(void);

/*! \brief Starts a new process group.
 *
 * Must be called from a child process, which will become the process group
 * leader. The process group will never die, unless killed using KillPgrp (which
 * WaitPgrp calls implicitly when the leader process terminates).
 *
 * \return Zero if the operation succeeded.
 */
void StartPgrp(void);

/*! \brief Spawns a helper process.
 *
 * Works just like execv(), but if path is a relative path, it looks it up
 * within HELPER_PATH.
 *
 * If it fails, it logs a message about what it tried to execute and how it
 * failed.
 */
int ExecvHelper(const char *path, const char *const argv[]);

/*! \brief Kills the given process group.
 *
 * \param pid The process group ID.
 * \param signo The signal to send to the process group.
 * \return Zero if and only if sending the signal succeeded.
 */
int KillPgrp(pid_t pid, int signo);

/*! \brief Waits for the given process group to terminate, or checks its status.
 *         If the leader process died, kill the entire group.
 *
 * \param name The name of the process group for logging.
 * \param pid The process group ID; it is set to zero if the process group died.
 * \param do_block Whether to wait for the process group to terminate.
 * \param already_killed Whether the caller already sent SIGTERM to the process
 *   group. If so, we will not log this signal as that'd be spam.
 * \param exit_status Variable that receives the exit status of the leader when
 *   it terminated. Will be negative for a signal, positive for a regular exit,
 *   or one of the WAIT_* constants.
 * \return True if the process group is still alive.
 */
int WaitPgrp(const char *name, pid_t *pid, int do_block, int already_killed,
             int *exit_status);

/*! \brief Waits for the given process to terminate, or checks its status.
 *
 * \param name The name of the process for logging.
 * \param pid The process ID; it is set to zero if the process died.
 * \param do_block Whether to wait for the process to terminate.
 * \param already_killed Whether the caller already sent SIGTERM to the process.
 *   If so, we will not log this signal as that'd be spam. \param exit_status
 *   Variable that receives the exit status of the leader when it terminated.
 *   Will be negative for a signal, positive for a regular exit, or one of the
 *   WAIT_* constants.
 * \return True if the process is still alive.
 */
int WaitProc(const char *name, pid_t *pid, int do_block, int already_killed,
             int *exit_status);

#endif
