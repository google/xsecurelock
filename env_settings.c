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
    fprintf(stderr, "Ignoring out-of-range value of %s: %s.", name, value);
    return def;
  }
  if ((endptr != NULL && *endptr != 0)) {
    fprintf(stderr, "Ignoring non-numeric value of %s: %s.", name, value);
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
    fprintf(stderr, "Ignoring out-of-range value of %s: %s.", name, value);
    return def;
  }
  if ((endptr != NULL && *endptr != 0)) {
    fprintf(stderr, "Ignoring non-numeric value of %s: %s.", name, value);
    return def;
  }
  return number;
}

int GetIntSetting(const char* name, int def) {
  long lnumber = GetLongSetting(name, def);
  int number = (int)lnumber;
  if (lnumber != (long)number) {
    fprintf(stderr, "Ignoring out-of-range value of %s: %d.", name, number);
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
