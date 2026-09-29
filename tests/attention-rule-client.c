#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

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

static int
managed(Display *dpy, Window win)
{
  Atom property = XInternAtom(dpy, "_RAWM_MONITOR", True);
  Atom actual = None;
  unsigned char *data = NULL;
  unsigned long count = 0, after = 0;
  int format = 0;
  int result = 0;

  if (property != None &&
      XGetWindowProperty(dpy, win, property, 0, 1, False, XA_CARDINAL,
                         &actual, &format, &count, &after, &data) == Success &&
      actual == XA_CARDINAL && format == 32 && count == 1 && data)
    result = 1;

  if (data)
    XFree(data);
  return result;
}

static void
wait_managed(Display *dpy, Window win)
{
  int tries;

  for (tries = 0; tries < 50; ++tries) {
    if (managed(dpy, win))
      return;
    pause_for_wm();
  }

  fputs("attention-rule-client: target was not admitted by rawm\n", stderr);
  exit(EXIT_FAILURE);
}

static char *
read_control_state(Display *dpy)
{
  char selection_name[64];
  Atom selection, property, utf8, actual = None;
  Window owner;
  unsigned char *data = NULL;
  unsigned long count = 0, after = 0;
  int format = 0;
  char *copy;

  snprintf(selection_name, sizeof(selection_name), "_RAWM_CONTROL_S%d",
           DefaultScreen(dpy));
  selection = XInternAtom(dpy, selection_name, True);
  property = XInternAtom(dpy, "_RAWM_STATE", True);
  utf8 = XInternAtom(dpy, "UTF8_STRING", True);
  if (selection == None || property == None || utf8 == None)
    return NULL;

  owner = XGetSelectionOwner(dpy, selection);
  if (owner == None ||
      XGetWindowProperty(dpy, owner, property, 0, 16384, False, utf8,
                         &actual, &format, &count, &after, &data) != Success ||
      actual != utf8 || format != 8 || !data || after != 0) {
    if (data)
      XFree(data);
    return NULL;
  }

  copy = malloc(count + 1);
  if (!copy) {
    XFree(data);
    return NULL;
  }
  memcpy(copy, data, count);
  copy[count] = '\0';
  XFree(data);
  return copy;
}

static int
selected_monitor_urgent(Display *dpy, unsigned int *urgent)
{
  char *state = read_control_state(dpy);
  char *line;

  if (!state)
    return 0;

  line = state;
  while (*line) {
    char *next = strchr(line, '\n');
    int monitor, selected, mx, my, mw, mh;
    unsigned int viewed, occupied, current_urgent;

    if (next)
      *next = '\0';
    if (sscanf(line,
               "monitor\t%d\t%d\t%d\t%d\t%d\t%d\t%x\t%x\t%x",
               &monitor, &selected, &mx, &my, &mw, &mh,
               &viewed, &occupied, &current_urgent) == 9 && selected) {
      (void)monitor;
      (void)mx;
      (void)my;
      (void)mw;
      (void)mh;
      (void)viewed;
      (void)occupied;
      *urgent = current_urgent;
      free(state);
      return 1;
    }
    if (!next)
      break;
    line = next + 1;
  }

  free(state);
  return 0;
}

static void
wait_urgent(Display *dpy, unsigned int expected, const char *what)
{
  int tries;

  for (tries = 0; tries < 50; ++tries) {
    unsigned int urgent = 0;

    if (selected_monitor_urgent(dpy, &urgent) && urgent == expected)
      return;
    pause_for_wm();
  }

  fprintf(stderr,
          "attention-rule-client: %s did not publish urgent mask %08x\n",
          what, expected);
  exit(EXIT_FAILURE);
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
  Window root, anchor, target;
  Atom active;
  unsigned int initial_urgent;
  unsigned int after_hints_urgent;
  int app_urgent = 0;
  int expect_wm = 0;
  int expect_none = 0;

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
  wait_managed(dpy, anchor);

  target = XCreateSimpleWindow(dpy, root, 80, 80, 280, 200, 0, 0, 0);
  XStoreName(dpy, target, "rawm-attention-target");
  set_class(dpy, target, "rawm-attention-target", "RawmAttentionTarget");
  if (app_urgent)
    set_urgency(dpy, target, 1);
  XMapWindow(dpy, target);
  XFlush(dpy);
  wait_managed(dpy, target);

  initial_urgent = (expect_wm || app_urgent) ? 0x00000002u : 0u;
  wait_urgent(dpy, initial_urgent, "initial attention state");

  if (expect_wm && has_urgency(dpy, target)) {
    fputs("attention-rule-client: WM attention forged XUrgencyHint\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  set_urgency(dpy, target, 0);
  after_hints_urgent = expect_wm ? 0x00000002u : 0u;
  wait_urgent(dpy, after_hints_urgent, "post-WM_HINTS attention state");

  if (expect_wm || expect_none) {
    activate(dpy, root, target, active);
    activate(dpy, root, anchor, active);
    wait_urgent(dpy, 0u, "post-focus attention state");
  }

  XDestroyWindow(dpy, target);
  XDestroyWindow(dpy, anchor);
  XSync(dpy, False);
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
