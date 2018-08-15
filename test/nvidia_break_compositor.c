#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <stdio.h>
#include <unistd.h>

int main() {
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Could not connect to $DISPLAY.\n");
    return 1;
  }
  Window w = XCompositeGetOverlayWindow(display, DefaultRootWindow(display));
  if (w == None) {
    fprintf(stderr, "No composite overlay window received.\n");
    return 1;
  }
  printf("%#llx\n", (unsigned long long)w);
  sleep(1);
  XCompositeReleaseOverlayWindow(display, w);
  return 0;
}
