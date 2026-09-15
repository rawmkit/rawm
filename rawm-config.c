#include "rawm-config.h"

#include <errno.h>
#include <libconfig.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

enum { COLOR_BORDER, COLOR_FOREGROUND, COLOR_BACKGROUND };

static void *
xcalloc(size_t n, size_t size)
{
  void *p = calloc(n, size);
  if (!p) {
    fputs("rawm: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return p;
}

static void *
xrealloc(void *p, size_t size)
{
  void *q = realloc(p, size);
  if (!q && size) {
    fputs("rawm: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return q;
}

static char *
xstrdup(const char *s)
{
  char *p = strdup(s ? s : "");
  if (!p) {
    fputs("rawm: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return p;
}

static void
replace_string(char **dst, const char *src)
{
  free(*dst);
  *dst = xstrdup(src);
}

static void
free_tags(RawmTagConfig *tags, size_t count)
{
  size_t i;
  for (i = 0; i < count; ++i)
    free(tags[i].name);
  free(tags);
}

static void
free_monitor_tags(RawmMonitorTags *mons, size_t count)
{
  size_t i;
  for (i = 0; i < count; ++i)
    free_tags(mons[i].tags, mons[i].tag_count);
  free(mons);
}

static void
free_rules(RawmRule *rules, size_t count)
{
  size_t i;
  for (i = 0; i < count; ++i) {
    free(rules[i].class_name);
    free(rules[i].instance);
    free(rules[i].title);
    free(rules[i].role);
  }
  free(rules);
}

static void
free_command(RawmCommand *cmd)
{
  size_t i;
  free(cmd->name);
  for (i = 0; i < cmd->argc; ++i)
    free(cmd->argv[i]);
  free(cmd->argv);
  memset(cmd, 0, sizeof(*cmd));
}

static void
free_commands(RawmCommand *cmds, size_t count)
{
  size_t i;
  for (i = 0; i < count; ++i)
    free_command(&cmds[i]);
  free(cmds);
}

static void
free_binding(RawmBinding *binding)
{
  free(binding->command);
  memset(binding, 0, sizeof(*binding));
}

static void
free_keys(RawmKeyBinding *keys, size_t count)
{
  size_t i;
  for (i = 0; i < count; ++i) {
    free(keys[i].key);
    free_binding(&keys[i].binding);
  }
  free(keys);
}

static void
free_buttons(RawmButtonBinding *buttons, size_t count)
{
  size_t i;
  for (i = 0; i < count; ++i)
    free_binding(&buttons[i].binding);
  free(buttons);
}

const char *
rawm_layout_name(RawmLayoutId layout)
{
  static const char *names[RAWM_LAYOUT_COUNT] = {
    "tile", "floating", "monocle", "bstack", "bstack_horizontal", "grid"
  };
  return layout >= 0 && layout < RAWM_LAYOUT_COUNT ? names[layout] : NULL;
}

static int
parse_layout_name(const char *name, RawmLayoutId *layout)
{
  int i;
  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i) {
    if (!strcmp(name, rawm_layout_name((RawmLayoutId)i))) {
      *layout = (RawmLayoutId)i;
      return 0;
    }
  }
  return -1;
}

static void
append_command(RawmConfig *cfg, const char *name, const char *const *argv,
               size_t argc)
{
  RawmCommand *cmd;
  size_t i;

  cfg->commands = xrealloc(cfg->commands,
                           (cfg->command_count + 1) * sizeof(*cfg->commands));
  cmd = &cfg->commands[cfg->command_count++];
  memset(cmd, 0, sizeof(*cmd));
  cmd->name = xstrdup(name);
  cmd->argv = xcalloc(argc + 1, sizeof(*cmd->argv));
  cmd->argc = argc;
  for (i = 0; i < argc; ++i)
    cmd->argv[i] = xstrdup(argv[i]);
}

static void
append_key(RawmConfig *cfg, unsigned int modifiers, const char *key,
           RawmAction action)
{
  RawmKeyBinding *binding;
  cfg->keys = xrealloc(cfg->keys, (cfg->key_count + 1) * sizeof(*cfg->keys));
  binding = &cfg->keys[cfg->key_count++];
  memset(binding, 0, sizeof(*binding));
  binding->modifiers = modifiers;
  binding->key = xstrdup(key);
  binding->binding.action = action;
}

static void
append_button(RawmConfig *cfg, RawmClick click, unsigned int modifiers,
              unsigned int button, RawmAction action)
{
  RawmButtonBinding *binding;
  cfg->buttons = xrealloc(cfg->buttons,
      (cfg->button_count + 1) * sizeof(*cfg->buttons));
  binding = &cfg->buttons[cfg->button_count++];
  memset(binding, 0, sizeof(*binding));
  binding->click = click;
  binding->modifiers = modifiers;
  binding->button = button;
  binding->binding.action = action;
}

static void
set_key_number(RawmKeyBinding *key, int number)
{
  key->binding.has_number = true;
  key->binding.number = number;
}

static void
set_key_factor(RawmKeyBinding *key, double factor)
{
  key->binding.has_factor = true;
  key->binding.factor = factor;
}

static void
set_key_layout(RawmKeyBinding *key, RawmLayoutId layout)
{
  key->binding.has_layout = true;
  key->binding.layout = layout;
}

static void
set_key_tag(RawmKeyBinding *key, unsigned int tag, bool all)
{
  key->binding.has_tag = true;
  key->binding.tag = tag;
  key->binding.all_tags = all;
}

void
rawm_config_init(RawmConfig *cfg)
{
  static const char *const terminal[] = { "st" };
  static const char *const launcher[] = { "dmenu_run" };
  static const char *scheme_defaults[RAWM_COLOR_SCHEMES][3] = {
    { "#cccccc", "#000000", "#cccccc" },
    { "#0066ff", "#ffffff", "#0066ff" },
    { "#0066ff", "#0066ff", "#ffffff" },
    { "#ff0000", "#ffffff", "#ff0000" }
  };
  static const char *symbols[RAWM_LAYOUT_COUNT] = {
    "[]=", "<1/1>", "[1/1]", "TTT", "===", "###"
  };
  static const char *tagkeys[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
  size_t i, j;
  unsigned int mod = RAWM_MOD_4;

  memset(cfg, 0, sizeof(*cfg));
  cfg->font = xstrdup("monospace:size=9");
  cfg->border_width = 2;
  cfg->snap = 32;
  cfg->opacity = 1.0;
  cfg->show_bar = true;
  cfg->top_bar = false;
  cfg->bar_height = 0;
  cfg->show_systray = true;
  cfg->systray_spacing = 2;
  cfg->autocenter_dialogs = false;
  cfg->resize_hints = false;
  cfg->master_factor = 0.55;
  cfg->master_count = 1;

  for (i = 0; i < RAWM_COLOR_SCHEMES; ++i)
    for (j = 0; j < 3; ++j)
      cfg->colors[i][j] = xstrdup(scheme_defaults[i][j]);
  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
    cfg->layout_symbols[i] = xstrdup(symbols[i]);

  cfg->tag_count = 9;
  cfg->tags = xcalloc(cfg->tag_count, sizeof(*cfg->tags));
  for (i = 0; i < cfg->tag_count; ++i) {
    char name[4];
    snprintf(name, sizeof(name), "%u", (unsigned int)i + 1);
    cfg->tags[i].name = xstrdup(name);
    cfg->tags[i].layout = RAWM_LAYOUT_TILE;
  }

  append_command(cfg, "terminal", terminal, 1);
  append_command(cfg, "launcher", launcher, 1);

  append_key(cfg, mod, "r", RAWM_ACTION_SPAWN);
  cfg->keys[cfg->key_count - 1].binding.command = xstrdup("launcher");
  append_key(cfg, mod | RAWM_MOD_SHIFT, "Return", RAWM_ACTION_SPAWN);
  cfg->keys[cfg->key_count - 1].binding.command = xstrdup("terminal");
  append_key(cfg, mod, "b", RAWM_ACTION_TOGGLE_BAR);
  append_key(cfg, mod, "n", RAWM_ACTION_RENAME_TAG);
  append_key(cfg, mod, "j", RAWM_ACTION_FOCUS_STACK);
  set_key_number(&cfg->keys[cfg->key_count - 1], 1);
  append_key(cfg, mod, "k", RAWM_ACTION_FOCUS_STACK);
  set_key_number(&cfg->keys[cfg->key_count - 1], -1);
  append_key(cfg, mod, "i", RAWM_ACTION_INC_MASTER);
  set_key_number(&cfg->keys[cfg->key_count - 1], 1);
  append_key(cfg, mod, "d", RAWM_ACTION_INC_MASTER);
  set_key_number(&cfg->keys[cfg->key_count - 1], -1);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "h", RAWM_ACTION_SET_MASTER_FACTOR);
  set_key_factor(&cfg->keys[cfg->key_count - 1], -0.01);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "l", RAWM_ACTION_SET_MASTER_FACTOR);
  set_key_factor(&cfg->keys[cfg->key_count - 1], 0.01);
  append_key(cfg, mod, "Return", RAWM_ACTION_ZOOM);
  append_key(cfg, mod, "Tab", RAWM_ACTION_VIEW);
  append_key(cfg, mod, "c", RAWM_ACTION_KILL_CLIENT);

  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i) {
    static const char *layout_keys[RAWM_LAYOUT_COUNT] = { "t", "f", "m", "s", "h", "g" };
    append_key(cfg, mod, layout_keys[i], RAWM_ACTION_SET_LAYOUT);
    set_key_layout(&cfg->keys[cfg->key_count - 1], (RawmLayoutId)i);
  }
  append_key(cfg, mod, "space", RAWM_ACTION_SET_LAYOUT);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "space", RAWM_ACTION_TOGGLE_FLOATING);
  append_key(cfg, mod, "0", RAWM_ACTION_VIEW);
  set_key_tag(&cfg->keys[cfg->key_count - 1], 0, true);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "0", RAWM_ACTION_TAG);
  set_key_tag(&cfg->keys[cfg->key_count - 1], 0, true);
  append_key(cfg, mod, "o", RAWM_ACTION_WINDOW_VIEW);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "f", RAWM_ACTION_TOGGLE_FULLSCREEN);
  append_key(cfg, mod, "comma", RAWM_ACTION_FOCUS_MONITOR);
  set_key_number(&cfg->keys[cfg->key_count - 1], -1);
  append_key(cfg, mod, "period", RAWM_ACTION_FOCUS_MONITOR);
  set_key_number(&cfg->keys[cfg->key_count - 1], 1);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "comma", RAWM_ACTION_TAG_MONITOR);
  set_key_number(&cfg->keys[cfg->key_count - 1], -1);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "period", RAWM_ACTION_TAG_MONITOR);
  set_key_number(&cfg->keys[cfg->key_count - 1], 1);
  append_key(cfg, mod | RAWM_MOD_SHIFT, "q", RAWM_ACTION_QUIT);
  append_key(cfg, mod | RAWM_MOD_CONTROL | RAWM_MOD_SHIFT, "q", RAWM_ACTION_QUIT);
  cfg->keys[cfg->key_count - 1].binding.restart = true;

  for (i = 0; i < 9; ++i) {
    append_key(cfg, mod, tagkeys[i], RAWM_ACTION_VIEW);
    set_key_tag(&cfg->keys[cfg->key_count - 1], (unsigned int)i + 1, false);
    append_key(cfg, mod | RAWM_MOD_CONTROL, tagkeys[i], RAWM_ACTION_TOGGLE_VIEW);
    set_key_tag(&cfg->keys[cfg->key_count - 1], (unsigned int)i + 1, false);
    append_key(cfg, mod | RAWM_MOD_SHIFT, tagkeys[i], RAWM_ACTION_TAG);
    set_key_tag(&cfg->keys[cfg->key_count - 1], (unsigned int)i + 1, false);
    append_key(cfg, mod | RAWM_MOD_CONTROL | RAWM_MOD_SHIFT, tagkeys[i], RAWM_ACTION_TOGGLE_TAG);
    set_key_tag(&cfg->keys[cfg->key_count - 1], (unsigned int)i + 1, false);
  }

  append_button(cfg, RAWM_CLICK_LAYOUT, 0, 1, RAWM_ACTION_SET_LAYOUT);
  append_button(cfg, RAWM_CLICK_LAYOUT, 0, 3, RAWM_ACTION_SET_LAYOUT);
  cfg->buttons[cfg->button_count - 1].binding.has_layout = true;
  cfg->buttons[cfg->button_count - 1].binding.layout = RAWM_LAYOUT_MONOCLE;
  append_button(cfg, RAWM_CLICK_STATUS, 0, 2, RAWM_ACTION_SPAWN);
  cfg->buttons[cfg->button_count - 1].binding.command = xstrdup("terminal");
  append_button(cfg, RAWM_CLICK_CLIENT, mod, 1, RAWM_ACTION_MOVE);
  append_button(cfg, RAWM_CLICK_CLIENT, mod, 2, RAWM_ACTION_TOGGLE_FLOATING);
  append_button(cfg, RAWM_CLICK_CLIENT, mod, 3, RAWM_ACTION_RESIZE);
  append_button(cfg, RAWM_CLICK_TAG_BAR, 0, 1, RAWM_ACTION_VIEW);
  append_button(cfg, RAWM_CLICK_TAG_BAR, 0, 3, RAWM_ACTION_TOGGLE_VIEW);
  append_button(cfg, RAWM_CLICK_TAG_BAR, mod, 1, RAWM_ACTION_TAG);
  append_button(cfg, RAWM_CLICK_TAG_BAR, mod, 3, RAWM_ACTION_TOGGLE_TAG);
}

