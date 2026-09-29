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

const char *
rawm_layout_name(RawmLayoutId layout)
{
  static const char *names[RAWM_LAYOUT_COUNT] = {
    "tile", "floating", "monocle", "bstack", "bstack_horizontal", "grid"
  };
  return layout >= 0 && layout < RAWM_LAYOUT_COUNT ? names[layout] : NULL;
}

static const char *
center_mode_name(RawmCenterMode mode)
{
  static const char *const names[] = { "none", "monitor", "parent" };

  return mode >= RAWM_CENTER_NONE && mode <= RAWM_CENTER_PARENT
           ? names[mode]
           : NULL;
}

static const char *
attention_mode_name(RawmAttentionMode mode)
{
  static const char *const names[] = { "none", "urgent" };

  return mode >= RAWM_ATTENTION_NONE && mode <= RAWM_ATTENTION_URGENT
           ? names[mode]
           : NULL;
}

static const char *
size_hints_mode_name(RawmSizeHintsMode mode)
{
  static const char *const names[] = { "never", "floating", "always" };

  return mode >= RAWM_SIZE_HINTS_NEVER && mode <= RAWM_SIZE_HINTS_ALWAYS
           ? names[mode]
           : NULL;
}

static const char *
floating_placement_name(RawmFloatingPlacement mode)
{
  static const char *const names[] = { "client", "cascade" };

  return mode >= RAWM_FLOATING_PLACEMENT_CLIENT &&
         mode <= RAWM_FLOATING_PLACEMENT_CASCADE
           ? names[mode]
           : NULL;
}

static int
parse_floating_placement(const char *name, RawmFloatingPlacement *mode)
{
  int i;

  for (i = RAWM_FLOATING_PLACEMENT_CLIENT;
       i <= RAWM_FLOATING_PLACEMENT_CASCADE; ++i) {
    if (!strcmp(name, floating_placement_name((RawmFloatingPlacement)i))) {
      *mode = (RawmFloatingPlacement)i;
      return 0;
    }
  }
  return -1;
}

static int
parse_size_hints_mode(const char *name, RawmSizeHintsMode *mode)
{
  int i;

  for (i = RAWM_SIZE_HINTS_NEVER; i <= RAWM_SIZE_HINTS_ALWAYS; ++i) {
    if (!strcmp(name, size_hints_mode_name((RawmSizeHintsMode)i))) {
      *mode = (RawmSizeHintsMode)i;
      return 0;
    }
  }
  return -1;
}

static int
parse_attention_mode(const char *name, RawmAttentionMode *mode)
{
  int i;

  for (i = RAWM_ATTENTION_NONE; i <= RAWM_ATTENTION_URGENT; ++i) {
    if (!strcmp(name, attention_mode_name((RawmAttentionMode)i))) {
      *mode = (RawmAttentionMode)i;
      return 0;
    }
  }
  return -1;
}

