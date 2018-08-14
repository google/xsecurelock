#include "unmap_all.h"

#include <X11/X.h>            // for Window, GrabModeAsync, Curren...
#include <X11/Xatom.h>        // for XA_CARDINAL, XA_ATOM
#include <X11/Xlib.h>         // for XEvent, False, XSelectInput
#include <X11/Xmu/WinUtil.h>  // For XmuClientWindow
#include <string.h>

int InitUnmapAllWindowsState(UnmapAllWindowsState* state, Display* display,
                             Window root_window, Window* ignored_windows,
                             unsigned int n_ignored_windows,
                             const char* my_res_class, const char* my_res_name,
                             int include_frame) {
  int should_proceed = 1;
  state->display = display;
  state->root_window = root_window;
  state->windows = NULL;
  state->n_windows = 0;

  XClassHint cls;
  Window unused_root_return, unused_parent_return;
  XQueryTree(state->display, state->root_window, &unused_root_return,
             &unused_parent_return, &state->windows, &state->n_windows);
  for (unsigned int i = 0; i < state->n_windows; ++i) {
    XWindowAttributes xwa;
    XGetWindowAttributes(display, state->windows[i], &xwa);
    // Not mapped -> nothing to do.
    if (xwa.map_state == IsUnmapped) {
      state->windows[i] = None;
      continue;
    }
    // Go down to the next WM_STATE window if available, as unmapping window
    // frames may confuse WMs.
    if (!include_frame) {
      state->windows[i] = XmuClientWindow(display, state->windows[i]);
    }
    // If any window we'd be unmapping is in the ignore list, skip it.
    for (unsigned int j = 0; j < n_ignored_windows; ++j) {
      if (state->windows[i] == ignored_windows[j]) {
        state->windows[i] = None;
      }
    }
    if (state->windows[i] == None) {
      continue;
    }
    if (XGetClassHint(state->display, state->windows[i], &cls)) {
      // If any window has my window class, we better not proceed with
      // unmapping as doing so could accidentally unlock the screen or
      // otherwise cause more damage than good.
      if ((my_res_class || my_res_name) &&
          !(my_res_class && strcmp(my_res_class, cls.res_class)) &&
          !(my_res_name && strcmp(my_res_name, cls.res_name))) {
        state->windows[i] = None;
        should_proceed = 0;
      }
      // HACK: Bspwm creates some subwindows of the root window that we
      // absolutely shouldn't ever unmap, as remapping them confuses Bspwm.
      if (!strcmp(cls.res_class, "Bspwm")) {
        state->windows[i] = None;
      }
    }
    XFree(cls.res_class);
    cls.res_class = NULL;
    XFree(cls.res_name);
    cls.res_name = NULL;
  }
  return should_proceed;
}

void UnmapAllWindows(UnmapAllWindowsState* state) {
  for (unsigned int i = 0; i < state->n_windows; ++i) {
    if (state->windows[i] != None) {
      XUnmapWindow(state->display, state->windows[i]);
    }
  }
}

void RemapAllWindows(UnmapAllWindowsState* state) {
  for (unsigned int i = 0; i < state->n_windows; ++i) {
    if (state->windows[i] != None) {
      XMapWindow(state->display, state->windows[i]);
    }
  }
}

void ClearUnmapAllWindowsState(UnmapAllWindowsState* state) {
  state->display = NULL;
  state->root_window = None;
  XFree(state->windows);
  state->windows = NULL;
  state->n_windows = 0;
}
