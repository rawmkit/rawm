#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
set_class(Display *dpy, Window win, const char *name, const char *class_name)
{
  XClassHint hint;

  hint.res_name = (char *)name;
  hint.res_class = (char *)class_name;
  XSetClassHint(dpy, win, &hint);
}

static void
wait_mapped(Display *dpy, Window win)
{
  XEvent ev;

  for (;;) {
    XNextEvent(dpy, &ev);
    if (ev.type == MapNotify && ev.xmap.window == win)
      return;
  }
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, parent, dialog;
  Atom type_atom, dialog_atom;
  XEvent ev;
  int orphan = argc == 2 && !strcmp(argv[1], "--orphan");

  if (argc > 2 || (argc == 2 && !orphan)) {
    fprintf(stderr, "usage: %s [--orphan]\n", argv[0]);
    return EXIT_FAILURE;
  }

  dpy = XOpenDisplay(NULL);
  if (!dpy) {
    fprintf(stderr, "dialog-center-client: cannot open display\n");
    return EXIT_FAILURE;
  }

  root = DefaultRootWindow(dpy);
  parent = XCreateSimpleWindow(dpy, root, 100, 100, 400, 300, 0, 0, 0);
  XStoreName(dpy, parent, "rawm-center-parent");
  set_class(dpy, parent, "rawm-center-parent", "RawmCenterParent");
  XSelectInput(dpy, parent, StructureNotifyMask);
  XMapWindow(dpy, parent);
  XFlush(dpy);
  wait_mapped(dpy, parent);

  dialog = XCreateSimpleWindow(dpy, root, 17, 23, 200, 100, 0, 0, 0);
  XStoreName(dpy, dialog, "rawm-center-dialog");
  set_class(dpy, dialog, "rawm-center-dialog", "RawmCenterDialog");
  if (!orphan)
    XSetTransientForHint(dpy, dialog, parent);

  type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  dialog_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  XChangeProperty(dpy, dialog, type_atom, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)&dialog_atom, 1);
  XSelectInput(dpy, dialog, StructureNotifyMask);
  XMapWindow(dpy, dialog);
  XFlush(dpy);
  wait_mapped(dpy, dialog);

  printf("%lu %lu\n", (unsigned long)parent, (unsigned long)dialog);
  fflush(stdout);

  for (;;) {
    XNextEvent(dpy, &ev);
    if (ev.type == DestroyNotify &&
        (ev.xdestroywindow.window == parent || ev.xdestroywindow.window == dialog))
      break;
  }

  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
