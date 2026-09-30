#include "rawm-color.h"

#include <stdio.h>
#include <stdlib.h>

#include <X11/Xutil.h>

static unsigned long
component_mask(unsigned short mask, short shift)
{
  return (unsigned long)mask << shift;
}

int
main(void)
{
  const RawmColor yellow = { UINT16_MAX, UINT16_MAX, 0 };
  Display *display;
  XVisualInfo template = {0};
  XVisualInfo *visuals;
  int count = 0;
  int i;

  display = XOpenDisplay(NULL);
  if (!display)
  {
    fputs("color-visual-test: cannot open display\n", stderr);
    return EXIT_FAILURE;
  }

  template.screen = DefaultScreen(display);
  visuals = XGetVisualInfo(display, VisualScreenMask, &template, &count);
  if (!visuals)
  {
    XCloseDisplay(display);
    fputs("color-visual-test: no visuals\n", stderr);
    return EXIT_FAILURE;
  }

  for (i = 0; i < count; ++i)
  {
    XRenderPictFormat *format;
    RawmColorPixel resolved;
    unsigned long alpha;

    if (visuals[i].class != TrueColor)
      continue;

    format = XRenderFindVisualFormat(display, visuals[i].visual);
    if (!format || format->type != PictTypeDirect || !format->direct.alphaMask)
      continue;

    if (!rawm_color_resolve(display, visuals[i].visual, None,
                            &yellow, &resolved))
    {
      fputs("color-visual-test: cannot resolve ARGB border color\n", stderr);
      XFree(visuals);
      XCloseDisplay(display);
      return EXIT_FAILURE;
    }

    alpha = component_mask(format->direct.alphaMask, format->direct.alpha);
    if ((resolved.pixel & alpha) != alpha)
    {
      fprintf(stderr,
              "color-visual-test: ARGB visual 0x%lx resolved transparent "
              "alpha bits: pixel=0x%lx alpha=0x%lx\n",
              visuals[i].visualid, resolved.pixel, alpha);
      XFree(visuals);
      XCloseDisplay(display);
      return EXIT_FAILURE;
    }
    if (resolved.allocated)
    {
      fputs("color-visual-test: direct visual unexpectedly allocated color\n",
            stderr);
      XFree(visuals);
      XCloseDisplay(display);
      return EXIT_FAILURE;
    }

    printf("color visual tests: ok (visual=0x%lx pixel=0x%lx)\n",
           visuals[i].visualid, resolved.pixel);
    XFree(visuals);
    XCloseDisplay(display);
    return EXIT_SUCCESS;
  }

  XFree(visuals);
  XCloseDisplay(display);
  return 77;
}
