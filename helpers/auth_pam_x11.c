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

#include <X11/X.h>                  // for Atom, Success, None, GCBackground
#include <X11/Xlib.h>               // for XDrawString, XTextWidth, XFontStruct
#include <X11/extensions/XKB.h>     // for XkbUseCoreKbd, XkbGroupNamesMask
#include <X11/extensions/XKBstr.h>  // for XkbStateRec, _XkbDesc, _XkbNamesRec
#include <locale.h>                 // for NULL, setlocale, LC_CTYPE
#include <pwd.h>                    // for getpwuid, passwd
#include <security/_pam_types.h>    // for PAM_SUCCESS, pam_strerror, pam_re...
#include <security/pam_appl.h>      // for pam_end, pam_start, pam_acct_mgmt
#include <stdio.h>                  // for fprintf, stderr, LogErrno, NULL
#include <stdlib.h>                 // for mblen, exit, free, calloc, getenv
#include <string.h>                 // for memcpy, strlen, memset
#include <sys/select.h>             // for timeval, select, FD_SET, FD_ZERO
#include <time.h>                   // for time
#include <unistd.h>                 // for gethostname, getuid, read, ssize_t

#ifdef HAVE_XFT_EXT
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#endif

#ifdef HAVE_XKB_EXT
#include <X11/XKBlib.h>  // for XkbFreeClientMap, XkbGetIndicator...
#endif

#include "../env_settings.h"      // for GetStringSetting
#include "../logging.h"           // for Log, LogErrno
#include "../mlock_page.h"        // for MLOCK_PAGE
#include "../xscreensaver_api.h"  // for ReadWindowID
#include "monitors.h"             // for Monitor, GetMonitors, IsMonitorCh...

//! The blinking interval in microseconds.
#define BLINK_INTERVAL (250 * 1000)

//! The maximum time to wait at a prompt for user input in microseconds.
int prompt_timeout;

//! Length of the "paranoid password display".
#define PARANOID_PASSWORD_LENGTH 32

//! Minimum distance the cursor shall move on keypress.
#define PARANOID_PASSWORD_MIN_CHANGE 4

//! Border to clear around text (mainly a workaround for bad TextWidth results).
#define TEXT_BORDER 0

//! Draw border rectangle (mainly for debugging).
#undef DRAW_BORDER

//! Extra line spacing.
#define LINE_SPACING 4

//! Whether password display should hide the length.
int paranoid_password;

//! If set, we can start a new login session.
int have_switch_user_command;

//! If set, the prompt will be fixed by <username>@.
int show_username;

//! If set, the prompt will be fixed by <hostname>. If >1, the hostname will be
// shown in full and not cut at the first dot.
int show_hostname;

//! The local hostname.
const char *hostname;

//! The username to authenticate as.
const char *username;

//! The X11 display.
Display *display;

//! The X11 window to draw in. Provided from $XSCREENSAVER_WINDOW.
Window window;

//! The X11 graphics context to draw with.
GC gc;

//! The X11 graphics context to draw warnings with.
GC gc_warning;

//! The X11 core font for the PAM messages.
XFontStruct *core_font;

#ifdef HAVE_XFT_EXT
//! The Xft font for the PAM messages.
XftColor xft_color;
XftColor xft_color_warning;
XftDraw *xft_draw;
XftFont *xft_font;
#endif

//! The background color.
unsigned long Background;

//! The foreground color.
unsigned long Foreground;

//! The warning color (used as foreground).
unsigned long Warning;

//! Set if a conversation error has happened during the last PAM call.
static int conv_error = 0;

//! The cursor character displayed at the end of the masked password input.
static const char cursor[] = "_";

//! The x offset to apply to the entire display (to mitigate burn-in).
static int x_offset = 0;

//! The y offset to apply to the entire display (to mitigate burn-in).
static int y_offset = 0;

//! Maximum offset value when dynamic changes are enabled.
static int burnin_mitigation_max_offset = 0;

//! How much the offsets are allowed to change dynamically, and if so, how high.
static int burnin_mitigation_max_offset_change = 0;

#define MAX_MONITORS 16
static int num_monitors;
static Monitor monitors[MAX_MONITORS];

int have_xkb_ext;

