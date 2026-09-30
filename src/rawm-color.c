/* See LICENSE file for copyright and license details. */

#include "rawm-color.h"

#include <limits.h>
#include <stdint.h>

static unsigned long
scale_component(uint16_t value, unsigned short mask)
{
  uint64_t scaled;

  if (!mask)
    return 0;

  scaled = (uint64_t)value * (uint64_t)mask + (UINT16_MAX / 2u);
  return (unsigned long)(scaled / UINT16_MAX);
}

bool
rawm_color_parse(Display *display, Colormap colormap, const char *spec,
                 RawmColor *color)
{
  XColor parsed;

  if (!display || colormap == None || !spec || !color)
    return false;
  if (!XParseColor(display, colormap, spec, &parsed))
    return false;

  color->red = parsed.red;
  color->green = parsed.green;
  color->blue = parsed.blue;
  return true;
}

unsigned long
rawm_color_pack_direct(const XRenderDirectFormat *format,
                       const RawmColor *color)
{
  unsigned long pixel = 0;

  if (!format || !color)
    return 0;

  pixel |= scale_component(color->red, (unsigned short)format->redMask)
           << format->red;
  pixel |= scale_component(color->green, (unsigned short)format->greenMask)
           << format->green;
  pixel |= scale_component(color->blue, (unsigned short)format->blueMask)
           << format->blue;

  if (format->alphaMask)
    pixel |= (unsigned long)(unsigned short)format->alphaMask << format->alpha;

  return pixel;
}

bool
rawm_color_resolve(Display *display, Visual *visual, Colormap colormap,
                   const RawmColor *color, RawmColorPixel *resolved)
{
  XRenderPictFormat *format;
  XColor allocated;

  if (!display || !visual || !color || !resolved)
    return false;

  format = XRenderFindVisualFormat(display, visual);
  if (visual->class == TrueColor && format && format->type == PictTypeDirect)
  {
    resolved->pixel = rawm_color_pack_direct(&format->direct, color);
    resolved->allocated = false;
    return true;
  }

  if (colormap == None)
    return false;

  allocated.red = color->red;
  allocated.green = color->green;
  allocated.blue = color->blue;
  allocated.flags = DoRed | DoGreen | DoBlue;
  if (!XAllocColor(display, colormap, &allocated))
    return false;

  resolved->pixel = allocated.pixel;
  resolved->allocated = visual->class == DirectColor ||
                        visual->class == GrayScale ||
                        visual->class == PseudoColor;
  return true;
}
