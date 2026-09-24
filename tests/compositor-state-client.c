#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
fail(const char *message)
{
  fprintf(stderr, "compositor-state-client: %s\n", message);
  exit(EXIT_FAILURE);
}

static unsigned long
cardinal(Display *display, Window window, const char *name)
{
  Atom actual_type = None;
  Atom property = XInternAtom(display, name, True);
  unsigned char *data = NULL;
  unsigned long after = 0, count = 0;
  int actual_format = 0;
  unsigned long value;

  if (property == None ||
      XGetWindowProperty(display, window, property, 0, 1, False, XA_CARDINAL,
                         &actual_type, &actual_format, &count, &after,
                         &data) != Success ||
      actual_type != XA_CARDINAL || actual_format != 32 || count != 1 || !data)
    fail("missing CARDINAL compositor property");

  value = *(unsigned long *)data;
  XFree(data);
  return value;
}

static void
string_property(Display *display, Window window, const char *name,
                char *buffer, size_t size)
{
  Atom actual_type = None;
  Atom property = XInternAtom(display, name, True);
  unsigned char *data = NULL;
  unsigned long after = 0, count = 0;
  int actual_format = 0;

  if (!size)
    fail("invalid string buffer");
  if (property == None ||
      XGetWindowProperty(display, window, property, 0, 64, False, AnyPropertyType,
                         &actual_type, &actual_format, &count, &after,
                         &data) != Success ||
      actual_type == None || actual_format != 8 || !data)
    fail("missing string compositor property");
  if (count >= size)
    fail("compositor property is too long");

  memcpy(buffer, data, count);
  buffer[count] = '\0';
  XFree(data);
}


