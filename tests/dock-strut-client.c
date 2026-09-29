#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t change_requested = 0;

static void
stop(int signo)
{
  (void)signo;
  running = 0;
}

static void
request_change(int signo)
{
  (void)signo;
  change_requested = 1;
}

static void
set_strut(Display *display, Window window, int partial, unsigned long top,
          unsigned long width)
{
  Atom cardinal = XA_CARDINAL;
  Atom strut = XInternAtom(display, "_NET_WM_STRUT", False);
  Atom strut_partial = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);
  unsigned long basic[4] = { 0u, 0u, top, 0u };
  unsigned long extended[12] = {
    0u, 0u, top, 0u,
    0u, 0u, 0u, 0u,
    0u, width ? width - 1u : 0u,
    0u, 0u
  };

  if (partial)
  {
    XChangeProperty(display, window, strut_partial, cardinal, 32,
                    PropModeReplace, (unsigned char *)extended, 12);
    XDeleteProperty(display, window, strut);
  }
  else
  {
    XChangeProperty(display, window, strut, cardinal, 32,
                    PropModeReplace, (unsigned char *)basic, 4);
    XDeleteProperty(display, window, strut_partial);
  }
}

static int
run_dock(Display *display, int partial, int override_redirect,
         unsigned long top, unsigned long changed_top)
{
  int screen = DefaultScreen(display);
  Window root = RootWindow(display, screen);
  unsigned long width = (unsigned long)DisplayWidth(display, screen);
  XSetWindowAttributes attributes;
  Atom window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
  Atom dock = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DOCK", False);
  Window window;

  memset(&attributes, 0, sizeof(attributes));
  attributes.override_redirect = override_redirect ? True : False;
  attributes.event_mask = StructureNotifyMask;
  window = XCreateWindow(display, root, 0, 0, (unsigned int)width,
                         (unsigned int)(top ? top : 1u), 0,
                         CopyFromParent, InputOutput, CopyFromParent,
                         CWOverrideRedirect | CWEventMask, &attributes);
  XChangeProperty(display, window, window_type, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)&dock, 1);
  set_strut(display, window, partial, top, width);
  XMapWindow(display, window);
  XFlush(display);

  printf("0x%lx\n", (unsigned long)window);
  fflush(stdout);

  while (running)
  {
    sleep(1);
    if (running && change_requested && changed_top)
    {
      change_requested = 0;
      set_strut(display, window, partial, changed_top, width);
      XFlush(display);
      printf("changed\n");
      fflush(stdout);
    }
  }

  XDestroyWindow(display, window);
  XFlush(display);
  return 0;
}

static int
run_client(Display *display)
{
  int screen = DefaultScreen(display);
  Window root = RootWindow(display, screen);
  Window window = XCreateSimpleWindow(display, root, 30, 30, 200, 120,
                                      0, 0, 0);

  XStoreName(display, window, "rawm-dock-strut-client");
  XMapWindow(display, window);
  XFlush(display);
  printf("0x%lx\n", (unsigned long)window);
  fflush(stdout);

  while (running)
    sleep(1);

  XDestroyWindow(display, window);
  XFlush(display);
  return 0;
}

int
main(int argc, char **argv)
{
  Display *display;
  int rc;

  signal(SIGTERM, stop);
  signal(SIGINT, stop);
  signal(SIGUSR1, request_change);

  display = XOpenDisplay(NULL);
  if (!display)
  {
    fputs("dock-strut-client: cannot open display\n", stderr);
    return 77;
  }

  if (argc == 2 && strcmp(argv[1], "client") == 0)
    rc = run_client(display);
  else if (argc >= 5 && strcmp(argv[1], "dock") == 0)
  {
    int partial;
    int override_redirect;
    unsigned long top;
    unsigned long changed_top = 0u;

    if (strcmp(argv[2], "partial") == 0)
      partial = 1;
    else if (strcmp(argv[2], "legacy") == 0)
      partial = 0;
    else
    {
      fputs("dock-strut-client: expected partial or legacy\n", stderr);
      XCloseDisplay(display);
      return 2;
    }
    override_redirect = strcmp(argv[3], "override") == 0;
    if (!override_redirect && strcmp(argv[3], "managed") != 0)
    {
      fputs("dock-strut-client: expected override or managed\n", stderr);
      XCloseDisplay(display);
      return 2;
    }
    top = strtoul(argv[4], NULL, 10);
    if (argc >= 6)
      changed_top = strtoul(argv[5], NULL, 10);
    rc = run_dock(display, partial, override_redirect, top, changed_top);
  }
  else
  {
    fputs("usage: dock-strut-client client | dock partial|legacy override|managed top [changed-top]\n", stderr);
    rc = 2;
  }

  XCloseDisplay(display);
  return rc;
}
