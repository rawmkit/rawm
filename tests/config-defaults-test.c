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
expect_command(const RawmConfig *cfg, const char *name,
               const char *const *argv, size_t argc)
{
  const RawmCommand *cmd = rawm_config_find_command(cfg, name);
  size_t i;

  if (!cmd)
    fail("required default command is missing");
  if (cmd->argc != argc)
    fail("required default command argc differs");
  for (i = 0; i < argc; ++i)
    if (strcmp(cmd->argv[i], argv[i]))
      fail("required default command argv differs");
}

static void
expect_spawn_key(const RawmConfig *cfg, unsigned int modifiers,
                 const char *key, const char *command)
{
  size_t i;

  for (i = 0; i < cfg->key_count; ++i)
    if (cfg->keys[i].modifiers == modifiers &&
        !strcmp(cfg->keys[i].key, key) &&
        cfg->keys[i].binding.action == RAWM_ACTION_SPAWN &&
        cfg->keys[i].binding.command &&
        !strcmp(cfg->keys[i].binding.command, command))
      return;

  fail("required default spawn binding is missing");
}

static void
check_desktop_defaults(const RawmConfig *cfg)
{
  static const char *const launcher[] = { "rawmenu-run" };
  static const char *const tag_rename[] = {
    "rawmenu", "-M", "input", "-p", "tag"
  };
  static const char *const window_select[] = {
    "rawmctl", "window", "select"
  };
  static const char *const lock[] = { "rawlock" };
  static const char *const clipboard_history[] = { "rawclip", "menu" };

  expect_command(cfg, "launcher", launcher,
                 sizeof(launcher) / sizeof(launcher[0]));
  expect_command(cfg, "tag_rename", tag_rename,
                 sizeof(tag_rename) / sizeof(tag_rename[0]));
  expect_command(cfg, "window_select", window_select,
                 sizeof(window_select) / sizeof(window_select[0]));
  expect_command(cfg, "lock", lock, sizeof(lock) / sizeof(lock[0]));
  expect_command(cfg, "clipboard_history", clipboard_history,
                 sizeof(clipboard_history) / sizeof(clipboard_history[0]));
  expect_spawn_key(cfg, RAWM_MOD_4, "r", "launcher");
  expect_spawn_key(cfg, RAWM_MOD_4, "w", "window_select");
  expect_spawn_key(cfg, RAWM_MOD_4, "v", "clipboard_history");
  expect_spawn_key(cfg, RAWM_MOD_CONTROL | RAWM_MOD_1,
                   "Delete", "lock");
}

static void
compare_binding(const RawmBinding *a, const RawmBinding *b)
{
  if (a->action != b->action ||
      a->has_layout != b->has_layout ||
      a->layout != b->layout ||
      a->has_number != b->has_number ||
      a->number != b->number ||
      a->has_factor != b->has_factor ||
      a->factor_absolute != b->factor_absolute ||
      a->factor != b->factor ||
      a->has_tag != b->has_tag ||
      a->all_tags != b->all_tags ||
      a->tag != b->tag ||
      a->restart != b->restart)
    fail("binding scalar policy differs");

  expect_string("binding command differs", a->command, b->command);
}

