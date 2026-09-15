#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void
pause_for_wm(void)
{
  struct timespec ts = { 0, 120000000L };
  nanosleep(&ts, NULL);
}

static Window
find_bar(Display *dpy, Window root)
{
  Window root_return, parent_return, *children = NULL;
  unsigned int count = 0, i;
  int screen_width = DisplayWidth(dpy, DefaultScreen(dpy));
  Window bar = None;

  if (!XQueryTree(dpy, root, &root_return, &parent_return, &children, &count))
    return None;
  for (i = 0; i < count; ++i) {
    XWindowAttributes attr;

    if (!XGetWindowAttributes(dpy, children[i], &attr))
      continue;
    if (attr.map_state == IsViewable && attr.width == screen_width &&
        attr.height > 0 && attr.height < 128) {
      bar = children[i];
      break;
    }
  }
  if (children)
    XFree(children);
  return bar;
}

static uint64_t
checksum_window(Display *dpy, Window window)
{
  XWindowAttributes attr;
  XImage *image;
  uint64_t hash = UINT64_C(1469598103934665603);
  int x, y;

  if (!XGetWindowAttributes(dpy, window, &attr))
    return 0;
  image = XGetImage(dpy, window, 0, 0, (unsigned int)attr.width,
                    (unsigned int)attr.height, AllPlanes, ZPixmap);
  if (!image)
    return 0;
  for (y = 0; y < attr.height; ++y) {
    for (x = 0; x < attr.width; ++x) {
      hash ^= (uint64_t)XGetPixel(image, x, y);
      hash *= UINT64_C(1099511628211);
    }
  }
  XDestroyImage(image);
  return hash;
}

static void
set_status(Display *dpy, Window root, Atom property, Atom type,
           const unsigned char *text, size_t length)
{
  XChangeProperty(dpy, root, property, type, 8, PropModeReplace, text,
                  (int)length);
  XFlush(dpy);
  pause_for_wm();
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, bar = None;
  Atom status, utf8;
  uint64_t baseline, legacy, active, rejected, malformed, oversized;
  unsigned char invalid_utf8[] = { 0xc0, 0xaf };
  unsigned char long_text[300];
  int disabled = argc == 2 && !strcmp(argv[1], "--disabled");
  int tries;

  if (argc > 2 || (argc == 2 && !disabled)) {
    fprintf(stderr, "usage: %s [--disabled]\n", argv[0]);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("status-client: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }
  root = DefaultRootWindow(dpy);
  status = XInternAtom(dpy, "_RAWM_STATUS_V1", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);

  for (tries = 0; tries < 50 && bar == None; ++tries) {
    bar = find_bar(dpy, root);
    if (bar == None)
      pause_for_wm();
  }
  if (bar == None) {
    fputs("status-client: cannot find rawm bar window\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  baseline = checksum_window(dpy, bar);
  XStoreName(dpy, root, "LEGACY-WM-NAME");
  XFlush(dpy);
  pause_for_wm();
  legacy = checksum_window(dpy, bar);
  if (legacy != baseline) {
    fputs("status-client: WM_NAME still affects status rendering\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8,
             (const unsigned char *)"RAWM-V1-STATUS", 14);
  active = checksum_window(dpy, bar);
  if ((!disabled && active == baseline) || (disabled && active != baseline)) {
    fputs(disabled
            ? "status-client: disabled status protocol changed the bar\n"
            : "status-client: rawm-v1 status did not change the bar\n",
          stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }
  if (disabled) {
    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
  }

  set_status(dpy, root, status, XA_STRING,
             (const unsigned char *)"WRONG-TYPE", 10);
  rejected = checksum_window(dpy, bar);
  if (rejected != baseline) {
    fputs("status-client: non-UTF8_STRING status was accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_status(dpy, root, status, utf8, invalid_utf8, sizeof(invalid_utf8));
  malformed = checksum_window(dpy, bar);
  if (malformed != baseline) {
    fputs("status-client: malformed UTF-8 status was accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  memset(long_text, 'x', sizeof(long_text));
  set_status(dpy, root, status, utf8, long_text, sizeof(long_text));
  oversized = checksum_window(dpy, bar);
  if (oversized != baseline) {
    fputs("status-client: oversized rawm-v1 status was accepted\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
