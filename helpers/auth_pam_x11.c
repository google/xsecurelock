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

#include <X11/X.h>                // for Window, GCBackground, etc
#include <X11/Xlib.h>             // for XDrawString, XGCValues, etc
#include <ctype.h>                // for tolower, toupper
#include <locale.h>               // for NULL, setlocale, LC_CTYPE
#include <pwd.h>                  // for getpwuid, passwd
#include <security/_pam_types.h>  // for PAM_SUCCESS, pam_strerror, etc
#include <security/pam_appl.h>    // for pam_acct_mgmt, etc
#include <stdio.h>                // for fprintf, stderr, NULL, etc
#include <stdlib.h>               // for free, getenv, calloc, exit, etc
#include <string.h>               // for strlen
#include <sys/select.h>           // for select, FD_SET, FD_ZERO, etc
#include <sys/time.h>             // for timeval
#include <unistd.h>               // for getuid, sleep, ssize_t

#ifdef HAVE_XKB
#include <X11/XKBlib.h>
#endif

#include "../mlock_page.h"

//! The blinking interval in microseconds.
#define BLINK_INTERVAL 250000

//! The maximum time to wait at a prompt for user input in microseconds.
#define PROMPT_TIMEOUT 60000000

/*! \brief Exit on conversation errors. This seems required on Linux PAM.
 *
 * Maybe this should be a ./configure option, but as it seems generally harmless
 * to call exit() from a PAM conversation, why not always do it.
 *
 * The symptom of the problem is BTW an endless loop of password prompts when
 * hitting Escape to abort the prompt. The intended result would be a
 * termination of pam_authenticate.
 */
#define EXIT_ON_CONVERSATION_ERROR

//! The X11 display.
Display *display;

//! The X11 window to draw in. Provided from $XSCREENSAVER_WINDOW.
Window window;

//! The X11 graphics context to draw with.
GC gc;

//! The font for the PAM messages.
XFontStruct *font;

//! The Black color (used as background).
unsigned long Black;

//! The White color (used as foreground).
unsigned long White;

#ifdef HAVE_XKB
/*! \brief Check which modifiers are active.
 *
 * \return The current modifier mask as a string.
 */
const char *get_indicators() {
  static char buf[128];
  char *p;

  XkbDescPtr xkb;
  xkb = XkbGetMap(display, 0, XkbUseCoreKbd);
  if (XkbGetNames(display, XkbIndicatorNamesMask | XkbGroupNamesMask, xkb) !=
      Success) {
    fprintf(stderr, "XkbGetNames failed\n");
    XkbFreeClientMap(xkb, 0, True);
    return "";
  }
  XkbStateRec state;
  if (XkbGetState(display, XkbUseCoreKbd, &state) != Success) {
    fprintf(stderr, "XkbGetState failed\n");
    XkbFreeClientMap(xkb, 0, True);
    return "";
  }
  unsigned int istate;
  if (XkbGetIndicatorState(display, XkbUseCoreKbd, &istate) != Success) {
    fprintf(stderr, "XkbGetIndicatorState failed\n");
    XkbFreeClientMap(xkb, 0, True);
    return "";
  }

  p = buf;

  const char *word = "Keyboard: ";
  size_t n = strlen(word);
  if (n >= sizeof(buf) - (p - buf)) {
    fprintf(stderr, "Not enough space to store intro '%s'.\n", word);
    return "";
  }
  memcpy(p, word, n);
  p += n;

  word = XGetAtomName(display, xkb->names->groups[state.group]);
  n = strlen(word);
  if (n >= sizeof(buf) - (p - buf)) {
    fprintf(stderr, "Not enough space to store group name '%s'.\n", word);
    return "";
  }
  memcpy(p, word, n);
  p += n;

  int i;
  for (i = 0; i < XkbNumIndicators; i++) {
    if (!(istate & (1 << i))) {
      continue;
    }
    Atom namea = xkb->names->indicators[i];
    if (namea == None) {
      continue;
    }
    const char *word = XGetAtomName(display, namea);
    size_t n = strlen(word);
    if (n + 2 >= sizeof(buf) - (p - buf)) {
      fprintf(stderr, "Not enough space to store modifier name '%s'.\n", word);
      continue;
    }
    memcpy(p, ", ", 2);
    memcpy(p+2, word, n);
    p += n+2;
  }
  *p = 0;
  return buf;
}
#endif