/*! \brief Check which modifiers are active.
 *
 * \param warning Will be set to 1 if something's "bad" with the keyboard
 *     layout (e.g. Caps Lock).
 *
 * \return The current modifier mask as a string.
 */
const char *get_indicators(int *warning) {
#ifdef HAVE_XKB_EXT
  static char buf[128];
  char *p;

  if (!have_xkb_ext) {
    return "";
  }

  XkbDescPtr xkb;
  xkb = XkbGetMap(display, 0, XkbUseCoreKbd);
  if (XkbGetNames(
          display,
          XkbIndicatorNamesMask | XkbGroupNamesMask | XkbSymbolsNameMask,
          xkb) != Success) {
    Log("XkbGetNames failed");
    XkbFreeClientMap(xkb, 0, True);
    return "";
  }
  XkbStateRec state;
  if (XkbGetState(display, XkbUseCoreKbd, &state) != Success) {
    Log("XkbGetState failed");
    XkbFreeClientMap(xkb, 0, True);
    return "";
  }
  unsigned int istate;
  if (XkbGetIndicatorState(display, XkbUseCoreKbd, &istate) != Success) {
    Log("XkbGetIndicatorState failed");
    XkbFreeClientMap(xkb, 0, True);
    return "";
  }

  // Detect Caps Lock.
  // Note: in very pathological cases the modifier might be set without an
  // XkbIndicator for it; then we show the line in red without telling the user
  // why. Such a situation has not been observd yet though.
  if (state.mods & LockMask) {
    *warning = 1;
  }

  p = buf;

  const char *word = "Keyboard: ";
  size_t n = strlen(word);
  if (n >= sizeof(buf) - (p - buf)) {
    Log("Not enough space to store intro '%s'", word);
    return "";
  }
  memcpy(p, word, n);
  p += n;

  int have_output = 0;
  Atom layouta = xkb->names->groups[state.group];  // Human-readable.
  if (layouta == None) {
    layouta = xkb->names->symbols;  // Machine-readable fallback.
  }
  if (layouta != None) {
    const char *layout = XGetAtomName(display, layouta);
    n = strlen(layout);
    if (n >= sizeof(buf) - (p - buf)) {
      Log("Not enough space to store layout name '%s'", layout);
      return "";
    }
    memcpy(p, layout, n);
    p += n;
    have_output = 1;
  }

  int i;
  for (i = 0; i < XkbNumIndicators; i++) {
    if (!(istate & (1 << i))) {
      continue;
    }
    Atom namea = xkb->names->indicators[i];
    if (namea == None) {
      continue;
    }
    if (have_output) {
      if (2 >= sizeof(buf) - (p - buf)) {
        Log("Not enough space to store another modifier name");
        break;
      }
      memcpy(p, ", ", 2);
      p += 2;
    }
    const char *name = XGetAtomName(display, namea);
    size_t n = strlen(name);
    if (n >= sizeof(buf) - (p - buf)) {
      Log("Not enough space to store modifier name '%s'", name);
      break;
    }
    memcpy(p, name, n);
    p += n;
    have_output = 1;
  }
  *p = 0;
  return have_output ? buf : "";
#else
  return "";
#endif
}

int TextAscent(void) {
#ifdef HAVE_XFT_EXT
  if (xft_font != NULL) {
    return xft_font->ascent;
  }
#endif
  return core_font->max_bounds.ascent;
}

int TextDescent(void) {
#ifdef HAVE_XFT_EXT
  if (xft_font != NULL) {
    return xft_font->descent;
  }
#endif
  return core_font->max_bounds.descent;
}

#ifdef HAVE_XFT_EXT
// Returns the amount of pixels to expand the logical box in extents so it
// covers the visible box.
int XGlyphInfoExpandAmount(XGlyphInfo *extents) {
  // Use whichever is larger - visible bounding box (bigger if font is italic)
  // or spacing to next character (bigger if last character is a space).
  // Best reference I could find:
  //   https://keithp.com/~keithp/render/Xft.tutorial
  // Visible bounding box: [-x, -x + width[
  // Logical bounding box: [0, xOff[
  // For centering we should always use the logical bounding box, however for
  // erasing we should use the visible bounding box. Thus our goal is to
  // expand the _logical_ box to fully cover the _visible_ box:
  int expand_left = extents->x;
  int expand_right = -extents->x + extents->width - extents->xOff;
  int expand_max = expand_left > expand_right ? expand_left : expand_right;
  int expand_positive = expand_max > 0 ? expand_max : 0;
  return expand_positive;
}
#endif

