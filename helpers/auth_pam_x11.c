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

#include <locale.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <X11/Xlib.h>

Display *display;
Window window;
GC gc;
XFontStruct *font;
unsigned long Black, White;

void display_string(const char *title, const char *str) {
  Window root;
  int x, y;
  unsigned int w, h, depth, b;

  // Guess the number of displays. No need to be accurate here. If we wanted to,
  // we'd support Xinerama or XRandR.
  if (!XGetGeometry(display, window, &root, &x, &y, &w, &h, &b, &depth)) {
    fprintf(stderr, "XGetGeometry failed.");
    // At least make it somehow visible, if for some odd reason the XDrawString
    // calls should succeed. If they don't, the default X11 error handler will
    // kill us, which is the desired behaviour.
    w = 640;
    h = 480;
  }
  int screens = (w * 9 + h * 8) / (h * 16);

  int len_title = strlen(title);
  int len_str = strlen(str);
  int tw_title = XTextWidth(font, title, len_title);
  int tw_str = XTextWidth(font, str, len_str);

  XClearWindow(display, window);

  int i;
  for (i = 0; i < screens; ++i) {
    int cx = (w * i) / screens + (w / screens) / 2;
    int cy = h / 2;

    XDrawString(display, window, gc, cx - tw_title / 2,
                cy - font->max_bounds.descent - 8, title, len_title);

    XDrawString(display, window, gc, cx - tw_str / 2,
                cy + font->max_bounds.ascent + 8, str, len_str);
  }

  // We have no event loop here. But this is a good point to flush the event
  // queue.
  XSync(display, True);
}

void alert(const char *msg, int is_error) {
  // Display message, wait for key or timeout.
  display_string(is_error ? "Error" : "PAM says", msg);
  sleep(1);
}

int prompt(const char *message, char **response, int echo) {
  // Ask something. Return strdup'd string.
  char pwbuf[256];
  size_t pwlen = 0;
  char displaybuf[sizeof(pwbuf) + 2];
  int blink = 0;

  for (;;) {
    size_t displaylen;
    if (echo) {
      memcpy(displaybuf, pwbuf, pwlen);
      displaylen = pwlen;
    } else {
      displaylen = 0;
      mblen(NULL, 0);
      size_t pos = 0;
      while (pos < pwlen) {
        ++displaylen;
        int len = mblen(pwbuf + pos, pwlen - pos);
        if (len <= 0) {
          break;
        }
        pos += len;
      }
      memset(displaybuf, '*', displaylen);
    }
    displaybuf[displaylen] = blink ? '_' : ' ';
    displaybuf[displaylen + 1] = 0;
    display_string(message, displaybuf);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;

    for (;;) {
      fd_set set;
      FD_ZERO(&set);
      FD_SET(0, &set);
      int nfds = select(1, &set, NULL, NULL, &timeout);
      if (nfds < 0) {
        perror("select");
        return PAM_CONV_ERR;
      }
      if (nfds == 0) {
        // Blink...
        break;
      }

      // From now on, only do nonblocking selects so we update the screen ASAP.
      timeout.tv_usec = 0;

      char inputbuf;
      ssize_t nread = read(0, &inputbuf, 1);
      if (nread <= 0) {
        return PAM_CONV_ERR;
      }
      switch (inputbuf) {
        case '\b':
        case '\177': {
          // Backwards skip with multibyte support.
          mblen(NULL, 0);
          size_t prevpos = 0;
          size_t pos = 0;
          while (pos < pwlen) {
            prevpos = pos;
            int len = mblen(pwbuf + pos, pwlen - pos);
            if (len <= 0) {
              break;
            }
            pos += len;
          }
          pwlen = prevpos;
          break;
        }
        case '\037':
          return PAM_CONV_ERR;
        case '\r':
        case '\n':
          *response = malloc(pwlen + 1);
          memcpy(*response, pwbuf, pwlen);
          response[pwlen] = 0;
          return PAM_SUCCESS;
        default:
          if (pwlen < sizeof(pwbuf)) {
            pwbuf[pwlen] = inputbuf;
            ++pwlen;
          } else {
            return PAM_CONV_ERR;
          }
          break;
      }
    }

    blink = !blink;
  }
}

