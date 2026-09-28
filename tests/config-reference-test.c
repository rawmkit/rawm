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
    "commands = {\n"
    "  probe = (\"probe\", \"-fn\", \"${ui.font}\",\n"
    "           \"--opacity=${ui.opacity}\",\n"
    "           \"--inner=${layout.gaps.inner}\",\n"
    "           \"--outer=${layout.gaps.outer}\",\n"
    "           \"--smart=${layout.gaps.smart}\",\n"
    "           \"--status=${status.enabled}\",\n"
    "           \"--separator=${status.separator}\",\n"
    "           \"--center=${behavior.dialog_center}\",\n"
    "           \"--hints=${behavior.size_hints}\",\n"
    "           \"--placement=${behavior.floating_placement}\",\n"
    "           \"--cascade=${behavior.cascade_step}\",\n"
    "           \"--bar=${ui.bar.position}\",\n"
    "           \"--bar-padding=${ui.bar.padding}\",\n"
    "           \"--bar-item-spacing=${ui.bar.item_spacing}\",\n"
    "           \"--bar-section-spacing=${ui.bar.section_spacing}\",\n"
    "           \"--bar-cell-margin=${ui.bar.cell_margin}\",\n"
    "           \"--bar-indicator=${ui.bar.indicator_size}\",\n"
    "           \"--tags=${ui.bar.tags.visibility}\",\n"
    "           \"--osd=${ui.osd.show}\",\n"
    "           \"--osd-position=${ui.osd.position}\",\n"
    "           \"--osd-geometry=${ui.osd.geometry}\",\n"
    "           \"--osd-duration=${ui.osd.duration_ms}\",\n"
    "           \"--osd-padding=${ui.osd.padding}\",\n"
    "           \"--osd-offset=${ui.osd.offset}\",\n"
    "           \"--osd-min-width=${ui.osd.min_width}\",\n"
    "           \"--tray=${ui.systray.show}\",\n"
    "           \"--tray-spacing=${ui.systray.spacing}\",\n"
    "           \"--tray-icon=${ui.systray.icon_size}\",\n"
    "           \"${ui.colors.selected.background}\",\n"
    "           \"${ui.colors.floating.border}\",\n"
    "           \"$${HOME}\");\n"
    "};\n"
    "status = { enabled = true; separator = \" :: \"; };\n"
    "behavior = { dialog_center = \"parent\"; size_hints = \"always\"; floating_placement = \"client\"; cascade_step = 48; };\n"
    "ui = {\n"
    "  font = \"face-${ui.colors.selected.foreground}\";\n"
    "  opacity = 0.85;\n"
    "  bar = { position = \"top\"; padding = 6; item_spacing = 4;\n"
    "          section_spacing = 12; cell_margin = 4; indicator_size = 2;\n"
    "          tags = { visibility = \"all\"; }; };\n"
    "  osd = { show = true; position = \"top\"; geometry = true; duration_ms = 650; padding = 8; offset = 21; min_width = 180; };\n"
    "  systray = { show = false; spacing = 7; icon_size = 19; };\n"
    "  colors = {\n"
    "    normal = { foreground = \"#111111\"; background = \"#222222\"; };\n"
    "    selected = {\n"
    "      border = \"#aabbcc\";\n"
    "      foreground = \"${ui.colors.normal.foreground}\";\n"
    "      background = \"${ui.colors.normal.background}\";\n"
    "    };\n"
    "  };\n"
    "};\n"
    "layout = { gaps = { inner = 7; outer = 11; smart = true; }; };\n";
  static const char *const expected[] = {
    "probe", "-fn", "face-#111111", "--opacity=0.85", "--inner=7",
    "--outer=11", "--smart=true", "--status=true",
    "--separator= :: ",
    "--center=parent", "--hints=always", "--placement=client",
    "--cascade=48", "--bar=top", "--bar-padding=6",
    "--bar-item-spacing=4", "--bar-section-spacing=12",
    "--bar-cell-margin=4", "--bar-indicator=2", "--tags=all",
    "--osd=true", "--osd-position=top", "--osd-geometry=true",
    "--osd-duration=650",
    "--osd-padding=8", "--osd-offset=21", "--osd-min-width=180",
    "--tray=false", "--tray-spacing=7", "--tray-icon=19",
    "#222222", "#aabbcc", "${HOME}"
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
  if (strcmp(cfg.colors[4][0], "#aabbcc"))
    fail("floating color did not inherit selected border");

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