/*! \brief Display a string in the window.
 *
 * The given title and message will be displayed on all screens. In case caps
 * lock is enabled, the string's case will be inverted.
 *
 * \param title The title of the message.
 * \param str The message itself.
 */
void display_string(const char *title, const char *str) {
  Window root;
  int x, y;
  unsigned int w, h, depth, b;

  static int region_x;
  static int region_y;
  static int region_w = 0;
  static int region_h = 0;

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
  int th = font->max_bounds.ascent + font->max_bounds.descent + 4;
  int to = font->max_bounds.ascent + 2; // Text at to has bbox from 0 to th.

  int len_title = strlen(title);
  int tw_title = XTextWidth(font, title, len_title);

  int len_str = strlen(str);
  int tw_str = XTextWidth(font, str, len_str);

#ifdef HAVE_XKB
  const char *indicators = get_indicators();
  int len_indicators = strlen(indicators);
  int tw_indicators = XTextWidth(font, indicators, len_indicators);
#endif

  if (region_w == 0 || region_h == 0) {
    XClearWindow(display, window);
  }

  int i;
  for (i = 0; i < screens; ++i) {
    int cx = (w * i) / screens + (w / screens) / 2;
    int cy = h / 2;
    int sy = cy + to - th * 2;

    // Clear the region last written to.
    if (region_w != 0 && region_h != 0) {
      XClearArea(display, window, cx + region_x, cy + region_y, region_w,
                 region_h, False);
    }

    XDrawString(display, window, gc, cx - tw_title / 2,
                sy, title, len_title);

    XDrawString(display, window, gc, cx - tw_str / 2,
                sy + th * 2, str, len_str);

#ifdef HAVE_XKB
    XDrawString(display, window, gc, cx - tw_indicators / 2,
                sy + th * 3, indicators, len_indicators);
#endif
  }

  // Remember the region we just wrote to, relative to cx and cy.
  region_w = tw_title;
  if (tw_str > region_w) {
    region_w = tw_str;
  }
#ifdef HAVE_XKB
  if (tw_indicators > region_w) {
    region_w = tw_indicators;
  }
#endif
  region_x = -region_w / 2;
#ifdef HAVE_XKB
  region_h = 4 * th;
#else
  region_h = 3 * th;
#endif
  region_y = -region_h / 2;

  // We have no event loop here. But this is a good point to flush the event
  // queue.
  XSync(display, True);
}

/*! \brief Show a message to the user.
 *
 * \param msg The message.
 * \param is_error If true, the message is assumed to be an error.
 */
void alert(const char *msg, int is_error) {
  // Display message, wait for key or timeout.
  display_string(is_error ? "Error" : "PAM says", msg);
  sleep(1);
}

//! The size of the buffer to store the password in. Not NUL terminated.
#define PWBUF_SIZE 256

//! The size of the buffer to use for display, with space for cursor and NUL.
#define DISPLAYBUF_SIZE (PWBUF_SIZE + 2)

/*! \brief Ask a question to the user.
 *
 * \param msg The message.
 * \param response The response will be stored in a newly allocated buffer here.
 *   The caller is supposed to eventually free() it.
 * \param echo If true, the input will be shown; otherwise it will be hidden
 *   (password entry).
 * \return PAM_SUCCESS if successful, anything else otherwise.
 */
