#include "rawm-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
fail(const char *what)
{
  fprintf(stderr, "config defaults test failed: %s\n", what);
  exit(EXIT_FAILURE);
}

static void
expect_string(const char *what, const char *a, const char *b)
{
  if ((!a && b) || (a && !b) || (a && b && strcmp(a, b)))
    fail(what);
}

static void
compare_config(const RawmConfig *a, const RawmConfig *b)
{
  size_t i, j;
#define SAME(field) do { if ((a)->field != (b)->field) fail(#field " differs"); } while (0)

  expect_string("font differs", a->font, b->font);
  for (i = 0; i < RAWM_COLOR_SCHEMES; ++i)
    for (j = 0; j < RAWM_COLOR_COMPONENTS; ++j)
      expect_string("color differs", a->colors[i][j], b->colors[i][j]);

  SAME(border_width); SAME(snap); SAME(opacity);
  SAME(show_osd); SAME(osd_top); SAME(osd_geometry); SAME(osd_duration_ms);
  SAME(osd_padding); SAME(osd_offset); SAME(osd_min_width);
  SAME(dialog_center); SAME(size_hints); SAME(floating_placement);
  SAME(cascade_step); SAME(master_factor); SAME(master_count);
  SAME(gap_inner); SAME(gap_outer); SAME(smart_gaps);

  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
    expect_string("layout symbol differs", a->layout_symbols[i], b->layout_symbols[i]);

  SAME(tag_count);
  for (i = 0; i < a->tag_count; ++i) {
    expect_string("tag name differs", a->tags[i].name, b->tags[i].name);
    if (a->tags[i].layout != b->tags[i].layout)
      fail("tag layout differs");
  }

  SAME(monitor_tag_count);
  SAME(rule_count);
  SAME(pointer_binding_count);
  for (i = 0; i < a->pointer_binding_count; ++i) {
    if (a->pointer_bindings[i].modifiers != b->pointer_bindings[i].modifiers ||
        a->pointer_bindings[i].button != b->pointer_bindings[i].button ||
        a->pointer_bindings[i].action != b->pointer_bindings[i].action)
      fail("pointer binding differs");
  }
#undef SAME
}

int
main(int argc, char **argv)
{
  RawmConfig defaults, shipped;

  if (argc != 2)
    fail("expected path to canonical rawm.conf");
  rawm_config_init(&defaults);
  rawm_config_init(&shipped);
  if (rawm_config_load(&shipped, argv[1], stderr) < 0)
    fail("canonical rawm.conf did not load");
  compare_config(&defaults, &shipped);
  rawm_config_destroy(&shipped);
  rawm_config_destroy(&defaults);
  return EXIT_SUCCESS;
}
