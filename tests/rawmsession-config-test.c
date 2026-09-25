#include "rawmsession-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
command_equal(const RawmSessionCommand *a, const RawmSessionCommand *b)
{
  size_t i;

  if (a->argc != b->argc)
    return 0;
  for (i = 0; i < a->argc; ++i)
    if (strcmp(a->argv[i], b->argv[i]))
      return 0;
  return 1;
}

int
main(int argc, char **argv)
{
  RawmSessionConfig defaults, installed;
  size_t i;
  int ok = 1;

  if (argc != 2) {
    fprintf(stderr, "usage: %s rawmsession.conf\n", argv[0]);
    return EXIT_FAILURE;
  }

  rawmsession_config_init(&defaults);
  rawmsession_config_init(&installed);
  if (rawmsession_config_load(&installed, argv[1], stderr) < 0) {
    ok = 0;
    goto out;
  }

  if (!command_equal(&defaults.primary, &installed.primary) ||
      defaults.sidecar_count != installed.sidecar_count) {
    ok = 0;
    goto out;
  }
  for (i = 0; i < defaults.sidecar_count; ++i)
    if (strcmp(defaults.sidecars[i].name, installed.sidecars[i].name) ||
        !command_equal(&defaults.sidecars[i], &installed.sidecars[i])) {
      ok = 0;
      break;
    }

out:
  rawmsession_config_destroy(&defaults);
  rawmsession_config_destroy(&installed);
  if (!ok)
    fputs("rawmsession compiled defaults differ from installed config\n", stderr);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