static int
parse_center_mode(const char *name, RawmCenterMode *mode)
{
  int i;

  for (i = RAWM_CENTER_NONE; i <= RAWM_CENTER_PARENT; ++i) {
    if (!strcmp(name, center_mode_name((RawmCenterMode)i))) {
      *mode = (RawmCenterMode)i;
      return 0;
    }
  }
  return -1;
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
append_pointer_binding(RawmConfig *cfg, unsigned int modifiers,
                       unsigned int button, RawmPointerAction action)
{
  RawmPointerBinding *binding;

  cfg->pointer_bindings = xrealloc(
      cfg->pointer_bindings,
      (cfg->pointer_binding_count + 1) * sizeof(*cfg->pointer_bindings));
  binding = &cfg->pointer_bindings[cfg->pointer_binding_count++];
  binding->modifiers = modifiers;
  binding->button = button;
  binding->action = action;
}

void
rawm_config_init(RawmConfig *cfg)
{
  static const char *scheme_defaults[RAWM_COLOR_SCHEMES][3] = {
    { "#cccccc", "#000000", "#cccccc" },
    { "#0066ff", "#ffffff", "#0066ff" },
    { "#0066ff", "#0066ff", "#ffffff" },
    { "#ff0000", "#ffffff", "#ff0000" },
    { "${ui.colors.selected.border}",
      "${ui.colors.selected.foreground}",
      "${ui.colors.selected.background}" }
  };
  static const char *symbols[RAWM_LAYOUT_COUNT] = {
    "[]=", "<1/1>", "[1/1]", "TTT", "===", "###"
  };
  size_t i, j;

  memset(cfg, 0, sizeof(*cfg));
  cfg->font = xstrdup("monospace:size=9");
  cfg->border_width = 2;
  cfg->snap = 32;
  cfg->opacity = 1.0;
  cfg->show_osd = false;
  cfg->osd_top = false;
  cfg->osd_geometry = false;
  cfg->osd_duration_ms = 800;
  cfg->osd_padding = 10;
  cfg->osd_offset = 24;
  cfg->osd_min_width = 0;
  cfg->dialog_center = RAWM_CENTER_NONE;
  cfg->size_hints = RAWM_SIZE_HINTS_FLOATING;
  cfg->floating_placement = RAWM_FLOATING_PLACEMENT_CASCADE;
  cfg->cascade_step = 32;
  cfg->master_factor = 0.55;
  cfg->master_count = 1;
  cfg->gap_inner = 0;
  cfg->gap_outer = 0;
  cfg->smart_gaps = false;

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

  append_pointer_binding(cfg, RAWM_MOD_4, 1, RAWM_POINTER_MOVE);
  append_pointer_binding(cfg, RAWM_MOD_4, 2, RAWM_POINTER_TOGGLE_FLOATING);
  append_pointer_binding(cfg, RAWM_MOD_4, 3, RAWM_POINTER_RESIZE);
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
  free(cfg->pointer_bindings);
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
    "font", "border_width", "snap", "opacity", "osd", "colors"
  };
  static const char *const color_allowed[] = {
    "normal", "selected", "urgent", "error", "floating"
  };
  config_setting_t *group;

  if (validate_members(err, ui, allowed, 6) < 0)
    return -1;
  if (read_string(err, ui, "font", &cfg->font) < 0 ||
      read_uint_range(err, ui, "border_width", 0, 64, &cfg->border_width) < 0 ||
      read_uint_range(err, ui, "snap", 0, 4096, &cfg->snap) < 0 ||
      read_double_range(err, ui, "opacity", 0.0, 1.0, &cfg->opacity) < 0)
    return -1;

  group = config_setting_get_member(ui, "osd");
  if (group) {
    static const char *const osd_allowed[] = {
      "show", "position", "geometry", "duration_ms", "padding", "offset",
      "min_width"
    };
    config_setting_t *position;
    const char *value;

    if (validate_members(err, group, osd_allowed, 7) < 0 ||
        read_bool(err, group, "show", &cfg->show_osd) < 0 ||
        read_bool(err, group, "geometry", &cfg->osd_geometry) < 0 ||
        read_uint_range(err, group, "duration_ms", 100, 5000,
                        &cfg->osd_duration_ms) < 0 ||
        read_uint_range(err, group, "padding", 0, 64,
                        &cfg->osd_padding) < 0 ||
        read_uint_range(err, group, "offset", 0, 512,
                        &cfg->osd_offset) < 0 ||
        read_uint_range(err, group, "min_width", 0, 4096,
                        &cfg->osd_min_width) < 0)
      return -1;

    position = config_setting_get_member(group, "position");
    if (position) {
      if (config_setting_type(position) != CONFIG_TYPE_STRING)
        return cfg_error(err, position, "osd.position must be a string",
                         "osd.position");
      value = config_setting_get_string(position);
      if (!strcmp(value, "top"))
        cfg->osd_top = true;
      else if (!strcmp(value, "bottom"))
        cfg->osd_top = false;
      else
        return cfg_error(err, position, "invalid osd position '%s'", value);
    }
  }

  group = config_setting_get_member(ui, "colors");
  if (group) {
    if (validate_members(err, group, color_allowed, 5) < 0 ||
        parse_color_scheme(err, cfg, group, "normal", 0) < 0 ||
        parse_color_scheme(err, cfg, group, "selected", 1) < 0 ||
        parse_color_scheme(err, cfg, group, "urgent", 2) < 0 ||
        parse_color_scheme(err, cfg, group, "error", 3) < 0 ||
        parse_color_scheme(err, cfg, group, "floating", 4) < 0)
      return -1;
  }
  return 0;
}

