/* SPDX-License-Identifier: MIT */
#ifndef RAWM_STATUS_WORKER_H
#define RAWM_STATUS_WORKER_H

#include "config.h"
#include "control.h"
#include "status.h"

#include <stdio.h>
#include <sys/types.h>

typedef struct {
  int fd;
  pid_t pid;
} RawmStatusWorker;

void rawm_status_worker_init(RawmStatusWorker *worker);
int rawm_status_worker_start(RawmStatusWorker *worker,
                             const RawmStatusConfig *config, FILE *err);
void rawm_status_worker_stop(RawmStatusWorker *worker, FILE *err);
int rawm_status_worker_receive(RawmStatusWorker *worker,
                               RawmStatusData *status, FILE *err);
int rawm_status_worker_control(RawmStatusWorker *worker,
                               RawmStatusControl control, FILE *err);
int rawm_status_worker_debug(const RawmStatusConfig *config, FILE *out,
                             FILE *err);

#endif
