#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>

int main() {
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Could not connect to $DISPLAY.\n");
    return 1;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "_NET_WM_CM_S%d", (int)DefaultScreen(display));
  Atom atom = XInternAtom(display, buf, False);
  Window w = XGetSelectionOwner(display, atom);
  if (w == None) {
    fprintf(stderr, "No compositor detected.\n");
    return 1;
  }
  printf("%#llx\n", (unsigned long long)w);
  return 0;
}
