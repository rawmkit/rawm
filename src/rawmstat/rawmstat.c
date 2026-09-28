/* SPDX-License-Identifier: MIT */
#include "rawmstat-config.h"
#include "rawmstat-source.h"
#include "status.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef VERSION
#define VERSION "unknown"
#endif

static volatile sig_atomic_t terminate_requested;
static volatile sig_atomic_t reload_requested;

static void
usage(FILE *out)
{
  fputs("usage: rawmstat [-p] [-c path]\n"
        "       rawmstat -C [-c path]\n"
        "       rawmstat -v\n", out);
}

static void
handle_signal(int signum)
{
  if (signum == SIGHUP)
    reload_requested = 1;
  else
    terminate_requested = 1;
}

static int
install_signals(void)
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  sigemptyset(&action.sa_mask);
  action.sa_handler = handle_signal;
  if (sigaction(SIGINT, &action, NULL) < 0 ||
      sigaction(SIGTERM, &action, NULL) < 0 ||
      sigaction(SIGHUP, &action, NULL) < 0) {
    perror("rawmstat: sigaction");
    return -1;
  }
  return 0;
}

static uint64_t
monotonic_ms(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
    perror("rawmstat: clock_gettime");
    exit(EXIT_FAILURE);
  }
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static RawmstatSource *
create_sources(const RawmstatConfig *cfg)
{
  RawmstatSource *sources;
  size_t i;

  if (!cfg->source_count)
    return NULL;
  sources = calloc(cfg->source_count, sizeof(*sources));
  if (!sources) {
    fputs("rawmstat: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  for (i = 0; i < cfg->source_count; ++i)
    rawmstat_source_init(&sources[i], &cfg->sources[i]);
  return sources;
}

static void
destroy_sources(RawmstatSource *sources, size_t count)
{
  size_t i;

  for (i = 0; i < count; ++i)
    rawmstat_source_destroy(&sources[i]);
  free(sources);
}

static RawmStatusResult
encode_snapshot(const RawmstatSource *sources, size_t count, char **value,
                size_t *length)
{
  RawmStatusView *views;
  size_t i, n = 0;
  RawmStatusResult result;

  if (!count)
    return rawm_status_v3_encode(NULL, 0, value, length);
  views = calloc(count, sizeof(*views));
  if (!views)
    return RAWM_STATUS_NO_MEMORY;
  for (i = 0; i < count; ++i) {
    if (!sources[i].has_value)
      continue;
    views[n].id = sources[i].config->id;
    views[n].text = sources[i].text;
    views[n].state = sources[i].state;
    ++n;
  }
  result = rawm_status_v3_encode(views, n, value, length);
  free(views);
  return result;
}

static int
publish_stdout(const char *value, size_t length)
{
  if (length && fwrite(value, 1, length, stdout) != length)
    return -1;
  if (fputc('\n', stdout) == EOF || fflush(stdout) == EOF)
    return -1;
  return 0;
}

static int
publish_x11(Display *display, Window root, Atom property, Atom utf8,
            const char *value, size_t length)
{
  XChangeProperty(display, root, property, utf8, 8, PropModeReplace,
                  (const unsigned char *)value, (int)length);
  XFlush(display);
  return 0;
}

static int
publish_snapshot(const RawmstatSource *sources, size_t count, bool print,
                 Display *display, Window root, Atom property, Atom utf8)
{
  char *value = NULL;
  size_t length = 0;
  RawmStatusResult result;
  int rc;

  result = encode_snapshot(sources, count, &value, &length);
  if (result == RAWM_STATUS_NO_MEMORY) {
    fputs("rawmstat: out of memory\n", stderr);
    return -1;
  }
  if (result == RAWM_STATUS_TOO_LARGE) {
    fprintf(stderr, "rawmstat: snapshot exceeds %u-byte rawm-v3 limit\n",
            RAWM_STATUS_V3_MAX);
    return 0;
  }
  if (result != RAWM_STATUS_OK) {
    fputs("rawmstat: internal invalid rawm-v3 snapshot\n", stderr);
    return -1;
  }
  rc = print ? publish_stdout(value, length)
             : publish_x11(display, root, property, utf8, value, length);
  free(value);
  return rc;
}

static int
reload_config(RawmstatConfig *cfg, RawmstatSource **sources,
              const char *path)
{
  RawmstatConfig replacement;
  RawmstatSource *replacement_sources;

  rawmstat_config_init(&replacement);
  if (rawmstat_config_load(&replacement, path, stderr) < 0) {
    rawmstat_config_destroy(&replacement);
    fputs("rawmstat: reload rejected; keeping current configuration\n", stderr);
    return -1;
  }
  replacement_sources = create_sources(&replacement);
  destroy_sources(*sources, cfg->source_count);
  rawmstat_config_destroy(cfg);
  *cfg = replacement;
  *sources = replacement_sources;
  fputs("rawmstat: configuration reloaded\n", stderr);
  return 0;
}

static int
run(RawmstatConfig *cfg, const char *path, bool print)
{
  RawmstatSource *sources = create_sources(cfg);
  Display *display = NULL;
  Window root = 0;
  Atom property = None, utf8 = None;
  bool publish = true;
  int rc = EXIT_SUCCESS;

  if (!print) {
    display = XOpenDisplay(NULL);
    if (!display) {
      fputs("rawmstat: cannot open display\n", stderr);
      destroy_sources(sources, cfg->source_count);
      return EXIT_FAILURE;
    }
    root = DefaultRootWindow(display);
    property = XInternAtom(display, "_RAWM_STATUS_V3", False);
    utf8 = XInternAtom(display, "UTF8_STRING", False);
  }

  if (install_signals() < 0) {
    rc = EXIT_FAILURE;
    goto out;
  }

  while (!terminate_requested) {
    uint64_t now = monotonic_ms();
    uint64_t next = now + 1000u;
    size_t i;

    if (reload_requested) {
      reload_requested = 0;
      if (reload_config(cfg, &sources, path) == 0)
        publish = true;
      now = monotonic_ms();
    }

    for (i = 0; i < cfg->source_count; ++i) {
      if (!sources[i].next_due_ms || now >= sources[i].next_due_ms) {
        if (rawmstat_source_refresh(&sources[i], now, stderr) > 0)
          publish = true;
      }
      if (sources[i].next_due_ms < next)
        next = sources[i].next_due_ms;
    }

    if (publish) {
      if (publish_snapshot(sources, cfg->source_count, print, display, root,
                           property, utf8) < 0) {
        rc = EXIT_FAILURE;
        break;
      }
      publish = false;
    }

    if (!terminate_requested) {
      int timeout;
      now = monotonic_ms();
      timeout = next <= now ? 0 : (int)(next - now);
      if (timeout > 1000)
        timeout = 1000;
      if (poll(NULL, 0, timeout) < 0 && errno != EINTR) {
        perror("rawmstat: poll");
        rc = EXIT_FAILURE;
        break;
      }
    }
  }

out:
  if (display) {
    XDeleteProperty(display, root, property);
    XFlush(display);
    XCloseDisplay(display);
  }
  destroy_sources(sources, cfg->source_count);
  return rc;
}

int
main(int argc, char **argv)
{
  RawmstatConfig config;
  const char *config_path = NULL;
  bool check = false, print = false;
  int option, rc;

  while ((option = getopt(argc, argv, "c:Cpv")) != -1) {
    switch (option) {
    case 'c':
      config_path = optarg;
      break;
    case 'C':
      check = true;
      break;
    case 'p':
      print = true;
      break;
    case 'v':
      printf("rawmstat %s\n", VERSION);
      return EXIT_SUCCESS;
    default:
      usage(stderr);
      return EXIT_FAILURE;
    }
  }
  if (optind != argc) {
    usage(stderr);
    return EXIT_FAILURE;
  }

  rawmstat_config_init(&config);
  if (rawmstat_config_load(&config, config_path, stderr) < 0) {
    rawmstat_config_destroy(&config);
    return EXIT_FAILURE;
  }
  if (check) {
    rawmstat_config_destroy(&config);
    return EXIT_SUCCESS;
  }
  rc = run(&config, config_path, print);
  rawmstat_config_destroy(&config);
  return rc;
}