static int
parse_behavior(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  static const char *const allowed[] = {
    "dialog_center", "size_hints", "floating_placement", "cascade_step"
  };
  config_setting_t *setting;
  const char *value;

  if (validate_members(err, group, allowed, 4) < 0)
    return -1;

  setting = config_setting_get_member(group, "dialog_center");
  if (setting) {
    if (config_setting_type(setting) != CONFIG_TYPE_STRING)
      return cfg_error(err, setting, "%s", "dialog_center must be a string");
    value = config_setting_get_string(setting);
    if (parse_center_mode(value, &cfg->dialog_center) < 0)
      return cfg_error(err, setting, "invalid dialog_center '%s'", value);
  }

  setting = config_setting_get_member(group, "size_hints");
  if (setting) {
    if (config_setting_type(setting) != CONFIG_TYPE_STRING)
      return cfg_error(err, setting, "%s", "size_hints must be a string");
    value = config_setting_get_string(setting);
    if (parse_size_hints_mode(value, &cfg->size_hints) < 0)
      return cfg_error(err, setting, "invalid size_hints '%s'", value);
  }

  setting = config_setting_get_member(group, "floating_placement");
  if (setting) {
    if (config_setting_type(setting) != CONFIG_TYPE_STRING)
      return cfg_error(err, setting, "%s", "floating_placement must be a string");
    value = config_setting_get_string(setting);
    if (parse_floating_placement(value, &cfg->floating_placement) < 0)
      return cfg_error(err, setting, "invalid floating_placement '%s'", value);
  }

  if (read_uint_range(err, group, "cascade_step", 1, 4096,
                      &cfg->cascade_step) < 0)
    return -1;

  return 0;
}

static int
parse_layout(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  static const char *const allowed[] = {
    "master_factor", "master_count", "gaps", "symbols"
  };
  static const char *const gap_allowed[] = { "inner", "outer", "smart" };
  static const char *const symbol_allowed[] = {
    "tile", "floating", "monocle", "bstack", "bstack_horizontal", "grid"
  };
  config_setting_t *gaps;
  config_setting_t *symbols;
  int inner = (int)cfg->gap_inner;
  int outer = (int)cfg->gap_outer;
  int count = cfg->master_count;
  int i;

  if (validate_members(err, group, allowed, 4) < 0 ||
      read_double_range(err, group, "master_factor", 0.05, 0.95,
                        &cfg->master_factor) < 0 ||
      read_int_range(err, group, "master_count", 0, 64, &count) < 0)
    return -1;
  cfg->master_count = count;

  gaps = config_setting_get_member(group, "gaps");
  if (gaps) {
    if (config_setting_type(gaps) != CONFIG_TYPE_GROUP)
      return cfg_error(err, gaps, "%s", "layout.gaps must be a group");
    if (validate_members(err, gaps, gap_allowed, 3) < 0 ||
        read_int_range(err, gaps, "inner", 0, 4096, &inner) < 0 ||
        read_int_range(err, gaps, "outer", 0, 4096, &outer) < 0 ||
        read_bool(err, gaps, "smart", &cfg->smart_gaps) < 0)
      return -1;
    cfg->gap_inner = (unsigned int)inner;
    cfg->gap_outer = (unsigned int)outer;
  }

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
    if (strlen(config_setting_get_string(name)) > RAWM_TAG_NAME_MAX) {
      char detail[96];

      snprintf(detail, sizeof detail,
               "tag name is too long (maximum %u bytes)",
               RAWM_TAG_NAME_MAX);
      cfg_error(err, name, "%s", detail);
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
    "class", "instance", "title", "role", "tags", "floating", "center",
    "fullscreen", "attention", "monitor"
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
    if (validate_members(err, entry, allowed, 10) < 0)
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
        read_int_range(err, entry, "monitor", -1, INT_MAX, &rules[i].monitor) < 0)
      goto fail;

    s = config_setting_get_member(entry, "center");
    if (s) {
      const char *value;

      if (config_setting_type(s) != CONFIG_TYPE_STRING) {
        cfg_error(err, s, "%s", "center must be a string");
        goto fail;
      }
      value = config_setting_get_string(s);
      if (parse_center_mode(value, &rules[i].center) < 0) {
        cfg_error(err, s, "invalid center mode '%s'", value);
        goto fail;
      }
      rules[i].has_center = true;
    }

    s = config_setting_get_member(entry, "fullscreen");
    if (s) {
      if (config_setting_type(s) != CONFIG_TYPE_BOOL) {
        cfg_error(err, s, "%s", "fullscreen must be boolean");
        goto fail;
      }
      rules[i].fullscreen = config_setting_get_bool(s) != 0;
      rules[i].has_fullscreen = true;
    }

    s = config_setting_get_member(entry, "attention");
    if (s) {
      const char *value;

      if (config_setting_type(s) != CONFIG_TYPE_STRING) {
        cfg_error(err, s, "%s", "attention must be a string");
        goto fail;
      }
      value = config_setting_get_string(s);
      if (parse_attention_mode(value, &rules[i].attention) < 0) {
        cfg_error(err, s, "invalid attention mode '%s'", value);
        goto fail;
      }
      rules[i].has_attention = true;
    }
  }
  free_rules(cfg->rules, cfg->rule_count);
  cfg->rules = rules;
  cfg->rule_count = (size_t)n;
  return 0;
