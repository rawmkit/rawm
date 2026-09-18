#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
set_class(Display *dpy, Window win, const char *name, const char *class_name)
{
  XClassHint hint;

  hint.res_name = (char *)name;
  hint.res_class = (char *)class_name;
  XSetClassHint(dpy, win, &hint);
}

static void
wait_mapped(Display *dpy, Window win)
{
  XEvent ev;

  for (;;) {
    XNextEvent(dpy, &ev);
    if (ev.type == MapNotify && ev.xmap.window == win)
      return;
  }
}

static void
set_initial_fullscreen(Display *dpy, Window win)
{
  Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
  Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);

  XChangeProperty(dpy, win, state, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)&fullscreen, 1);
}

static void
remove_fullscreen(Display *dpy, Window root, Window win)
{
  Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
  Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  XEvent ev;

  memset(&ev, 0, sizeof(ev));
  ev.xclient.type = ClientMessage;
  ev.xclient.window = win;
  ev.xclient.message_type = state;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 0; /* _NET_WM_STATE_REMOVE */
  ev.xclient.data.l[1] = (long)fullscreen;
  ev.xclient.data.l[2] = 0;
  ev.xclient.data.l[3] = 1; /* normal application */
  XSendEvent(dpy, root, False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(dpy);
}

static void
sleep_ms(long ms)
{
  struct timespec ts;

  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) < 0)
    ;
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, win;
  XEvent ev;
  int initial_fullscreen = 0;
  int remove_after_ms = 0;
  int i;

  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--initial-fullscreen")) {
      initial_fullscreen = 1;
    } else if (!strcmp(argv[i], "--remove-after") && i + 1 < argc) {
      remove_after_ms = atoi(argv[++i]);
      if (remove_after_ms <= 0) {
        fprintf(stderr, "invalid --remove-after value\n");
        return EXIT_FAILURE;
      }
    } else {
      fprintf(stderr, "usage: %s [--initial-fullscreen] [--remove-after MS]\n",
              argv[0]);
      return EXIT_FAILURE;
    }
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fprintf(stderr, "fullscreen-rule-client: cannot open display\n");
    return EXIT_FAILURE;
  }

  root = DefaultRootWindow(dpy);
  win = XCreateSimpleWindow(dpy, root, 50, 60, 320, 200, 0, 0, 0);
  XStoreName(dpy, win, "rawm-fullscreen-rule-client");
  set_class(dpy, win, "rawm-fullscreen-rule-client", "RawmFullscreenRule");
  if (initial_fullscreen)
    set_initial_fullscreen(dpy, win);

  XSelectInput(dpy, win, StructureNotifyMask);
  XMapWindow(dpy, win);
  XFlush(dpy);
  wait_mapped(dpy, win);

  printf("%lu\n", (unsigned long)win);
  fflush(stdout);

  if (remove_after_ms) {
    sleep_ms(remove_after_ms);
    remove_fullscreen(dpy, root, win);
    puts("removed");
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
