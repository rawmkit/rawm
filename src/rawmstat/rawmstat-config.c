/* SPDX-License-Identifier: MIT */
#include "rawmstat-config.h"
#include "status.h"

#include <errno.h>
#include <libconfig.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

static void *
xcalloc(size_t n, size_t size)
{
  void *p = calloc(n, size);

  if (!p) {
    fputs("rawmstat: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return p;
}

static char *
xstrdup(const char *s)
{
  char *p = strdup(s ? s : "");

  if (!p) {
    fputs("rawmstat: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return p;
}

static void
free_source(RawmstatSourceConfig *source)
{
  size_t i;

  free(source->id);
  free(source->format);
  free(source->path);
  free(source->interface);
  for (i = 0; i < source->argc; ++i)
    free(source->argv[i]);
  free(source->argv);
  memset(source, 0, sizeof(*source));
}

static void
free_sources(RawmstatSourceConfig *sources, size_t count)
{
  size_t i;

  for (i = 0; i < count; ++i)
    free_source(&sources[i]);
  free(sources);
}

const char *
rawmstat_provider_name(RawmstatProvider provider)
{
  switch (provider) {
  case RAWMSTAT_PROVIDER_BATTERY:
    return "battery";
  case RAWMSTAT_PROVIDER_CLOCK:
    return "clock";
  case RAWMSTAT_PROVIDER_CPU:
    return "cpu";
  case RAWMSTAT_PROVIDER_EXEC:
    return "exec";
  case RAWMSTAT_PROVIDER_FILESYSTEM:
    return "filesystem";
  case RAWMSTAT_PROVIDER_MEMORY:
    return "memory";
  case RAWMSTAT_PROVIDER_NETWORK:
    return "network";
  }
  return NULL;
}

static int
parse_provider(const char *name, RawmstatProvider *provider)
{
  int i;

  for (i = RAWMSTAT_PROVIDER_BATTERY; i <= RAWMSTAT_PROVIDER_NETWORK; ++i) {
    if (!strcmp(name, rawmstat_provider_name((RawmstatProvider)i))) {
      *provider = (RawmstatProvider)i;
      return 0;
    }
  }
  return -1;
}

void
rawmstat_config_init(RawmstatConfig *cfg)
{
  RawmstatSourceConfig *source;

  memset(cfg, 0, sizeof(*cfg));
  cfg->sources = xcalloc(1, sizeof(*cfg->sources));
  cfg->source_count = 1;

  source = &cfg->sources[0];
  source->id = xstrdup("clock");
  source->provider = RAWMSTAT_PROVIDER_CLOCK;
  source->period_ms = 5000;
  source->format = xstrdup("%a %b %d %H:%M");
  source->timeout_ms = RAWMSTAT_DEFAULT_EXEC_TIMEOUT_MS;
}

void
rawmstat_config_destroy(RawmstatConfig *cfg)
{
  free_sources(cfg->sources, cfg->source_count);
  memset(cfg, 0, sizeof(*cfg));
}

static int
cfg_error(FILE *err, const config_setting_t *setting, const char *fmt,
          const char *detail)
{
  const char *file = setting ? config_setting_source_file(setting) : NULL;
  unsigned int line = setting ? config_setting_source_line(setting) : 0;

  if (file && line)
    fprintf(err, "rawmstat: %s:%u: ", file, line);
  else
    fputs("rawmstat: configuration error: ", err);
  fprintf(err, fmt, detail);
  fputc('\n', err);
  return -1;
}

static bool
name_allowed(const char *name, const char *const *allowed, size_t count)
{
  size_t i;

  for (i = 0; i < count; ++i)
    if (!strcmp(name, allowed[i]))
      return true;
  return false;
}

static int
validate_members(FILE *err, const config_setting_t *group,
                 const char *const *allowed, size_t count)
{
  int i, n;

  if (!group || config_setting_type(group) != CONFIG_TYPE_GROUP)
    return cfg_error(err, group, "expected a group%s", "");
  n = config_setting_length(group);
  for (i = 0; i < n; ++i) {
    config_setting_t *member = config_setting_get_elem(group, (unsigned int)i);
    const char *name = config_setting_name(member);

    if (!name_allowed(name, allowed, count))
      return cfg_error(err, member, "unknown setting '%s'", name);
  }
  return 0;
}

static int
read_uint(FILE *err, const config_setting_t *group, const char *name,
          unsigned int min, unsigned int max, unsigned int *out)
{
  config_setting_t *setting = config_setting_get_member(group, name);
  long long value;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_INT &&
      config_setting_type(setting) != CONFIG_TYPE_INT64)
    return cfg_error(err, setting, "%s must be an integer", name);
  value = config_setting_type(setting) == CONFIG_TYPE_INT
            ? (long long)config_setting_get_int(setting)
            : config_setting_get_int64(setting);
  if (value < (long long)min || value > (long long)max)
    return cfg_error(err, setting, "%s is out of range", name);
  *out = (unsigned int)value;
  return 0;
}

static int
replace_optional_string(FILE *err, const config_setting_t *group,
                        const char *name, char **out)
{
  config_setting_t *setting = config_setting_get_member(group, name);

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_STRING)
    return cfg_error(err, setting, "%s must be a string", name);
  free(*out);
  *out = xstrdup(config_setting_get_string(setting));
  return 0;
}

static int
parse_command(FILE *err, const config_setting_t *group,
              RawmstatSourceConfig *source)
{
  config_setting_t *setting = config_setting_get_member(group, "command");
  char **argv;
  int i, n;

  if (!setting)
    return 0;
  if (config_setting_type(setting) != CONFIG_TYPE_ARRAY &&
      config_setting_type(setting) != CONFIG_TYPE_LIST)
    return cfg_error(err, setting, "command must be a list%s", "");
  n = config_setting_length(setting);
  if (n <= 0)
    return cfg_error(err, setting, "command must not be empty%s", "");
  argv = xcalloc((size_t)n + 1, sizeof(*argv));
  for (i = 0; i < n; ++i) {
    config_setting_t *arg = config_setting_get_elem(setting, (unsigned int)i);

    if (config_setting_type(arg) != CONFIG_TYPE_STRING) {
      size_t j;
      for (j = 0; j < (size_t)i; ++j)
        free(argv[j]);
      free(argv);
      return cfg_error(err, arg, "command arguments must be strings%s", "");
    }
    argv[i] = xstrdup(config_setting_get_string(arg));
  }
  source->argv = argv;
  source->argc = (size_t)n;
  return 0;
}

static void
set_provider_defaults(RawmstatSourceConfig *source)
{
  source->period_ms = 5000;
  source->warning = 0;
  source->critical = 0;
  source->timeout_ms = RAWMSTAT_DEFAULT_EXEC_TIMEOUT_MS;

  switch (source->provider) {
  case RAWMSTAT_PROVIDER_BATTERY:
    source->period_ms = 15000;
    source->warning = 20;
    source->critical = 10;
    break;
  case RAWMSTAT_PROVIDER_CLOCK:
    source->format = xstrdup("%a %b %d %H:%M");
    break;
  case RAWMSTAT_PROVIDER_CPU:
    source->period_ms = 2000;
    source->warning = 80;
    source->critical = 95;
    break;
  case RAWMSTAT_PROVIDER_FILESYSTEM:
    source->period_ms = 60000;
    source->warning = 20;
    source->critical = 5;
    source->path = xstrdup("/");
    break;
  case RAWMSTAT_PROVIDER_MEMORY:
    source->warning = 85;
    source->critical = 95;
    break;
  case RAWMSTAT_PROVIDER_NETWORK:
    source->period_ms = 2000;
    source->interface = xstrdup("default");
    break;
  case RAWMSTAT_PROVIDER_EXEC:
    break;
  }
}

static int
parse_source(FILE *err, RawmstatSourceConfig *source,
             const config_setting_t *group)
{
  static const char *const allowed[] = {
    "provider", "period_ms", "warning", "critical", "format", "path",
    "interface", "command", "timeout_ms"
  };
  config_setting_t *setting;
  const char *provider_name;

  if (validate_members(err, group, allowed, sizeof(allowed) / sizeof(allowed[0])) < 0)
    return -1;

  setting = config_setting_get_member(group, "provider");
  if (!setting || config_setting_type(setting) != CONFIG_TYPE_STRING)
    return cfg_error(err, setting ? setting : group,
                     "source provider must be a string%s", "");
  provider_name = config_setting_get_string(setting);
  if (parse_provider(provider_name, &source->provider) < 0)
    return cfg_error(err, setting, "unknown provider '%s'", provider_name);
  set_provider_defaults(source);

  if (read_uint(err, group, "period_ms", 100, RAWMSTAT_MAX_PERIOD_MS,
                &source->period_ms) < 0 ||
      read_uint(err, group, "warning", 0, UINT_MAX, &source->warning) < 0 ||
      read_uint(err, group, "critical", 0, UINT_MAX, &source->critical) < 0 ||
      read_uint(err, group, "timeout_ms", 1, 600000, &source->timeout_ms) < 0 ||
      replace_optional_string(err, group, "format", &source->format) < 0 ||
      replace_optional_string(err, group, "path", &source->path) < 0 ||
      replace_optional_string(err, group, "interface", &source->interface) < 0 ||
      parse_command(err, group, source) < 0)
    return -1;
  return 0;
}

static int
parse_sources(FILE *err, RawmstatConfig *cfg, const config_setting_t *group)
{
  RawmstatSourceConfig *sources;
  int i, n;

  if (!group || config_setting_type(group) != CONFIG_TYPE_GROUP)
    return cfg_error(err, group, "sources must be a group%s", "");
  n = config_setting_length(group);
  sources = n > 0 ? xcalloc((size_t)n, sizeof(*sources)) : NULL;
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(group, (unsigned int)i);
    const char *name = config_setting_name(entry);

    if (!name || !rawm_status_id_valid((const unsigned char *)name, strlen(name))) {
      free_sources(sources, (size_t)i);
      return cfg_error(err, entry, "invalid source id '%s'", name ? name : "");
    }
    sources[i].id = xstrdup(name);
    if (parse_source(err, &sources[i], entry) < 0) {
      free_sources(sources, (size_t)i + 1);
      return -1;
    }
  }

  free_sources(cfg->sources, cfg->source_count);
  cfg->sources = sources;
  cfg->source_count = (size_t)n;
  return 0;
}

int
rawmstat_config_validate(const RawmstatConfig *cfg, FILE *err)
{
  size_t i, j;

  for (i = 0; i < cfg->source_count; ++i) {
    const RawmstatSourceConfig *source = &cfg->sources[i];

    if (!rawm_status_id_valid((const unsigned char *)source->id,
                              strlen(source->id))) {
      fprintf(err, "rawmstat: configuration error: invalid source id '%s'\n",
              source->id ? source->id : "");
      return -1;
    }
    for (j = 0; j < i; ++j) {
      if (!strcmp(cfg->sources[j].id, source->id)) {
        fprintf(err, "rawmstat: configuration error: duplicate source '%s'\n",
                source->id);
        return -1;
      }
    }
    if (!rawmstat_provider_name(source->provider)) {
      fprintf(err, "rawmstat: configuration error: source '%s' has invalid provider\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWMSTAT_PROVIDER_EXEC && !source->argc) {
      fprintf(err, "rawmstat: configuration error: exec source '%s' has no command\n",
              source->id);
      return -1;
    }
    if (source->provider != RAWMSTAT_PROVIDER_EXEC && source->argc) {
      fprintf(err, "rawmstat: configuration error: source '%s' command is valid only for exec provider\n",
              source->id);
      return -1;
    }
    if ((source->provider == RAWMSTAT_PROVIDER_CPU ||
         source->provider == RAWMSTAT_PROVIDER_MEMORY) &&
        source->warning >= source->critical) {
      fprintf(err, "rawmstat: configuration error: source '%s' requires warning < critical\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWMSTAT_PROVIDER_BATTERY &&
        source->critical >= source->warning) {
      fprintf(err, "rawmstat: configuration error: source '%s' requires critical < warning\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWMSTAT_PROVIDER_FILESYSTEM &&
        source->critical >= source->warning) {
      fprintf(err, "rawmstat: configuration error: source '%s' requires critical < warning\n",
              source->id);
      return -1;
    }
    if (source->provider == RAWMSTAT_PROVIDER_CLOCK &&
        (!source->format || !source->format[0])) {
      fprintf(err, "rawmstat: configuration error: clock source '%s' has empty format\n",
              source->id);
      return -1;
    }
  }
  return 0;
}

static int
load_file(RawmstatConfig *cfg, const char *path, FILE *err)
{
  config_t config;
  config_setting_t *root, *sources;
  struct stat st;
  int rc = -1;

  if (stat(path, &st) < 0) {
    fprintf(err, "rawmstat: cannot read configuration %s: %s\n", path,
            strerror(errno));
    return -1;
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(err, "rawmstat: configuration %s is not a regular file\n", path);
    return -1;
  }

  config_init(&config);
  if (!config_read_file(&config, path)) {
    fprintf(err, "rawmstat: %s:%d: %s\n",
            config_error_file(&config) ? config_error_file(&config) : path,
            config_error_line(&config), config_error_text(&config));
    goto out;
  }
  root = config_root_setting(&config);
  {
    static const char *const allowed[] = { "sources" };
    if (validate_members(err, root, allowed, 1) < 0)
      goto out;
  }
  sources = config_setting_get_member(root, "sources");
  if (sources && parse_sources(err, cfg, sources) < 0)
    goto out;
  rc = rawmstat_config_validate(cfg, err);
out:
  config_destroy(&config);
  return rc;
}

static char *
user_config_path(void)
{
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  char *path;
  size_t len;

  if (xdg && xdg[0]) {
    len = strlen(xdg) + strlen("/rawm/rawmstat.conf") + 1;
    path = xcalloc(len, 1);
    snprintf(path, len, "%s/rawm/rawmstat.conf", xdg);
    return path;
  }
  if (!home || !home[0])
    return NULL;
  len = strlen(home) + strlen("/.config/rawm/rawmstat.conf") + 1;
  path = xcalloc(len, 1);
  snprintf(path, len, "%s/.config/rawm/rawmstat.conf", home);
  return path;
}

int
rawmstat_config_load(RawmstatConfig *cfg, const char *explicit_path, FILE *err)
{
  char system_path[PATH_MAX];
  char *user_path;
  struct stat st;
  int rc;

  if (explicit_path)
    return load_file(cfg, explicit_path, err);

  snprintf(system_path, sizeof(system_path), "%s/rawm/rawmstat.conf",
           SYSCONFDIR);
  if (stat(system_path, &st) == 0 && load_file(cfg, system_path, err) < 0)
    return -1;

  user_path = user_config_path();
  if (user_path && stat(user_path, &st) == 0) {
    rc = load_file(cfg, user_path, err);
    free(user_path);
    return rc;
  }
  free(user_path);
  return rawmstat_config_validate(cfg, err);
}
