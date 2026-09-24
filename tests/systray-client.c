#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define XEMBED_MAPPED (1L << 0)
#define SYSTEM_TRAY_REQUEST_DOCK 0

static void
sleep_ms(long ms)
{
  struct timespec ts;

  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

static void
fail(const char *message)
{
  fprintf(stderr, "systray-client: %s\n", message);
  exit(EXIT_FAILURE);
}

static Window
find_surface(Display *dpy, Window root, Atom surface_atom, const char *wanted)
{
  Window root_return, parent_return, *children = NULL;
  unsigned int count = 0;
  Window found = None;

  if (!XQueryTree(dpy, root, &root_return, &parent_return, &children, &count))
    return None;

  for (unsigned int i = 0; i < count; ++i)
  {
    Atom actual = None;
    int format = 0;
    unsigned long nitems = 0, after = 0;
    unsigned char *data = NULL;

    if (XGetWindowProperty(dpy, children[i], surface_atom,
                           0L, 64L, False, AnyPropertyType,
                           &actual, &format, &nitems, &after, &data)
        == Success &&
        actual != None &&
        format == 8 &&
        data &&
        strlen(wanted) == nitems &&
        !memcmp(data, wanted, nitems))
      found = children[i];

    if (data)
      XFree(data);
    if (found != None)
      break;
  }

  if (children)
    XFree(children);
  return found;
}

static int
window_width(Display *dpy, Window window)
{
  XWindowAttributes attributes;

  if (!XGetWindowAttributes(dpy, window, &attributes))
    fail("cannot query window attributes");
  return attributes.width;
}

static int
window_height(Display *dpy, Window window)
{
  XWindowAttributes attributes;

  if (!XGetWindowAttributes(dpy, window, &attributes))
    fail("cannot query window attributes");
  return attributes.height;
}

static int
window_viewable(Display *dpy, Window window)
{
  XWindowAttributes attributes;

  if (!XGetWindowAttributes(dpy, window, &attributes))
    fail("cannot query window map state");
  return attributes.map_state == IsViewable;
}

static Window
window_parent(Display *dpy, Window window)
{
  Window root_return, parent_return, *children = NULL;
  unsigned int count = 0;

  if (!XQueryTree(dpy, window, &root_return, &parent_return, &children, &count))
    fail("cannot query icon parent");
  if (children)
    XFree(children);
  return parent_return;
}

static void
set_xembed_info(Display *dpy, Window icon, Atom xembed_info,
                unsigned long flags)
{
  unsigned long info[2] = { 0, flags };

  XChangeProperty(dpy, icon, xembed_info, xembed_info, 32,
                  PropModeReplace, (unsigned char *)info, 2);
  XFlush(dpy);
}

static void
request_dock(Display *dpy, Window tray, Atom opcode, Window icon)
{
  XEvent event;

  memset(&event, 0, sizeof event);
  event.xclient.type = ClientMessage;
  event.xclient.window = tray;
  event.xclient.message_type = opcode;
  event.xclient.format = 32;
  event.xclient.data.l[0] = CurrentTime;
  event.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
  event.xclient.data.l[2] = (long)icon;

  if (!XSendEvent(dpy, tray, False, NoEventMask, &event))
    fail("cannot send tray dock request");
  XFlush(dpy);
}

static void
wait_for(int (*predicate)(Display *, Window), Display *dpy, Window window,
         int wanted, const char *message)
{
  for (int i = 0; i < 60; ++i)
  {
    if (predicate(dpy, window) == wanted)
      return;
    sleep_ms(25);
  }
  fail(message);
}

int
main(void)
{
  Display *dpy;
  Window root, tray = None, bar = None, icon;
  Atom surface_atom, xembed_info, opcode, selection;
  char selection_name[64];
  int screen, screen_width;
  int tray_width;

  dpy = XOpenDisplay(NULL);
  if (!dpy)
    fail("cannot open display");

  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  screen_width = DisplayWidth(dpy, screen);

  snprintf(selection_name, sizeof selection_name,
           "_NET_SYSTEM_TRAY_S%d", screen);
  selection = XInternAtom(dpy, selection_name, False);
  opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False);
  surface_atom = XInternAtom(dpy, "_RAWM_SURFACE", False);

  for (int i = 0; i < 60; ++i)
  {
    tray = XGetSelectionOwner(dpy, selection);
    bar = find_surface(dpy, root, surface_atom, "bar");
    if (tray != None && bar != None)
      break;
    sleep_ms(25);
  }

  if (tray == None)
    fail("system tray selection owner not found");
  if (bar == None)
    fail("rawm bar surface not found");
  if (window_viewable(dpy, tray))
    fail("empty tray must start unmapped");
  if (window_width(dpy, bar) != screen_width)
    fail("empty tray must not reserve bar width");

  icon = XCreateSimpleWindow(dpy, root, 10, 10, 16, 16, 0, 0, 0);
  set_xembed_info(dpy, icon, xembed_info, XEMBED_MAPPED);
  request_dock(dpy, tray, opcode, icon);

  for (int i = 0; i < 60; ++i)
  {
    if (window_parent(dpy, icon) == tray && window_viewable(dpy, tray))
      break;
    sleep_ms(25);
  }

  if (window_parent(dpy, icon) != tray)
    fail("icon was not reparented into tray");
  if (!window_viewable(dpy, tray))
    fail("mapped XEmbed icon did not map tray");

  tray_width = window_width(dpy, tray);
  if (tray_width != 34)
    fail("tray width does not match spacing + normalized icon geometry");
  if (window_width(dpy, bar) + tray_width != screen_width)
    fail("bar did not reserve exactly the visible tray width");
  if (window_height(dpy, icon) != 24)
    fail("tray icon was not normalized to bar height");

  set_xembed_info(dpy, icon, xembed_info, 0);
  wait_for(window_viewable, dpy, tray, 0,
           "clearing XEMBED_MAPPED did not unmap tray");
  if (window_width(dpy, bar) != screen_width)
    fail("unmapped icon still reserved bar width");

  set_xembed_info(dpy, icon, xembed_info, XEMBED_MAPPED);
  wait_for(window_viewable, dpy, tray, 1,
           "restoring XEMBED_MAPPED did not remap tray");
  tray_width = window_width(dpy, tray);
  if (tray_width != 34 ||
      window_width(dpy, bar) + tray_width != screen_width)
    fail("remapped icon did not restore tray/bar geometry");

  XDestroyWindow(dpy, icon);
  XFlush(dpy);
  wait_for(window_viewable, dpy, tray, 0,
           "destroying final icon did not unmap tray");
  if (window_width(dpy, bar) != screen_width)
    fail("destroyed final icon still reserved bar width");

  XCloseDisplay(dpy);
  puts("systray client: ok");
  return EXIT_SUCCESS;
}
