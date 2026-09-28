/* SPDX-License-Identifier: MIT */
#ifndef RAWM_STATUS_SOURCE_H
#define RAWM_STATUS_SOURCE_H

#include "config.h"
#include "status.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const RawmStatusSourceConfig *config;
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
} RawmStatusSource;

void rawm_status_source_init(RawmStatusSource *source,
                             const RawmStatusSourceConfig *config);
void rawm_status_source_destroy(RawmStatusSource *source);
int rawm_status_source_refresh(RawmStatusSource *source, uint64_t now_ms,
                               const volatile sig_atomic_t *terminate, FILE *err);

#endif
