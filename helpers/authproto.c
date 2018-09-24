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

#include <errno.h>
#include <stdlib.h>  // for rand, free, mblen, size_t, exit
#include <string.h>

#include "../logging.h"     // for Log, LogErrno
#include "../mlock_page.h"  // for MLOCK_PAGE

void WritePacket(int fd, char type, const char *message) {
  size_t len = strlen(message);
  if (len > 0xFFFF) {
    Log("Overlong message to send (%d bytes), trimming to 65535", (int)len);
    len = 0xFFFF;
  }
  if (write(fd, &type, 1) != 1) {
    LogErrno("write");
  }
  unsigned char hi = (len >> 8) & 0xFF;
  unsigned char lo = len & 0xFF;
  if (write(fd, &hi, 1) != 1) {
    LogErrno("write");
  }
  if (write(fd, &lo, 1) != 1) {
    LogErrno("write");
  }
  if (write(fd, message, len) != (ssize_t)len) {
    LogErrno("write");
  }
}

char ReadPacket(int fd, char **message, int eof_permitted) {
  char type;
  errno = 0;
  if (read(fd, &type, 1) != 1) {
    if (errno != 0) {
      LogErrno("read");
    } else if (!eof_permitted) {
      Log("read: unexpected end of file");
    }
    return 0;
  }
  if (type == 0) {
    Log("invalid packet type 0");
  }
  unsigned char hi, lo;
  errno = 0;
  if (read(fd, &hi, 1) != 1) {
    if (errno != 0) {
      LogErrno("read");
    } else {
      Log("read: unexpected end of file");
    }
    return 0;
  }
  errno = 0;
  if (read(fd, &lo, 1) != 1) {
    if (errno != 0) {
      LogErrno("read");
    } else {
      Log("read: unexpected end of file");
    }
    return 0;
  }
  size_t len = (((size_t)hi) << 8) + lo;
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
  return type;
}
