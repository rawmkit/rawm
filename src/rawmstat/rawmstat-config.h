/* SPDX-License-Identifier: MIT */
#ifndef RAWMSTAT_CONFIG_H
#define RAWMSTAT_CONFIG_H

#include <stddef.h>
#include <stdio.h>

#define RAWMSTAT_DEFAULT_EXEC_TIMEOUT_MS 2000u
#define RAWMSTAT_MAX_PERIOD_MS 3600000u

typedef enum {
  RAWMSTAT_PROVIDER_BATTERY,
  RAWMSTAT_PROVIDER_CLOCK,
  RAWMSTAT_PROVIDER_CPU,
  RAWMSTAT_PROVIDER_EXEC,
  RAWMSTAT_PROVIDER_FILESYSTEM,
  RAWMSTAT_PROVIDER_MEMORY,
  RAWMSTAT_PROVIDER_NETWORK
} RawmstatProvider;

typedef struct {
  char *id;
  RawmstatProvider provider;
  unsigned int period_ms;
  unsigned int warning;
  unsigned int critical;
  char *format;
  char *path;
  char *interface;
  char **argv;
  size_t argc;
  unsigned int timeout_ms;
} RawmstatSourceConfig;

typedef struct {
  RawmstatSourceConfig *sources;
  size_t source_count;
} RawmstatConfig;

void rawmstat_config_init(RawmstatConfig *cfg);
void rawmstat_config_destroy(RawmstatConfig *cfg);
int rawmstat_config_load(RawmstatConfig *cfg, const char *explicit_path,
                         FILE *err);
int rawmstat_config_validate(const RawmstatConfig *cfg, FILE *err);
const char *rawmstat_provider_name(RawmstatProvider provider);

#endif
