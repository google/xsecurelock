#include "logging.h"

#include <errno.h>   // for errno
#include <stdarg.h>  // for va_end, va_list, va_start
#include <stdio.h>   // for fputs, stderr, vfprintf, perror, NULL

void Log(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("xsecurelock: ", stderr);
  vfprintf(stderr, format, args);  // Flawfinder: ignore
  fputs(".\n", stderr);
  va_end(args);
}

void LogErrno(const char *format, ...) {
  int errno_save = errno;
  va_list args;
  va_start(args, format);
  fputs("xsecurelock: ", stderr);
  vfprintf(stderr, format, args);  // Flawfinder: ignore
  fputs(": ", stderr);
  errno = errno_save;
  perror(NULL);
  va_end(args);
  errno = errno_save;
}
