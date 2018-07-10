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

#include "env_settings.h"

#include <errno.h>   // for errno, ERANGE
#include <stdio.h>   // for fprintf, NULL, stderr
#include <stdlib.h>  // for getenv, strtol, strtoull
#include <string.h>  // for strchr
#include <unistd.h>  // for access, X_OK

#include "logging.h"

unsigned long long GetUnsignedLongLongSetting(const char* name,
                                              unsigned long long def) {
  const char* value = getenv(name);
  if (value == NULL || value[0] == 0) {
    return def;
  }
  char* endptr = NULL;
  errno = 0;
  unsigned long long number = strtoull(value, &endptr, 0);
  if (errno == ERANGE) {
    Log("Ignoring out-of-range value of %s: %s", name, value);
    return def;
  }
  if ((endptr != NULL && *endptr != 0)) {
    Log("Ignoring non-numeric value of %s: %s", name, value);
    return def;
  }
  return number;
}

long GetLongSetting(const char* name, long def) {
  const char* value = getenv(name);
  if (value == NULL || value[0] == 0) {
    return def;
  }
  char* endptr = NULL;
  errno = 0;
  long number = strtol(value, &endptr, 0);
  if (errno == ERANGE) {
    Log("Ignoring out-of-range value of %s: %s", name, value);
    return def;
  }
  if ((endptr != NULL && *endptr != 0)) {
    Log("Ignoring non-numeric value of %s: %s", name, value);
    return def;
  }
  return number;
}

int GetIntSetting(const char* name, int def) {
  long lnumber = GetLongSetting(name, def);
  int number = (int)lnumber;
  if (lnumber != (long)number) {
    Log("Ignoring out-of-range value of %s: %d", name, number);
    return def;
  }
  return number;
}

const char* GetStringSetting(const char* name, const char* def) {
  const char* value = getenv(name);
  if (value == NULL || value[0] == 0) {
    return def;
  }
  return value;
}

const char* GetExecutablePathSetting(const char* name, const char* def,
                                     int is_auth) {
  const char* value = getenv(name);
  if (value == NULL || value[0] == 0) {
    return def;
  }
  if (strchr(value, '/') && value[0] != '/') {
    Log("Executable name '%s' must be either an absolute path or a file within "
        "%s",
        value, HELPER_PATH);
    return def;
  }
  const char* basename = strrchr(value, '/');
  if (basename == NULL) {
    basename = value;  // No slash, use as is.
  } else {
    ++basename;  // Skip the slash.
  }
  if (is_auth) {
    if (strncmp(basename, "auth_", 5)) {
      Log("Auth executable name '%s' must start with auth_", value);
      return def;
    }
  } else {
    if (!strncmp(basename, "auth_", 5)) {
      Log("Non-auth executable name '%s' must not start with auth_", value);
      return def;
    }
  }
  if (access(value, X_OK)) {
    Log("Executable '%s' must be executable", value);
    return def;
  }
  return value;
}
