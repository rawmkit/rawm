#include "rawm-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
fail(const char *message)
{
  fprintf(stderr, "config reference test failed: %s\n", message);
  exit(EXIT_FAILURE);
}

static void
write_all(int fd, const char *text)
{
  size_t len = strlen(text);
  const char *p = text;

  while (len) {
    ssize_t n = write(fd, p, len);
    if (n < 0)
      fail("cannot write temporary configuration");
    p += (size_t)n;
    len -= (size_t)n;
  }
}

int
main(void)
{
  static const char source[] =
    "behavior = { dialog_center = \"parent\"; size_hints = \"always\"; "
    "floating_placement = \"client\"; cascade_step = 48; };\n"
    "ui = {\n"
    "  font = \"face-${ui.colors.selected.foreground}-${ui.opacity}\";\n"
    "  opacity = 0.85;\n"
    "  colors = {\n"
    "    normal = { foreground = \"#111111\"; background = \"#222222\"; };\n"
    "    selected = {\n"
    "      border = \"#aabbcc\";\n"
    "      foreground = \"${ui.colors.normal.foreground}\";\n"
    "      background = \"${ui.colors.normal.background}\";\n"
    "    };\n"
    "  };\n"
    "};\n"
    "layout = { gaps = { inner = 7; outer = 11; smart = true; }; "
    "symbols = { tile = \"g${layout.gaps.inner}-${layout.gaps.smart}\"; }; };\n";
  char path[] = "/tmp/rawm-config-reference.XXXXXX";
  RawmConfig cfg;
  int fd;

  fd = mkstemp(path);
  if (fd < 0)
    fail("cannot create temporary configuration");
  write_all(fd, source);
  if (close(fd) < 0) {
    unlink(path);
    fail("cannot close temporary configuration");
  }

  rawm_config_init(&cfg);
  if (rawm_config_load(&cfg, path, stderr) < 0) {
    rawm_config_destroy(&cfg);
    unlink(path);
    fail("valid references were rejected");
  }
  unlink(path);

  if (strcmp(cfg.font, "face-#111111-0.85"))
    fail("recursive/scalar string reference did not resolve");
  if (strcmp(cfg.colors[1][2], "#222222"))
    fail("color alias did not resolve");
  if (strcmp(cfg.colors[3][0], "#aabbcc"))
    fail("floating color did not inherit selected border");
  if (strcmp(cfg.layout_symbols[RAWM_LAYOUT_TILE], "g7-true"))
    fail("layout scalar references did not resolve");

  rawm_config_destroy(&cfg);
  return EXIT_SUCCESS;
}
