#include <X11/Xatom.h>
#include <X11/Xlib.h>

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
  if (argc != 1)
    fail("usage: compositor-state-client [--inspect WINDOW]");

  root = DefaultRootWindow(display);
  window = XCreateSimpleWindow(display, root, 60, 70, 320, 200, 0, 0, 0);
  XStoreName(display, window, "rawm-compositor-state");
  XSelectInput(display, window, StructureNotifyMask);
  XMapWindow(display, window);
  XFlush(display);

  printf("0x%lx\n", window);
  fflush(stdout);

  for (;;)
  {
    XNextEvent(display, &event);
    if (event.type == DestroyNotify)
      break;
  }

  XCloseDisplay(display);
  return EXIT_SUCCESS;
}