static Window
find_surface(Display *display, Window root, const char *wanted)
{
  Atom surface_atom = XInternAtom(display, "_RAWM_SURFACE", True);
  Window root_return, parent_return, *children = NULL;
  unsigned int count = 0, i;
  Window found = None;

  if (surface_atom == None ||
      !XQueryTree(display, root, &root_return, &parent_return, &children, &count))
    return None;

  for (i = 0; i < count; ++i)
  {
    Atom actual_type = None;
    unsigned char *data = NULL;
    unsigned long after = 0, nitems = 0;
    int actual_format = 0;

    if (XGetWindowProperty(display, children[i], surface_atom, 0, 64, False,
                           AnyPropertyType, &actual_type, &actual_format,
                           &nitems, &after, &data) == Success &&
        actual_format == 8 && data && strlen(wanted) == nitems &&
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
inspect_client_geometry(Display *display, Window window)
{
  XWindowAttributes attributes;

  if (!XGetWindowAttributes(display, window, &attributes))
    fail("cannot inspect client geometry");

  printf("%d %d %d %d\n",
         attributes.x, attributes.y, attributes.width, attributes.height);
  return EXIT_SUCCESS;
}

static int
inspect_surface_geometry(Display *display, const char *wanted)
{
  Window window = find_surface(display, DefaultRootWindow(display), wanted);
  XWindowAttributes attributes;

  if (window == None)
    fail("surface not found");
  if (!XGetWindowAttributes(display, window, &attributes))
    fail("cannot inspect surface geometry");

  printf("width=%d height=%d\n", attributes.width, attributes.height);
  return EXIT_SUCCESS;
}

static int
inspect_surface_state(Display *display, const char *wanted)
{
  Window window = find_surface(display, DefaultRootWindow(display), wanted);
  XWindowAttributes attributes;

  if (window == None)
    fail("surface not found");
  if (!XGetWindowAttributes(display, window, &attributes))
    fail("cannot inspect surface state");

  puts(attributes.map_state == IsViewable ? "mapped" : "unmapped");
  return EXIT_SUCCESS;
}

static int
inspect_surface(Display *display, const char *wanted)
{
  Window window = find_surface(display, DefaultRootWindow(display), wanted);
  Atom type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", True);
  Atom dock_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", True);
  Atom actual_type = None;
  unsigned char *data = NULL;
  unsigned long after = 0, count = 0;
  int actual_format = 0;
  XClassHint hint = {0};
  const char *type = "other";

  if (window == None)
    fail("surface not found");
  if (!XGetClassHint(display, window, &hint) || !hint.res_name || !hint.res_class)
    fail("surface WM_CLASS missing");
  if (type_atom != None && dock_atom != None &&
      XGetWindowProperty(display, window, type_atom, 0, 1, False, XA_ATOM,
                         &actual_type, &actual_format, &count, &after,
                         &data) == Success &&
      actual_type == XA_ATOM && actual_format == 32 && count == 1 && data &&
      *(Atom *)data == dock_atom)
    type = "dock";

  printf("surface=%s monitor=%lu class=%s instance=%s type=%s\n",
         wanted, cardinal(display, window, "_RAWM_MONITOR"),
         hint.res_class, hint.res_name, type);

  if (data)
    XFree(data);
  XFree(hint.res_name);
  XFree(hint.res_class);
  return EXIT_SUCCESS;
}

static Window
parse_window(const char *text)
{
  char *end = NULL;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 0);
  if (errno || !end || *end || !value)
    fail("invalid window id");
  return (Window)value;
}

static int
inspect(Display *display, Window window)
{
  char layout[64];

  string_property(display, window, "_RAWM_LAYOUT", layout, sizeof(layout));
  printf("floating=%lu selected=%lu layout=%s monitor=%lu\n",
         cardinal(display, window, "_RAWM_FLOATING"),
         cardinal(display, window, "_RAWM_SELECTED"),
         layout,
         cardinal(display, window, "_RAWM_MONITOR"));
  return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
  Display *display;
  Window root, window;
  Atom wm_delete, wm_protocols;
  XEvent event;

  display = XOpenDisplay(NULL);
  if (!display)
    fail("cannot open display");

  if (argc == 3 && !strcmp(argv[1], "--inspect"))
  {
    int result = inspect(display, parse_window(argv[2]));
    XCloseDisplay(display);
    return result;
  }
  if (argc == 3 && !strcmp(argv[1], "--surface"))
  {
    int result = inspect_surface(display, argv[2]);
    XCloseDisplay(display);
    return result;
  }
  if (argc == 3 && !strcmp(argv[1], "--client-geometry"))
  {
    int result = inspect_client_geometry(display, parse_window(argv[2]));
    XCloseDisplay(display);
    return result;
  }
  if (argc == 3 && !strcmp(argv[1], "--surface-state"))
  {
    int result = inspect_surface_state(display, argv[2]);
    XCloseDisplay(display);
    return result;
  }
  if (argc == 3 && !strcmp(argv[1], "--surface-geometry"))
  {
    int result = inspect_surface_geometry(display, argv[2]);
    XCloseDisplay(display);
    return result;
  }
  if (argc != 1)
    fail("usage: compositor-state-client [--inspect WINDOW | --client-geometry WINDOW | --surface NAME | --surface-state NAME | --surface-geometry NAME]");

  root = DefaultRootWindow(display);
  window = XCreateSimpleWindow(display, root, 60, 70, 320, 200, 0, 0, 0);
  XStoreName(display, window, "rawm-compositor-state");
  wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  if (!XSetWMProtocols(display, window, &wm_delete, 1))
    fail("cannot advertise WM_DELETE_WINDOW");
  XSelectInput(display, window, StructureNotifyMask);
  XMapWindow(display, window);
  XFlush(display);

  printf("0x%lx\n", window);
  fflush(stdout);

  for (;;)
  {
    XNextEvent(display, &event);
    if (event.type == ClientMessage &&
        event.xclient.message_type == wm_protocols &&
        (Atom)event.xclient.data.l[0] == wm_delete)
    {
      XDestroyWindow(display, window);
      XFlush(display);
    }
    else if (event.type == DestroyNotify)
      break;
  }

  XCloseDisplay(display);
  return EXIT_SUCCESS;
}
