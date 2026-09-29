#ifndef RAWM_CONFIG_H
#define RAWM_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define RAWM_MAX_TAGS 31
#define RAWM_TAG_NAME_MAX 22u
#define RAWM_COLOR_SCHEMES 4

typedef enum {
  RAWM_CENTER_NONE,
  RAWM_CENTER_MONITOR,
  RAWM_CENTER_PARENT
} RawmCenterMode;

typedef enum {
  RAWM_ATTENTION_NONE,
  RAWM_ATTENTION_URGENT
} RawmAttentionMode;

typedef enum {
  RAWM_SIZE_HINTS_NEVER,
  RAWM_SIZE_HINTS_FLOATING,
  RAWM_SIZE_HINTS_ALWAYS
} RawmSizeHintsMode;

typedef enum {
  RAWM_FLOATING_PLACEMENT_CLIENT,
  RAWM_FLOATING_PLACEMENT_CASCADE
} RawmFloatingPlacement;

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
  RAWM_POINTER_MOVE,
  RAWM_POINTER_TOGGLE_FLOATING,
  RAWM_POINTER_RESIZE
} RawmPointerAction;

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
  bool has_attention;
  RawmAttentionMode attention;
  int monitor;
} RawmRule;

typedef struct {
  unsigned int modifiers;
  unsigned int button;
  RawmPointerAction action;
} RawmPointerBinding;

typedef struct {
  char *colors[RAWM_COLOR_SCHEMES];
  unsigned int border_width;
  unsigned int snap;
  double opacity;

  RawmCenterMode dialog_center;
  RawmSizeHintsMode size_hints;
  RawmFloatingPlacement floating_placement;
  unsigned int cascade_step;

  double master_factor;
  int master_count;
  unsigned int gap_inner;
  unsigned int gap_outer;
  bool smart_gaps;

  RawmTagConfig *tags;
  size_t tag_count;
  RawmMonitorTags *monitor_tags;
  size_t monitor_tag_count;

  RawmRule *rules;
  size_t rule_count;
  RawmPointerBinding *pointer_bindings;
  size_t pointer_binding_count;
} RawmConfig;

void rawm_config_init(RawmConfig *cfg);
void rawm_config_destroy(RawmConfig *cfg);
int rawm_config_load(RawmConfig *cfg, const char *explicit_path, FILE *err);
int rawm_config_validate(const RawmConfig *cfg, FILE *err);
const RawmTagConfig *rawm_config_tags_for_monitor(const RawmConfig *cfg,
                                                   int monitor);
const char *rawm_layout_name(RawmLayoutId layout);

#endif
