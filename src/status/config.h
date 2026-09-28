/* SPDX-License-Identifier: MIT */
#ifndef RAWM_STATUS_CONFIG_H
#define RAWM_STATUS_CONFIG_H

#include <libconfig.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define RAWM_STATUS_DEFAULT_EXEC_TIMEOUT_MS 2000u
#define RAWM_STATUS_MAX_PERIOD_MS 3600000u

typedef enum {
  RAWM_STATUS_PROVIDER_ALSA,
  RAWM_STATUS_PROVIDER_BATTERY,
  RAWM_STATUS_PROVIDER_CLOCK,
  RAWM_STATUS_PROVIDER_CPU,
  RAWM_STATUS_PROVIDER_EXEC,
  RAWM_STATUS_PROVIDER_FILESYSTEM,
  RAWM_STATUS_PROVIDER_MEMORY,
  RAWM_STATUS_PROVIDER_NETWORK
} RawmStatusProvider;

typedef struct {
  char *id;
  bool enabled;
  RawmStatusProvider provider;
  unsigned int period_ms;
  unsigned int warning;
  unsigned int critical;
  unsigned int step;
  char *device;
  char *control;
  char *format;
  char *path;
  char *interface;
  char **argv;
  size_t argc;
  unsigned int timeout_ms;
} RawmStatusSourceConfig;

typedef struct {
  RawmStatusSourceConfig *sources;
  size_t source_count;
} RawmStatusConfig;

void rawm_status_config_init(RawmStatusConfig *cfg);
void rawm_status_config_destroy(RawmStatusConfig *cfg);
int rawm_status_config_parse_sources(RawmStatusConfig *cfg,
                                     const config_setting_t *group, FILE *err);
int rawm_status_config_validate(const RawmStatusConfig *cfg, FILE *err);
const char *rawm_status_provider_name(RawmStatusProvider provider);

#endif