int TextWidth(const char *string, int len) {
#ifdef HAVE_XFT_EXT
  if (xft_font != NULL) {
    XGlyphInfo extents;
    XftTextExtentsUtf8(display, xft_font, (const FcChar8 *)string, len,
                       &extents);
    return extents.xOff + 2 * XGlyphInfoExpandAmount(&extents);
  }
#endif
  return XTextWidth(core_font, string, len);
}

void DrawString(int x, int y, int is_warning, const char *string, int len) {
#ifdef HAVE_XFT_EXT
  if (xft_font != NULL) {
    // HACK: Query text extents here to make the text fit into the specified
    // box. For y this is covered by the usual ascent/descent behavior - for x
    // we however do have to work around font descents being drawn to the left
    // of the cursor.
    XGlyphInfo extents;
    XftTextExtentsUtf8(display, xft_font, (const FcChar8 *)string, len,
                       &extents);
    XftDrawStringUtf8(xft_draw, is_warning ? &xft_color_warning : &xft_color,
                      xft_font, x + XGlyphInfoExpandAmount(&extents), y,
                      (const FcChar8 *)string, len);
    return;
  }
#endif
  XDrawString(display, window, is_warning ? gc_warning : gc, x, y, string, len);
}

void StrAppend(char **output, size_t *output_size, const char *input,
               size_t input_size) {
  if (*output_size <= input_size) {
    // Cut the input off. Sorry.
    input_size = *output_size - 1;
  }
  memcpy(*output, input, input_size);
  *output += input_size;
  *output_size -= input_size;
}

void BuildTitle(char *output, size_t output_size, const char *input) {
  if (show_username) {
    size_t username_len = strlen(username);
    StrAppend(&output, &output_size, username, username_len);
  }

  if (show_username && show_hostname) {
    StrAppend(&output, &output_size, "@", 1);
  }

  if (show_hostname) {
    size_t hostname_len =
        show_hostname > 1 ? strlen(hostname) : strcspn(hostname, ".");
    StrAppend(&output, &output_size, hostname, hostname_len);
  }

  if (*input == 0) {
    *output = 0;
    return;
  }

  StrAppend(&output, &output_size, " - ", 3);
  strncpy(output, input, output_size - 1);
  output[output_size - 1] = 0;
}

/*! \brief Display a string in the window.
 *
 * The given title and message will be displayed on all screens. In case caps
 * lock is enabled, the string's case will be inverted.
 *
 * \param title The title of the message.
 * \param str The message itself.
 */