fail:
  free_rules(rules, (size_t)n);
  return -1;
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
parse_pointer_action(const char *name, RawmPointerAction *action)
{
  static const struct {
    const char *name;
    RawmPointerAction action;
  } names[] = {
    { "move", RAWM_POINTER_MOVE },
    { "toggle_floating", RAWM_POINTER_TOGGLE_FLOATING },
    { "resize", RAWM_POINTER_RESIZE }
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
parse_pointer_bindings(FILE *err, RawmConfig *cfg, const config_setting_t *list)
{
  static const char *const allowed[] = { "modifiers", "button", "action" };
  RawmPointerBinding *bindings;
  int i, n;

  if (config_setting_type(list) != CONFIG_TYPE_LIST &&
      config_setting_type(list) != CONFIG_TYPE_ARRAY)
    return cfg_error(err, list, "%s must be a list", "pointer.bindings");

  n = config_setting_length(list);
  bindings = xcalloc((size_t)n, sizeof(*bindings));
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(list, (unsigned int)i);
    config_setting_t *button;
    config_setting_t *action;
    const char *action_name;
    int button_number;

    if (config_setting_type(entry) != CONFIG_TYPE_GROUP) {
      cfg_error(err, entry, "%s entries must be groups", "pointer.bindings");
      goto fail;
    }
    if (validate_members(err, entry, allowed, 3) < 0 ||
        parse_modifiers(err, entry, &bindings[i].modifiers) < 0)
      goto fail;

    button = config_setting_get_member(entry, "button");
    if (!button || config_setting_type(button) != CONFIG_TYPE_INT) {
      cfg_error(err, entry, "%s requires integer member 'button'",
                "pointer binding");
      goto fail;
    }
    button_number = config_setting_get_int(button);
    if (button_number < 1 || button_number > 9) {
      cfg_error(err, button, "%s is out of range", "button");
      goto fail;
    }
    bindings[i].button = (unsigned int)button_number;

    action = config_setting_get_member(entry, "action");
    if (!action || config_setting_type(action) != CONFIG_TYPE_STRING) {
      cfg_error(err, entry, "%s requires string member 'action'",
                "pointer binding");
      goto fail;
    }
    action_name = config_setting_get_string(action);
    if (parse_pointer_action(action_name, &bindings[i].action) < 0) {
      cfg_error(err, action, "unknown pointer action '%s'", action_name);
      goto fail;
    }
  }

  free(cfg->pointer_bindings);
  cfg->pointer_bindings = bindings;
  cfg->pointer_binding_count = (size_t)n;
  return 0;

fail:
  free(bindings);
  return -1;
}

static int
parse_pointer(FILE *err, RawmConfig *cfg, const config_setting_t *group)
{
  static const char *const allowed[] = { "bindings" };
  config_setting_t *setting;

  if (validate_members(err, group, allowed, 1) < 0)
    return -1;
  setting = config_setting_get_member(group, "bindings");
  if (setting && parse_pointer_bindings(err, cfg, setting) < 0)
    return -1;
  return 0;
}

static int
parse_layer(FILE *err, RawmConfig *cfg, config_t *source)
{
  static const char *const allowed[] = {
    "ui", "behavior", "layout", "tags", "rules", "pointer"
  };
  config_setting_t *root = config_root_setting(source);
  config_setting_t *setting;

  if (validate_members(err, root, allowed, 6) < 0)
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
  setting = config_setting_get_member(root, "pointer");
  if (setting && parse_pointer(err, cfg, setting) < 0)
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

typedef struct {
  const char *path;
  char **slot;
  unsigned char state;
} RawmStringReference;

typedef struct {
  RawmConfig *cfg;
  RawmStringReference *strings;
  size_t string_count;
  FILE *err;
  const char *stack[64];
  size_t depth;
} RawmReferenceResolver;

static int
append_text(char **buf, size_t *len, size_t *cap, const char *text,
            size_t text_len)
{
  size_t need;
  char *p;

  if (text_len > SIZE_MAX - *len - 1)
    return -1;
  need = *len + text_len + 1;
  if (need > *cap) {
    size_t newcap = *cap ? *cap : 64;
    while (newcap < need) {
      if (newcap > SIZE_MAX / 2) {
        newcap = need;
        break;
      }
      newcap *= 2;
    }
    p = realloc(*buf, newcap);
    if (!p)
      return -1;
    *buf = p;
    *cap = newcap;
  }
  memcpy(*buf + *len, text, text_len);
  *len += text_len;
  (*buf)[*len] = '\0';
  return 0;
}

static int
reference_error(RawmReferenceResolver *resolver, const char *context,
                const char *fmt, const char *detail)
{
  fprintf(resolver->err, "rawm: configuration error: %s: ", context);
  fprintf(resolver->err, fmt, detail);
  fputc('\n', resolver->err);
  return -1;
}

static int
find_string_reference(RawmReferenceResolver *resolver, const char *path)
{
  size_t i;
  for (i = 0; i < resolver->string_count; ++i)
    if (!strcmp(path, resolver->strings[i].path))
      return (int)i;
  return -1;
}

static int
format_scalar_reference(RawmReferenceResolver *resolver, const char *path,
                        char *buf, size_t buflen, const char **value)
{
  RawmConfig *cfg = resolver->cfg;

  if (!strcmp(path, "ui.border_width"))
    snprintf(buf, buflen, "%u", cfg->border_width);
  else if (!strcmp(path, "ui.snap"))
    snprintf(buf, buflen, "%u", cfg->snap);
  else if (!strcmp(path, "ui.opacity"))
    snprintf(buf, buflen, "%.15g", cfg->opacity);
  else if (!strcmp(path, "ui.osd.show"))
    snprintf(buf, buflen, "%s", cfg->show_osd ? "true" : "false");
  else if (!strcmp(path, "ui.osd.position"))
    snprintf(buf, buflen, "%s", cfg->osd_top ? "top" : "bottom");
  else if (!strcmp(path, "ui.osd.geometry"))
    snprintf(buf, buflen, "%s", cfg->osd_geometry ? "true" : "false");
  else if (!strcmp(path, "ui.osd.duration_ms"))
    snprintf(buf, buflen, "%u", cfg->osd_duration_ms);
  else if (!strcmp(path, "ui.osd.padding"))
    snprintf(buf, buflen, "%u", cfg->osd_padding);
  else if (!strcmp(path, "ui.osd.offset"))
    snprintf(buf, buflen, "%u", cfg->osd_offset);
  else if (!strcmp(path, "ui.osd.min_width"))
    snprintf(buf, buflen, "%u", cfg->osd_min_width);
  else if (!strcmp(path, "behavior.dialog_center"))
    snprintf(buf, buflen, "%s", center_mode_name(cfg->dialog_center));
  else if (!strcmp(path, "behavior.size_hints"))
    snprintf(buf, buflen, "%s", size_hints_mode_name(cfg->size_hints));
  else if (!strcmp(path, "behavior.floating_placement"))
    snprintf(buf, buflen, "%s", floating_placement_name(cfg->floating_placement));
  else if (!strcmp(path, "behavior.cascade_step"))
    snprintf(buf, buflen, "%u", cfg->cascade_step);
  else if (!strcmp(path, "layout.master_factor"))
    snprintf(buf, buflen, "%.15g", cfg->master_factor);
  else if (!strcmp(path, "layout.master_count"))
    snprintf(buf, buflen, "%d", cfg->master_count);
  else if (!strcmp(path, "layout.gaps.inner"))
    snprintf(buf, buflen, "%u", cfg->gap_inner);
  else if (!strcmp(path, "layout.gaps.outer"))
    snprintf(buf, buflen, "%u", cfg->gap_outer);
  else if (!strcmp(path, "layout.gaps.smart"))
    snprintf(buf, buflen, "%s", cfg->smart_gaps ? "true" : "false");
  else
    return -1;

  *value = buf;
  return 0;
}

static int resolve_string_reference(RawmReferenceResolver *resolver, int index);

static int
lookup_reference(RawmReferenceResolver *resolver, const char *path,
                 const char **value, char *scalar, size_t scalar_len)
{
  int index = find_string_reference(resolver, path);
  if (index >= 0) {
    if (resolve_string_reference(resolver, index) < 0)
      return -1;
    *value = *resolver->strings[index].slot;
    return 0;
  }
  if (format_scalar_reference(resolver, path, scalar, scalar_len, value) == 0)
    return 0;
  return 1;
}

static int
expand_references(RawmReferenceResolver *resolver, const char *input,
                  const char *context, char **output)
{
  const char *p = input;
  char *result = NULL;
  size_t len = 0, cap = 0;

  while (*p) {
    const char *start = strstr(p, "${");
    const char *end;
    char *path;
    const char *value;
    char scalar[64];
    int rc;

    if (!start) {
      if (append_text(&result, &len, &cap, p, strlen(p)) < 0)
        goto oom;
      break;
    }

    if (start > p && start[-1] == '$') {
      if (append_text(&result, &len, &cap, p,
                      (size_t)(start - p - 1)) < 0 ||
          append_text(&result, &len, &cap, "${", 2) < 0)
        goto oom;
      p = start + 2;
      continue;
    }

    if (append_text(&result, &len, &cap, p, (size_t)(start - p)) < 0)
      goto oom;
    end = strchr(start + 2, '}');
    if (!end) {
      free(result);
      return reference_error(resolver, context,
                             "unterminated reference '%s'", start);
    }
    if (end == start + 2) {
      free(result);
      return reference_error(resolver, context,
                             "empty reference '%s'", "${}");
    }

    path = xcalloc((size_t)(end - (start + 2)) + 1, 1);
    memcpy(path, start + 2, (size_t)(end - (start + 2)));
    rc = lookup_reference(resolver, path, &value, scalar, sizeof(scalar));
    if (rc > 0) {
      char display[320];
      snprintf(display, sizeof(display), "${%s}", path);
      free(path);
      free(result);
      return reference_error(resolver, context,
                             "unknown reference '%s'", display);
    }
    if (rc < 0) {
      free(path);
      free(result);
      return -1;
    }
    if (append_text(&result, &len, &cap, value, strlen(value)) < 0) {
      free(path);
      goto oom;
    }
    free(path);
    p = end + 1;
  }

  if (!result)
    result = xstrdup("");
  *output = result;
  return 0;

oom:
  free(result);
  fputs("rawm: out of memory\n", resolver->err);
  return -1;
}

static int
resolve_string_reference(RawmReferenceResolver *resolver, int index)
{
  RawmStringReference *ref = &resolver->strings[index];
  char *expanded;
  size_t i;

  if (ref->state == 2)
    return 0;
  if (ref->state == 1) {
    fprintf(resolver->err, "rawm: configuration error: cyclic reference: ");
    for (i = 0; i < resolver->depth; ++i)
      fprintf(resolver->err, "%s -> ", resolver->stack[i]);
    fprintf(resolver->err, "%s\n", ref->path);
    return -1;
  }
  if (resolver->depth >= sizeof(resolver->stack) / sizeof(resolver->stack[0]))
    return reference_error(resolver, ref->path,
                           "reference nesting is too deep near '%s'", ref->path);

  ref->state = 1;
  resolver->stack[resolver->depth++] = ref->path;
  if (expand_references(resolver, *ref->slot, ref->path, &expanded) < 0) {
    --resolver->depth;
    ref->state = 0;
    return -1;
  }
  --resolver->depth;
  free(*ref->slot);
  *ref->slot = expanded;
  ref->state = 2;
  return 0;
}

static int
resolve_config_references(RawmConfig *cfg, FILE *err)
{
  static const char *const color_names[] = {
    "normal", "selected", "urgent", "error", "floating"
  };
  static const char *const component_names[] = {
    "border", "foreground", "background"
  };
  RawmStringReference strings[1 + RAWM_COLOR_SCHEMES * RAWM_COLOR_COMPONENTS +
                              RAWM_LAYOUT_COUNT];
  char color_paths[RAWM_COLOR_SCHEMES][RAWM_COLOR_COMPONENTS][64];
  char layout_paths[RAWM_LAYOUT_COUNT][64];
  RawmReferenceResolver resolver;
  size_t n = 0, i, j;

  memset(&resolver, 0, sizeof(resolver));
  resolver.cfg = cfg;
  resolver.strings = strings;
  resolver.err = err;

  strings[n++] = (RawmStringReference){ "ui.font", &cfg->font, 0 };
  for (i = 0; i < RAWM_COLOR_SCHEMES; ++i) {
    for (j = 0; j < RAWM_COLOR_COMPONENTS; ++j) {
      snprintf(color_paths[i][j], sizeof(color_paths[i][j]),
               "ui.colors.%s.%s", color_names[i], component_names[j]);
      strings[n++] = (RawmStringReference){ color_paths[i][j],
                                            &cfg->colors[i][j], 0 };
    }
  }
  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i) {
    snprintf(layout_paths[i], sizeof(layout_paths[i]), "layout.symbols.%s",
             rawm_layout_name((RawmLayoutId)i));
    strings[n++] = (RawmStringReference){ layout_paths[i],
                                          &cfg->layout_symbols[i], 0 };
  }
  resolver.string_count = n;

  for (i = 0; i < n; ++i)
    if (resolve_string_reference(&resolver, (int)i) < 0)
      return -1;

  return 0;
}

static char *
user_config_path(void)
{
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home;
  size_t len;
  char *path;
  if (xdg && *xdg && xdg[0] == '/') {
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
    if (load_file(err, cfg, explicit_path, true) < 0 ||
        resolve_config_references(cfg, err) < 0)
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
  if (resolve_config_references(cfg, err) < 0)
    return -1;
  return rawm_config_validate(cfg, err);
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
  if (!size_hints_mode_name(cfg->size_hints)) {
    fputs("rawm: configuration error: invalid size-hint policy\n", err);
    return -1;
  }
  if (!floating_placement_name(cfg->floating_placement)) {
    fputs("rawm: configuration error: invalid floating-placement policy\n", err);
    return -1;
  }
  for (i = 0; i < RAWM_LAYOUT_COUNT; ++i) {
    if (!cfg->layout_symbols[i] ||
        strlen(cfg->layout_symbols[i]) > RAWM_LAYOUT_SYMBOL_MAX) {
      fprintf(err,
              "rawm: configuration error: layout symbol '%s' exceeds %u bytes\n",
              rawm_layout_name((RawmLayoutId)i), RAWM_LAYOUT_SYMBOL_MAX);
      return -1;
    }
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
  for (i = 0; i < cfg->pointer_binding_count; ++i) {
    for (j = 0; j < i; ++j) {
      if (cfg->pointer_bindings[j].modifiers ==
              cfg->pointer_bindings[i].modifiers &&
          cfg->pointer_bindings[j].button == cfg->pointer_bindings[i].button) {
        fputs("rawm: configuration error: duplicate pointer binding\n", err);
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
