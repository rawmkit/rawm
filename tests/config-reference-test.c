#include "../rawm-config.h"

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
    "commands = {\n"
    "  probe = (\"probe\", \"-fn\", \"${ui.font}\",\n"
    "           \"--opacity=${ui.opacity}\",\n"
    "           \"--inner=${layout.gaps.inner}\",\n"
    "           \"--outer=${layout.gaps.outer}\",\n"
    "           \"--smart=${layout.gaps.smart}\",\n"
    "           \"--bar=${ui.bar.position}\",\n"
    "           \"--tray=${ui.systray.show}\",\n"
    "           \"${ui.colors.selected.background}\",\n"
    "           \"$${HOME}\");\n"
    "};\n"
    "ui = {\n"
    "  font = \"face-${ui.colors.selected.foreground}\";\n"
    "  opacity = 0.85;\n"
    "  bar = { position = \"top\"; };\n"
    "  systray = { show = false; };\n"
    "  colors = {\n"
    "    normal = { foreground = \"#111111\"; background = \"#222222\"; };\n"
    "    selected = {\n"
    "      foreground = \"${ui.colors.normal.foreground}\";\n"
    "      background = \"${ui.colors.normal.background}\";\n"
    "    };\n"
    "  };\n"
    "};\n"
    "layout = { gaps = { inner = 7; outer = 11; smart = true; }; };\n";
  static const char *const expected[] = {
    "probe", "-fn", "face-#111111", "--opacity=0.85", "--inner=7",
    "--outer=11", "--smart=true", "--bar=top", "--tray=false",
    "#222222", "${HOME}"
  };
  char path[] = "/tmp/rawm-config-reference.XXXXXX";
  RawmConfig cfg;
  const RawmCommand *cmd;
  int fd;
  size_t i;

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

  if (strcmp(cfg.font, "face-#111111"))
    fail("recursive string reference did not resolve");
  if (strcmp(cfg.colors[1][2], "#222222"))
    fail("color alias did not resolve");

  cmd = rawm_config_find_command(&cfg, "probe");
  if (!cmd)
    fail("probe command is missing");
  if (cmd->argc != sizeof(expected) / sizeof(expected[0]))
    fail("probe command argument count changed");
  for (i = 0; i < cmd->argc; ++i)
    if (strcmp(cmd->argv[i], expected[i]))
      fail("command argument reference resolved incorrectly");

  rawm_config_destroy(&cfg);
  return EXIT_SUCCESS;
}
