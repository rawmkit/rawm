/* SPDX-License-Identifier: MIT */
#ifndef RAWMSTAT_SOURCE_H
#define RAWMSTAT_SOURCE_H

#include "rawmstat-config.h"
#include "status.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const RawmstatSourceConfig *config;
  char *text;
  RawmStatusState state;
  bool has_value;
  bool cpu_baseline;
  uint64_t cpu_total;
  uint64_t cpu_idle;
  bool network_baseline;
  char *network_interface;
  uint64_t network_rx;
  uint64_t network_tx;
  uint64_t network_time_ms;
  uint64_t next_due_ms;
} RawmstatSource;

void rawmstat_source_init(RawmstatSource *source,
                          const RawmstatSourceConfig *config);
void rawmstat_source_destroy(RawmstatSource *source);
int rawmstat_source_refresh(RawmstatSource *source, uint64_t now_ms, FILE *err);

#endif