void
rawm_config_destroy(RawmConfig *cfg)
{
  size_t i, j;
  free(cfg->font);
  for (i = 0; i < RAWM_COLOR_SCHEMES; ++i)
    for (j = 0; j < RAWM_COLOR_COMPONENTS; ++j)
      free(cfg->colors[i][j]);
  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
    free(cfg->layout_symbols[i]);
  free_tags(cfg->tags, cfg->tag_count);
  free_monitor_tags(cfg->monitor_tags, cfg->monitor_tag_count);
  free_rules(cfg->rules, cfg->rule_count);
  free_commands(cfg->commands, cfg->command_count);
  free_keys(cfg->keys, cfg->key_count);
  free_buttons(cfg->buttons, cfg->button_count);
  memset(cfg, 0, sizeof(*cfg));
}

static int
cfg_error(FILE *err, const config_setting_t *s, const char *fmt,
          const char *detail)
{
  const char *file = s ? config_setting_source_file(s) : NULL;
  unsigned int line = s ? config_setting_source_line(s) : 0;
  fprintf(err, "rawm: configuration error");
  if (file)
    fprintf(err, " in %s", file);
  if (line)
    fprintf(err, ":%u", line);
  fprintf(err, ": ");
  fprintf(err, fmt, detail);
  fputc('\n', err);
  return -1;
}

