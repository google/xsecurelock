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

#include "authproto.h"

#include <errno.h>   // for errno
#include <stdlib.h>  // for malloc, size_t
#include <stdio.h>   // for snprintf
#include <string.h>  // for strlen
#include <unistd.h>  // for read, write, ssize_t

#include "../logging.h"     // for LogErrno, Log
#include "../mlock_page.h"  // for MLOCK_PAGE

void WritePacket(int fd, char type, const char *message) {
  size_t len_s = strlen(message);
  if (len_s >= 0xFFFF) {
    Log("overlong message, cannot write (hardcoded limit)");
    return;
  }
  int len = len_s;
  if (len < 0 || (size_t)len != len_s) {
    Log("overlong message, cannot write (does not fit in int)");
    return;
  }
  char prefix[16];
  int prefixlen = snprintf(prefix, sizeof(prefix), "%c %d\n", type, len);
  if (prefixlen < 0 || (size_t)prefixlen >= sizeof(prefix)) {
    Log("overlong prefix, cannot write");
    return;
  }
  // Yes, we're wasting syscalls here. This doesn't need to be fast though, and
  // this way we can avoid an extra buffer.
  if (write(fd, prefix, prefixlen) != prefixlen) {
    LogErrno("write");
  }
  if (write(fd, message, len) != (ssize_t)len) {
    LogErrno("write");
  }
  if (write(fd, "\n", 1) != 1) {
    LogErrno("write");
  }
}

static int readchar(int fd, char *c, int eof_permitted) {
  errno = 0;
  if (read(fd, c, 1) != 1) {
    if (errno != 0) {
      LogErrno("read");
    } else if (!eof_permitted) {
      Log("read: unexpected end of file");
    }
    return 0;
  }
  return 1;
}

char ReadPacket(int fd, char **message, int eof_permitted) {
  char type;
  if (!readchar(fd, &type, eof_permitted)) {
    return 0;
  }
  if (type == 0) {
    Log("invalid packet type 0");
    return 0;
  }
  char c;
  if (!readchar(fd, &c, 0)) {
    return 0;
  }
  if (c != ' ') {
    Log("invalid character after packet type, expecting space");
    return 0;
  }
  int len = 0;
  for (;;) {
    errno = 0;
    if (!readchar(fd, &c, 0)) {
      return 0;
    }
    switch (c) {
      case '\n': goto have_len;
      case '0': len = len * 10 + 0; break;
      case '1': len = len * 10 + 1; break;
      case '2': len = len * 10 + 2; break;
      case '3': len = len * 10 + 3; break;
      case '4': len = len * 10 + 4; break;
      case '5': len = len * 10 + 5; break;
      case '6': len = len * 10 + 6; break;
      case '7': len = len * 10 + 7; break;
      case '8': len = len * 10 + 8; break;
      case '9': len = len * 10 + 9; break;
      default:
        Log("invalid character during packet length, expecting 0-9 or newline");
        return 0;
    }
  }
have_len:
  if (len < 0 || len >= 0xFFFF) {
    Log("invalid length %d", len);
    return 0;
  }
  *message = malloc(len + 1);
  if ((type == PTYPE_RESPONSE_LIKE_PASSWORD) &&
      MLOCK_PAGE(*message, len + 1) < 0) {
    // We continue anyway, as the user being unable to unlock the screen is
    // worse.
    LogErrno("mlock");
  }
  errno = 0;
  if (read(fd, *message, len) != (ssize_t)len) {
    if (errno != 0) {
      LogErrno("read");
    } else {
      Log("read: unexpected end of file");
    }
    return 0;
  }
  (*message)[len] = 0;
  if (!readchar(fd, &c, 0)) {
    return 0;
  }
  if (c != '\n') {
    Log("invalid character after packet message, expecting newline");
    return 0;
  }
  return type;
}
