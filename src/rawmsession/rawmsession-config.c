#include "rawmsession-config.h"

#include <ctype.h>
#include <errno.h>
#include <libconfig.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

static void *
xcalloc(size_t count, size_t size)
{
  void *ptr;

  if (count == 0)
    count = 1;
  if (size == 0)
    size = 1;
  ptr = calloc(count, size);
  if (!ptr) {
    fputs("rawmsession: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return ptr;
}

static char *
xstrdup(const char *value)
{
  char *copy = strdup(value ? value : "");

  if (!copy) {
    fputs("rawmsession: out of memory\n", stderr);
    exit(EXIT_FAILURE);
  }
  return copy;
}

static void
command_destroy(RawmSessionCommand *command)
{
  size_t i;

  if (!command)
    return;
  free(command->name);
  for (i = 0; i < command->argc; ++i)
    free(command->argv[i]);
  free(command->argv);
  memset(command, 0, sizeof(*command));
}

static void
sidecars_destroy(RawmSessionConfig *cfg)
{
  size_t i;

  for (i = 0; i < cfg->sidecar_count; ++i)
    command_destroy(&cfg->sidecars[i]);
  free(cfg->sidecars);
  cfg->sidecars = NULL;
  cfg->sidecar_count = 0;
}

static void
command_set(RawmSessionCommand *command, const char *name,
            const char *const argv[], size_t argc)
{
  size_t i;

  command_destroy(command);
  command->name = name ? xstrdup(name) : NULL;
  command->argv = xcalloc(argc + 1, sizeof(*command->argv));
  command->argc = argc;
  for (i = 0; i < argc; ++i)
    command->argv[i] = xstrdup(argv[i]);
}

void
rawmsession_config_init(RawmSessionConfig *cfg)
{
  static const char *const primary[] = { "rawm" };
  static const char *const compositor[] = { "picom" };

  memset(cfg, 0, sizeof(*cfg));
  command_set(&cfg->primary, "primary", primary, 1);
  cfg->sidecars = xcalloc(1, sizeof(*cfg->sidecars));
  cfg->sidecar_count = 1;
  command_set(&cfg->sidecars[0], "compositor", compositor, 1);
}

void
rawmsession_config_destroy(RawmSessionConfig *cfg)
{
  command_destroy(&cfg->primary);
  sidecars_destroy(cfg);
}

static int
cfg_error(FILE *err, const config_setting_t *setting, const char *fmt,
          const char *detail)
{
  const char *file = setting ? config_setting_source_file(setting) : NULL;
  unsigned int line = setting ? config_setting_source_line(setting) : 0;

  if (file && line)
    fprintf(err, "rawmsession: %s:%u: ", file, line);
  else
    fputs("rawmsession: configuration error: ", err);
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

static bool
sidecar_name_valid(const char *name)
{
  const unsigned char *p;

  if (!name || !*name)
    return false;
  for (p = (const unsigned char *)name; *p; ++p)
    if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.'))
      return false;
  return true;
}

static int
parse_command(FILE *err, const config_setting_t *setting,
              const char *description, const char *name,
              RawmSessionCommand *command)
{
  char **argv;
  int i, argc;

  if (!setting || (config_setting_type(setting) != CONFIG_TYPE_LIST &&
                   config_setting_type(setting) != CONFIG_TYPE_ARRAY))
    return cfg_error(err, setting, "%s must be a list", description);
  argc = config_setting_length(setting);
  if (argc <= 0)
    return cfg_error(err, setting, "%s must not be empty", description);

  argv = xcalloc((size_t)argc + 1, sizeof(*argv));
  for (i = 0; i < argc; ++i) {
    config_setting_t *arg = config_setting_get_elem(setting, (unsigned int)i);
    const char *value;

    if (!arg || config_setting_type(arg) != CONFIG_TYPE_STRING) {
      while (i > 0)
        free(argv[--i]);
      free(argv);
      return cfg_error(err, arg ? arg : setting,
                       "%s entries must be strings", description);
    }
    value = config_setting_get_string(arg);
    if (!value || !*value) {
      while (i > 0)
        free(argv[--i]);
      free(argv);
      return cfg_error(err, arg, "%s entries must not be empty", description);
    }
    argv[i] = xstrdup(value);
  }

  command_destroy(command);
  command->name = name ? xstrdup(name) : NULL;
  command->argv = argv;
  command->argc = (size_t)argc;
  return 0;
}

static int
parse_sidecars(FILE *err, const config_setting_t *list,
               RawmSessionConfig *cfg)
{
  static const char *const allowed[] = { "name", "command" };
  RawmSessionCommand *sidecars;
  int i, n;

  if (!list || (config_setting_type(list) != CONFIG_TYPE_LIST &&
                config_setting_type(list) != CONFIG_TYPE_ARRAY))
    return cfg_error(err, list, "sidecars must be a list%s", "");

  n = config_setting_length(list);
  sidecars = xcalloc((size_t)n, sizeof(*sidecars));
  for (i = 0; i < n; ++i) {
    config_setting_t *entry = config_setting_get_elem(list, (unsigned int)i);
    config_setting_t *setting;
    const char *name;

    if (validate_members(err, entry, allowed, 2) < 0)
      goto fail;

    setting = config_setting_get_member(entry, "name");
    if (!setting || config_setting_type(setting) != CONFIG_TYPE_STRING) {
      cfg_error(err, setting ? setting : entry,
                "sidecar requires string setting '%s'", "name");
      goto fail;
    }
    name = config_setting_get_string(setting);
    if (!sidecar_name_valid(name)) {
      cfg_error(err, setting,
                "sidecar name must use characters from %s",
                "[A-Za-z0-9_.-]");
      goto fail;
    }

    setting = config_setting_get_member(entry, "command");
    if (parse_command(err, setting, "sidecar command", name,
                      &sidecars[i]) < 0)
      goto fail;
  }

  sidecars_destroy(cfg);
  cfg->sidecars = sidecars;
  cfg->sidecar_count = (size_t)n;
  return 0;

fail:
  while (i >= 0) {
    command_destroy(&sidecars[i]);
    --i;
  }
  free(sidecars);
  return -1;
}

static int
parse_layer(FILE *err, RawmSessionConfig *cfg, config_t *source)
{
  static const char *const allowed[] = { "primary", "sidecars" };
  config_setting_t *root = config_root_setting(source);
  config_setting_t *setting;

  if (validate_members(err, root, allowed, 2) < 0)
    return -1;

  setting = config_setting_get_member(root, "primary");
  if (setting && parse_command(err, setting, "primary", "primary",
                               &cfg->primary) < 0)
    return -1;

  setting = config_setting_get_member(root, "sidecars");
  if (setting && parse_sidecars(err, setting, cfg) < 0)
    return -1;

  return 0;
}

static char *
dirname_dup(const char *path)
{
  const char *slash = strrchr(path, '/');
  size_t len;
  char *dir;

  if (!slash)
    return xstrdup(".");
  if (slash == path)
    return xstrdup("/");
  len = (size_t)(slash - path);
  dir = xcalloc(len + 1, 1);
  memcpy(dir, path, len);
  return dir;
}

static int
load_file(FILE *err, RawmSessionConfig *cfg, const char *path, bool required)
{
  config_t source;
  struct stat st;
  char *dir;
  int rc;

  if (stat(path, &st) < 0) {
    if (!required && errno == ENOENT)
      return 0;
    fprintf(err, "rawmsession: cannot read configuration %s: %s\n", path,
            strerror(errno));
    return -1;
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(err, "rawmsession: configuration %s is not a regular file\n",
            path);
    return -1;
  }

  config_init(&source);
  dir = dirname_dup(path);
  config_set_include_dir(&source, dir);
  if (!config_read_file(&source, path)) {
    fprintf(err, "rawmsession: %s:%d: %s\n",
            config_error_file(&source) ? config_error_file(&source) : path,
            config_error_line(&source),
            config_error_text(&source) ? config_error_text(&source)
                                       : "configuration error");
    free(dir);
    config_destroy(&source);
    return -1;
  }
  rc = parse_layer(err, cfg, &source);
  free(dir);
  config_destroy(&source);
  return rc;
}

static char *
user_config_path(void)
{
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home;
  size_t len;
  char *path;

  if (xdg && *xdg && xdg[0] == '/') {
    len = strlen(xdg) + strlen("/rawm/rawmsession.conf") + 1;
    path = xcalloc(len, 1);
    snprintf(path, len, "%s/rawm/rawmsession.conf", xdg);
    return path;
  }
  home = getenv("HOME");
  if (!home || !*home)
    return NULL;
  len = strlen(home) + strlen("/.config/rawm/rawmsession.conf") + 1;
  path = xcalloc(len, 1);
  snprintf(path, len, "%s/.config/rawm/rawmsession.conf", home);
  return path;
}

int
rawmsession_config_validate(const RawmSessionConfig *cfg, FILE *err)
{
  size_t i, j;

  if (!cfg->primary.argv || cfg->primary.argc == 0 ||
      !cfg->primary.argv[0] || !*cfg->primary.argv[0]) {
    fputs("rawmsession: configuration error: primary command is empty\n", err);
    return -1;
  }
  for (i = 0; i < cfg->sidecar_count; ++i) {
    const RawmSessionCommand *sidecar = &cfg->sidecars[i];

    if (!sidecar_name_valid(sidecar->name)) {
      fprintf(err,
              "rawmsession: configuration error: invalid sidecar name at index %zu\n",
              i);
      return -1;
    }
    if (!sidecar->argv || sidecar->argc == 0 ||
        !sidecar->argv[0] || !*sidecar->argv[0]) {
      fprintf(err,
              "rawmsession: configuration error: sidecar '%s' has an empty command\n",
              sidecar->name);
      return -1;
    }
    for (j = i + 1; j < cfg->sidecar_count; ++j)
      if (!strcmp(sidecar->name, cfg->sidecars[j].name)) {
        fprintf(err,
                "rawmsession: configuration error: duplicate sidecar name '%s'\n",
                sidecar->name);
        return -1;
      }
  }
  return 0;
}

int
rawmsession_config_load(RawmSessionConfig *cfg, const char *explicit_path,
                        FILE *err)
{
  char system_path[PATH_MAX];
  char *user_path;

  if (explicit_path) {
    if (load_file(err, cfg, explicit_path, true) < 0)
      return -1;
    return rawmsession_config_validate(cfg, err);
  }

  snprintf(system_path, sizeof(system_path), "%s/rawm/rawmsession.conf",
           SYSCONFDIR);
  if (load_file(err, cfg, system_path, false) < 0)
    return -1;
  user_path = user_config_path();
  if (user_path) {
    int rc = load_file(err, cfg, user_path, false);

    free(user_path);
    if (rc < 0)
      return -1;
  }
  return rawmsession_config_validate(cfg, err);
}