static int
expect_type(FILE *err, const config_setting_t *s, int type, const char *what)
{
  if (!s || config_setting_type(s) != type)
    return cfg_error(err, s, "%s has the wrong type", what);
  return 0;
}

static int
name_allowed(const char *name, const char *const *allowed, size_t count)
{
  size_t i;
  for (i = 0; i < count; ++i)
    if (!strcmp(name, allowed[i]))
      return 1;
  return 0;
}

static int
validate_members(FILE *err, const config_setting_t *group,
                 const char *const *allowed, size_t count)
{
  int i, n;
  if (expect_type(err, group, CONFIG_TYPE_GROUP, "group") < 0)
    return -1;
  n = config_setting_length(group);
  for (i = 0; i < n; ++i) {
    config_setting_t *member = config_setting_get_elem(group, (unsigned int)i);
    const char *name = config_setting_name(member);
    if (!name_allowed(name, allowed, count))
      return cfg_error(err, member, "unknown setting '%s'", name);
  }
  return 0;
}

static int
read_string(FILE *err, const config_setting_t *group, const char *name,
            char **dst)
{
  config_setting_t *s = config_setting_get_member(group, name);
  if (!s)
    return 0;
  if (config_setting_type(s) != CONFIG_TYPE_STRING)
    return cfg_error(err, s, "%s must be a string", name);
  replace_string(dst, config_setting_get_string(s));
  return 0;
}

static int
read_bool(FILE *err, const config_setting_t *group, const char *name,
          bool *dst)
{
  config_setting_t *s = config_setting_get_member(group, name);
  if (!s)
    return 0;
  if (config_setting_type(s) != CONFIG_TYPE_BOOL)
    return cfg_error(err, s, "%s must be boolean", name);
  *dst = config_setting_get_bool(s) != 0;
  return 0;
}

static int
read_int_range(FILE *err, const config_setting_t *group, const char *name,
               int min, int max, int *dst)
{
  config_setting_t *s = config_setting_get_member(group, name);
  int value;
  if (!s)
    return 0;
  if (config_setting_type(s) != CONFIG_TYPE_INT)
    return cfg_error(err, s, "%s must be an integer", name);
  value = config_setting_get_int(s);
  if (value < min || value > max)
    return cfg_error(err, s, "%s is out of range", name);
  *dst = value;
  return 0;
}

static int
read_uint_range(FILE *err, const config_setting_t *group, const char *name,
                unsigned int min, unsigned int max, unsigned int *dst)
{
  int value;
  int target;
  if (*dst > INT_MAX)
    target = INT_MAX;
  else
    target = (int)*dst;
  if (read_int_range(err, group, name, (int)min, (int)max, &target) < 0)
    return -1;
  value = target;
  *dst = (unsigned int)value;
  return 0;
}

static int
read_double_range(FILE *err, const config_setting_t *group, const char *name,
                  double min, double max, double *dst)
{
  config_setting_t *s = config_setting_get_member(group, name);
  double value;
  if (!s)
    return 0;
  if (config_setting_type(s) == CONFIG_TYPE_FLOAT)
    value = config_setting_get_float(s);
  else if (config_setting_type(s) == CONFIG_TYPE_INT)
    value = config_setting_get_int(s);
  else
    return cfg_error(err, s, "%s must be numeric", name);
  if (value < min || value > max)
    return cfg_error(err, s, "%s is out of range", name);
  *dst = value;
  return 0;
}

static int
parse_color_scheme(FILE *err, RawmConfig *cfg, const config_setting_t *colors,
                   const char *name, size_t index)
{
  static const char *const allowed[] = { "border", "foreground", "background" };
  config_setting_t *group = config_setting_get_member(colors, name);
  if (!group)
    return 0;
  if (validate_members(err, group, allowed, 3) < 0)
    return -1;
  if (read_string(err, group, "border", &cfg->colors[index][COLOR_BORDER]) < 0 ||
      read_string(err, group, "foreground", &cfg->colors[index][COLOR_FOREGROUND]) < 0 ||
      read_string(err, group, "background", &cfg->colors[index][COLOR_BACKGROUND]) < 0)
    return -1;
  return 0;
}

