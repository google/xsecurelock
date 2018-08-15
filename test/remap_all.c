#include "../unmap_all.h"

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Could not connect to $DISPLAY.\n");
    return 1;
  }
  UnmapAllWindowsState state;
  XGrabServer(display);
  InitUnmapAllWindowsState(&state, display, DefaultRootWindow(display), NULL, 0,
                           NULL, NULL, argc > 1 ? atoi(argv[1]) : 0);
  UnmapAllWindows(&state);
  RemapAllWindows(&state);
  XUngrabServer(display);
  ClearUnmapAllWindowsState(&state);
  XCloseDisplay(display);
  return 0;
}
