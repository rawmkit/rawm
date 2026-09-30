#ifndef RAWM_COLOR_H
#define RAWM_COLOR_H

#include <stdbool.h>
#include <stdint.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>

typedef struct {
  uint16_t red;
  uint16_t green;
  uint16_t blue;
} RawmColor;

typedef struct {
  unsigned long pixel;
  bool allocated;
} RawmColorPixel;

bool rawm_color_parse(Display *display, Colormap colormap, const char *spec,
                      RawmColor *color);
unsigned long rawm_color_pack_direct(const XRenderDirectFormat *format,
                                     const RawmColor *color);
bool rawm_color_resolve(Display *display, Visual *visual, Colormap colormap,
                        const RawmColor *color, RawmColorPixel *resolved);

#endif /* RAWM_COLOR_H */