static int
parse_ui(FILE *err, RawmConfig *cfg, const config_setting_t *ui)
{
  static const char *const allowed[] = {
    "font", "border_width", "snap", "opacity", "bar", "systray", "colors"
  };
  static const char *const bar_allowed[] = { "show", "position", "height" };
  static const char *const tray_allowed[] = { "show", "spacing" };
  static const char *const color_allowed[] = { "normal", "selected", "urgent", "error" };
  config_setting_t *group;
  int h;

  if (validate_members(err, ui, allowed, 7) < 0)
    return -1;
  if (read_string(err, ui, "font", &cfg->font) < 0 ||
      read_uint_range(err, ui, "border_width", 0, 64, &cfg->border_width) < 0 ||
      read_uint_range(err, ui, "snap", 0, 4096, &cfg->snap) < 0 ||
      read_double_range(err, ui, "opacity", 0.0, 1.0, &cfg->opacity) < 0)
    return -1;

  group = config_setting_get_member(ui, "bar");
  if (group) {
    config_setting_t *position;
    const char *value;
    if (validate_members(err, group, bar_allowed, 3) < 0 ||
        read_bool(err, group, "show", &cfg->show_bar) < 0)
      return -1;
    h = cfg->bar_height;
    if (read_int_range(err, group, "height", 0, 512, &h) < 0)
      return -1;
    cfg->bar_height = h;
    position = config_setting_get_member(group, "position");
    if (position) {
      if (config_setting_type(position) != CONFIG_TYPE_STRING)
        return cfg_error(err, position, "bar.position must be a string", "bar.position");
      value = config_setting_get_string(position);
      if (!strcmp(value, "top"))
        cfg->top_bar = true;
      else if (!strcmp(value, "bottom"))
        cfg->top_bar = false;
      else
        return cfg_error(err, position, "invalid bar position '%s'", value);
    }
  }

  group = config_setting_get_member(ui, "systray");
  if (group) {
    if (validate_members(err, group, tray_allowed, 2) < 0 ||
        read_bool(err, group, "show", &cfg->show_systray) < 0 ||
        read_uint_range(err, group, "spacing", 0, 64, &cfg->systray_spacing) < 0)
      return -1;
  }

  group = config_setting_get_member(ui, "colors");
  if (group) {
    if (validate_members(err, group, color_allowed, 4) < 0 ||
        parse_color_scheme(err, cfg, group, "normal", 0) < 0 ||
        parse_color_scheme(err, cfg, group, "selected", 1) < 0 ||
        parse_color_scheme(err, cfg, group, "urgent", 2) < 0 ||
        parse_color_scheme(err, cfg, group, "error", 3) < 0)
      return -1;
  }
  return 0;
}

static int
parse_behavior(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  static const char *const allowed[] = { "autocenter_dialogs", "resize_hints" };
  if (validate_members(err, group, allowed, 2) < 0 ||
      read_bool(err, group, "autocenter_dialogs", &cfg->autocenter_dialogs) < 0 ||
      read_bool(err, group, "resize_hints", &cfg->resize_hints) < 0)
    return -1;
  return 0;
}

static int
parse_layout(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  static const char *const allowed[] = { "master_factor", "master_count", "symbols" };
  static const char *const symbol_allowed[] = {
    "tile", "floating", "monocle", "bstack", "bstack_horizontal", "grid"
  };
  config_setting_t *symbols;
  int count = cfg->master_count;
  int i;

  if (validate_members(err, group, allowed, 3) < 0 ||
      read_double_range(err, group, "master_factor", 0.05, 0.95,
                        &cfg->master_factor) < 0 ||
      read_int_range(err, group, "master_count", 0, 64, &count) < 0)
    return -1;
  cfg->master_count = count;
  symbols = config_setting_get_member(group, "symbols");
  if (!symbols)
    return 0;
  if (validate_members(err, symbols, symbol_allowed, RAWM_LAYOUT_COUNT) < 0)
    return -1;
  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i)
    if (read_string(err, symbols, rawm_layout_name((RawmLayoutId)i),
                    &cfg->layout_symbols[i]) < 0)
      return -1;
  return 0;
}

static int
parse_tag_list(FILE *err, const config_setting_t *list, RawmTagConfig **out,
               size_t *out_count)
{
  static const char *const allowed[] = { "name", "layout" };
  RawmTagConfig *tags;
  int i, n;

  if (!list || (config_setting_type(list) != CONFIG_TYPE_LIST &&
                config_setting_type(list) != CONFIG_TYPE_ARRAY))
    return cfg_error(err, list, "%s must be a list", "tags");
  n = config_setting_length(list);
  if (n < 1 || n > RAWM_MAX_TAGS)
    return cfg_error(err, list, "%s must contain between 1 and 31 entries", "tags");
  tags = xcalloc((size_t)n, sizeof(*tags));
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(list, (unsigned int)i);
    config_setting_t *name, *layout;
    const char *layout_name;
    if (validate_members(err, entry, allowed, 2) < 0)
      goto fail;
    name = config_setting_get_member(entry, "name");
    if (!name || config_setting_type(name) != CONFIG_TYPE_STRING) {
      cfg_error(err, entry, "%s requires string member 'name'", "tag");
      goto fail;
    }
    if (strlen(config_setting_get_string(name)) > 22) {
      cfg_error(err, name, "%s is too long (maximum 22 bytes)", "tag name");
      goto fail;
    }
    tags[i].name = xstrdup(config_setting_get_string(name));
    tags[i].layout = RAWM_LAYOUT_TILE;
    layout = config_setting_get_member(entry, "layout");
    if (layout) {
      if (config_setting_type(layout) != CONFIG_TYPE_STRING) {
        cfg_error(err, layout, "%s must be a string", "tag layout");
        goto fail;
      }
      layout_name = config_setting_get_string(layout);
      if (parse_layout_name(layout_name, &tags[i].layout) < 0) {
        cfg_error(err, layout, "unknown layout '%s'", layout_name);
        goto fail;
      }
    }
  }
  *out = tags;
  *out_count = (size_t)n;
  return 0;
fail:
  free_tags(tags, (size_t)n);
  return -1;
}

