#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CONTROL_VERSION 1u

static void
fail(const char *message)
{
  fprintf(stderr, "control-client: %s\n", message);
  exit(EXIT_FAILURE);
}

static long
parse_long(const char *text)
{
  char *end = NULL;
  long value;

  errno = 0;
  value = strtol(text, &end, 0);
  if (errno || !end || end == text || *end)
    fail("invalid integer argument");
  return value;
}


static Window
parse_window(const char *text)
{
  unsigned long value = (unsigned long)parse_long(text);

  if (value == 0 || value > UINT32_MAX)
    fail("invalid window XID");
  return (Window)value;
}

static int
set_window_title(Display *display, Window window, const char *title)
{
  XStoreName(display, window, title);
  XFlush(display);
  return EXIT_SUCCESS;
}

static int
set_window_class(Display *display, Window window, const char *class_name)
{
  XClassHint hint;

  hint.res_name = (char *)"control-client";
  hint.res_class = (char *)class_name;
  if (!XSetClassHint(display, window, &hint))
    return EXIT_FAILURE;
  XFlush(display);
  return EXIT_SUCCESS;
}

static unsigned char *
get_property(Display *display, Window window, Atom property, Atom type,
             int *format, unsigned long *count)
{
  Atom actual_type = None;
  unsigned long after = 0;
  unsigned char *data = NULL;

  if (XGetWindowProperty(display, window, property, 0, 65536, False, type,
                         &actual_type, format, count, &after, &data) != Success ||
      actual_type != type || after != 0)
  {
    if (data)
      XFree(data);
    return NULL;
  }
  return data;
}

static Window
control_window(Display *display)
{
  int screen = DefaultScreen(display);
  char selection_name[64];
  Atom selection;
  int selection_length;

  selection_length = snprintf(selection_name, sizeof(selection_name),
                              "_RAWM_CONTROL_S%d", screen);
  if (selection_length < 0 ||
      (size_t)selection_length >= sizeof(selection_name))
    fail("control selection name is too long");
  selection = XInternAtom(display, selection_name, True);
  if (selection == None)
    return None;
  return XGetSelectionOwner(display, selection);
}

static int
print_version(Display *display, Window root)
{
  Window owner = control_window(display);
  Atom property = XInternAtom(display, "_RAWM_CONTROL_VERSION", True);
  unsigned long count = 0;
  int format = 0;
  unsigned char *data;

  (void)root;
  if (owner == None || property == None)
    return EXIT_FAILURE;
  data = get_property(display, owner, property, XA_CARDINAL, &format, &count);
  if (!data || format != 32 || count != 1)
  {
    if (data)
      XFree(data);
    return EXIT_FAILURE;
  }
  printf("%lu\n", *(unsigned long *)data);
  XFree(data);
  return EXIT_SUCCESS;
}

static int
print_state(Display *display, Window root)
{
  Window owner = control_window(display);
  Atom property = XInternAtom(display, "_RAWM_STATE", True);
  Atom utf8 = XInternAtom(display, "UTF8_STRING", True);
  unsigned long count = 0;
  int format = 0;
  unsigned char *data;

  (void)root;
  if (owner == None || property == None || utf8 == None)
    return EXIT_FAILURE;
  data = get_property(display, owner, property, utf8, &format, &count);
  if (!data || format != 8)
  {
    if (data)
      XFree(data);
    return EXIT_FAILURE;
  }
  if (count && fwrite(data, 1, count, stdout) != count)
    fail("cannot write state");
  XFree(data);
  return EXIT_SUCCESS;
}

static int
print_clients(Display *display, Window root)
{
  Window owner = control_window(display);
  Atom property = XInternAtom(display, "_RAWM_CLIENTS", True);
  Atom utf8 = XInternAtom(display, "UTF8_STRING", True);
  unsigned long count = 0;
  int format = 0;
  unsigned char *data;

  (void)root;
  if (owner == None || property == None || utf8 == None)
    return EXIT_FAILURE;
  data = get_property(display, owner, property, utf8, &format, &count);
  if (!data || format != 8)
  {
    if (data)
      XFree(data);
    return EXIT_FAILURE;
  }
  if (count && fwrite(data, 1, count, stdout) != count)
    fail("cannot write client inventory");
  XFree(data);
  return EXIT_SUCCESS;
}

static int
print_feedback(Display *display, Window root)
{
  Window owner = control_window(display);
  Atom property = XInternAtom(display, "_RAWM_FEEDBACK", True);
  Atom utf8 = XInternAtom(display, "UTF8_STRING", True);
  unsigned long count = 0;
  int format = 0;
  unsigned char *data;

  (void)root;
  if (owner == None || property == None || utf8 == None)
    return EXIT_FAILURE;
  data = get_property(display, owner, property, utf8, &format, &count);
  if (!data || format != 8)
  {
    if (data)
      XFree(data);
    return EXIT_FAILURE;
  }
  if (count && fwrite(data, 1, count, stdout) != count)
    fail("cannot write transient feedback");
  XFree(data);
  return EXIT_SUCCESS;
}

