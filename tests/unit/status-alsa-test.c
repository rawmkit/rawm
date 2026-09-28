#include "status/alsa.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct _snd_mixer {
  int marker;
};

struct _snd_mixer_elem {
  int marker;
};

struct _snd_mixer_selem_id {
  int marker;
};

static struct _snd_mixer mixer;
static struct _snd_mixer_elem element;
static int muted;
static int handled_events;

static void
fail(const char *message)
{
  fprintf(stderr, "status ALSA test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

int
snd_mixer_open(snd_mixer_t **out, int mode)
{
  (void)mode;
  *out = &mixer;
  return 0;
}

int
snd_mixer_close(snd_mixer_t *handle)
{
  (void)handle;
  return 0;
}

int
snd_mixer_attach(snd_mixer_t *handle, const char *name)
{
  (void)handle;
  return strcmp(name, "default") ? -1 : 0;
}

int
snd_mixer_selem_register(snd_mixer_t *handle,
                         struct snd_mixer_selem_regopt *options,
                         snd_mixer_class_t **classp)
{
  (void)handle;
  (void)options;
  (void)classp;
  return 0;
}

int
snd_mixer_load(snd_mixer_t *handle)
{
  (void)handle;
  return 0;
}

size_t
snd_mixer_selem_id_sizeof(void)
{
  return sizeof(struct _snd_mixer_selem_id);
}

void
snd_mixer_selem_id_set_index(snd_mixer_selem_id_t *id, unsigned int index)
{
  (void)id;
  (void)index;
}

void
snd_mixer_selem_id_set_name(snd_mixer_selem_id_t *id, const char *name)
{
  (void)id;
  (void)name;
}

snd_mixer_elem_t *
snd_mixer_find_selem(snd_mixer_t *handle, const snd_mixer_selem_id_t *id)
{
  (void)handle;
  (void)id;
  return &element;
}

int
snd_mixer_selem_has_playback_volume(snd_mixer_elem_t *elem)
{
  (void)elem;
  return 1;
}

int
snd_mixer_selem_get_playback_volume_range(snd_mixer_elem_t *elem,
                                           long *minimum, long *maximum)
{
  (void)elem;
  *minimum = 0;
  *maximum = 100;
  return 0;
}

int
snd_mixer_selem_has_playback_switch(snd_mixer_elem_t *elem)
{
  (void)elem;
  return 1;
}

int
snd_mixer_selem_has_playback_channel(snd_mixer_elem_t *elem,
                                     snd_mixer_selem_channel_id_t channel)
{
  (void)elem;
  return channel == SND_MIXER_SCHN_FRONT_LEFT ||
         channel == SND_MIXER_SCHN_FRONT_RIGHT;
}

int
snd_mixer_selem_get_playback_volume(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel,
                                    long *value)
{
  (void)elem;
  *value = channel == SND_MIXER_SCHN_FRONT_LEFT ? 50 : 100;
  return 0;
}

int
snd_mixer_selem_get_playback_switch(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel,
                                    int *value)
{
  (void)elem;
  (void)channel;
  *value = !muted;
  return 0;
}

int
snd_mixer_poll_descriptors_count(snd_mixer_t *handle)
{
  (void)handle;
  return 1;
}

int
snd_mixer_poll_descriptors(snd_mixer_t *handle, struct pollfd *pfds,
                           unsigned int count)
{
  (void)handle;
  if (count != 1)
    return -1;
  pfds[0].fd = 7;
  pfds[0].events = POLLIN;
  return 1;
}

int
snd_mixer_poll_descriptors_revents(snd_mixer_t *handle, struct pollfd *pfds,
                                   unsigned int count,
                                   unsigned short *revents)
{
  (void)handle;
  if (count != 1)
    return -1;
  *revents = pfds[0].revents;
  return 0;
}

int
snd_mixer_handle_events(snd_mixer_t *handle)
{
  (void)handle;
  ++handled_events;
  return 0;
}

const char *
snd_strerror(int error)
{
  (void)error;
  return "test error";
}

int
main(void)
{
  RawmStatusSourceConfig config = {0};
  RawmStatusAlsa *state = NULL;
  RawmStatusState semantic = RAWM_STATUS_NORMAL;
  struct pollfd pfd = {0};
  char buffer[64];

  config.device = "default";
  config.control = "Master";

  if (rawm_status_alsa_sample(&state, &config, buffer, sizeof(buffer),
                              &semantic, stderr) < 0)
    fail("normal sample failed");
  if (strcmp(buffer, "vol 75%") || semantic != RAWM_STATUS_NORMAL)
    fail("multi-channel volume was not averaged");

  muted = 1;
  if (rawm_status_alsa_sample(&state, &config, buffer, sizeof(buffer),
                              &semantic, stderr) < 0)
    fail("muted sample failed");
  if (strcmp(buffer, "vol mute") || semantic != RAWM_STATUS_WARNING)
    fail("mute did not produce warning semantics");

  if (rawm_status_alsa_pollfd_count(state) != 1)
    fail("mixer poll descriptor count changed");
  if (rawm_status_alsa_pollfds(state, &pfd, 1) < 0 ||
      pfd.fd != 7 || !(pfd.events & POLLIN))
    fail("mixer poll descriptor was not exported");

  pfd.revents = POLLIN;
  if (rawm_status_alsa_handle_events(&state, &pfd, 1, stderr) != 1 ||
      handled_events != 1)
    fail("mixer input event was not handled");

  pfd.revents = POLLHUP;
  if (rawm_status_alsa_handle_events(&state, &pfd, 1, stderr) != -1 || state)
    fail("mixer hangup did not invalidate provider state");

  return EXIT_SUCCESS;
}
