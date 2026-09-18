#ifndef RAWM_CONFIG_H
#define RAWM_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define RAWM_MAX_TAGS 31
#define RAWM_COLOR_SCHEMES 4
#define RAWM_COLOR_COMPONENTS 3

typedef enum {
  RAWM_CENTER_NONE,
  RAWM_CENTER_MONITOR,
  RAWM_CENTER_PARENT
} RawmCenterMode;

typedef enum {
  RAWM_STATUS_PROTOCOL_V2
} RawmStatusProtocol;

typedef enum {
  RAWM_LAYOUT_TILE,
  RAWM_LAYOUT_FLOATING,
  RAWM_LAYOUT_MONOCLE,
  RAWM_LAYOUT_BSTACK,
  RAWM_LAYOUT_BSTACK_HORIZONTAL,
  RAWM_LAYOUT_GRID,
  RAWM_LAYOUT_COUNT
} RawmLayoutId;

typedef enum {
  RAWM_ACTION_SPAWN,
  RAWM_ACTION_TOGGLE_BAR,
  RAWM_ACTION_RENAME_TAG,
  RAWM_ACTION_FOCUS_STACK,
  RAWM_ACTION_FOCUS_INDEX,
  RAWM_ACTION_INC_MASTER,
  RAWM_ACTION_SET_MASTER_FACTOR,
  RAWM_ACTION_ZOOM,
  RAWM_ACTION_VIEW,
  RAWM_ACTION_TOGGLE_VIEW,
  RAWM_ACTION_KILL_CLIENT,
  RAWM_ACTION_SET_LAYOUT,
  RAWM_ACTION_TOGGLE_FLOATING,
  RAWM_ACTION_TAG,
  RAWM_ACTION_TOGGLE_TAG,
  RAWM_ACTION_WINDOW_VIEW,
  RAWM_ACTION_TOGGLE_FULLSCREEN,
  RAWM_ACTION_FOCUS_MONITOR,
  RAWM_ACTION_TAG_MONITOR,
  RAWM_ACTION_QUIT,
  RAWM_ACTION_MOVE,
  RAWM_ACTION_RESIZE
} RawmAction;

typedef enum {
  RAWM_CLICK_TAG_BAR,
  RAWM_CLICK_LAYOUT,
  RAWM_CLICK_STATUS,
  RAWM_CLICK_TITLE,
  RAWM_CLICK_CLIENT,
  RAWM_CLICK_ROOT
} RawmClick;

enum {
  RAWM_MOD_SHIFT   = 1u << 0,
  RAWM_MOD_CONTROL = 1u << 1,
  RAWM_MOD_1       = 1u << 2,
  RAWM_MOD_2       = 1u << 3,
  RAWM_MOD_3       = 1u << 4,
  RAWM_MOD_4       = 1u << 5,
  RAWM_MOD_5       = 1u << 6
};

typedef struct {
  char *name;
  RawmLayoutId layout;
} RawmTagConfig;

typedef struct {
  int monitor;
  RawmTagConfig *tags;
  size_t tag_count;
} RawmMonitorTags;

typedef struct {
  char *class_name;
  char *instance;
  char *title;
  char *role;
  unsigned int tags;
  bool floating;
  bool has_center;
  RawmCenterMode center;
  bool has_fullscreen;
  bool fullscreen;
  int monitor;
} RawmRule;

typedef struct {
  char *name;
  char **argv;
  size_t argc;
} RawmCommand;

typedef struct {
  RawmAction action;
  char *command;
  bool has_layout;
  RawmLayoutId layout;
  bool has_number;
  int number;
  bool has_factor;
  bool factor_absolute;
  double factor;
  bool has_tag;
  bool all_tags;
  unsigned int tag;
  bool restart;
} RawmBinding;

typedef struct {
  unsigned int modifiers;
  char *key;
  RawmBinding binding;
} RawmKeyBinding;

typedef struct {
  RawmClick click;
  unsigned int modifiers;
  unsigned int button;
  RawmBinding binding;
} RawmButtonBinding;

typedef struct {
  char *font;
  char *colors[RAWM_COLOR_SCHEMES][RAWM_COLOR_COMPONENTS];
  unsigned int border_width;
  unsigned int snap;
  double opacity;
  bool show_bar;
  bool top_bar;
  int bar_height;
  bool show_systray;
  unsigned int systray_spacing;

  bool status_enabled;
  RawmStatusProtocol status_protocol;
  char *status_separator;

  RawmCenterMode dialog_center;
  bool resize_hints;

  double master_factor;
  int master_count;
  unsigned int gap_inner;
  unsigned int gap_outer;
  bool smart_gaps;
  char *layout_symbols[RAWM_LAYOUT_COUNT];

  RawmTagConfig *tags;
  size_t tag_count;
  RawmMonitorTags *monitor_tags;
  size_t monitor_tag_count;

  RawmRule *rules;
  size_t rule_count;
  RawmCommand *commands;
  size_t command_count;
  RawmKeyBinding *keys;
  size_t key_count;
  RawmButtonBinding *buttons;
  size_t button_count;
} RawmConfig;

void rawm_config_init(RawmConfig *cfg);
void rawm_config_destroy(RawmConfig *cfg);
int rawm_config_load(RawmConfig *cfg, const char *explicit_path, FILE *err);
int rawm_config_validate(const RawmConfig *cfg, FILE *err);
const RawmCommand *rawm_config_find_command(const RawmConfig *cfg,
                                             const char *name);
const RawmTagConfig *rawm_config_tags_for_monitor(const RawmConfig *cfg,
                                                   int monitor);
const char *rawm_layout_name(RawmLayoutId layout);

#endif