static int
window_client(Display *display, Window root, const char *class_name,
              const char *title)
{
  XClassHint hint;
  Window window;

  window = XCreateSimpleWindow(display, root, 40, 40, 240, 120, 0, 0, 0);
  if (window == None)
    fail("cannot create test window");
  hint.res_name = (char *)"control-client";
  hint.res_class = (char *)class_name;
  XSetClassHint(display, window, &hint);
  XStoreName(display, window, title);
  XMapWindow(display, window);
  XFlush(display);
  printf("0x%lx\n", (unsigned long)window);
  fflush(stdout);
  for (;;)
    pause();
  return EXIT_SUCCESS;
}

static int
supports(Display *display, Window root, const char *name)
{
  Window owner = control_window(display);
  Atom property = XInternAtom(display, "_RAWM_SUPPORTED_COMMANDS", True);
  Atom command = XInternAtom(display, name, True);
  unsigned long count = 0, i;
  int format = 0;
  unsigned char *data;
  Atom *atoms;

  (void)root;
  if (owner == None || property == None || command == None)
    return EXIT_FAILURE;
  data = get_property(display, owner, property, XA_ATOM, &format, &count);
  if (!data || format != 32)
  {
    if (data)
      XFree(data);
    return EXIT_FAILURE;
  }

  atoms = (Atom *)data;
  for (i = 0; i < count; ++i)
  {
    if (atoms[i] == command)
    {
      XFree(data);
      return EXIT_SUCCESS;
    }
  }
  XFree(data);
  return EXIT_FAILURE;
}

static void
sleep_millis(long millis)
{
  struct timespec delay;

  delay.tv_sec = millis / 1000;
  delay.tv_nsec = (millis % 1000) * 1000000L;
  while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
    /* retry */;
}

static int
send_command(Display *display, Window root, const char *name,
             long arg0, long arg1, const char *text)
{
  Window owner = control_window(display);
  Atom command_type = XInternAtom(display, "_RAWM_COMMAND", False);
  Atom command = XInternAtom(display, name, False);
  Atom text_atom = XInternAtom(display, "_RAWM_COMMAND_TEXT", False);
  Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
  Window requestor = None;
  XEvent event;
  int attempts;

  if (owner == None)
    return EXIT_FAILURE;

  if (text)
  {
    requestor = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(display, requestor, text_atom, utf8, 8, PropModeReplace,
                    (const unsigned char *)text, (int)strlen(text));
  }

  memset(&event, 0, sizeof(event));
  event.xclient.type = ClientMessage;
  event.xclient.display = display;
  event.xclient.window = owner;
  event.xclient.message_type = command_type;
  event.xclient.format = 32;
  event.xclient.data.l[0] = CONTROL_VERSION;
  event.xclient.data.l[1] = (long)command;
  event.xclient.data.l[2] = (long)(int32_t)arg0;
  event.xclient.data.l[3] = (long)(int32_t)arg1;
  event.xclient.data.l[4] = (long)requestor;

  if (!XSendEvent(display, owner, False, NoEventMask, &event))
    fail("cannot send command event");
  XFlush(display);

  if (!text)
    return EXIT_SUCCESS;

  for (attempts = 0; attempts < 100; ++attempts)
  {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long count = 0, after = 0;
    unsigned char *data = NULL;

    if (XGetWindowProperty(display, requestor, text_atom, 0, 1, False,
                           AnyPropertyType, &actual_type, &actual_format,
                           &count, &after, &data) != Success)
      break;
    if (data)
      XFree(data);
    if (actual_type == None)
      break;
    sleep_millis(10);
  }

  XDestroyWindow(display, requestor);
  XFlush(display);
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

  if (argc == 4 && !strcmp(argv[1], "--set-title"))
  {
    result = set_window_title(display, parse_window(argv[2]), argv[3]);
    XCloseDisplay(display);
    return result;
  }
  if (argc == 4 && !strcmp(argv[1], "--set-class"))
  {
    result = set_window_class(display, parse_window(argv[2]), argv[3]);
    XCloseDisplay(display);
    return result;
  }

  if (argc == 2 && !strcmp(argv[1], "--version"))
    result = print_version(display, root);
  else if (argc == 2 && !strcmp(argv[1], "--state"))
    result = print_state(display, root);
  else if (argc == 2 && !strcmp(argv[1], "--clients"))
    result = print_clients(display, root);
  else if (argc == 2 && !strcmp(argv[1], "--feedback"))
    result = print_feedback(display, root);
  else if (argc == 4 && !strcmp(argv[1], "--window"))
    result = window_client(display, root, argv[2], argv[3]);
  else if (argc == 3 && !strcmp(argv[1], "--supports"))
    result = supports(display, root, argv[2]);
  else if ((argc == 3 || argc == 4 || argc == 5 || argc == 6) &&
           !strcmp(argv[1], "--send"))
  {
    long arg0 = argc >= 4 ? parse_long(argv[3]) : 0;
    long arg1 = argc >= 5 ? parse_long(argv[4]) : 0;
    const char *text = argc >= 6 ? argv[5] : NULL;
    result = send_command(display, root, argv[2], arg0, arg1, text);
  }
  else
    fail("usage: control-client --version | --state | --clients | --feedback | "
         "--window CLASS TITLE | --supports ATOM | "
         "--send ATOM [ARG0 [ARG1 [TEXT]]]");

  XCloseDisplay(display);
  return result;
}
