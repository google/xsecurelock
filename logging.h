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

#ifndef LOGGING_H
#define LOGGING_H

/*! \brief Prints the given string to the error log (stderr).
 *
 * For a format expanding to "Foo", this will log "xsecurelock: Foo.".
 *
 * \param format A printf format string, followed by its arguments.
 */
void Log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/*! \brief Prints the given string to the error log (stderr).
 *
 * For a format expanding to "Foo", this may log "xsecurelock: Foo: No such
 * file or directory". The value of errno is preserved by this function.
 *
 * \param format A printf format string, followed by its arguments.
 */
void LogErrno(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