static int
parse_tags(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  static const char *const allowed[] = { "default", "monitors" };
  static const char *const monitor_allowed[] = { "monitor", "tags" };
  config_setting_t *setting;
  RawmMonitorTags *mons = NULL;
  int n = 0;

  if (validate_members(err, group, allowed, 2) < 0)
    return -1;
  setting = config_setting_get_member(group, "default");
  if (setting) {
    RawmTagConfig *tags = NULL;
    size_t count = 0;
    if (parse_tag_list(err, setting, &tags, &count) < 0)
      return -1;
    free_tags(cfg->tags, cfg->tag_count);
    cfg->tags = tags;
    cfg->tag_count = count;
  }
  setting = config_setting_get_member(group, "monitors");
  if (setting) {
    int i;
    if (config_setting_type(setting) != CONFIG_TYPE_LIST &&
        config_setting_type(setting) != CONFIG_TYPE_ARRAY)
      return cfg_error(err, setting, "%s must be a list", "tags.monitors");
    n = config_setting_length(setting);
    mons = xcalloc((size_t)n, sizeof(*mons));
    for (i = 0; i < n; ++i) {
      config_setting_t *entry = config_setting_get_elem(setting, (unsigned int)i);
      config_setting_t *monitor, *tag_list;
      int j;
      if (validate_members(err, entry, monitor_allowed, 2) < 0)
        goto fail;
      monitor = config_setting_get_member(entry, "monitor");
      tag_list = config_setting_get_member(entry, "tags");
      if (!monitor || config_setting_type(monitor) != CONFIG_TYPE_INT) {
        cfg_error(err, entry, "%s requires integer member 'monitor'", "monitor tag override");
        goto fail;
      }
      mons[i].monitor = config_setting_get_int(monitor);
      if (mons[i].monitor < 0) {
        cfg_error(err, monitor, "%s must be non-negative", "monitor");
        goto fail;
      }
      for (j = 0; j < i; ++j) {
        if (mons[j].monitor == mons[i].monitor) {
          cfg_error(err, monitor, "%s is duplicated", "monitor tag override");
          goto fail;
        }
      }
      if (parse_tag_list(err, tag_list, &mons[i].tags, &mons[i].tag_count) < 0)
        goto fail;
    }
    free_monitor_tags(cfg->monitor_tags, cfg->monitor_tag_count);
    cfg->monitor_tags = mons;
    cfg->monitor_tag_count = (size_t)n;
  }
  return 0;
fail:
  free_monitor_tags(mons, (size_t)n);
  return -1;
}

static int
parse_tag_mask(FILE *err, const config_setting_t *setting, unsigned int *mask)
{
  unsigned int result = 0;
  int i, n;
  if (!setting) {
    *mask = 0;
    return 0;
  }
  if (config_setting_type(setting) == CONFIG_TYPE_STRING) {
    const char *s = config_setting_get_string(setting);
    if (strcmp(s, "all"))
      return cfg_error(err, setting, "invalid tag selector '%s'", s);
    *mask = UINT_MAX;
    return 0;
  }
  if (config_setting_type(setting) != CONFIG_TYPE_LIST &&
      config_setting_type(setting) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, setting, "%s must be a list of tag numbers or 'all'", "tags");
  n = config_setting_length(setting);
  for (i = 0; i < n; ++i) {
    config_setting_t *elem = config_setting_get_elem(setting, (unsigned int)i);
    int tag;
    if (config_setting_type(elem) != CONFIG_TYPE_INT)
      return cfg_error(err, elem, "%s must contain only integers", "tags");
    tag = config_setting_get_int(elem);
    if (tag < 1 || tag > RAWM_MAX_TAGS)
      return cfg_error(err, elem, "%s contains an out-of-range tag", "tags");
    result |= 1u << (tag - 1);
  }
  *mask = result;
  return 0;
}

static int
parse_rules(FILE *err, RawmConfig *cfg, const config_setting_t *list)
{
  static const char *const allowed[] = {
    "class", "instance", "title", "role", "tags", "floating", "centered", "monitor"
  };
  RawmRule *rules;
  int i, n;

  if (config_setting_type(list) != CONFIG_TYPE_LIST &&
      config_setting_type(list) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, list, "%s must be a list", "rules");
  n = config_setting_length(list);
  rules = xcalloc((size_t)n, sizeof(*rules));
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(list, (unsigned int)i);
    config_setting_t *s;
    const char *names[] = { "class", "instance", "title", "role" };
    char **targets[] = { &rules[i].class_name, &rules[i].instance,
                        &rules[i].title, &rules[i].role };
    int j;
    rules[i].monitor = -1;
    if (validate_members(err, entry, allowed, 8) < 0)
      goto fail;
    for (j = 0; j < 4; ++j) {
      s = config_setting_get_member(entry, names[j]);
      if (s) {
        if (config_setting_type(s) != CONFIG_TYPE_STRING) {
          cfg_error(err, s, "%s must be a string", names[j]);
          goto fail;
        }
        *targets[j] = xstrdup(config_setting_get_string(s));
      }
    }
    s = config_setting_get_member(entry, "tags");
    if (parse_tag_mask(err, s, &rules[i].tags) < 0)
      goto fail;
    if (read_bool(err, entry, "floating", &rules[i].floating) < 0 ||
        read_bool(err, entry, "centered", &rules[i].centered) < 0 ||
        read_int_range(err, entry, "monitor", -1, INT_MAX, &rules[i].monitor) < 0)
      goto fail;
  }
  free_rules(cfg->rules, cfg->rule_count);
  cfg->rules = rules;
  cfg->rule_count = (size_t)n;
  return 0;
fail:
  free_rules(rules, (size_t)n);
  return -1;
}

static RawmCommand *
find_command_mutable(RawmConfig *cfg, const char *name)
{
  size_t i;
  for (i = 0; i < cfg->command_count; ++i)
    if (!strcmp(cfg->commands[i].name, name))
      return &cfg->commands[i];
  return NULL;
}

const RawmCommand *
rawm_config_find_command(const RawmConfig *cfg, const char *name)
{
  size_t i;
  for (i = 0; i < cfg->command_count; ++i)
    if (!strcmp(cfg->commands[i].name, name))
      return &cfg->commands[i];
  return NULL;
}

static int
parse_commands(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  int i, n;
  if (config_setting_type(group) != CONFIG_TYPE_GROUP)
    return cfg_error(err, group, "%s must be a group", "commands");
  n = config_setting_length(group);
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(group, (unsigned int)i);
    const char *name = config_setting_name(entry);
    RawmCommand cmd = {0};
    RawmCommand *old;
    int j, argc;
    if (config_setting_type(entry) != CONFIG_TYPE_LIST &&
        config_setting_type(entry) != CONFIG_TYPE_ARRAY)
      return cfg_error(err, entry, "%s must be a list of strings", name);
    argc = config_setting_length(entry);
    if (argc < 1)
      return cfg_error(err, entry, "%s must not be empty", name);
    cmd.name = xstrdup(name);
    cmd.argv = xcalloc((size_t)argc + 1, sizeof(*cmd.argv));
    cmd.argc = (size_t)argc;
    for (j = 0; j < argc; ++j) {
      config_setting_t *arg = config_setting_get_elem(entry, (unsigned int)j);
      if (config_setting_type(arg) != CONFIG_TYPE_STRING) {
        free_command(&cmd);
        return cfg_error(err, arg, "%s arguments must be strings", name);
      }
      cmd.argv[j] = xstrdup(config_setting_get_string(arg));
    }
    old = find_command_mutable(cfg, name);
    if (old) {
      free_command(old);
      *old = cmd;
    } else {
      cfg->commands = xrealloc(cfg->commands,
          (cfg->command_count + 1) * sizeof(*cfg->commands));
      cfg->commands[cfg->command_count++] = cmd;
    }
  }
  return 0;
}