int prompt(const char *msg, char **response, int echo) {
  // Ask something. Return strdup'd string.
  struct {
    // Input buffer. Not NUL-terminated.
    char pwbuf[PWBUF_SIZE];
    // Current input length.
    size_t pwlen;

    // Display buffer. If echo is 0, this will only contain asterisks, a
    // possible cursor, and be NUL-terminated.
    char displaybuf[DISPLAYBUF_SIZE];
    // Display buffer length.
    size_t displaylen;

    // Character read buffer.
    char inputbuf;

    // Temporary position variables that might leak properties about the
    // password and thus are in the private struct too.
    size_t prevpos;
    size_t pos;
    int len;
  } priv;
  int blinks = 0;

  if (!echo && MLOCK_PAGE(&priv, sizeof(priv)) < 0) {
    perror("mlock");
    // We continue anyway, as the user being unable to unlock the screen is
    // worse. But let's alert the user.
    alert("Password will not be stored securely.", 1);
  }

  priv.pwlen = 0;

  int max_blinks = PROMPT_TIMEOUT / BLINK_INTERVAL;

  for (;;) {
    if (echo) {
      memcpy(priv.displaybuf, priv.pwbuf, priv.pwlen);
      priv.displaylen = priv.pwlen;
    } else {
      mblen(NULL, 0);
      priv.pos = priv.displaylen = 0;
      while (priv.pos < priv.pwlen) {
        ++priv.displaylen;
        // Note: this won't read past priv.pwlen.
        priv.len = mblen(priv.pwbuf + priv.pos, priv.pwlen - priv.pos);
        if (priv.len <= 0) {
          // This guarantees to "eat" one byte each step. Therefore,
          // priv.displaylen <= priv.pwlen is ensured.
          break;
        }
        priv.pos += priv.len;
      }
      memset(priv.displaybuf, '*', priv.displaylen);
    }
    // Note that priv.pwlen <= sizeof(priv.pwbuf) and thus
    // priv.pwlen + 2 <= sizeof(priv.displaybuf).
    priv.displaybuf[priv.displaylen] = (blinks % 2) ? ' ' : '_';
    priv.displaybuf[priv.displaylen + 1] = 0;
    display_string(msg, priv.displaybuf);

    // Blink the cursor.
    ++blinks;
    if (blinks > max_blinks) {
      return PAM_CONV_ERR;
    }

    struct timeval timeout;
    timeout.tv_sec = BLINK_INTERVAL / 1000000;
    timeout.tv_usec = BLINK_INTERVAL % 1000000;

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

      // Force the cursor to be in visible state while typing. This also resets
      // the prompt timeout.
      blinks = 0;

      ssize_t nread = read(0, &priv.inputbuf, 1);
      if (nread <= 0) {
        return PAM_CONV_ERR;
      }
      switch (priv.inputbuf) {
        case '\b':
        case '\177': {
          // Backwards skip with multibyte support.
          mblen(NULL, 0);
          priv.pos = priv.prevpos = 0;
          while (priv.pos < priv.pwlen) {
            priv.prevpos = priv.pos;
            // Note: this won't read past priv.pwlen.
            priv.len = mblen(priv.pwbuf + priv.pos, priv.pwlen - priv.pos);
            if (priv.len <= 0) {
              // This guarantees to "eat" one byte each step. Therefore,
              // this cannot loop endlessly.
              break;
            }
            priv.pos += priv.len;
          }
          priv.pwlen = priv.prevpos;
          break;
        }
        case 0:
        case '\033':
          return PAM_CONV_ERR;
        case '\r':
        case '\n':
          *response = malloc(priv.pwlen + 1);
          if (!echo && MLOCK_PAGE(*response, priv.pwlen + 1) < 0) {
            perror("mlock");
            // We continue anyway, as the user being unable to unlock the screen
            // is worse. But let's alert the user of this.
            alert("Password has not been stored securely.", 1);
          }
          memcpy(*response, priv.pwbuf, priv.pwlen);
          (*response)[priv.pwlen] = 0;
          return PAM_SUCCESS;
        default:
          if (priv.pwlen < sizeof(priv.pwbuf)) {
            priv.pwbuf[priv.pwlen] = priv.inputbuf;
            ++priv.pwlen;
          } else {
            return PAM_CONV_ERR;
          }
          break;
      }
    }
  }
}

/*! \brief Perform a single PAM conversation step.
 *
 * \param msg The PAM message.
 * \param resp The PAM response to store the output in.
 * \return The PAM status (PAM_SUCCESS in case of success, or anything else in
 *   case of error).
 */
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

/*! \brief Perform a PAM conversation.
 *
 * \param num_msg The number of conversation steps to execute.
 * \param msg The PAM messages.
 * \param resp The PAM responses to store the output in.
 * \param appdata_ptr Unused.
 * \return The PAM status (PAM_SUCCESS in case of success, or anything else in
 *   case of error).
 */
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
#ifdef EXIT_ON_CONVERSATION_ERROR
      exit(1);