int converse_one(const struct pam_message *msg, struct pam_response *resp) {
  switch (msg->msg_style) {
    case PAM_PROMPT_ECHO_OFF:
      return prompt(msg->msg, &resp->resp, 0);
    case PAM_PROMPT_ECHO_ON:
      return prompt(msg->msg, &resp->resp, 1);
    case PAM_ERROR_MSG:
      alert(msg->msg, 1);
      return PAM_SUCCESS;
    case PAM_TEXT_INFO:
      alert(msg->msg, 0);
      return PAM_SUCCESS;
  }
  return PAM_CONV_ERR;
}

int converse(int num_msg, const struct pam_message **msg,
             struct pam_response **resp, void *appdata_ptr) {
  (void)appdata_ptr;

  *resp = calloc(num_msg, sizeof(struct pam_response));

  int i;
  for (i = 0; i < num_msg; ++i) {
    int status = converse_one(msg[i], &(*resp)[i]);
    if (status != PAM_SUCCESS) {
      for (i = 0; i < num_msg; ++i) {
        free((*resp)[i].resp);
      }
      free(*resp);
      *resp = NULL;
      return status;
    }
  }

  return PAM_SUCCESS;
}

int main() {
  setlocale(LC_CTYPE, "");

  if ((display = XOpenDisplay(NULL)) == NULL) {
    fprintf(stderr, "could not connect to $DISPLAY\n");
    return 1;
  }

  char hostname[256];
  if (gethostname(hostname, sizeof(hostname))) {
    perror("gethostname");
    return 1;
  }

  struct passwd *pwd = getpwuid(getuid());
  if (!pwd) {
    perror("getpwuid");
    return 1;
  }

  char *window_id_str = getenv("XSCREENSAVER_WINDOW");
  if (window_id_str != NULL) {
    char *end;
    long long int window_id = strtoll(window_id_str, &end, 0);
    if (*end) {
      fprintf(stderr, "invalid window ID: %s\n", window_id_str);
      return 1;
    }
    window = window_id;
  }

  Black = BlackPixel(display, DefaultScreen(display));
  White = WhitePixel(display, DefaultScreen(display));
  font = XLoadQueryFont(display, "fixed");
  if (font == NULL) {
    fprintf(stderr, "could not load a mind-bogglingly stupid font\n");
    exit(1);
  }

  XGCValues gcattrs;
  gcattrs.function = GXcopy;
  gcattrs.foreground = White;
  gcattrs.background = Black;
  gc = XCreateGC(display, window, GCFunction | GCForeground | GCBackground,
                 &gcattrs);
  XSetWindowBackground(display, window, Black);

  struct pam_conv conv;
  conv.conv = converse;
  conv.appdata_ptr = NULL;

  // This program is simple and short-lived enough to just rely on mlockall().
  if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
    perror("mlockall");
    return 1;
  }

  pam_handle_t *pam;
  int status = pam_start("common-auth", pwd->pw_name, &conv, &pam);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_start: %d\n",
            status);  // Or can one call pam_strerror on a NULL handle?
    return 1;
  }

  status = pam_set_item(pam, PAM_RHOST, hostname);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_set_item: %s\n", pam_strerror(pam, status));
    return 1;
  }
  status = pam_set_item(pam, PAM_RUSER, pwd->pw_name);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_set_item: %s\n", pam_strerror(pam, status));
    return 1;
  }
  status = pam_set_item(pam, PAM_TTY, getenv("DISPLAY"));
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_set_item: %s\n", pam_strerror(pam, status));
    return 1;
  }

  status = pam_authenticate(pam, PAM_DISALLOW_NULL_AUTHTOK);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_authenticate: %s\n", pam_strerror(pam, status));
    return 1;
  }

  status = pam_end(pam, status);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_end: %s\n", pam_strerror(pam, status));
    return 1;
  }

  return 0;
}
