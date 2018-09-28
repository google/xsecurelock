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

#ifndef ENV_INFO_H
#define ENV_INFO_H

#include <stdlib.h>

/*! \brief Loads the current host name.
 *
 * \param hostname_buf The buffer to write the host name to.
 * \param hostname_buflen The size of the buffer.
 * \return Whether fetching the host name succeeded.
 */
int GetHostName(char* hostname_buf, size_t hostname_buflen);

/*! \brief Loads the current user name.
 *
 * \param username_buf The buffer to write the user name to.
 * \param username_buflen The size of the buffer.
 * \return Whether fetching the user name succeeded.
 */
int GetUserName(char* username_buf, size_t username_buflen);

#endif