static void
compare_config(const RawmConfig *a, const RawmConfig *b)
{
  size_t i, j;

#define SAME(field) \
  do { \
    if ((a)->field != (b)->field) \
      fail(#field " differs"); \
  } while (0)

  expect_string("font differs", a->font, b->font);

  for (i = 0; i < RAWM_COLOR_SCHEMES; ++i)
    for (j = 0; j < RAWM_COLOR_COMPONENTS; ++j)
      expect_string("color differs", a->colors[i][j], b->colors[i][j]);

  SAME(border_width);
  SAME(snap);
  SAME(opacity);
  SAME(show_bar);
  SAME(top_bar);
  SAME(bar_height);
  SAME(bar_padding);
  SAME(bar_item_spacing);
  SAME(bar_section_spacing);
  SAME(bar_cell_margin);
  SAME(bar_indicator_size);
  SAME(tag_visibility);

  SAME(show_osd);
  SAME(osd_top);
  SAME(osd_geometry);
  SAME(osd_duration_ms);
  SAME(osd_padding);
  SAME(osd_offset);
  SAME(osd_min_width);

  SAME(show_systray);
  SAME(systray_spacing);
  SAME(systray_icon_size);

  SAME(status_enabled);
  SAME(status_protocol);
  expect_string("status separator differs",
                a->status_separator, b->status_separator);

  SAME(dialog_center);
  SAME(size_hints);
  SAME(floating_placement);
  SAME(cascade_step);

  SAME(master_factor);
  SAME(master_count);
  SAME(gap_inner);
  SAME(gap_outer);
  SAME(smart_gaps);

  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
    expect_string("layout symbol differs",
                  a->layout_symbols[i], b->layout_symbols[i]);

  SAME(tag_count);
  for (i = 0; i < a->tag_count; ++i)
  {
    expect_string("tag name differs", a->tags[i].name, b->tags[i].name);
    if (a->tags[i].layout != b->tags[i].layout)
      fail("tag layout differs");
  }

  SAME(monitor_tag_count);
  for (i = 0; i < a->monitor_tag_count; ++i)
  {
    if (a->monitor_tags[i].monitor != b->monitor_tags[i].monitor ||
        a->monitor_tags[i].tag_count != b->monitor_tags[i].tag_count)
      fail("monitor tag policy differs");

    for (j = 0; j < a->monitor_tags[i].tag_count; ++j)
    {
      expect_string("monitor tag name differs",
                    a->monitor_tags[i].tags[j].name,
                    b->monitor_tags[i].tags[j].name);
      if (a->monitor_tags[i].tags[j].layout !=
          b->monitor_tags[i].tags[j].layout)
        fail("monitor tag layout differs");
    }
  }

  SAME(rule_count);
  for (i = 0; i < a->rule_count; ++i)
  {
    expect_string("rule class differs",
                  a->rules[i].class_name, b->rules[i].class_name);
    expect_string("rule instance differs",
                  a->rules[i].instance, b->rules[i].instance);
    expect_string("rule title differs",
                  a->rules[i].title, b->rules[i].title);
    expect_string("rule role differs",
                  a->rules[i].role, b->rules[i].role);

    if (a->rules[i].tags != b->rules[i].tags ||
        a->rules[i].floating != b->rules[i].floating ||
        a->rules[i].has_center != b->rules[i].has_center ||
        a->rules[i].center != b->rules[i].center ||
        a->rules[i].has_fullscreen != b->rules[i].has_fullscreen ||
        a->rules[i].fullscreen != b->rules[i].fullscreen ||
        a->rules[i].has_attention != b->rules[i].has_attention ||
        a->rules[i].attention != b->rules[i].attention ||
        a->rules[i].monitor != b->rules[i].monitor)
      fail("rule scalar policy differs");
  }

  SAME(command_count);
  for (i = 0; i < a->command_count; ++i)
  {
    expect_string("command name differs",
                  a->commands[i].name, b->commands[i].name);
    if (a->commands[i].argc != b->commands[i].argc)
      fail("command argc differs");

    for (j = 0; j < a->commands[i].argc; ++j)
      expect_string("command argument differs",
                    a->commands[i].argv[j], b->commands[i].argv[j]);
  }

  SAME(key_count);
  for (i = 0; i < a->key_count; ++i)
  {
    if (a->keys[i].modifiers != b->keys[i].modifiers)
      fail("key modifiers differ");
    expect_string("key name differs", a->keys[i].key, b->keys[i].key);
    compare_binding(&a->keys[i].binding, &b->keys[i].binding);
  }

  SAME(button_count);
  for (i = 0; i < a->button_count; ++i)
  {
    if (a->buttons[i].click != b->buttons[i].click ||
        a->buttons[i].modifiers != b->buttons[i].modifiers ||
        a->buttons[i].button != b->buttons[i].button)
      fail("button scalar policy differs");

    expect_string("button status id differs",
                  a->buttons[i].status_id, b->buttons[i].status_id);
    compare_binding(&a->buttons[i].binding, &b->buttons[i].binding);
  }

#undef SAME
}

int
main(int argc, char **argv)
{
  char empty_path[] = "/tmp/rawm-config-defaults.XXXXXX";
  RawmConfig built_in;
  RawmConfig installed;
  int fd;

  if (argc != 2)
    fail("usage: config-defaults-test CONFIG");

  fd = mkstemp(empty_path);
  if (fd < 0)
    fail("cannot create empty configuration layer");
  if (close(fd) < 0)
  {
    unlink(empty_path);
    fail("cannot close empty configuration layer");
  }

  rawm_config_init(&built_in);
  rawm_config_init(&installed);

  /*
   * Loading an empty explicit layer resolves references in the compiled
   * defaults without changing their policy.
   */
  if (rawm_config_load(&built_in, empty_path, stderr) < 0)
  {
    unlink(empty_path);
    rawm_config_destroy(&installed);
    rawm_config_destroy(&built_in);
    fail("built-in defaults did not resolve");
  }
  unlink(empty_path);

  if (rawm_config_load(&installed, argv[1], stderr) < 0)
  {
    rawm_config_destroy(&installed);
    rawm_config_destroy(&built_in);
    fail("installed configuration did not validate");
  }

  compare_config(&built_in, &installed);
  check_desktop_defaults(&built_in);

  rawm_config_destroy(&installed);
  rawm_config_destroy(&built_in);
  return EXIT_SUCCESS;
}
