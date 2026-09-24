#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
  Display *display;
  XineramaScreenInfo *screens;
  int count = 0;

  display = XOpenDisplay(NULL);
  if (!display)
  {
    fputs("xinerama-probe: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }

  if (!XineramaIsActive(display))
  {
    XCloseDisplay(display);
    return 77;
  }

  screens = XineramaQueryScreens(display, &count);
  if (screens)
    XFree(screens);
  XCloseDisplay(display);

  if (count < 2)
    return 77;

  printf("%d\n", count);
  return EXIT_SUCCESS;
}
