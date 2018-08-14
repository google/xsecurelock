#include "logging.h"

#include <errno.h>   // for errno
#include <stdarg.h>  // for va_end, va_list, va_start
#include <stdio.h>   // for fputs, stderr, vfprintf, perror, NULL
#include <time.h>
#include <unistd.h>

static void PrintLogPrefix(void) {
  time_t t = time(NULL);
  struct tm tm_buf;
  struct tm *tm = gmtime_r(&t, &tm_buf);
  char s[32];
  if (!strftime(s, sizeof(s), "%Y-%m-%dT%H:%M:%SZ ", tm)) {
    *s = 0;
  }
  fprintf(stderr, "%s%ld xsecurelock: ", s, (long)getpid());
}

void Log(const char *format, ...) {
  va_list args;
  va_start(args, format);
  PrintLogPrefix();
  vfprintf(stderr, format, args);
  fputs(".\n", stderr);
  va_end(args);
}

void LogErrno(const char *format, ...) {
  int errno_save = errno;
  va_list args;
  va_start(args, format);
  PrintLogPrefix();
  vfprintf(stderr, format, args);
  fputs(": ", stderr);
  errno = errno_save;
  perror(NULL);
  va_end(args);
  errno = errno_save;
}
