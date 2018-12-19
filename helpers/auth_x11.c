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

#include <X11/X.h>       // for Success, None, Atom, GCBackground
#include <X11/Xlib.h>    // for DefaultScreen, Screen, True, XCr...
#include <errno.h>       // for ECHILD, EINTR, errno
#include <locale.h>      // for NULL, setlocale, LC_CTYPE
#include <stdlib.h>      // for free, rand, mblen, size_t, exit
#include <string.h>      // for strlen, memcpy, memset, strcspn
#include <sys/select.h>  // for timeval, select, fd_set, FD_SET
#include <sys/wait.h>    // for waitpid
#include <time.h>        // for time, time_t
#include <unistd.h>      // for close, dup2, pipe, dup, execl, fork

#ifdef HAVE_XFT_EXT
#include <X11/Xft/Xft.h>             // for XftColorAllocValue, XftFontOpenName
#include <X11/extensions/Xrender.h>  // for XRenderColor, XGlyphInfo
#include <fontconfig/fontconfig.h>   // for FcChar8
#endif

#ifdef HAVE_XKB_EXT
#include <X11/XKBlib.h>             // for XkbFreeClientMap, XkbGetIndicato...
#include <X11/extensions/XKB.h>     // for XkbUseCoreKbd, XkbGroupNamesMask
#include <X11/extensions/XKBstr.h>  // for XkbStateRec, _XkbDesc, _XkbNamesRec
#endif

#include "../env_info.h"          // for GetHostName, GetUserName
#include "../env_settings.h"      // for GetIntSetting, GetStringSetting
#include "../logging.h"           // for Log, LogErrno
#include "../mlock_page.h"        // for MLOCK_PAGE
#include "../wait_pgrp.h"         // for WaitPgrp
#include "../xscreensaver_api.h"  // for ReadWindowID
#include "authproto.h"            // for WritePacket, ReadPacket, PTYPE_R...
#include "monitors.h"             // for Monitor, GetMonitors, IsMonitorC...

//! The authproto helper to use.
const char *authproto_executable;

//! The blinking interval in microseconds.
#define BLINK_INTERVAL (250 * 1000)

//! The maximum time to wait at a prompt for user input in seconds.
int prompt_timeout;

//! Length of the "paranoid password display".
#define PARANOID_PASSWORD_LENGTH 32

//! Minimum distance the cursor shall move on keypress.
#define PARANOID_PASSWORD_MIN_CHANGE 4

//! Border to clear around text (mainly a workaround for bad TextWidth results).
#define TEXT_BORDER 0

//! Draw border rectangle (mainly for debugging).
#undef DRAW_BORDER

//! Clear the outside area too (may work around driver issues, costs CPU).
#undef CLEAR_OUTSIDE

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

//! If set, data and time will be shown.
int show_datetime;

//! The date format to display.
const char *datetime_format = "%c";

//! The local hostname.
char hostname[256];

//! The username to authenticate as.
char username[256];

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

//! Whether to play sounds during authentication.
static int auth_sounds = 0;

#define MAX_MONITORS 16
static int num_monitors;
static Monitor monitors[MAX_MONITORS];

int have_xkb_ext;

enum Sound { SOUND_PROMPT, SOUND_INFO, SOUND_ERROR, SOUND_SUCCESS };

#define NOTE_E3 165
#define NOTE_BB3 233
#define NOTE_B4 494
#define NOTE_E5 659
#define NOTE_B5 988
#define NOTE_E6 1319
int sounds[][2] = {
    /* SOUND_PROMPT=  */ {NOTE_B4, NOTE_E5},
    /* SOUND_INFO=    */ {NOTE_E5, NOTE_E5},
    /* SOUND_ERROR=   */ {NOTE_BB3, NOTE_E3},
    /* SOUND_SUCCESS= */ {NOTE_B5, NOTE_E6},
};
#define SOUND_SLEEP_MS 125
#define SOUND_TONE_MS 100

/*! \brief Play a sound sequence.
 */