static int
parse_modifier(FILE *err, const config_setting_t *s, unsigned int *mask)
{
  struct ModifierName { const char *name; unsigned int bit; };
  static const struct ModifierName names[] = {
    { "Shift", RAWM_MOD_SHIFT }, { "Control", RAWM_MOD_CONTROL },
    { "Mod1", RAWM_MOD_1 }, { "Mod2", RAWM_MOD_2 }, { "Mod3", RAWM_MOD_3 },
    { "Mod4", RAWM_MOD_4 }, { "Mod5", RAWM_MOD_5 }
  };
  const char *value;
  size_t i;
  if (config_setting_type(s) != CONFIG_TYPE_STRING)
    return cfg_error(err, s, "%s must contain only strings", "modifiers");
  value = config_setting_get_string(s);
  for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    if (!strcmp(value, names[i].name)) {
      *mask |= names[i].bit;
      return 0;
    }
  }
  return cfg_error(err, s, "unknown modifier '%s'", value);
}

static int
parse_modifiers(FILE *err, const config_setting_t *entry, unsigned int *mask)
{
  config_setting_t *mods = config_setting_get_member(entry, "modifiers");
  int i, n;
  *mask = 0;
  if (!mods)
    return 0;
  if (config_setting_type(mods) != CONFIG_TYPE_LIST &&
      config_setting_type(mods) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, mods, "%s must be a list", "modifiers");
  n = config_setting_length(mods);
  for (i = 0; i < n; ++i)
    if (parse_modifier(err, config_setting_get_elem(mods, (unsigned int)i), mask) < 0)
      return -1;
  return 0;
}

static int
parse_action_name(const char *name, RawmAction *action)
{
  static const struct { const char *name; RawmAction action; } names[] = {
    { "spawn", RAWM_ACTION_SPAWN }, { "toggle_bar", RAWM_ACTION_TOGGLE_BAR },
    { "rename_tag", RAWM_ACTION_RENAME_TAG }, { "focus_stack", RAWM_ACTION_FOCUS_STACK },
    { "focus_index", RAWM_ACTION_FOCUS_INDEX }, { "inc_master", RAWM_ACTION_INC_MASTER },
    { "set_master_factor", RAWM_ACTION_SET_MASTER_FACTOR }, { "zoom", RAWM_ACTION_ZOOM },
    { "view", RAWM_ACTION_VIEW }, { "toggle_view", RAWM_ACTION_TOGGLE_VIEW },
    { "kill_client", RAWM_ACTION_KILL_CLIENT }, { "set_layout", RAWM_ACTION_SET_LAYOUT },
    { "toggle_floating", RAWM_ACTION_TOGGLE_FLOATING }, { "tag", RAWM_ACTION_TAG },
    { "toggle_tag", RAWM_ACTION_TOGGLE_TAG }, { "window_view", RAWM_ACTION_WINDOW_VIEW },
    { "toggle_fullscreen", RAWM_ACTION_TOGGLE_FULLSCREEN },
    { "focus_monitor", RAWM_ACTION_FOCUS_MONITOR }, { "tag_monitor", RAWM_ACTION_TAG_MONITOR },
    { "quit", RAWM_ACTION_QUIT }, { "move", RAWM_ACTION_MOVE }, { "resize", RAWM_ACTION_RESIZE }
  };
  size_t i;
  for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    if (!strcmp(name, names[i].name)) {
      *action = names[i].action;
      return 0;
    }
  }
  return -1;
}

static int
parse_binding(FILE *err, const config_setting_t *entry, RawmBinding *binding)
{
  static const char *const allowed[] = {
    "modifiers", "key", "click", "button", "action", "command", "layout",
    "number", "factor", "tag", "restart"
  };
  config_setting_t *s;
  const char *value;

  memset(binding, 0, sizeof(*binding));
  if (validate_members(err, entry, allowed, 11) < 0)
    return -1;
  s = config_setting_get_member(entry, "action");
  if (!s || config_setting_type(s) != CONFIG_TYPE_STRING)
    return cfg_error(err, entry, "%s requires string member 'action'", "binding");
  value = config_setting_get_string(s);
  if (parse_action_name(value, &binding->action) < 0)
    return cfg_error(err, s, "unknown action '%s'", value);

  s = config_setting_get_member(entry, "command");
  if (s) {
    if (config_setting_type(s) != CONFIG_TYPE_STRING)
      return cfg_error(err, s, "%s must be a string", "command");
    binding->command = xstrdup(config_setting_get_string(s));
  }
  s = config_setting_get_member(entry, "layout");
  if (s) {
    if (config_setting_type(s) != CONFIG_TYPE_STRING)
      return cfg_error(err, s, "%s must be a string", "layout");
    value = config_setting_get_string(s);
    if (parse_layout_name(value, &binding->layout) < 0)
      return cfg_error(err, s, "unknown layout '%s'", value);
    binding->has_layout = true;
  }
  s = config_setting_get_member(entry, "number");
  if (s) {
    if (config_setting_type(s) != CONFIG_TYPE_INT)
      return cfg_error(err, s, "%s must be an integer", "number");
    binding->has_number = true;
    binding->number = config_setting_get_int(s);
  }
  s = config_setting_get_member(entry, "factor");
  if (s) {
    if (config_setting_type(s) == CONFIG_TYPE_FLOAT)
      binding->factor = config_setting_get_float(s);
    else if (config_setting_type(s) == CONFIG_TYPE_INT)
      binding->factor = config_setting_get_int(s);
    else
      return cfg_error(err, s, "%s must be numeric", "factor");
    binding->has_factor = true;
  }
  s = config_setting_get_member(entry, "tag");
  if (s) {
    binding->has_tag = true;
    if (config_setting_type(s) == CONFIG_TYPE_INT) {
      int tag = config_setting_get_int(s);
      if (tag < 1 || tag > RAWM_MAX_TAGS)
        return cfg_error(err, s, "%s is out of range", "tag");
      binding->tag = (unsigned int)tag;
    } else if (config_setting_type(s) == CONFIG_TYPE_STRING &&
               !strcmp(config_setting_get_string(s), "all")) {
      binding->all_tags = true;
    } else {
      return cfg_error(err, s, "%s must be a tag number or 'all'", "tag");
    }
  }
  s = config_setting_get_member(entry, "restart");
  if (s) {
    if (config_setting_type(s) != CONFIG_TYPE_BOOL)
      return cfg_error(err, s, "%s must be boolean", "restart");
    binding->restart = config_setting_get_bool(s) != 0;
  }
  return 0;
}

