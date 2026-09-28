/* SPDX-License-Identifier: MIT */
#ifndef RAWM_STATUS_ALSA_H
#define RAWM_STATUS_ALSA_H

#include "config.h"
#include "control.h"
#include "status.h"

#include <poll.h>
#include <stddef.h>
#include <stdio.h>

typedef struct RawmStatusAlsa RawmStatusAlsa;

int rawm_status_alsa_sample(RawmStatusAlsa **state,
                            const RawmStatusSourceConfig *config,
                            char *buffer, size_t size,
                            RawmStatusState *semantic, FILE *err);
int rawm_status_alsa_control(RawmStatusAlsa **state,
                             const RawmStatusSourceConfig *config,
                             RawmStatusControl control, FILE *err);
void rawm_status_alsa_destroy(RawmStatusAlsa **state);
size_t rawm_status_alsa_pollfd_count(RawmStatusAlsa *state);
int rawm_status_alsa_pollfds(RawmStatusAlsa *state, struct pollfd *pfds,
                             size_t count);
int rawm_status_alsa_handle_events(RawmStatusAlsa **state,
                                   struct pollfd *pfds, size_t count,
                                   FILE *err);

#endif