void PlaySound(Display *display, enum Sound snd) {
  XKeyboardState state;
  XKeyboardControl control;
  struct timespec sleeptime;

  if (!auth_sounds) {
    return;
  }

  XGetKeyboardControl(display, &state);

  // bell_percent changes note length on Linux, so let's use the middle value
  // to get a 1:1 mapping.
  control.bell_percent = 50;
  control.bell_duration = SOUND_TONE_MS;
  control.bell_pitch = sounds[snd][0];
  XChangeKeyboardControl(display, KBBellPercent | KBBellDuration | KBBellPitch,
                         &control);
  XBell(display, 0);

  XFlush(display);

  sleeptime.tv_sec = SOUND_SLEEP_MS / 1000;
  sleeptime.tv_nsec = 1000000L * (SOUND_SLEEP_MS % 1000);
  nanosleep(&sleeptime, NULL);

  control.bell_pitch = sounds[snd][1];
  XChangeKeyboardControl(display, KBBellPitch, &control);
  XBell(display, 0);

  control.bell_percent = state.bell_percent;
  control.bell_duration = state.bell_duration;
  control.bell_pitch = state.bell_pitch;
  XChangeKeyboardControl(display, KBBellPercent | KBBellDuration | KBBellPitch,
                         &control);

  XFlush(display);
}

/*! \brief Switch to the next keyboard layout.
 */
void SwitchKeyboardLayout(void) {
#ifdef HAVE_XKB_EXT
  if (!have_xkb_ext) {
    return;
  }

  XkbDescPtr xkb;
  xkb = XkbGetMap(display, 0, XkbUseCoreKbd);
  if (XkbGetControls(display, XkbGroupsWrapMask, xkb) != Success) {
    Log("XkbGetControls failed");
    XkbFreeKeyboard(xkb, 0, True);
    return;
  }
  if (xkb->ctrls->num_groups < 1) {
    Log("XkbGetControls returned less than 1 group");
    XkbFreeKeyboard(xkb, 0, True);
    return;
  }
  XkbStateRec state;
  if (XkbGetState(display, XkbUseCoreKbd, &state) != Success) {
    Log("XkbGetState failed");
    XkbFreeKeyboard(xkb, 0, True);
    return;
  }

  XkbLockGroup(display, XkbUseCoreKbd,
               (state.group + 1) % xkb->ctrls->num_groups);

  XkbFreeKeyboard(xkb, 0, True);
#endif
}

/*! \brief Check which modifiers are active.
 *
 * \param warning Will be set to 1 if something's "bad" with the keyboard
 *     layout (e.g. Caps Lock).
 * \param have_multiple_layouts Will be set to 1 if more than one keyboard
 *     layout is available for switching.
 *
 * \return The current modifier mask as a string.
 */
