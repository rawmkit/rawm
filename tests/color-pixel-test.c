#include "rawm-color.h"

#include <stdio.h>
#include <stdlib.h>

static void
expect_pixel(const char *name, unsigned long actual, unsigned long expected)
{
  if (actual == expected)
    return;

  fprintf(stderr,
          "color-pixel-test: %s: got 0x%lx, expected 0x%lx\n",
          name, actual, expected);
  exit(EXIT_FAILURE);
}

int
main(void)
{
  const RawmColor yellow = { UINT16_MAX, UINT16_MAX, 0 };
  const RawmColor gray = { 0x8000u, 0x8000u, 0x8000u };
  const XRenderDirectFormat rgb24 = {
    .red = 16, .redMask = 0xff,
    .green = 8, .greenMask = 0xff,
    .blue = 0, .blueMask = 0xff,
    .alpha = 0, .alphaMask = 0
  };
  const XRenderDirectFormat argb32 = {
    .red = 16, .redMask = 0xff,
    .green = 8, .greenMask = 0xff,
    .blue = 0, .blueMask = 0xff,
    .alpha = 24, .alphaMask = 0xff
  };
  const XRenderDirectFormat a16 = {
    .red = 0, .redMask = 0,
    .green = 0, .greenMask = 0,
    .blue = 0, .blueMask = 0,
    .alpha = 16, .alphaMask = (short)0xffff
  };

  expect_pixel("24-bit RGB yellow",
               rawm_color_pack_direct(&rgb24, &yellow), 0x00ffff00ul);
  expect_pixel("32-bit ARGB yellow",
               rawm_color_pack_direct(&argb32, &yellow), 0xffffff00ul);
  expect_pixel("32-bit ARGB midpoint scaling",
               rawm_color_pack_direct(&argb32, &gray), 0xff808080ul);
  expect_pixel("signed 16-bit alpha mask",
               rawm_color_pack_direct(&a16, &yellow), 0xffff0000ul);

  puts("color pixel tests: ok");
  return EXIT_SUCCESS;
}