#endif
      return status;
    }
  }

  return PAM_SUCCESS;
}

/*! \brief Perform PAM authentication.
 *
 * \param username The user name to authenticate as.
 * \param hostname The host name to authenticate on.
 * \param conv The PAM conversation handler.
 * \param pam The PAM handle will be returned here.
 * \return The PAM status (PAM_SUCCESS after successful authentication, or
 *   anything else in case of error).
 */
int authenticate(const char *username, const char *hostname,
                 struct pam_conv *conv, pam_handle_t **pam) {
  const char *service_name = getenv("XSECURELOCK_PAM_SERVICE");
  if (service_name == NULL || service_name[0] == '\0')
    service_name = PAM_SERVICE_NAME;
  int status = pam_start(service_name, username, conv, pam);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_start: %d\n",
            status);  // Or can one call pam_strerror on a NULL handle?
    return status;
  }

  status = pam_set_item(*pam, PAM_RHOST, hostname);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_set_item: %s\n", pam_strerror(*pam, status));
    return status;
  }
  status = pam_set_item(*pam, PAM_RUSER, username);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_set_item: %s\n", pam_strerror(*pam, status));
    return status;
  }
  status = pam_set_item(*pam, PAM_TTY, getenv("DISPLAY"));
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_set_item: %s\n", pam_strerror(*pam, status));
    return status;
  }

  status = pam_authenticate(*pam, 0);
  if (status != PAM_SUCCESS) {
    fprintf(stderr, "pam_authenticate: %s\n", pam_strerror(*pam, status));
    return status;
  }

  int status2 = pam_acct_mgmt(*pam, 0);

  if (status2 == PAM_NEW_AUTHTOK_REQD) {
    status2 = pam_chauthtok(*pam, PAM_CHANGE_EXPIRED_AUTHTOK);
#ifdef PAM_CHECK_ACCOUNT_TYPE
    if (status2 != PAM_SUCCESS) {
      fprintf(stderr, "pam_chauthtok: %s\n", pam_strerror(*pam, status2));
      return status2;
    }
#endif
  }

#ifdef PAM_CHECK_ACCOUNT_TYPE
  if (status2 != PAM_SUCCESS) {
    // If this one is true, it must be coming from pam_acct_mgmt, as
    // pam_chauthtok's result already has been checked against PAM_SUCCESS.
    fprintf(stderr, "pam_acct_mgmt: %s\n", pam_strerror(*pam, status2));
    return status2;
  }
#endif

  return status;
}

/*! \brief The main program.
 *
 * Usage: XSCREENSAVER_WINDOW=window_id ./auth_pam_x11; status=$?
 *
 * \return 0 if authentication successful, anything else otherwise.
 */
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

  font = NULL;
  const char *font_name = getenv("XSECURELOCK_FONT");
  if (font_name != NULL && font_name[0] != 0) {
    font = XLoadQueryFont(display, font_name);
    if (font == NULL) {
      fprintf(stderr, "could not load the specified font %s - trying to fall back to fixed\n", font_name);
    }
  }
  if (font == NULL) {
    font = XLoadQueryFont(display, "fixed");
  }
  if (font == NULL) {
    fprintf(stderr, "could not load a mind-bogglingly stupid font\n");
    exit(1);
  }

  XGCValues gcattrs;
  gcattrs.function = GXcopy;
  gcattrs.foreground = White;
  gcattrs.background = Black;
  gcattrs.font = font->fid;
  gc = XCreateGC(display, window,
                 GCFunction | GCForeground | GCBackground | GCFont, &gcattrs);
  XSetWindowBackground(display, window, Black);

  struct pam_conv conv;
  conv.conv = converse;
  conv.appdata_ptr = NULL;

  pam_handle_t *pam;
  int status = authenticate(pwd->pw_name, hostname, &conv, &pam);
  int status2 = pam_end(pam, status);
  if (status != PAM_SUCCESS) {
    // The caller already displayed an error.
    return 1;
  }
  if (status2 != PAM_SUCCESS) {
    fprintf(stderr, "pam_end: %s\n", pam_strerror(pam, status2));
    return 1;
  }

  return 0;
}
