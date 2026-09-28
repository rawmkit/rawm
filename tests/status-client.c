#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

int
main(void)
{
  Display *dpy;
  Window root, bar = None;
  XWindowAttributes attr;
  XEvent event;
  int tries;

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fputs("status-client: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }
  root = DefaultRootWindow(dpy);
  for (tries = 0; tries < 50 && bar == None; ++tries) {
    bar = find_bar(dpy, root);
    if (bar == None)
      pause_for_wm();
  }
  if (bar == None || !XGetWindowAttributes(dpy, bar, &attr)) {
    fputs("status-client: cannot find rawm bar window\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  memset(&event, 0, sizeof(event));
  event.xbutton.type = ButtonPress;
  event.xbutton.display = dpy;
  event.xbutton.window = bar;
  event.xbutton.root = root;
  event.xbutton.subwindow = None;
  event.xbutton.time = CurrentTime;
  event.xbutton.x = attr.width - 3;
  event.xbutton.y = attr.height / 2;
  event.xbutton.x_root = event.xbutton.x;
  event.xbutton.y_root = attr.y + event.xbutton.y;
  event.xbutton.state = 0;
  event.xbutton.button = Button1;
  event.xbutton.same_screen = True;

  if (!XSendEvent(dpy, bar, False, ButtonPressMask, &event)) {
    fputs("status-client: could not send status click\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }
  XFlush(dpy);
  pause_for_wm();
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
