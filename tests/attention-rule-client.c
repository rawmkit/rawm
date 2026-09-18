#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
pause_for_wm(void)
{
  struct timespec ts = { 0, 140000000L };
  nanosleep(&ts, NULL);
}

static void
set_class(Display *dpy, Window win, const char *name, const char *class_name)
{
  XClassHint hint;

  hint.res_name = (char *)name;
  hint.res_class = (char *)class_name;
  XSetClassHint(dpy, win, &hint);
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
set_urgency(Display *dpy, Window win, int urgent)
{
  XWMHints *hints = XGetWMHints(dpy, win);

  if (!hints)
    hints = XAllocWMHints();
  if (!hints) {
    fputs("attention-rule-client: cannot allocate WM hints\n", stderr);
    exit(EXIT_FAILURE);
  }
  hints->flags |= InputHint;
  hints->input = True;
  if (urgent)
    hints->flags |= XUrgencyHint;
  else
    hints->flags &= ~XUrgencyHint;
  XSetWMHints(dpy, win, hints);
  XFree(hints);
  XFlush(dpy);
}

static int
has_urgency(Display *dpy, Window win)
{
  XWMHints *hints = XGetWMHints(dpy, win);
  int urgent = hints && (hints->flags & XUrgencyHint);

  if (hints)
    XFree(hints);
  return urgent;
}

static void
activate(Display *dpy, Window root, Window win, Atom active)
{
  XEvent ev;

  memset(&ev, 0, sizeof(ev));
  ev.xclient.type = ClientMessage;
  ev.xclient.window = win;
  ev.xclient.message_type = active;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 2;
  ev.xclient.data.l[1] = CurrentTime;
  XSendEvent(dpy, root, False,
             SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(dpy);
  pause_for_wm();
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, bar = None, anchor, target;
  Atom active;
  uint64_t initial, after_hints, after_focus;
  int app_urgent = 0;
  int expect_wm = 0;
  int expect_none = 0;
  int tries;

  if (argc != 2) {
    fprintf(stderr, "usage: %s wm|none|app\n", argv[0]);
    return EXIT_FAILURE;
  }
  if (!strcmp(argv[1], "wm"))
    expect_wm = 1;
  else if (!strcmp(argv[1], "none"))
    expect_none = 1;
  else if (!strcmp(argv[1], "app"))
    app_urgent = 1;
  else {
    fprintf(stderr, "usage: %s wm|none|app\n", argv[0]);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("attention-rule-client: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }
  root = DefaultRootWindow(dpy);
  active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

  anchor = XCreateSimpleWindow(dpy, root, 40, 40, 260, 180, 0, 0, 0);
  XStoreName(dpy, anchor, "rawm-attention-anchor");
  set_class(dpy, anchor, "rawm-attention-anchor", "RawmAttentionAnchor");
  XMapWindow(dpy, anchor);
  XFlush(dpy);
  pause_for_wm();

  for (tries = 0; tries < 50 && bar == None; ++tries) {
    bar = find_bar(dpy, root);
    if (bar == None)
      pause_for_wm();
  }
  if (bar == None) {
    fputs("attention-rule-client: cannot find rawm bar window\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  target = XCreateSimpleWindow(dpy, root, 80, 80, 280, 200, 0, 0, 0);
  XStoreName(dpy, target, "rawm-attention-target");
  set_class(dpy, target, "rawm-attention-target", "RawmAttentionTarget");
  if (app_urgent)
    set_urgency(dpy, target, 1);
  XMapWindow(dpy, target);
  XFlush(dpy);
  pause_for_wm();

  initial = checksum_window(dpy, bar);
  if (!initial) {
    fputs("attention-rule-client: cannot checksum bar\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  if (expect_wm && has_urgency(dpy, target)) {
    fputs("attention-rule-client: WM attention forged XUrgencyHint\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_urgency(dpy, target, 0);
  pause_for_wm();
  after_hints = checksum_window(dpy, bar);

  if (expect_wm && after_hints != initial) {
    fputs("attention-rule-client: WM_HINTS update cleared WM-owned attention\n",
          stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }
  if (app_urgent && after_hints == initial) {
    fputs("attention-rule-client: clearing XUrgencyHint did not clear app urgency\n",
          stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  if (expect_wm || expect_none) {
    activate(dpy, root, target, active);
    activate(dpy, root, anchor, active);
    after_focus = checksum_window(dpy, bar);

    if (expect_wm && after_focus == initial) {
      fputs("attention-rule-client: focusing client did not consume WM attention\n",
            stderr);
      XCloseDisplay(dpy);
      return EXIT_FAILURE;
    }
    if (expect_none && after_focus != initial) {
      fputs("attention-rule-client: attention=none produced hidden urgency\n",
            stderr);
      XCloseDisplay(dpy);
      return EXIT_FAILURE;
    }
  }

  XDestroyWindow(dpy, target);
  XDestroyWindow(dpy, anchor);
  XSync(dpy, False);
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
