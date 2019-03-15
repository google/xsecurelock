#include "env_info.h"

#include <pwd.h>     // for getpwuid_r, passwd
#include <stdlib.h>  // for rand, free, mblen, size_t, exit
#include <string.h>
#include <unistd.h>  // for gethostname, getuid, read, sysconf

#include "logging.h"
#include "mlock_page.h"
#include "util.h"

int GetHostName(char* hostname_buf, size_t hostname_buflen) {
  if (gethostname(hostname_buf, hostname_buflen)) {
    LogErrno("gethostname");
    return 0;
  }
  hostname_buf[hostname_buflen - 1] = 0;
  return 1;
}

int GetUserName(char* username_buf, size_t username_buflen) {
  struct passwd* pwd = NULL;
  struct passwd pwd_storage;
  char* pwd_buf;
  long pwd_bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (pwd_bufsize < 0) {
    pwd_bufsize = 1 << 20;
  }
  pwd_buf = malloc((size_t)pwd_bufsize);
  if (!pwd_buf) {
    LogErrno("malloc(pwd_bufsize)");
    return 0;
  }
  if (MLOCK_PAGE(pwd_buf, pwd_bufsize) < 0) {
    // We continue anyway, as very likely getpwuid_r won't retrieve a password
    // hash on modern systems.
    LogErrno("mlock");
  }
  getpwuid_r(getuid(), &pwd_storage, pwd_buf, (size_t)pwd_bufsize, &pwd);
  if (!pwd) {
    LogErrno("getpwuid_r");
    free(pwd_buf);
    return 0;
  }
  if (strlen(pwd->pw_name) >= username_buflen) {
    Log("Username too long: got %d, want < %d", (int)strlen(pwd->pw_name),
        (int)username_buflen);
    free(pwd_buf);
    return 0;
  }
  strncpy(username_buf, pwd->pw_name, username_buflen);
  username_buf[username_buflen - 1] = 0;
  explicit_bzero(pwd_buf, pwd_bufsize);
  free(pwd_buf);
  return 1;
}