static int
parse_keys(FILE *err, RawmConfig *cfg, const config_setting_t *list)
{
  RawmKeyBinding *keys;
  int i, n;
  if (config_setting_type(list) != CONFIG_TYPE_LIST &&
      config_setting_type(list) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, list, "%s must be a list", "input.keys");
  n = config_setting_length(list);
  keys = xcalloc((size_t)n, sizeof(*keys));
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(list, (unsigned int)i);
    config_setting_t *key;
    if (config_setting_type(entry) != CONFIG_TYPE_GROUP) {
      cfg_error(err, entry, "%s entries must be groups", "input.keys");
      goto fail;
    }
    if (parse_modifiers(err, entry, &keys[i].modifiers) < 0 ||
        parse_binding(err, entry, &keys[i].binding) < 0)
      goto fail;
    key = config_setting_get_member(entry, "key");
    if (!key || config_setting_type(key) != CONFIG_TYPE_STRING) {
      cfg_error(err, entry, "%s requires string member 'key'", "key binding");
      goto fail;
    }
    keys[i].key = xstrdup(config_setting_get_string(key));
  }
  free_keys(cfg->keys, cfg->key_count);
  cfg->keys = keys;
  cfg->key_count = (size_t)n;
  return 0;
fail:
  free_keys(keys, (size_t)n);
  return -1;
}

static int
parse_click_name(const char *name, RawmClick *click)
{
  static const struct { const char *name; RawmClick click; } names[] = {
    { "tag_bar", RAWM_CLICK_TAG_BAR }, { "layout", RAWM_CLICK_LAYOUT },
    { "status", RAWM_CLICK_STATUS }, { "title", RAWM_CLICK_TITLE },
    { "client", RAWM_CLICK_CLIENT }, { "root", RAWM_CLICK_ROOT }
  };
  size_t i;
  for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    if (!strcmp(name, names[i].name)) {
      *click = names[i].click;
      return 0;
    }
  return -1;
}

static int
parse_buttons(FILE *err, RawmConfig *cfg, const config_setting_t *list)
{
  RawmButtonBinding *buttons;
  int i, n;
  if (config_setting_type(list) != CONFIG_TYPE_LIST &&
      config_setting_type(list) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, list, "%s must be a list", "input.buttons");
  n = config_setting_length(list);
  buttons = xcalloc((size_t)n, sizeof(*buttons));
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(list, (unsigned int)i);
    config_setting_t *click, *button;
    const char *click_name;
    if (config_setting_type(entry) != CONFIG_TYPE_GROUP) {
      cfg_error(err, entry, "%s entries must be groups", "input.buttons");
      goto fail;
    }
    if (parse_modifiers(err, entry, &buttons[i].modifiers) < 0 ||
        parse_binding(err, entry, &buttons[i].binding) < 0)
      goto fail;
    click = config_setting_get_member(entry, "click");
    button = config_setting_get_member(entry, "button");
    if (!click || config_setting_type(click) != CONFIG_TYPE_STRING) {
      cfg_error(err, entry, "%s requires string member 'click'", "button binding");
      goto fail;
    }
    click_name = config_setting_get_string(click);
    if (parse_click_name(click_name, &buttons[i].click) < 0) {
      cfg_error(err, click, "unknown click area '%s'", click_name);
      goto fail;
    }
    if (!button || config_setting_type(button) != CONFIG_TYPE_INT) {
      cfg_error(err, entry, "%s requires integer member 'button'", "button binding");
      goto fail;
    }
    buttons[i].button = (unsigned int)config_setting_get_int(button);
    if (buttons[i].button < 1 || buttons[i].button > 9) {
      cfg_error(err, button, "%s is out of range", "button");
      goto fail;
    }
  }
  free_buttons(cfg->buttons, cfg->button_count);
  cfg->buttons = buttons;
  cfg->button_count = (size_t)n;
  return 0;
fail:
  free_buttons(buttons, (size_t)n);
  return -1;
}

static int
parse_input(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  static const char *const allowed[] = { "keys", "buttons" };
  config_setting_t *setting;
  if (validate_members(err, group, allowed, 2) < 0)
    return -1;
  setting = config_setting_get_member(group, "keys");
  if (setting && parse_keys(err, cfg, setting) < 0)
    return -1;
  setting = config_setting_get_member(group, "buttons");
  if (setting && parse_buttons(err, cfg, setting) < 0)
    return -1;
  return 0;
}

static int
parse_layer(FILE *err, RawmConfig *cfg, config_t *source)
{
  static const char *const allowed[] = {
    "ui", "behavior", "layout", "tags", "rules", "commands", "input"
  };
  config_setting_t *root = config_root_setting(source);
  config_setting_t *setting;

  if (validate_members(err, root, allowed, 7) < 0)
    return -1;
  setting = config_setting_get_member(root, "ui");
  if (setting && parse_ui(err, cfg, setting) < 0)
    return -1;
  setting = config_setting_get_member(root, "behavior");
  if (setting && parse_behavior(err, cfg, setting) < 0)
    return -1;
  setting = config_setting_get_member(root, "layout");
  if (setting && parse_layout(err, cfg, setting) < 0)
    return -1;
  setting = config_setting_get_member(root, "tags");
  if (setting && parse_tags(err, cfg, setting) < 0)
    return -1;
  setting = config_setting_get_member(root, "rules");
  if (setting && parse_rules(err, cfg, setting) < 0)
    return -1;
  setting = config_setting_get_member(root, "commands");
  if (setting && parse_commands(err, cfg, setting) < 0)
    return -1;
  setting = config_setting_get_member(root, "input");
  if (setting && parse_input(err, cfg, setting) < 0)
    return -1;
  return 0;
}

static char *
dirname_dup(const char *path)
{
  const char *slash = strrchr(path, '/');
  size_t len;
  char *dir;
  if (!slash)
    return xstrdup(".");
  if (slash == path)
    return xstrdup("/");
  len = (size_t)(slash - path);
  dir = xcalloc(len + 1, 1);
  memcpy(dir, path, len);
  return dir;
}

static int
load_file(FILE *err, RawmConfig *cfg, const char *path, bool required)
{
  config_t source;
  struct stat st;
  char *dir;
  int rc;

  if (stat(path, &st) < 0) {
    if (!required && errno == ENOENT)
      return 0;
    fprintf(err, "rawm: cannot read configuration %s: %s\n", path,
            strerror(errno));
    return -1;
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(err, "rawm: configuration %s is not a regular file\n", path);
    return -1;
  }

  config_init(&source);
  dir = dirname_dup(path);
  config_set_include_dir(&source, dir);
  if (!config_read_file(&source, path)) {
    fprintf(err, "rawm: %s:%d: %s\n",
            config_error_file(&source) ? config_error_file(&source) : path,
            config_error_line(&source),
            config_error_text(&source) ? config_error_text(&source) : "configuration error");
    free(dir);
    config_destroy(&source);
    return -1;
  }
  rc = parse_layer(err, cfg, &source);
  free(dir);
  config_destroy(&source);
  return rc;
}