void display_string(const char *title, const char *str) {
  static int region_x;
  static int region_y;
  static int region_w = 0;
  static int region_h = 0;

  char full_title[256];
  BuildTitle(full_title, sizeof(full_title), title);

  int th = TextAscent() + TextDescent() + LINE_SPACING;
  int to = TextAscent() + LINE_SPACING / 2;  // Text at to fits into 0 to th.

  int len_full_title = strlen(full_title);
  int tw_full_title = TextWidth(full_title, len_full_title);

  int len_str = strlen(str);
  int tw_str = TextWidth(str, len_str);

  int indicators_warning = 0;
  const char *indicators = get_indicators(&indicators_warning);
  int len_indicators = strlen(indicators);
  int tw_indicators = TextWidth(indicators, len_indicators);

  const char *switch_user = have_switch_user_command
                                ? "Press Ctrl-Alt-L or Win-L to switch user"
                                : "";
  int len_switch_user = strlen(switch_user);
  int tw_switch_user = TextWidth(switch_user, len_switch_user);

  // Compute the region we will be using, relative to cx and cy.
  int box_w = tw_full_title;
  if (box_w < tw_str) {
    box_w = tw_str;
  }
  if (box_w < tw_indicators) {
    box_w = tw_indicators;
  }
  if (box_w < tw_switch_user) {
    box_w = tw_switch_user;
  }
  int border = TEXT_BORDER + burnin_mitigation_max_offset_change;
  int box_h = (have_switch_user_command ? 5 : 4) * th;
  if (region_w < box_w + 2 * border) {
    region_w = box_w + 2 * border;
  }
  region_x = -region_w / 2;
  if (region_h < box_h + 2 * border) {
    region_h = box_h + 2 * border;
  }
  region_y = -region_h / 2;

  if (burnin_mitigation_max_offset_change > 0) {
    x_offset += rand() % (2 * burnin_mitigation_max_offset_change + 1) -
                burnin_mitigation_max_offset_change;
    if (x_offset < -burnin_mitigation_max_offset) {
      x_offset = -burnin_mitigation_max_offset;
    }
    if (x_offset > burnin_mitigation_max_offset) {
      x_offset = burnin_mitigation_max_offset;
    }
    y_offset += rand() % (2 * burnin_mitigation_max_offset_change + 1) -
                burnin_mitigation_max_offset_change;
    if (y_offset < -burnin_mitigation_max_offset) {
      y_offset = -burnin_mitigation_max_offset;
    }
    if (y_offset > burnin_mitigation_max_offset) {
      y_offset = burnin_mitigation_max_offset;
    }
  }

  int i;
  for (i = 0; i < num_monitors; ++i) {
    int cx = monitors[i].x + monitors[i].width / 2 + x_offset;
    int cy = monitors[i].y + monitors[i].height / 2 + y_offset;
    int sy = cy + to - box_h / 2;

    // Clip all following output to the bounds of this monitor.
    XRectangle rect;
    rect.x = monitors[i].x;
    rect.y = monitors[i].y;
    rect.width = monitors[i].width;
    rect.height = monitors[i].height;
    XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXBanded);

    // Clear the region last written to.
    if (region_w != 0 && region_h != 0) {
      XClearArea(display, window,               //
                 cx + region_x, cy + region_y,  //
                 region_w, region_h,            //
                 False);
    }

#ifdef DRAW_BORDER
    XDrawRectangle(display, window, gc,             //
                   cx - box_w / 2, cy - box_h / 2,  //
                   box_w - 1, box_h - 1);
    XDrawRectangle(display, window, gc,           //
                   cx + region_x, cy + region_y,  //
                   region_w - 1, region_h - 1);
#endif

    DrawString(cx - tw_full_title / 2, sy, 0, full_title, len_full_title);

    DrawString(cx - tw_str / 2, sy + th * 2, 0, str, len_str);
    DrawString(cx - tw_indicators / 2, sy + th * 3, indicators_warning,
               indicators, len_indicators);
    if (have_switch_user_command) {
      DrawString(cx - tw_switch_user / 2, sy + th * 4, 0, switch_user,
                 len_switch_user);
    }

    // Disable clipping again.
    XSetClipMask(display, gc, None);
  }

  // Make the things just drawn appear on the screen as soon as possible.
  XFlush(display);
}

