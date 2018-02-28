#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <stdio.h>
#include <stdlib.h>

void DumpWindow(const char *title, Window w) {
  printf("# %s window = %#llx\n", title, (unsigned long long)w);
  if (w == None) {
    return;
  }

  // Lots of hackery to dump all we know about this window.
  char buf[128];
  buf[sizeof(buf) - 1] = 0;
  snprintf(buf, sizeof(buf) - 1, "xwininfo -all -id %#llx",
           (unsigned long long)w);
  printf("$ %s\n", buf);
  fflush(stdout);
  system(buf);
  snprintf(buf, sizeof(buf) - 1, "xprop -id %#llx", (unsigned long long)w);
  printf("$ %s\n", buf);
  fflush(stdout);
  system(buf);
  snprintf(
      buf, sizeof(buf) - 1,
      "ps \"$(xprop -id %#llx _NET_WM_PID | cut -d ' ' -f 3)\" 2>/dev/null",
      (unsigned long long)w);
  printf("$ %s\n", buf);
  fflush(stdout);
  system(buf);
  if (((unsigned long long)w) >> 16 != 0) {
    // Lists all other windows from the same X11 client.
    snprintf(
        buf, sizeof(buf) - 1,
        "xwininfo -root -tree | grep '%#llx[0-9a-f][0-9a-f][0-9a-f][0-9a-f] '",
        ((unsigned long long)w) >> 16);
    printf("$ %s\n", buf);
    fflush(stdout);
    system(buf);
  }
}

int main() {
  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fprintf(stderr, "Could not connect to $DISPLAY.\n");
    return 1;
  }

  char buf[64];                                 // Flawfinder: ignore
  snprintf(buf, sizeof(buf), "_NET_WM_CM_S%d",  // Flawfinder: ignore
           (int)DefaultScreen(display));
  buf[sizeof(buf) - 1] = 0;
  Atom atom = XInternAtom(display, buf, False);
  DumpWindow(buf, XGetSelectionOwner(display, atom));

  Window cow = XCompositeGetOverlayWindow(display, DefaultRootWindow(display));
  // Instantly release to prevent black screen with compton --backend glx.
  XCompositeReleaseOverlayWindow(display, cow);
  DumpWindow("Composite overlay", cow);

  return 0;
}
