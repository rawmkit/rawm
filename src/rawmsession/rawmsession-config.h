#ifndef RAWMSESSION_CONFIG_H
#define RAWMSESSION_CONFIG_H

#include <stddef.h>
#include <stdio.h>

typedef struct {
  char *name;
  char **argv;
  size_t argc;
} RawmSessionCommand;

typedef struct {
  RawmSessionCommand primary;
  RawmSessionCommand *sidecars;
  size_t sidecar_count;
} RawmSessionConfig;

void rawmsession_config_init(RawmSessionConfig *cfg);
void rawmsession_config_destroy(RawmSessionConfig *cfg);
int rawmsession_config_load(RawmSessionConfig *cfg, const char *explicit_path,
                            FILE *err);
int rawmsession_config_validate(const RawmSessionConfig *cfg, FILE *err);

#endif
