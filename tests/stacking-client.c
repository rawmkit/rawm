#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
fail(const char *message)
{
  fprintf(stderr, "stacking-client: %s\n", message);
  exit(EXIT_FAILURE);
}

static Window
parse_window(const char *text)
{
  char *end = NULL;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 0);
  if (errno || !end || end == text || *end || value == 0 ||
      value > UINT32_MAX)
    fail("invalid window XID");
  return (Window)value;
}

static int
window_above(Display *display, Window lower, Window upper)
{
  Window root = DefaultRootWindow(display);
  Window root_return, parent_return, *children = NULL;
  unsigned int count = 0;
  unsigned int i;
  int lower_index = -1;
  int upper_index = -1;

  if (!XQueryTree(display, root, &root_return, &parent_return,
                  &children, &count))
    return 0;

  for (i = 0; i < count; ++i)
  {
    if (children[i] == lower)
      lower_index = (int)i;
    if (children[i] == upper)
      upper_index = (int)i;
  }

  if (children)
    XFree(children);
  return lower_index >= 0 && upper_index > lower_index;
}

static int
run_client(Display *display, Window root, const char *class_name)
{
  XClassHint hint;
  Window window;

  window = XCreateSimpleWindow(display, root, 40, 40, 240, 120,
                               0, 0, 0);
  if (window == None)
    fail("cannot create managed client");

  hint.res_name = (char *)"stacking-client";
  hint.res_class = (char *)class_name;
  XSetClassHint(display, window, &hint);
  XStoreName(display, window, class_name);
  XMapWindow(display, window);
  XFlush(display);

  printf("0x%lx\n", (unsigned long)window);
  fflush(stdout);
  for (;;)
    pause();
  return EXIT_SUCCESS;
}

static int
run_external(Display *display, Window root, const char *type_name)
{
  XSetWindowAttributes attrs;
  Atom net_type;
  Atom window_type;
  Window window;

  memset(&attrs, 0, sizeof(attrs));
  attrs.override_redirect = True;
  window = XCreateWindow(display, root, 80, 80, 220, 100, 0,
                         CopyFromParent, InputOutput, CopyFromParent,
                         CWOverrideRedirect, &attrs);
  if (window == None)
    fail("cannot create external surface");

  net_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  window_type = XInternAtom(display, type_name, False);
  XChangeProperty(display, window, net_type, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)&window_type, 1);
  XMapRaised(display, window);
  XFlush(display);

  printf("0x%lx\n", (unsigned long)window);
  fflush(stdout);
  for (;;)
    pause();
  return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
  Display *display;
  Window root;
  int result;

  display = XOpenDisplay(NULL);
  if (!display)
    fail("cannot open display");
  root = DefaultRootWindow(display);

  if (argc == 3 && strcmp(argv[1], "client") == 0)
    result = run_client(display, root, argv[2]);
  else if (argc == 2 && strcmp(argv[1], "notification") == 0)
    result = run_external(display, root, "_NET_WM_WINDOW_TYPE_NOTIFICATION");
  else if (argc == 2 && strcmp(argv[1], "popup") == 0)
    result = run_external(display, root, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
  else if (argc == 2 && strcmp(argv[1], "dock") == 0)
    result = run_external(display, root, "_NET_WM_WINDOW_TYPE_DOCK");
  else if (argc == 4 && strcmp(argv[1], "above") == 0)
    result = window_above(display, parse_window(argv[2]),
                          parse_window(argv[3]))
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
  else
    fail("usage: stacking-client client CLASS | notification | popup | "
         "dock | above LOWER-XID UPPER-XID");

  XCloseDisplay(display);
  return result;
}