void wait_for_keypress(int seconds) {
  // Sleep for up to 1 second _or_ a key press.
  struct timeval timeout;
  timeout.tv_sec = seconds;
  timeout.tv_usec = 0;
  fd_set set;
  memset(&set, 0, sizeof(set));  // For clang-analyzer.
  FD_ZERO(&set);
  FD_SET(0, &set);
  select(1, &set, NULL, NULL, &timeout);
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
    // The received X11 event.
    XEvent ev;

    // Input buffer. Not NUL-terminated.
    char pwbuf[PWBUF_SIZE];
    // Current input length.
    size_t pwlen;

    // Display buffer. If echo is 0, this will only contain asterisks, a
    // possible cursor, and be NUL-terminated.
    char displaybuf[DISPLAYBUF_SIZE];
    // Display buffer length.
    size_t displaylen;

    // The display marker changes on every input action to a value from 0 to
    // PARANOID_PASSWORD-1. It indicates where to display the "cursor".
    size_t displaymarker;

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
    LogErrno("mlock");
    // We continue anyway, as the user being unable to unlock the screen is
    // worse. But let's alert the user.
    display_string("Error", "Password will not be stored securely.");
    wait_for_keypress(1);
  }

  priv.pwlen = 0;
  priv.displaymarker = rand() % PARANOID_PASSWORD_LENGTH;

  int max_blinks = (prompt_timeout * 1000 * 1000) / BLINK_INTERVAL;

  // Unfortunately we may have to break out of multiple loops at once here but
  // still do common cleanup work. So we have to track the return value in a
  // variable.
  int status = PAM_CONV_ERR;
  int done = 0;

  while (!done) {
    if (echo) {
      if (priv.pwlen != 0) {
        memcpy(priv.displaybuf, priv.pwbuf, priv.pwlen);
      }
      priv.displaylen = priv.pwlen;
      // Note that priv.pwlen <= sizeof(priv.pwbuf) and thus
      // priv.pwlen + 2 <= sizeof(priv.displaybuf).
      priv.displaybuf[priv.displaylen] = (blinks % 2) ? ' ' : *cursor;
      priv.displaybuf[priv.displaylen + 1] = '\0';
    } else if (paranoid_password) {
      priv.displaylen = PARANOID_PASSWORD_LENGTH;
      memset(priv.displaybuf, '_', priv.displaylen);
      priv.displaybuf[priv.pwlen ? priv.displaymarker : 0] =
          (blinks % 2) ? '|' : '-';
      priv.displaybuf[priv.displaylen] = '\0';
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
      // Note that priv.pwlen <= sizeof(priv.pwbuf) and thus
      // priv.pwlen + 2 <= sizeof(priv.displaybuf).
      priv.displaybuf[priv.displaylen] = (blinks % 2) ? ' ' : *cursor;
      priv.displaybuf[priv.displaylen + 1] = '\0';
    }
    display_string(msg, priv.displaybuf);

    // Blink the cursor.
    ++blinks;
    if (blinks > max_blinks) {
      done = 1;
      break;
    }

    struct timeval timeout;
    timeout.tv_sec = BLINK_INTERVAL / 1000000;
    timeout.tv_usec = BLINK_INTERVAL % 1000000;

    while (!done) {
      fd_set set;
      memset(&set, 0, sizeof(set));  // For clang-analyzer.
      FD_ZERO(&set);
      FD_SET(0, &set);
      int nfds = select(1, &set, NULL, NULL, &timeout);
      if (nfds < 0) {
        LogErrno("select");
        done = 1;
        break;
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
        Log("EOF on password input - bailing out");
        done = 1;
        break;
      }
      switch (priv.inputbuf) {
        case '\b':      // Backspace.
        case '\177': {  // Delete (note: i3lock does not handle this one).
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
          if (priv.prevpos != priv.pwlen) {
            priv.displaymarker =
                (priv.displaymarker - 1 + PARANOID_PASSWORD_MIN_CHANGE +
                 rand() % (PARANOID_PASSWORD_LENGTH -
                           2 * PARANOID_PASSWORD_MIN_CHANGE)) %
                    (PARANOID_PASSWORD_LENGTH - 1) +
                1;
          }
          priv.pwlen = priv.prevpos;
          break;
        }
        case '\001':  // Ctrl-A.
          // Clearing input line on just Ctrl-A is odd - but commonly
          // requested. In most toolkits, Ctrl-A does not immediately erase but
          // almost every keypress other than arrow keys will erase afterwards.
          priv.pwlen = 0;
          break;
        case '\025':  // Ctrl-U.
          // Delete the entire input line.
          // i3lock: supports Ctrl-U but not Ctrl-A.
          // xscreensaver: supports Ctrl-U and Ctrl-X but not Ctrl-A.
          priv.pwlen = 0;
          break;
        case 0:       // Shouldn't happen.
        case '\033':  // Escape.
          done = 1;
          break;
        case '\r':  // Return.
        case '\n':  // Return.
          *response = malloc(priv.pwlen + 1);
          if (!echo && MLOCK_PAGE(*response, priv.pwlen + 1) < 0) {
            LogErrno("mlock");
            // We continue anyway, as the user being unable to unlock the screen
            // is worse. But let's alert the user of this.
            display_string("Error", "Password has not been stored securely.");
            wait_for_keypress(1);
          }
          if (priv.pwlen != 0) {
            memcpy(*response, priv.pwbuf, priv.pwlen);
          }
          (*response)[priv.pwlen] = 0;
          status = PAM_SUCCESS;
          done = 1;
          break;
        default:
          if (priv.inputbuf >= '\000' && priv.inputbuf <= '\037') {
            // Other control character. We ignore them (and specifically do not
            // update the cursor on them) to "discourage" their use in
            // passwords, as most login screens do not support them anyway.
            break;
          }
          if (priv.pwlen < sizeof(priv.pwbuf)) {
            priv.pwbuf[priv.pwlen] = priv.inputbuf;
            ++priv.pwlen;
            priv.displaymarker =
                (priv.displaymarker - 1 + PARANOID_PASSWORD_MIN_CHANGE +
                 rand() % (PARANOID_PASSWORD_LENGTH -
                           2 * PARANOID_PASSWORD_MIN_CHANGE)) %
                    (PARANOID_PASSWORD_LENGTH - 1) +
                1;
          } else {
            Log("Password entered is too long - bailing out");
            done = 1;
            break;
          }
          break;
      }
    }

    // Handle X11 events that queued up.
    while (!done && XPending(display) && (XNextEvent(display, &priv.ev), 1)) {
      if (IsMonitorChangeEvent(display, priv.ev.type)) {
        num_monitors = GetMonitors(display, window, monitors, MAX_MONITORS);
        XClearWindow(display, window);
      }
    }
  }

  // priv contains password related data, so better clear it.
  memset(&priv, 0, sizeof(priv));

  if (!done) {
    Log("Unreachable code - the loop above must set done");
  }
  return status;
}