const char *GetIndicators(int *warning, int *have_multiple_layouts) {
#ifdef HAVE_XKB_EXT
  static char buf[128];
  char *p;

  if (!have_xkb_ext) {
    return "";
  }

  XkbDescPtr xkb;
  xkb = XkbGetMap(display, 0, XkbUseCoreKbd);
  if (XkbGetControls(display, XkbGroupsWrapMask, xkb) != Success) {
    Log("XkbGetControls failed");
    XkbFreeKeyboard(xkb, 0, True);
    return "";
  }
  if (XkbGetNames(
          display,
          XkbIndicatorNamesMask | XkbGroupNamesMask | XkbSymbolsNameMask,
          xkb) != Success) {
    Log("XkbGetNames failed");
    XkbFreeKeyboard(xkb, 0, True);
    return "";
  }
  XkbStateRec state;
  if (XkbGetState(display, XkbUseCoreKbd, &state) != Success) {
    Log("XkbGetState failed");
    XkbFreeKeyboard(xkb, 0, True);
    return "";
  }
  unsigned int istate;
  if (XkbGetIndicatorState(display, XkbUseCoreKbd, &istate) != Success) {
    Log("XkbGetIndicatorState failed");
    XkbFreeKeyboard(xkb, 0, True);
    return "";
  }

  // Detect Caps Lock.
  // Note: in very pathological cases the modifier might be set without an
  // XkbIndicator for it; then we show the line in red without telling the user
  // why. Such a situation has not been observd yet though.
  if (state.mods & LockMask) {
    *warning = 1;
  }

  // Provide info about multiple layouts.
  if (xkb->ctrls->num_groups > 1) {
    *have_multiple_layouts = 1;
  }

  p = buf;

  const char *word = "Keyboard: ";
  size_t n = strlen(word);
  if (n >= sizeof(buf) - (p - buf)) {
    Log("Not enough space to store intro '%s'", word);
    XkbFreeKeyboard(xkb, 0, True);
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
    char *layout = XGetAtomName(display, layouta);
    n = strlen(layout);
    if (n >= sizeof(buf) - (p - buf)) {
      Log("Not enough space to store layout name '%s'", layout);
      XFree(layout);
      XkbFreeKeyboard(xkb, 0, True);
      return "";
    }
    memcpy(p, layout, n);
    XFree(layout);
    p += n;
    have_output = 1;
  }

  int i;
  for (i = 0; i < XkbNumIndicators; i++) {
    if (!(istate & (1U << i))) {
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
    char *name = XGetAtomName(display, namea);
    size_t n = strlen(name);
    if (n >= sizeof(buf) - (p - buf)) {
      Log("Not enough space to store modifier name '%s'", name);
      XFree(name);
      break;
    }
    memcpy(p, name, n);
    XFree(name);
    p += n;
    have_output = 1;
  }
  *p = 0;
  XkbFreeKeyboard(xkb, 0, True);
  return have_output ? buf : "";
#else
  *warning = *warning;                              // Shut up clang-analyzer.
  *have_multiple_layouts = *have_multiple_layouts;  // Shut up clang-analyzer.
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

  if (show_username || show_hostname) {
    StrAppend(&output, &output_size, " - ", 3);
  }
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
  int have_multiple_layouts = 0;
  const char *indicators =
      GetIndicators(&indicators_warning, &have_multiple_layouts);
  int len_indicators = strlen(indicators);
  int tw_indicators = TextWidth(indicators, len_indicators);

  const char *switch_layout =
      have_multiple_layouts ? "Press Ctrl-Tab to switch keyboard layout" : "";
  int len_switch_layout = strlen(switch_layout);
  int tw_switch_layout = TextWidth(switch_layout, len_switch_layout);

  const char *switch_user = have_switch_user_command
                                ? "Press Ctrl-Alt-O or Win-O to switch user"
                                : "";
  int len_switch_user = strlen(switch_user);
  int tw_switch_user = TextWidth(switch_user, len_switch_user);

  char datetime[80] = "";
  if (show_datetime) {
    time_t rawtime;
    struct tm timeinfo_buf;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime_r(&rawtime, &timeinfo_buf);
    if (timeinfo == NULL ||
        strftime(datetime, sizeof(datetime), datetime_format, timeinfo) == 0) {
      // The datetime buffer was too small to fit the time format, and in this
      // case the buffer contents are undefined. Let's just make it a valid
      // empty string then so all else will go well.
      datetime[0] = 0;
    }
  }

  int len_datetime = strlen(datetime);
  int tw_datetime = TextWidth(datetime, len_datetime);

  // Compute the region we will be using, relative to cx and cy.
  int box_w = tw_full_title;
  if (box_w < tw_str) {
    box_w = tw_str;
  }
  if (box_w < tw_indicators) {
    box_w = tw_indicators;
  }
  if (box_w < tw_switch_layout) {
    box_w = tw_switch_layout;
  }
  if (box_w < tw_switch_user) {
    box_w = tw_switch_user;
  }
  int border = TEXT_BORDER + burnin_mitigation_max_offset_change;
  int box_h = (4 + have_multiple_layouts + have_switch_user_command +
               show_datetime * 2) *
              th;
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
    int y = cy + to - box_h / 2;

    // Clip all following output to the bounds of this monitor.
    XRectangle rect;
    rect.x = monitors[i].x;
    rect.y = monitors[i].y;
    rect.width = monitors[i].width;
    rect.height = monitors[i].height;
    XSetClipRectangles(display, gc, 0, 0, &rect, 1, YXBanded);

    // Clear the region last written to.
    XClearArea(display, window,               //
               cx + region_x, cy + region_y,  //
               region_w, region_h,            //
               False);

#ifdef DRAW_BORDER
    XDrawRectangle(display, window, gc,             //
                   cx - box_w / 2, cy - box_h / 2,  //
                   box_w - 1, box_h - 1);
    XDrawRectangle(display, window, gc,           //
                   cx + region_x, cy + region_y,  //
                   region_w - 1, region_h - 1);
#endif

    if (show_datetime) {
      DrawString(cx - tw_datetime / 2, y, 0, datetime, len_datetime);
      y += th * 2;
    }

    DrawString(cx - tw_full_title / 2, y, 0, full_title, len_full_title);
    y += th * 2;

    DrawString(cx - tw_str / 2, y, 0, str, len_str);
    y += th;

    DrawString(cx - tw_indicators / 2, y, indicators_warning, indicators,
               len_indicators);
    y += th;

    if (have_multiple_layouts) {
      DrawString(cx - tw_switch_layout / 2, y, 0, switch_layout,
                 len_switch_layout);
      y += th;
    }

    if (have_switch_user_command) {
      DrawString(cx - tw_switch_user / 2, y, 0, switch_user, len_switch_user);
      // y += th;
    }

#ifdef CLEAR_OUTSIDE
    // Clear everything else last. This minimizes flicker.
    if (cy + region_y - rect.y > 0) {
      XClearArea(display, window,                     //
                 rect.x, rect.y,                      //
                 rect.width, cy + region_y - rect.y,  //
                 False);
    }
    if (cx + region_x - rect.x > 0) {
      XClearArea(display, window,                   //
                 rect.x, cy + region_y,             //
                 cx + region_x - rect.x, region_h,  //
                 False);
    }
    if (rect.x + rect.width - cx - region_x - region_w > 0) {
      XClearArea(display, window,                                           //
                 cx + region_x + region_w, cy + region_y,                   //
                 rect.x + rect.width - cx - region_x - region_w, region_h,  //
                 False);
    }
    if (rect.y + rect.height - cy - region_y - region_h > 0) {
      XClearArea(display, window,                                  //
                 rect.x, cy + region_y + region_h, rect.width,     //
                 rect.y + rect.height - cy - region_y - region_h,  //
                 False);
    }
#endif

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
 * \return 1 if successful, anything else otherwise.
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
  int blink_state = 0;

  if (!echo && MLOCK_PAGE(&priv, sizeof(priv)) < 0) {
    LogErrno("mlock");
    // We continue anyway, as the user being unable to unlock the screen is
    // worse. But let's alert the user.
    display_string("Error", "Password will not be stored securely.");
    wait_for_keypress(1);
  }

  priv.pwlen = 0;
  priv.displaymarker = rand() % PARANOID_PASSWORD_LENGTH;

  time_t deadline = time(NULL) + prompt_timeout;

  // Unfortunately we may have to break out of multiple loops at once here but
  // still do common cleanup work. So we have to track the return value in a
  // variable.
  int status = 0;
  int done = 0;
  int played_sound = 0;

  while (!done) {
    if (echo) {
      if (priv.pwlen != 0) {
        memcpy(priv.displaybuf, priv.pwbuf, priv.pwlen);
      }
      priv.displaylen = priv.pwlen;
      // Note that priv.pwlen <= sizeof(priv.pwbuf) and thus
      // priv.pwlen + 2 <= sizeof(priv.displaybuf).
      priv.displaybuf[priv.displaylen] = blink_state ? ' ' : *cursor;
      priv.displaybuf[priv.displaylen + 1] = '\0';
    } else if (paranoid_password) {
      priv.displaylen = PARANOID_PASSWORD_LENGTH;
      memset(priv.displaybuf, '_', priv.displaylen);
      priv.displaybuf[priv.pwlen ? priv.displaymarker : 0] =
          blink_state ? '|' : '-';
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
      priv.displaybuf[priv.displaylen] = blink_state ? ' ' : *cursor;
      priv.displaybuf[priv.displaylen + 1] = '\0';
    }
    display_string(msg, priv.displaybuf);

    if (!played_sound) {
      PlaySound(display, SOUND_PROMPT);
      played_sound = 1;
    }

    // Blink the cursor.
    blink_state = !blink_state;

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
      time_t now = time(NULL);
      if (now > deadline) {
        Log("AUTH_TIMEOUT hit");
        done = 1;
        break;
      }
      if (deadline > now + prompt_timeout) {
        // Guard against the system clock stepping back.
        deadline = now + prompt_timeout;
      }
      if (nfds == 0) {
        // Blink...
        break;
      }

      // From now on, only do nonblocking selects so we update the screen ASAP.
      timeout.tv_usec = 0;

      // Force the cursor to be in visible state while typing.
      blink_state = 0;

      // Reset the prompt timeout.
      deadline = now + prompt_timeout;

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
        case '\023':  // Ctrl-S.
          SwitchKeyboardLayout();
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
          status = 1;
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
        XWindowAttributes xwa;
        XGetWindowAttributes(display, DefaultRootWindow(display), &xwa);
        XMoveResizeWindow(display, window, 0, 0, xwa.width, xwa.height);
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

/*! \brief Perform authentication using a helper proxy.
 *
 * \return The authentication status (0 for OK, 1 otherwise).
 */
int authenticate() {
  int requestfd[2], responsefd[2];
  if (pipe(requestfd)) {
    LogErrno("pipe");
    return 1;
  }
  if (pipe(responsefd)) {
    LogErrno("pipe");
    return 1;
  }

  // Use authproto_pam.
  pid_t childpid = fork();
  if (childpid == -1) {
    LogErrno("fork");
    return 1;
  }

  if (childpid == 0) {
    // Child process. Just run authproto_pam.
    // But first, move requestfd[1] to 1 and responsefd[0] to 0.
    close(requestfd[0]);
    close(responsefd[1]);

    if (requestfd[1] == 0) {
      // Tricky case. We don't _expect_ this to happen - after all,
      // initially our own fd 0 should be bound to xsecurelock's main
      // program - but nevertheless let's handle it.
      // At least this implies that no other fd is 0.
      int requestfd1 = dup(requestfd[1]);
      if (requestfd1 == -1) {
        LogErrno("dup");
        _exit(EXIT_FAILURE);
      }
      close(requestfd[1]);
      if (dup2(responsefd[0], 0) == -1) {
        LogErrno("dup2");
        _exit(EXIT_FAILURE);
      }
      close(responsefd[0]);
      if (requestfd1 != 1) {
        if (dup2(requestfd1, 1) == -1) {
          LogErrno("dup2");
          _exit(EXIT_FAILURE);
        }
        close(requestfd1);
      }
    } else {
      if (responsefd[0] != 0) {
        if (dup2(responsefd[0], 0) == -1) {
          LogErrno("dup2");
          _exit(EXIT_FAILURE);
        }
        close(responsefd[0]);
      }
      if (requestfd[1] != 1) {
        if (dup2(requestfd[1], 1) == -1) {
          LogErrno("dup2");
          _exit(EXIT_FAILURE);
        }
        close(requestfd[1]);
      }
    }

    execl(authproto_executable, authproto_executable, NULL);
    LogErrno("execl");
    sleep(2);  // Reduce log spam or other effects from failed execl.
    _exit(EXIT_FAILURE);
  }

  // Otherwise, we're in the parent process.
  close(requestfd[1]);
  close(responsefd[0]);
  for (;;) {
    char *message;
    char *response;
    char type = ReadPacket(requestfd[0], &message, 1);
    switch (type) {
      case PTYPE_INFO_MESSAGE:
        display_string("PAM says", message);
        free(message);
        PlaySound(display, SOUND_INFO);
        wait_for_keypress(1);
        break;
      case PTYPE_ERROR_MESSAGE:
        display_string("Error", message);
        free(message);
        PlaySound(display, SOUND_ERROR);
        wait_for_keypress(1);
        break;
      case PTYPE_PROMPT_LIKE_USERNAME:
        if (prompt(message, &response, 1)) {
          WritePacket(responsefd[1], PTYPE_RESPONSE_LIKE_USERNAME, response);
          free(response);
        } else {
          WritePacket(responsefd[1], PTYPE_RESPONSE_CANCELLED, "");
        }
        free(message);
        display_string("Processing...", "");
        break;
      case PTYPE_PROMPT_LIKE_PASSWORD:
        if (prompt(message, &response, 0)) {
          WritePacket(responsefd[1], PTYPE_RESPONSE_LIKE_PASSWORD, response);
          free(response);
        } else {
          WritePacket(responsefd[1], PTYPE_RESPONSE_CANCELLED, "");
        }
        free(message);
        display_string("Processing...", "");
        break;
      case 0:
        goto done;
      default:
        Log("Unknown message type %02x", (int)type);
        free(message);
        goto done;
    }
  }
done:
  close(requestfd[0]);
  close(responsefd[1]);
  int status;
  if (!WaitPgrp("authproto", childpid, 1, 0, &status)) {
    Log("WaitPgrp returned false but we were blocking");
    abort();
  }
  if (status == 0) {
    PlaySound(display, SOUND_SUCCESS);
  }
  return status != 0;
}

/*! \brief The main program.
 *
 * Usage: XSCREENSAVER_WINDOW=window_id ./auth_x11; status=$?
 *
 * \return 0 if authentication successful, anything else otherwise.
 */
int main() {
  setlocale(LC_CTYPE, "");
  setlocale(LC_TIME, "");

  // This is used by displaymarker only (no security relevance of the RNG).
  srand(time(NULL));

  authproto_executable = GetExecutablePathSetting("XSECURELOCK_AUTHPROTO",
                                                  AUTHPROTO_EXECUTABLE, 0);

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
  show_datetime = GetIntSetting("XSECURELOCK_SHOW_DATETIME", 0);
  datetime_format = GetStringSetting("XSECURELOCK_DATETIME_FORMAT", "%c");
  have_switch_user_command =
      !!*GetStringSetting("XSECURELOCK_SWITCH_USER_COMMAND", "");
  auth_sounds = GetIntSetting("XSECURELOCK_AUTH_SOUNDS", 0);

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

  if (!GetHostName(hostname, sizeof(hostname))) {
    return 1;
  }
  if (!GetUserName(username, sizeof(username))) {
    return 1;
  }

  window = ReadWindowID();
  if (window == None) {
    Log("Invalid/no window ID in XSCREENSAVER_WINDOW");
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
    return 1;
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
  XMapRaised(display, window);

  SelectMonitorChangeEvents(display, window);
  int w = DisplayWidth(display, DefaultScreen(display));
  int h = DisplayHeight(display, DefaultScreen(display));
  XMoveResizeWindow(display, window, 0, 0, w, h);
  num_monitors = GetMonitors(display, window, monitors, MAX_MONITORS);

  int status = authenticate();

  // Clear any possible processing message.
  display_string("", "");

#ifdef HAVE_XFT_EXT
  if (xft_font != NULL) {
    XftColorFree(display, DefaultVisual(display, DefaultScreen(display)),
                 DefaultColormap(display, DefaultScreen(display)),
                 &xft_color_warning);
    XftColorFree(display, DefaultVisual(display, DefaultScreen(display)),
                 DefaultColormap(display, DefaultScreen(display)),
                 &xft_color);
    XftDrawDestroy(xft_draw);
    XftFontClose(display, xft_font);
  }
#endif

  return status;
}
