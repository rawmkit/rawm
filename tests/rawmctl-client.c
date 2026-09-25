#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *message)
{
  fprintf(stderr, "rawmctl-client: %s\n", message);
  exit(EXIT_FAILURE);
}

static Window
parse_window(const char *text)
{
  char *end = NULL;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 0);
  if (errno || !end || end == text || *end || !value)
    fail("invalid window id");
  return (Window)value;
}

static int
print_active(Display *display)
{
  Window root = DefaultRootWindow(display);
  Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long count = 0, after = 0;
  unsigned char *data = NULL;

  if (active == None ||
      XGetWindowProperty(display, root, active, 0, 1, False, XA_WINDOW,
                         &actual_type, &actual_format, &count, &after,
                         &data) != Success ||
      actual_type != XA_WINDOW || actual_format != 32 || count != 1 || !data)
  {
    if (data)
      XFree(data);
    puts("none");
    return EXIT_SUCCESS;
  }

  printf("0x%lx\n", *(Window *)data);
  XFree(data);
  return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
  Display *display;
  Window root, window;
  XClassHint hint;
  Atom utf8, net_wm_name, wm_delete, wm_protocols;
  XEvent event;

  display = XOpenDisplay(NULL);
  if (!display)
    fail("cannot open display");

  if (argc == 2 && !strcmp(argv[1], "--active"))
  {
    int result = print_active(display);
    XCloseDisplay(display);
    return result;
  }
  if (argc == 3 && !strcmp(argv[1], "--destroy"))
  {
    XDestroyWindow(display, parse_window(argv[2]));
    XSync(display, False);
    XCloseDisplay(display);
    return EXIT_SUCCESS;
  }
  if (argc >= 3 && !strcmp(argv[1], "--client-list"))
  {
    Window *windows;
    Atom client_list;
    int i;

    windows = calloc((size_t)(argc - 2), sizeof(*windows));
    if (!windows)
      fail("out of memory");
    for (i = 2; i < argc; ++i)
      windows[i - 2] = parse_window(argv[i]);

    client_list = XInternAtom(display, "_NET_CLIENT_LIST", False);
    XChangeProperty(display, DefaultRootWindow(display), client_list, XA_WINDOW,
                    32, PropModeReplace, (unsigned char *)windows, argc - 2);
    XSync(display, False);
    free(windows);
    XCloseDisplay(display);
    return EXIT_SUCCESS;
  }
  if (argc != 3)
    fail("usage: rawmctl-client CLASS TITLE | --active | --destroy WINDOW | --client-list WINDOW...");

  root = DefaultRootWindow(display);
  window = XCreateSimpleWindow(display, root, 80, 90, 320, 180, 0, 0, 0);

  hint.res_name = "rawmctl-client";
  hint.res_class = argv[1];
  if (!XSetClassHint(display, window, &hint))
    fail("cannot set WM_CLASS");

  XStoreName(display, window, argv[2]);
  utf8 = XInternAtom(display, "UTF8_STRING", False);
  net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
  XChangeProperty(display, window, net_wm_name, utf8, 8, PropModeReplace,
                  (unsigned char *)argv[2], (int)strlen(argv[2]));

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
