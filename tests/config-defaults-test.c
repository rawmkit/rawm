#include "rawm-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
  size_t i;
#define SAME(field) do { if ((a)->field != (b)->field) fail(#field " differs"); } while (0)

  for (i = 0; i < RAWM_COLOR_SCHEMES; ++i)
    expect_string("color differs", a->colors[i], b->colors[i]);

  SAME(border_width); SAME(snap); SAME(opacity);
  SAME(dialog_center); SAME(size_hints); SAME(floating_placement);
  SAME(cascade_step); SAME(master_factor); SAME(master_count);
  SAME(gap_inner); SAME(gap_outer); SAME(smart_gaps);


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
  char empty_path[] = "/tmp/rawm-config-defaults.XXXXXX";
  RawmConfig defaults, shipped;
  int fd;

  if (argc != 2)
    fail("expected path to canonical rawm.conf");

  fd = mkstemp(empty_path);
  if (fd < 0)
    fail("cannot create empty configuration layer");
  if (close(fd) < 0) {
    unlink(empty_path);
    fail("cannot close empty configuration layer");
  }

  rawm_config_init(&defaults);
  rawm_config_init(&shipped);

  /* Resolve references in compiled defaults without changing their policy. */
  if (rawm_config_load(&defaults, empty_path, stderr) < 0) {
    unlink(empty_path);
    rawm_config_destroy(&shipped);
    rawm_config_destroy(&defaults);
    fail("built-in defaults did not resolve");
  }
  unlink(empty_path);

  if (rawm_config_load(&shipped, argv[1], stderr) < 0) {
    rawm_config_destroy(&shipped);
    rawm_config_destroy(&defaults);
    fail("canonical rawm.conf did not load");
  }
  compare_config(&defaults, &shipped);
  rawm_config_destroy(&shipped);
  rawm_config_destroy(&defaults);
  return EXIT_SUCCESS;
}
