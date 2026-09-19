#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
sleep_ms(long ms)
{
  struct timespec ts;

  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
    ;
}

static long
number(const char *s, const char *what)
{
  char *end = NULL;
  long n;

  errno = 0;
  n = strtol(s, &end, 10);
  if (errno || !end || *end) {
    fprintf(stderr, "geometry-client: invalid %s: %s\n", what, s);
    exit(EXIT_FAILURE);
  }
  return n;
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, win;
  XClassHint class_hint;
  XSizeHints hints;
  XEvent ev;
  const char *name = "rawm-geometry-client";
  int x = 40, y = 60, w = 323, h = 217;
  int incw = 1, inch = 1;
  long resize_after = 0;
  int rx = 0, ry = 0, rw = 0, rh = 0;
  int i;

  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--name") && i + 1 < argc) {
      name = argv[++i];
    } else if (!strcmp(argv[i], "--geometry") && i + 4 < argc) {
      x = (int)number(argv[++i], "x");
      y = (int)number(argv[++i], "y");
      w = (int)number(argv[++i], "width");
      h = (int)number(argv[++i], "height");
    } else if (!strcmp(argv[i], "--increment") && i + 2 < argc) {
      incw = (int)number(argv[++i], "width increment");
      inch = (int)number(argv[++i], "height increment");
    } else if (!strcmp(argv[i], "--resize-after") && i + 5 < argc) {
      resize_after = number(argv[++i], "resize delay");
      rx = (int)number(argv[++i], "resize x");
      ry = (int)number(argv[++i], "resize y");
      rw = (int)number(argv[++i], "resize width");
      rh = (int)number(argv[++i], "resize height");
    } else {
      fprintf(stderr,
              "usage: %s [--name NAME] [--geometry X Y W H] "
              "[--increment W H] [--resize-after MS X Y W H]\n",
              argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (w < 1 || h < 1 || incw < 1 || inch < 1 || resize_after < 0 ||
      (resize_after && (rw < 1 || rh < 1))) {
    fputs("geometry-client: invalid geometry\n", stderr);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("geometry-client: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }

  root = DefaultRootWindow(dpy);
  win = XCreateSimpleWindow(dpy, root, x, y, (unsigned int)w, (unsigned int)h,
                            0, 0, 0);
  XStoreName(dpy, win, name);
  class_hint.res_name = (char *)name;
  class_hint.res_class = "RawmGeometry";
  XSetClassHint(dpy, win, &class_hint);

  memset(&hints, 0, sizeof(hints));
  hints.flags = PBaseSize | PMinSize | PResizeInc;
  hints.base_width = 0;
  hints.base_height = 0;
  hints.min_width = 1;
  hints.min_height = 1;
  hints.width_inc = incw;
  hints.height_inc = inch;
  XSetWMNormalHints(dpy, win, &hints);

  XSelectInput(dpy, win, StructureNotifyMask);
  XMapWindow(dpy, win);
  XFlush(dpy);

  for (;;) {
    XNextEvent(dpy, &ev);
    if (ev.type == MapNotify && ev.xmap.window == win)
      break;
  }

  printf("%lu\n", (unsigned long)win);
  fflush(stdout);

  if (resize_after) {
    sleep_ms(resize_after);
    XMoveResizeWindow(dpy, win, rx, ry, (unsigned int)rw, (unsigned int)rh);
    XFlush(dpy);
    puts("resized");
    fflush(stdout);
  }

  for (;;) {
    XNextEvent(dpy, &ev);
    if (ev.type == DestroyNotify && ev.xdestroywindow.window == win)
      break;
  }

  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