/*! \brief Perform a single PAM conversation step.
 *
 * \param msg The PAM message.
 * \param resp The PAM response to store the output in.
 * \return The PAM status (PAM_SUCCESS in case of success, or anything else in
 *   case of error).
 */
int converse_one(const struct pam_message *msg, struct pam_response *resp) {
  resp->resp_retcode = 0;  // Unused but should be set to zero.
  switch (msg->msg_style) {
    case PAM_PROMPT_ECHO_OFF:
      return prompt(msg->msg, &resp->resp, 0);
    case PAM_PROMPT_ECHO_ON:
      return prompt(msg->msg, &resp->resp, 1);
    case PAM_ERROR_MSG:
      display_string("Error", msg->msg);
      wait_for_keypress(1);
      return PAM_SUCCESS;
    case PAM_TEXT_INFO:
      display_string("PAM says", msg->msg);
      wait_for_keypress(1);
      return PAM_SUCCESS;
    default:
      return PAM_CONV_ERR;
  }
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

  if (conv_error) {
    Log("converse() got called again with %d messages (first: %s) after "
        "having failed before - this is very likely a bug in the PAM "
        "module having made the call. Bailing out",
        num_msg, num_msg <= 0 ? "(none)" : msg[0]->msg);
    exit(1);
  }

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
      conv_error = 1;
      return status;
    }
  }

  // We're returning to PAM, so let's show the processing prompt.
  display_string("Processing...", "");

  return PAM_SUCCESS;
}

/*! \brief Perform a single PAM operation with retrying logic.
 */
