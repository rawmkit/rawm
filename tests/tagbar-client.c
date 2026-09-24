#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdint.h>
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

  for (i = 0; i < count; ++i)
  {
    XWindowAttributes attr;

    if (!XGetWindowAttributes(dpy, children[i], &attr))
      continue;
    if (attr.map_state == IsViewable && attr.width == screen_width &&
        attr.height == 24)
    {
      bar = children[i];
      break;
    }
  }

  if (children)
    XFree(children);

  return bar;
}

static Window
create_client(Display *dpy, Window root, const char *name)
{
  XClassHint hint;
  Window win;

  win = XCreateSimpleWindow(dpy, root, 20, 40, 220, 140, 0, 0, 0);
  XStoreName(dpy, win, name);
  hint.res_name = (char *)name;
  hint.res_class = "RawmTagBar";
  XSetClassHint(dpy, win, &hint);
  XMapWindow(dpy, win);
  XFlush(dpy);
  pause_for_wm();
  return win;
}

static int
root_x(Display *dpy, Window root, Window win)
{
  Window child;
  int x, y;

  if (!XTranslateCoordinates(dpy, win, root, 0, 0, &x, &y, &child))
    return -100000;
  return x;
}

static unsigned long
named_pixel(Display *dpy, const char *name)
{
  XColor exact, screen;
  Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));

  if (!XAllocNamedColor(dpy, cmap, name, &screen, &exact))
  {
    fprintf(stderr, "tagbar-client: cannot allocate color %s\n", name);
    exit(EXIT_FAILURE);
  }

  return screen.pixel;
}

static XImage *
bar_image(Display *dpy, Window bar)
{
  XWindowAttributes attr;

  if (!XGetWindowAttributes(dpy, bar, &attr))
    return NULL;

  return XGetImage(dpy, bar, 0, 0, (unsigned int)attr.width,
                   (unsigned int)attr.height, AllPlanes, ZPixmap);
}

static int
selected_tag_width(Display *dpy, Window bar, unsigned long selected_bg)
{
  XImage *image = bar_image(dpy, bar);
  int x;

  if (!image)
    return -1;

  for (x = 0; x < image->width; ++x)
  {
    if (XGetPixel(image, x, 0) != selected_bg)
      break;
  }

  XDestroyImage(image);
  return x;
}

static int
assert_no_tag_marker(Display *dpy, Window bar, unsigned long selected_bg)
{
  XImage *image = bar_image(dpy, bar);
  unsigned long pixel;

  if (!image)
  {
    fputs("tagbar-client: cannot capture bar\n", stderr);
    return -1;
  }

  pixel = XGetPixel(image, 1, 1);
  XDestroyImage(image);
  if (pixel != selected_bg)
  {
    fprintf(stderr,
            "tagbar-client: tag marker still changes pixel (1,1): %lu != %lu\n",
            pixel, selected_bg);
    return -1;
  }

  return 0;
}

static void
click_bar(Display *dpy, Window root, Window bar, int x)
{
  XWindowAttributes attr;
  XEvent event;

  memset(&event, 0, sizeof(event));
  XGetWindowAttributes(dpy, bar, &attr);
  event.xbutton.type = ButtonPress;
  event.xbutton.display = dpy;
  event.xbutton.window = bar;
  event.xbutton.root = root;
  event.xbutton.time = CurrentTime;
  event.xbutton.x = x;
  event.xbutton.y = attr.height / 2;
  event.xbutton.x_root = x;
  event.xbutton.y_root = attr.y + attr.height / 2;
  event.xbutton.button = Button1;
  event.xbutton.same_screen = True;
  XSendEvent(dpy, bar, False, ButtonPressMask, &event);
  XFlush(dpy);
  pause_for_wm();
}

int
main(int argc, char **argv)
{
  Display *dpy;
  Window root, bar = None, first, third;
  unsigned long selected_bg;
  int width, tries;
  int occupied;

  if (argc != 2 || (strcmp(argv[1], "occupied") && strcmp(argv[1], "all")))
  {
    fprintf(stderr, "usage: %s occupied|all\n", argv[0]);
    return EXIT_FAILURE;
  }
  occupied = !strcmp(argv[1], "occupied");

  dpy = XOpenDisplay(NULL);
  if (!dpy)
  {
    fputs("tagbar-client: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }

  root = DefaultRootWindow(dpy);
  first = create_client(dpy, root, "rawm-tagbar-one");
  third = create_client(dpy, root, "rawm-tagbar-three");

  for (tries = 0; tries < 50 && bar == None; ++tries)
  {
    bar = find_bar(dpy, root);
    if (bar == None)
      pause_for_wm();
  }
  if (bar == None)
  {
    fputs("tagbar-client: cannot find rawm bar window\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  selected_bg = named_pixel(dpy, "#304050");
  if (assert_no_tag_marker(dpy, bar, selected_bg) < 0)
  {
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  width = selected_tag_width(dpy, bar, selected_bg);
  if (width <= 1)
  {
    fputs("tagbar-client: cannot determine selected tag width\n", stderr);
    XCloseDisplay(dpy);
    return EXIT_FAILURE;
  }

  click_bar(dpy, root, bar, width + 2);

  if (occupied)
  {
    if (root_x(dpy, root, first) >= 0 || root_x(dpy, root, third) < 0)
    {
      fputs("tagbar-client: occupied mode click did not select next visible tag\n",
            stderr);
      XCloseDisplay(dpy);
      return EXIT_FAILURE;
    }
  }
  else
  {
    if (root_x(dpy, root, first) >= 0 || root_x(dpy, root, third) >= 0)
    {
      fputs("tagbar-client: all mode click skipped vacant tag\n", stderr);
      XCloseDisplay(dpy);
      return EXIT_FAILURE;
    }
  }

  XDestroyWindow(dpy, first);
  XDestroyWindow(dpy, third);
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