static char *
user_config_path(void)
{
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home;
  size_t len;
  char *path;
  if (xdg && *xdg) {
    len = strlen(xdg) + strlen("/rawm/rawm.conf") + 1;
    path = xcalloc(len, 1);
    snprintf(path, len, "%s/rawm/rawm.conf", xdg);
    return path;
  }
  home = getenv("HOME");
  if (!home || !*home)
    return NULL;
  len = strlen(home) + strlen("/.config/rawm/rawm.conf") + 1;
  path = xcalloc(len, 1);
  snprintf(path, len, "%s/.config/rawm/rawm.conf", home);
  return path;
}

int
rawm_config_load(RawmConfig *cfg, const char *explicit_path, FILE *err)
{
  char system_path[PATH_MAX];
  char *user_path;

  if (explicit_path) {
    if (load_file(err, cfg, explicit_path, true) < 0)
      return -1;
    return rawm_config_validate(cfg, err);
  }

  snprintf(system_path, sizeof(system_path), "%s/rawm/rawm.conf", SYSCONFDIR);
  if (load_file(err, cfg, system_path, false) < 0)
    return -1;
  user_path = user_config_path();
  if (user_path) {
    int rc = load_file(err, cfg, user_path, false);
    free(user_path);
    if (rc < 0)
      return -1;
  }
  return rawm_config_validate(cfg, err);
}

static int
validate_binding(const RawmConfig *cfg, const RawmBinding *b, bool mouse,
                 RawmClick click, FILE *err)
{
  bool dynamic_tag = mouse && click == RAWM_CLICK_TAG_BAR;
  switch (b->action) {
  case RAWM_ACTION_SPAWN:
    if (!b->command || !rawm_config_find_command(cfg, b->command)) {
      fprintf(err, "rawm: configuration error: spawn binding references unknown command '%s'\n",
              b->command ? b->command : "");
      return -1;
    }
    break;
  case RAWM_ACTION_FOCUS_STACK:
  case RAWM_ACTION_FOCUS_INDEX:
  case RAWM_ACTION_INC_MASTER:
  case RAWM_ACTION_FOCUS_MONITOR:
  case RAWM_ACTION_TAG_MONITOR:
    if (!b->has_number) {
      fputs("rawm: configuration error: binding action requires 'number'\n", err);
      return -1;
    }
    break;
  case RAWM_ACTION_SET_MASTER_FACTOR:
    if (!b->has_factor || b->factor <= -1.0 || b->factor >= 2.0) {
      fputs("rawm: configuration error: set_master_factor requires a sensible 'factor'\n", err);
      return -1;
    }
    break;
  case RAWM_ACTION_VIEW:
  case RAWM_ACTION_TOGGLE_VIEW:
    if (b->has_tag && !b->all_tags && b->tag > cfg->tag_count) {
      fputs("rawm: configuration error: binding refers to a tag that does not exist\n", err);
      return -1;
    }
    break;
  case RAWM_ACTION_TAG:
  case RAWM_ACTION_TOGGLE_TAG:
    if (!b->has_tag && !dynamic_tag) {
      fputs("rawm: configuration error: tag action requires 'tag' outside tag-bar mouse bindings\n", err);
      return -1;
    }
    if (b->has_tag && !b->all_tags && b->tag > cfg->tag_count) {
      fputs("rawm: configuration error: binding refers to a tag that does not exist\n", err);
      return -1;
    }
    break;
  case RAWM_ACTION_SET_LAYOUT:
    break;
  default:
    break;
  }
  return 0;
}

int
rawm_config_validate(const RawmConfig *cfg, FILE *err)
{
  size_t i, j;
  unsigned int valid_mask = cfg->tag_count == 31
                          ? 0x7fffffffu
                          : ((1u << cfg->tag_count) - 1u);

  if (!cfg->font || !*cfg->font) {
    fputs("rawm: configuration error: ui.font must not be empty\n", err);
    return -1;
  }
  for (i = 0; i < cfg->monitor_tag_count; ++i) {
    if (cfg->monitor_tags[i].tag_count != cfg->tag_count) {
      fprintf(err, "rawm: configuration error: monitor %d defines %zu tags; expected %zu\n",
              cfg->monitor_tags[i].monitor, cfg->monitor_tags[i].tag_count,
              cfg->tag_count);
      return -1;
    }
  }
  for (i = 0; i < cfg->rule_count; ++i) {
    if (cfg->rules[i].tags != UINT_MAX && (cfg->rules[i].tags & ~valid_mask)) {
      fputs("rawm: configuration error: rule refers to a tag that does not exist\n", err);
      return -1;
    }
  }
  for (i = 0; i < cfg->key_count; ++i) {
    if (!cfg->keys[i].key || !*cfg->keys[i].key) {
      fputs("rawm: configuration error: key name must not be empty\n", err);
      return -1;
    }
    if (validate_binding(cfg, &cfg->keys[i].binding, false, RAWM_CLICK_ROOT, err) < 0)
      return -1;
    for (j = 0; j < i; ++j) {
      if (cfg->keys[j].modifiers == cfg->keys[i].modifiers &&
          !strcmp(cfg->keys[j].key, cfg->keys[i].key)) {
        fprintf(err, "rawm: configuration error: duplicate key binding for '%s'\n",
                cfg->keys[i].key);
        return -1;
      }
    }
  }
  for (i = 0; i < cfg->button_count; ++i) {
    if (validate_binding(cfg, &cfg->buttons[i].binding, true,
                         cfg->buttons[i].click, err) < 0)
      return -1;
    for (j = 0; j < i; ++j) {
      if (cfg->buttons[j].click == cfg->buttons[i].click &&
          cfg->buttons[j].modifiers == cfg->buttons[i].modifiers &&
          cfg->buttons[j].button == cfg->buttons[i].button) {
        fputs("rawm: configuration error: duplicate mouse binding\n", err);
        return -1;
      }
    }
  }
  return 0;
}

const RawmTagConfig *
rawm_config_tags_for_monitor(const RawmConfig *cfg, int monitor)
{
  size_t i;
  for (i = 0; i < cfg->monitor_tag_count; ++i)
    if (cfg->monitor_tags[i].monitor == monitor)
      return cfg->monitor_tags[i].tags;
  return cfg->tags;
}