int call_pam_with_retries(int (*pam_call)(pam_handle_t *, int),
                          pam_handle_t *pam, int flags) {
  int attempt = 0;
  for (;;) {
    conv_error = 0;

    // We're entering PAM, so let's show a processing prompt.
    display_string("Processing...", "");

    int status = pam_call(pam, flags);
    if (conv_error) {  // Timeout or escape.
      return status;
    }
    switch (status) {
      // Never retry these:
      case PAM_ABORT:             // This is fine.
      case PAM_MAXTRIES:          // D'oh.
      case PAM_NEW_AUTHTOK_REQD:  // hunter2 no longer good enough.
      case PAM_SUCCESS:           // Duh.
        return status;
      default:
        // Let's try again then.
        ++attempt;
        if (attempt >= 3) {
          return status;
        }
        break;
    }
  }
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
int authenticate(struct pam_conv *conv, pam_handle_t **pam) {
  const char *service_name =
      GetStringSetting("XSECURELOCK_PAM_SERVICE", PAM_SERVICE_NAME);
  int status = pam_start(service_name, username, conv, pam);
  if (status != PAM_SUCCESS) {
    Log("pam_start: %d",
        status);  // Or can one call pam_strerror on a NULL handle?
    return status;
  }

  status = pam_set_item(*pam, PAM_RHOST, hostname);
  if (status != PAM_SUCCESS) {
    Log("pam_set_item: %s", pam_strerror(*pam, status));
    return status;
  }
  status = pam_set_item(*pam, PAM_RUSER, username);
  if (status != PAM_SUCCESS) {
    Log("pam_set_item: %s", pam_strerror(*pam, status));
    return status;
  }
  const char *display = getenv("DISPLAY");
  status = pam_set_item(*pam, PAM_TTY, display);
  if (status != PAM_SUCCESS) {
    Log("pam_set_item: %s", pam_strerror(*pam, status));
    return status;
  }

  status = call_pam_with_retries(pam_authenticate, *pam, 0);
  if (status != PAM_SUCCESS) {
    if (!conv_error) {
      Log("pam_authenticate: %s", pam_strerror(*pam, status));
    }
    return status;
  }

  int status2 = call_pam_with_retries(pam_acct_mgmt, *pam, 0);
  if (status2 == PAM_NEW_AUTHTOK_REQD) {
    status2 =
        call_pam_with_retries(pam_chauthtok, *pam, PAM_CHANGE_EXPIRED_AUTHTOK);
#ifdef PAM_CHECK_ACCOUNT_TYPE
    if (status2 != PAM_SUCCESS) {
      if (!conv_error) {
        Log("pam_chauthtok: %s", pam_strerror(*pam, status2));
      }
      return status2;
    }
#else
    (void)status2;
#endif
  }

#ifdef PAM_CHECK_ACCOUNT_TYPE
  if (status2 != PAM_SUCCESS) {
    // If this one is true, it must be coming from pam_acct_mgmt, as
    // pam_chauthtok's result already has been checked against PAM_SUCCESS.
    if (!conv_error) {
      Log("pam_acct_mgmt: %s", pam_strerror(*pam, status2));
    }
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

  // This is used by displaymarker only (no security relevance of the RNG).
  srand(time(NULL));

  // Unless disabled, we shift the login prompt randomly around by a few
  // pixels. This should mostly mitigate burn-in effects from the prompt
  // being displayed all the time, e.g. because the user's mouse is "shivering"
  // and thus the auth prompt reappears soon after timeout.
  burnin_mitigation_max_offset =
      GetIntSetting("XSECURELOCK_BURNIN_MITIGATION", 16);
  if (burnin_mitigation_max_offset > 0) {
    x_offset = rand() % (2 * burnin_mitigation_max_offset + 1) -
               burnin_mitigation_max_offset;
    y_offset = rand() % (2 * burnin_mitigation_max_offset + 1) -
               burnin_mitigation_max_offset;
  }

  // If requested, mitigate burn-in even more by moving the auth prompt while
  // displayed. I bet many will find this annoying though.
  burnin_mitigation_max_offset_change =
      GetIntSetting("XSECURELOCK_BURNIN_MITIGATION_DYNAMIC", 0);

  prompt_timeout = GetIntSetting("XSECURELOCK_AUTH_TIMEOUT", 5 * 60);
  show_username = GetIntSetting("XSECURELOCK_SHOW_USERNAME", 1);
  show_hostname = GetIntSetting("XSECURELOCK_SHOW_HOSTNAME", 1);
  paranoid_password = GetIntSetting("XSECURELOCK_PARANOID_PASSWORD", 1);
  have_switch_user_command =
      *GetStringSetting("XSECURELOCK_SWITCH_USER_COMMAND", "");

  if ((display = XOpenDisplay(NULL)) == NULL) {
    Log("Could not connect to $DISPLAY");
    return 1;
  }

#ifdef HAVE_XKB_EXT
  int xkb_opcode, xkb_event_base, xkb_error_base;
  int xkb_major_version = XkbMajorVersion, xkb_minor_version = XkbMinorVersion;
  have_xkb_ext =
      XkbQueryExtension(display, &xkb_opcode, &xkb_event_base, &xkb_error_base,
                        &xkb_major_version, &xkb_minor_version);
#endif

  char hostname_storage[256];
  if (gethostname(hostname_storage, sizeof(hostname_storage))) {
    LogErrno("gethostname");
    return 1;
  }
  hostname_storage[sizeof(hostname_storage) - 1] = 0;
  hostname = hostname_storage;

  struct passwd *pwd = NULL;
  struct passwd pwd_storage;
  char *pwd_buf;
  long pwd_bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (pwd_bufsize < 0) {
    pwd_bufsize = 1 << 20;
  }
  pwd_buf = malloc((size_t)pwd_bufsize);
  if (!pwd_buf) {
    LogErrno("malloc(pwd_bufsize)");
    return 1;
  }
  getpwuid_r(getuid(), &pwd_storage, pwd_buf, (size_t)pwd_bufsize, &pwd);
  if (!pwd) {
    LogErrno("getpwuid_r");
    free(pwd_buf);
    return 1;
  }
  username = pwd->pw_name;

  window = ReadWindowID();
  if (window == None) {
    Log("Invalid/no window ID in XSCREENSAVER_WINDOW");
    free(pwd_buf);
    return 1;
  }

  Background = BlackPixel(display, DefaultScreen(display));
  Foreground = WhitePixel(display, DefaultScreen(display));
  XColor color, dummy;
  XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                   "red", &color, &dummy);
  Warning = color.pixel;

  core_font = NULL;
#ifdef HAVE_XFT_EXT
  xft_font = NULL;
#endif

  const char *font_name = GetStringSetting("XSECURELOCK_FONT", "");

  // First try parsing the font name as an X11 core font. We're trying these
  // first as their font name format is more restrictive (usually starts with a
  // dash), except for when font aliases are used.
  int have_font = 0;
  if (font_name[0] != 0) {
    core_font = XLoadQueryFont(display, font_name);
    have_font = (core_font != NULL);
#ifdef HAVE_XFT_EXT
    if (!have_font) {
      xft_font = XftFontOpenName(display, DefaultScreen(display), font_name);
      have_font = (xft_font != NULL);
    }
#endif
  }
  if (!have_font) {
    if (font_name[0] != 0) {
      Log("Could not load the specified font %s - trying a default font",
          font_name);
    }
#ifdef HAVE_XFT_EXT
    xft_font = XftFontOpenName(display, DefaultScreen(display), "monospace");
    have_font = (xft_font != NULL);
#endif
    if (!have_font) {
      core_font = XLoadQueryFont(display, "fixed");
      have_font = (core_font != NULL);
    }
  }
  if (!have_font) {
    Log("Could not load a mind-bogglingly stupid font");
    exit(1);
  }

  XGCValues gcattrs;
  gcattrs.function = GXcopy;
  gcattrs.foreground = Foreground;
  gcattrs.background = Background;
  if (core_font != NULL) {
    gcattrs.font = core_font->fid;
  }
  gc = XCreateGC(display, window,
                 GCFunction | GCForeground | GCBackground |
                     (core_font != NULL ? GCFont : 0),
                 &gcattrs);
  gcattrs.foreground = Warning;
  gc_warning = XCreateGC(display, window,
                         GCFunction | GCForeground | GCBackground |
                             (core_font != NULL ? GCFont : 0),
                         &gcattrs);

#ifdef HAVE_XFT_EXT
  if (xft_font != NULL) {
    xft_draw = XftDrawCreate(display, window,
                             DefaultVisual(display, DefaultScreen(display)),
                             DefaultColormap(display, DefaultScreen(display)));

    XRenderColor xrcolor;
    xrcolor.red = 65535;
    xrcolor.green = 65535;
    xrcolor.blue = 65535;
    xrcolor.alpha = 65535;
    XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                       DefaultColormap(display, DefaultScreen(display)),
                       &xrcolor, &xft_color);
    xrcolor.red = 65535;
    xrcolor.green = 0;
    xrcolor.blue = 0;
    xrcolor.alpha = 65535;
    XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                       DefaultColormap(display, DefaultScreen(display)),
                       &xrcolor, &xft_color_warning);
  }
#endif

  XSetWindowBackground(display, window, Background);

  SelectMonitorChangeEvents(display, window);
  num_monitors = GetMonitors(display, window, monitors, MAX_MONITORS);

  struct pam_conv conv;
  conv.conv = converse;
  conv.appdata_ptr = NULL;

  pam_handle_t *pam;
  int status = authenticate(&conv, &pam);
  int status2 = pam_end(pam, status);

  // Clear any possible processing message.
  display_string("", "");

  // Done with PAM, so we can free the getpwuid_r buffer now.
  free(pwd_buf);

  if (status != PAM_SUCCESS) {
    // The caller already displayed an error.
    return 1;
  }
  if (status2 != PAM_SUCCESS) {
    Log("pam_end: %s", pam_strerror(pam, status2));
    return 1;
  }

  return 0;
}
