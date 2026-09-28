#!/bin/sh
set -eu

rawm=${1:-./rawm}
tmp=${TMPDIR:-/tmp}/rawm-config-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

fail()
{
  echo "config test failed: $*" >&2
  exit 1
}

skip()
{
  echo "config tests: skipped ($*)"
  exit 77
}

command -v grep >/dev/null 2>&1 || skip "grep not found"

expect_ok()
{
  "$rawm" -C -c "$1" >/dev/null 2>"$tmp/stderr" || {
    cat "$tmp/stderr" >&2
    fail "expected $1 to validate"
  }
}

expect_fail()
{
  if "$rawm" -C -c "$1" >/dev/null 2>"$tmp/stderr"; then
    fail "expected $1 to fail validation"
  fi
}

expect_ok config/rawm.conf

cat >"$tmp/valid.conf" <<'EOC'
ui = {
  font = "monospace:size=11";
  bar = {
    position = "top";
    padding = 6;
    item_spacing = 4;
    section_spacing = 12;
    cell_margin = 4;
    indicator_size = 2;
  };
};
status = { enabled = true; separator = " :: "; };
behavior = { dialog_center = "parent"; };
layout = {
  master_factor = 0.60;
  gaps = { inner = 8; outer = 12; smart = true; };
};
tags = {
  default = (
    { name = "dev"; layout = "tile"; },
    { name = "web"; layout = "monocle"; },
    { name = "chat"; layout = "tile"; }
  );
};
commands = {
  helper = ("printf", "%s\\n", "ok");
};
input = {
  keys = (
    { modifiers = ("Mod4"); key = "r"; action = "toggle_bar"; },
    { modifiers = ("Mod4"); key = "Return";
      action = "spawn"; command = "terminal"; },
    { modifiers = ("Mod4"); key = "1"; action = "view"; tag = 1; },
    { modifiers = ("Mod4", "Shift"); key = "h";
      action = "set_master_factor"; delta = -0.01; },
    { modifiers = ("Mod4", "Shift"); key = "m";
      action = "set_master_factor"; factor = 0.60; }
  );
  buttons = (
    { click = "tag_bar"; button = 1; action = "view"; },
    { click = "client"; modifiers = ("Mod4"); button = 1; action = "move"; }
  );
};
EOC
expect_ok "$tmp/valid.conf"

cat >"$tmp/label-limits.conf" <<'EOC'
layout = {
  symbols = {
    tile = "12345678901234567890123456789012";
  };
};
tags = {
  default = (
    { name = "abcdefghijklmnopqrstuv"; layout = "tile"; }
  );
};
input = {
  keys = ();
  buttons = ();
};
EOC
expect_ok "$tmp/label-limits.conf"

cat >"$tmp/tag-name-too-long.conf" <<'EOC'
tags = {
  default = (
    { name = "abcdefghijklmnopqrstuvw"; layout = "tile"; }
  );
};
EOC
expect_fail "$tmp/tag-name-too-long.conf"
grep -q "tag name is too long (maximum 22 bytes)" "$tmp/stderr" ||
  fail "tag-name byte-limit diagnostic missing"

cat >"$tmp/layout-symbol-too-long.conf" <<'EOC'
layout = {
  symbols = {
    tile = "123456789012345678901234567890123";
  };
};
EOC
expect_fail "$tmp/layout-symbol-too-long.conf"
grep -q "layout symbol 'tile' exceeds 32 bytes" "$tmp/stderr" ||
  fail "layout-symbol byte-limit diagnostic missing"

cat >"$tmp/layout-symbol-reference-too-long.conf" <<'EOC'
ui = {
  font = "123456789012345678901234567890123";
};
layout = {
  symbols = {
    tile = "${ui.font}";
  };
};
EOC
expect_fail "$tmp/layout-symbol-reference-too-long.conf"
grep -q "layout symbol 'tile' exceeds 32 bytes" "$tmp/stderr" ||
  fail "expanded layout-symbol byte-limit diagnostic missing"

cat >"$tmp/cycle-view.conf" <<'EOC'
input = {
  keys = (
    { key = "F11"; action = "cycle_view"; number = -1; },
    { key = "F12"; action = "cycle_view"; number = 1; }
  );
  buttons = (
    { click = "tag_bar"; button = 4; action = "cycle_view"; number = -1; },
    { click = "tag_bar"; button = 5; action = "cycle_view"; number = 1; }
  );
};
EOC
expect_ok "$tmp/cycle-view.conf"

cat >"$tmp/cycle-view-direction.conf" <<'EOC'
input = {
  keys = (
    { key = "F11"; action = "cycle_view"; number = 0; }
  );
};
EOC
expect_fail "$tmp/cycle-view-direction.conf"
grep -q "cycle_view requires 'number' -1 or 1" "$tmp/stderr" ||
  fail "cycle_view direction diagnostic missing"

cat >"$tmp/cycle-view-extra.conf" <<'EOC'
input = {
  keys = (
    { key = "F11"; action = "cycle_view"; number = 1; tag = 1; }
  );
};
EOC
expect_fail "$tmp/cycle-view-extra.conf"
grep -q "arguments not used" "$tmp/stderr" ||
  fail "cycle_view extra argument diagnostic missing"

cat >"$tmp/cycle-layout.conf" <<'EOC'
input = {
  keys = (
    { key = "F9"; action = "cycle_layout"; number = -1; },
    { key = "F10"; action = "cycle_layout"; number = 1; }
  );
  buttons = (
    { click = "layout"; button = 4; action = "cycle_layout"; number = -1; },
    { click = "layout"; button = 5; action = "cycle_layout"; number = 1; }
  );
};
EOC
expect_ok "$tmp/cycle-layout.conf"

cat >"$tmp/cycle-layout-direction.conf" <<'EOC'
input = {
  keys = (
    { key = "F9"; action = "cycle_layout"; number = 2; }
  );
};
EOC
expect_fail "$tmp/cycle-layout-direction.conf"
grep -q "cycle_layout requires 'number' -1 or 1" "$tmp/stderr" ||
  fail "cycle_layout direction diagnostic missing"

cat >"$tmp/cycle-layout-missing-direction.conf" <<'EOC'
input = {
  keys = (
    { key = "F9"; action = "cycle_layout"; }
  );
};
EOC
expect_fail "$tmp/cycle-layout-missing-direction.conf"
grep -q "cycle_layout requires 'number' -1 or 1" "$tmp/stderr" ||
  fail "cycle_layout missing direction diagnostic missing"

cat >"$tmp/rename-tag-command.conf" <<'EOC'
commands = { rename = ("printf", "work\\n"); };
input = {
  keys = (
    { key = "F1"; action = "rename_tag"; command = "rename"; }
  );
};
EOC
expect_ok "$tmp/rename-tag-command.conf"

cat >"$tmp/rename-tag-missing-command.conf" <<'EOC'
input = {
  keys = (
    { key = "F1"; action = "rename_tag"; }
  );
};
EOC
expect_fail "$tmp/rename-tag-missing-command.conf"
grep -q "rename_tag binding references unknown command" "$tmp/stderr" ||
  fail "rename_tag missing command diagnostic missing"

cat >"$tmp/rename-tag-unknown-command.conf" <<'EOC'
input = {
  keys = (
    { key = "F1"; action = "rename_tag"; command = "missing"; }
  );
};
EOC
expect_fail "$tmp/rename-tag-unknown-command.conf"
grep -q "rename_tag binding references unknown command 'missing'" "$tmp/stderr" ||
  fail "rename_tag unknown command diagnostic missing"

cat >"$tmp/status-button-id.conf" <<'EOC'
commands = { noop = ("true"); };
input = {
  buttons = (
    { click = "status"; status_id = "volume"; button = 1; action = "spawn"; command = "noop"; },
    { click = "status"; status_id = "network"; button = 1; action = "spawn"; command = "noop"; },
    { click = "status"; button = 1; action = "spawn"; command = "noop"; }
  );
};
EOC
expect_ok "$tmp/status-button-id.conf"

cat >"$tmp/status-button-id-invalid-area.conf" <<'EOC'
input = {
  buttons = (
    { click = "title"; status_id = "volume"; button = 1; action = "zoom"; }
  );
};
EOC
expect_fail "$tmp/status-button-id-invalid-area.conf"
grep -q "status_id is only valid" "$tmp/stderr" ||
  fail "status_id click-area diagnostic missing"

cat >"$tmp/status-button-id-invalid-value.conf" <<'EOC'
input = {
  buttons = (
    { click = "status"; status_id = "bad id"; button = 1; action = "quit"; }
  );
};
EOC
expect_fail "$tmp/status-button-id-invalid-value.conf"
grep -q "valid status segment id" "$tmp/stderr" ||
  fail "invalid status_id diagnostic missing"

cat >"$tmp/status-button-id-duplicate.conf" <<'EOC'
input = {
  buttons = (
    { click = "status"; status_id = "volume"; button = 1; action = "quit"; },
    { click = "status"; status_id = "volume"; button = 1; action = "quit"; }
  );
};
EOC
expect_fail "$tmp/status-button-id-duplicate.conf"
grep -q "duplicate mouse binding" "$tmp/stderr" ||
  fail "duplicate status_id binding diagnostic missing"

cat >"$tmp/bar-metric-range.conf" <<'EOC'
ui = { bar = { indicator_size = 33; }; };
EOC
expect_fail "$tmp/bar-metric-range.conf"
grep -q "indicator_size" "$tmp/stderr" ||
  fail "bar indicator range diagnostic missing"

cat >"$tmp/bar-cell-margin-range.conf" <<'EOC'
ui = { bar = { cell_margin = 65; }; };
EOC
expect_fail "$tmp/bar-cell-margin-range.conf"
grep -q "cell_margin" "$tmp/stderr" ||
  fail "bar cell margin range diagnostic missing"


cat >"$tmp/systray-icon-size-range.conf" <<'EOC'
ui = { systray = { icon_size = 513; }; };
EOC
expect_fail "$tmp/systray-icon-size-range.conf"
grep -q "icon_size" "$tmp/stderr" ||
  fail "systray icon-size range diagnostic missing"

cat >"$tmp/osd-geometry-type.conf" <<'EOC'
ui = { osd = { geometry = "yes"; }; };
EOC
expect_fail "$tmp/osd-geometry-type.conf"
grep -q "geometry" "$tmp/stderr" ||
  fail "osd geometry type diagnostic missing"

cat >"$tmp/osd-position.conf" <<'EOC'
ui = { osd = { position = "middle"; }; };
EOC
expect_fail "$tmp/osd-position.conf"
grep -q "invalid osd position" "$tmp/stderr" ||
  fail "osd position diagnostic missing"

cat >"$tmp/osd-duration-range.conf" <<'EOC'
ui = { osd = { duration_ms = 99; }; };
EOC
expect_fail "$tmp/osd-duration-range.conf"
grep -q "duration_ms" "$tmp/stderr" ||
  fail "osd duration range diagnostic missing"


cat >"$tmp/osd-width-range.conf" <<'EOC'
ui = { osd = { min_width = 4097; }; };
EOC
expect_fail "$tmp/osd-width-range.conf"
grep -q "min_width" "$tmp/stderr" ||
  fail "osd minimum width range diagnostic missing"

cat >"$tmp/status-disabled.conf" <<'EOC'
status = { enabled = false; };
EOC
expect_ok "$tmp/status-disabled.conf"

cat >"$tmp/tag-visibility-occupied.conf" <<'EOC'
ui = { bar = { tags = { visibility = "occupied"; }; }; };
EOC
expect_ok "$tmp/tag-visibility-occupied.conf"

cat >"$tmp/tag-visibility-all.conf" <<'EOC'
ui = { bar = { tags = { visibility = "all"; }; }; };
EOC
expect_ok "$tmp/tag-visibility-all.conf"

cat >"$tmp/tag-visibility-invalid.conf" <<'EOC'
ui = { bar = { tags = { visibility = "smart"; }; }; };
EOC
expect_fail "$tmp/tag-visibility-invalid.conf"
grep -q "invalid tag visibility" "$tmp/stderr" ||
  fail "invalid tag visibility diagnostic missing"

cat >"$tmp/tag-visibility-type.conf" <<'EOC'
ui = { bar = { tags = { visibility = true; }; }; };
EOC
expect_fail "$tmp/tag-visibility-type.conf"

cat >"$tmp/status-protocol.conf" <<'EOC'
status = { enabled = true; protocol = "rawm-v2"; };
EOC
expect_fail "$tmp/status-protocol.conf"
grep -q "unknown setting 'protocol'" "$tmp/stderr" ||
  fail "retired status protocol setting was not rejected"

cat >"$tmp/status-member.conf" <<'EOC'
status = { enabled = true; protcol = "obsolete"; };
EOC
expect_fail "$tmp/status-member.conf"

cat >"$tmp/status-type.conf" <<'EOC'
status = { enabled = "yes"; };
EOC
expect_fail "$tmp/status-type.conf"

cat >"$tmp/status-separator-type.conf" <<'EOC'
status = { separator = 1; };
EOC
expect_fail "$tmp/status-separator-type.conf"

cat >"$tmp/size-hints.conf" <<'EOC'
behavior = { size_hints = "never"; };
EOC
expect_ok "$tmp/size-hints.conf"
cat >"$tmp/size-hints-floating.conf" <<'EOC'
behavior = { size_hints = "floating"; };
EOC
expect_ok "$tmp/size-hints-floating.conf"
cat >"$tmp/size-hints-always.conf" <<'EOC'
behavior = { size_hints = "always"; };
EOC
expect_ok "$tmp/size-hints-always.conf"
cat >"$tmp/size-hints-invalid.conf" <<'EOC'
behavior = { size_hints = "sometimes"; };
EOC
expect_fail "$tmp/size-hints-invalid.conf"
cat >"$tmp/size-hints-type.conf" <<'EOC'
behavior = { size_hints = true; };
EOC
expect_fail "$tmp/size-hints-type.conf"
cat >"$tmp/legacy-resize-hints.conf" <<'EOC'
behavior = { resize_hints = false; };
EOC
expect_fail "$tmp/legacy-resize-hints.conf"

cat >"$tmp/floating-placement-cascade.conf" <<'EOC'
behavior = { floating_placement = "cascade"; cascade_step = 24; };
EOC
expect_ok "$tmp/floating-placement-cascade.conf"
cat >"$tmp/floating-placement-client.conf" <<'EOC'
behavior = { floating_placement = "client"; };
EOC
expect_ok "$tmp/floating-placement-client.conf"
cat >"$tmp/floating-placement-invalid.conf" <<'EOC'
behavior = { floating_placement = "smart"; };
EOC
expect_fail "$tmp/floating-placement-invalid.conf"
cat >"$tmp/floating-placement-type.conf" <<'EOC'
behavior = { floating_placement = true; };
EOC
expect_fail "$tmp/floating-placement-type.conf"
cat >"$tmp/cascade-step-zero.conf" <<'EOC'
behavior = { cascade_step = 0; };
EOC
expect_fail "$tmp/cascade-step-zero.conf"
cat >"$tmp/cascade-step-type.conf" <<'EOC'
behavior = { cascade_step = "32"; };
EOC
expect_fail "$tmp/cascade-step-type.conf"

cat >"$tmp/center-mode.conf" <<'EOC'
behavior = { dialog_center = "monitor"; };
rules = (
  { class = "DialogA"; floating = true; center = "parent"; },
  { class = "DialogB"; floating = true; center = "monitor"; },
  { class = "DialogC"; floating = true; center = "none"; }
);
EOC
expect_ok "$tmp/center-mode.conf"

cat >"$tmp/center-mode-invalid.conf" <<'EOC'
behavior = { dialog_center = "screen-ish"; };
EOC
expect_fail "$tmp/center-mode-invalid.conf"

cat >"$tmp/center-mode-type.conf" <<'EOC'
behavior = { dialog_center = true; };
EOC
expect_fail "$tmp/center-mode-type.conf"

cat >"$tmp/legacy-autocenter.conf" <<'EOC'
behavior = { autocenter_dialogs = true; };
EOC
expect_fail "$tmp/legacy-autocenter.conf"

cat >"$tmp/legacy-centered.conf" <<'EOC'
rules = ({ class = "Old"; centered = true; });
EOC
expect_fail "$tmp/legacy-centered.conf"

cat >"$tmp/fullscreen-rule.conf" <<'EOC'
rules = (
  { class = "VideoPlayer"; tags = (8); fullscreen = true; },
  { class = "NeverFullscreen"; fullscreen = false; }
);
EOC
expect_ok "$tmp/fullscreen-rule.conf"

cat >"$tmp/fullscreen-rule-type.conf" <<'EOC'
rules = ({ class = "Broken"; fullscreen = "yes"; });
EOC
expect_fail "$tmp/fullscreen-rule-type.conf"

cat >"$tmp/attention-rule.conf" <<'EOC'
rules = (
  { class = "BackgroundVideo"; tags = (8); attention = "urgent"; },
  { class = "QuietVideo"; attention = "none"; }
);
EOC
expect_ok "$tmp/attention-rule.conf"

cat >"$tmp/attention-rule-value.conf" <<'EOC'
rules = ({ class = "Broken"; attention = "loud"; });
EOC
expect_fail "$tmp/attention-rule-value.conf"

cat >"$tmp/attention-rule-type.conf" <<'EOC'
rules = ({ class = "Broken"; attention = true; });
EOC
expect_fail "$tmp/attention-rule-type.conf"

cat >"$tmp/gap-range.conf" <<'EOC'
layout = { gaps = { inner = -1; }; };
EOC
expect_fail "$tmp/gap-range.conf"

cat >"$tmp/gap-member.conf" <<'EOC'
layout = { gaps = { innner = 8; }; };
EOC
expect_fail "$tmp/gap-member.conf"

cat >"$tmp/gap-type.conf" <<'EOC'
layout = { gaps = 8; };
EOC
expect_fail "$tmp/gap-type.conf"

cat >"$tmp/include-body.conf" <<'EOC'
ui = { font = "monospace:size=10"; };
EOC
cat >"$tmp/include.conf" <<'EOC'
@include "include-body.conf"
EOC
expect_ok "$tmp/include.conf"

cat >"$tmp/unknown.conf" <<'EOC'
ui = { border_wdith = 7; };
EOC
expect_fail "$tmp/unknown.conf"

cat >"$tmp/layout.conf" <<'EOC'
tags = { default = ({ name = "one"; layout = "spiral"; }); };
EOC
expect_fail "$tmp/layout.conf"

cat >"$tmp/command.conf" <<'EOC'
input = { keys = (
  { key = "Return"; action = "spawn"; command = "does_not_exist"; }
); };
EOC
expect_fail "$tmp/command.conf"

cat >"$tmp/duplicate.conf" <<'EOC'
input = { keys = (
  { modifiers = ("Mod4"); key = "Return"; action = "toggle_bar"; },
  { modifiers = ("Mod4"); key = "Return"; action = "zoom"; }
); };
EOC
expect_fail "$tmp/duplicate.conf"

cat >"$tmp/keysym.conf" <<'EOC'
input = { keys = (
  { key = "DefinitelyNotAnXKeysym"; action = "toggle_bar"; }
); };
EOC
expect_fail "$tmp/keysym.conf"

cat >"$tmp/tag.conf" <<'EOC'
tags = { default = (
  { name = "one"; },
  { name = "two"; }
); };
input = { keys = (
  { key = "3"; action = "view"; tag = 3; }
); };
EOC
expect_fail "$tmp/tag.conf"

cat >"$tmp/monitor.conf" <<'EOC'
tags = {
  default = (
    { name = "one"; },
    { name = "two"; }
  );
  monitors = (
    { monitor = 1; tags = ({ name = "only-one"; }); }
  );
};
EOC
expect_fail "$tmp/monitor.conf"

cat >"$tmp/extra.conf" <<'EOC'
input = { keys = (
  { key = "b"; action = "toggle_bar"; number = 1; }
); };
EOC
expect_fail "$tmp/extra.conf"


cat >"$tmp/misplaced.conf" <<'EOC'
input = { keys = (
  { key = "b"; click = "client"; action = "toggle_bar"; }
); };
EOC
expect_fail "$tmp/misplaced.conf"

cat >"$tmp/factor.conf" <<'EOC'
input = { keys = (
  { key = "h"; action = "set_master_factor"; factor = 1.25; }
); };
EOC
expect_fail "$tmp/factor.conf"

cat >"$tmp/reference.conf" <<'EOC'
ui = {
  font = "${ui.colors.normal.foreground}";
  colors = {
    normal = { foreground = "monospace:size=12"; };
    selected = { background = "${ui.colors.normal.background}"; };
  };
};
commands = {
  probe = ("printf", "%s\\n", "${ui.font}", "${ui.opacity}", "$${HOME}");
};
EOC
expect_ok "$tmp/reference.conf"

cat >"$tmp/reference-unknown.conf" <<'EOC'
commands = { probe = ("printf", "${ui.does_not_exist}"); };
EOC
expect_fail "$tmp/reference-unknown.conf"
grep -q "unknown reference" "$tmp/stderr" ||
  fail "unknown reference did not produce a useful diagnostic"

cat >"$tmp/reference-cycle.conf" <<'EOC'
ui = { colors = {
  normal = { foreground = "${ui.colors.selected.foreground}"; };
  selected = { foreground = "${ui.colors.normal.foreground}"; };
}; };
EOC
expect_fail "$tmp/reference-cycle.conf"
grep -q "cyclic reference" "$tmp/stderr" ||
  fail "reference cycle did not produce a useful diagnostic"

cat >"$tmp/reference-unclosed.conf" <<'EOC'
commands = { probe = ("printf", "${ui.font"); };
EOC
expect_fail "$tmp/reference-unclosed.conf"
grep -q "unterminated reference" "$tmp/stderr" ||
  fail "unterminated reference did not produce a useful diagnostic"

mkdir -p "$tmp/home/.config/rawm"
cat >"$tmp/home/.config/rawm/rawm.conf" <<'EOC'
this_setting_does_not_exist = true;
EOC
if HOME="$tmp/home" XDG_CONFIG_HOME=relative "$rawm" -C >/dev/null 2>"$tmp/stderr"; then
  fail "relative XDG_CONFIG_HOME did not fall back to HOME/.config"
fi

HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/home/.config" \
  "$rawm" -C -c config/rawm.conf >/dev/null 2>"$tmp/stderr" || {
  cat "$tmp/stderr" >&2
  fail "explicit -c did not isolate the selected file from user configuration"
}


cat >"$tmp/status-sources.conf" <<'EOC'
status = {
  sources = {
    volume = { provider = "alsa"; device = "default"; control = "Master"; period_ms = 5000; };
    battery = { provider = "battery"; period_ms = 15000; warning = 20; critical = 10; };
    cpu = { provider = "cpu"; period_ms = 2000; warning = 80; critical = 95; };
    memory = { provider = "memory"; period_ms = 5000; warning = 85; critical = 95; };
    disk = { provider = "filesystem"; path = "/"; period_ms = 60000; warning = 20; critical = 5; };
    network = { provider = "network"; interface = "default"; period_ms = 2000; };
    helper = { provider = "exec"; command = ("printf", "ok\\n"); period_ms = 1000; timeout_ms = 500; };
    clock = { period_ms = 30000; };
  };
};
EOC
expect_ok "$tmp/status-sources.conf"

cat >"$tmp/status-source-disable.conf" <<'EOC'
status = { sources = { clock = { enabled = false; }; }; };
EOC
expect_ok "$tmp/status-source-disable.conf"

cat >"$tmp/status-source-alsa-step.conf" <<'EOC'
status = { sources = { volume = { provider = "alsa"; step = 5; }; }; };
input = { keys = (
  { key = "XF86AudioRaiseVolume"; action = "volume_up"; },
  { key = "XF86AudioLowerVolume"; action = "volume_down"; },
  { key = "XF86AudioMute"; action = "volume_mute"; }
); };
EOC
expect_ok "$tmp/status-source-alsa-step.conf"

cat >"$tmp/status-source-step-native.conf" <<'EOC'
status = { sources = { cpu = { provider = "cpu"; step = 5; }; }; };
EOC
expect_fail "$tmp/status-source-step-native.conf"
grep -q "step is valid only for alsa" "$tmp/stderr" ||
  fail "non-ALSA status step was accepted"

cat >"$tmp/status-source-step-range.conf" <<'EOC'
status = { sources = { volume = { provider = "alsa"; step = 0; }; }; };
EOC
expect_fail "$tmp/status-source-step-range.conf"
grep -q "step is out of range" "$tmp/stderr" ||
  fail "invalid ALSA status step was accepted"

cat >"$tmp/volume-action-no-alsa.conf" <<'EOC'
input = { keys = (
  { key = "XF86AudioMute"; action = "volume_mute"; }
); };
EOC
expect_fail "$tmp/volume-action-no-alsa.conf"
grep -q "requires status.enabled and exactly one enabled ALSA status source" "$tmp/stderr" ||
  fail "native volume action without ALSA source was accepted"

cat >"$tmp/volume-action-status-disabled.conf" <<'EOC'
status = { enabled = false; sources = { volume = { provider = "alsa"; }; }; };
input = { keys = (
  { key = "XF86AudioMute"; action = "volume_mute"; }
); };
EOC
expect_fail "$tmp/volume-action-status-disabled.conf"
grep -q "requires status.enabled and exactly one enabled ALSA status source" "$tmp/stderr" ||
  fail "native volume action with disabled status subsystem was accepted"

cat >"$tmp/volume-action-ambiguous.conf" <<'EOC'
status = { sources = {
  volume = { provider = "alsa"; };
  headset = { provider = "alsa"; device = "hw:1"; control = "Master"; };
}; };
input = { keys = (
  { key = "XF86AudioMute"; action = "volume_mute"; }
); };
EOC
expect_fail "$tmp/volume-action-ambiguous.conf"
grep -q "requires status.enabled and exactly one enabled ALSA status source" "$tmp/stderr" ||
  fail "ambiguous native volume action was accepted"

cat >"$tmp/status-source-new-no-provider.conf" <<'EOC'
status = { sources = { probe = { period_ms = 1000; }; }; };
EOC
expect_fail "$tmp/status-source-new-no-provider.conf"
grep -q "new source requires a provider" "$tmp/stderr" ||
  fail "new status source without provider was accepted"

cat >"$tmp/status-source-provider.conf" <<'EOC'
status = { sources = { x = { provider = "not-a-provider"; }; }; };
EOC
expect_fail "$tmp/status-source-provider.conf"
grep -q "unknown provider" "$tmp/stderr" || fail "unknown status provider diagnostic missing"

cat >"$tmp/status-source-exec.conf" <<'EOC'
status = { sources = { x = { provider = "exec"; }; }; };
EOC
expect_fail "$tmp/status-source-exec.conf"
grep -q "has no command" "$tmp/stderr" || fail "missing status exec command diagnostic missing"

cat >"$tmp/status-source-cpu-threshold.conf" <<'EOC'
status = { sources = { cpu = { provider = "cpu"; warning = 95; critical = 80; }; }; };
EOC
expect_fail "$tmp/status-source-cpu-threshold.conf"
grep -q "warning < critical" "$tmp/stderr" || fail "CPU threshold diagnostic missing"

cat >"$tmp/status-source-battery-threshold.conf" <<'EOC'
status = { sources = { battery = { provider = "battery"; warning = 10; critical = 20; }; }; };
EOC
expect_fail "$tmp/status-source-battery-threshold.conf"
grep -q "critical < warning" "$tmp/stderr" || fail "battery threshold diagnostic missing"

cat >"$tmp/status-source-period.conf" <<'EOC'
status = { sources = { clock = { period_ms = 99; }; }; };
EOC
expect_fail "$tmp/status-source-period.conf"
grep -q "period_ms is out of range" "$tmp/stderr" || fail "status period range diagnostic missing"

cat >"$tmp/status-source-command-native.conf" <<'EOC'
status = { sources = { clock = { command = ("date"); }; }; };
EOC
expect_fail "$tmp/status-source-command-native.conf"
grep -q "command is valid only for exec" "$tmp/stderr" || fail "native status command rejection missing"

cat >"$tmp/status-source-cpu-format.conf" <<'EOC'
status = { sources = { cpu = { provider = "cpu"; format = "%H:%M"; }; }; };
EOC
expect_fail "$tmp/status-source-cpu-format.conf"
grep -q "format is valid only for clock" "$tmp/stderr" ||
  fail "provider-specific format diagnostic missing"

cat >"$tmp/status-source-clock-threshold.conf" <<'EOC'
status = { sources = { clock = { warning = 1; }; }; };
EOC
expect_fail "$tmp/status-source-clock-threshold.conf"
grep -q "warning/critical are not valid" "$tmp/stderr" ||
  fail "provider-specific threshold diagnostic missing"

cat >"$tmp/status-source-network-timeout.conf" <<'EOC'
status = { sources = { network = { provider = "network"; timeout_ms = 1000; }; }; };
EOC
expect_fail "$tmp/status-source-network-timeout.conf"
grep -q "timeout_ms is valid only for exec" "$tmp/stderr" ||
  fail "provider-specific timeout diagnostic missing"

cat >"$tmp/status-source-network-interface-long.conf" <<'EOC'
status = { sources = { network = { provider = "network"; interface = "interface-name-is-too-long"; }; }; };
EOC
expect_fail "$tmp/status-source-network-interface-long.conf"
grep -q "invalid interface" "$tmp/stderr" || fail "overlong network interface was not rejected"

cat >"$tmp/status-source-network-interface-path.conf" <<'EOC'
status = { sources = { network = { provider = "network"; interface = ".."; }; }; };
EOC
expect_fail "$tmp/status-source-network-interface-path.conf"
grep -q "invalid interface" "$tmp/stderr" || fail "path-like network interface was not rejected"

cat >"$tmp/status-source-alsa-empty.conf" <<'EOC'
status = { sources = { volume = { provider = "alsa"; device = ""; control = "Master"; }; }; };
EOC
expect_fail "$tmp/status-source-alsa-empty.conf"
grep -q "requires non-empty device and control" "$tmp/stderr" ||
  fail "empty ALSA device was accepted"

cat >"$tmp/status-source-alsa-field.conf" <<'EOC'
status = { sources = { volume = { provider = "alsa"; command = ("amixer"); }; }; };
EOC
expect_fail "$tmp/status-source-alsa-field.conf"
grep -q "command is valid only for exec" "$tmp/stderr" ||
  fail "ALSA provider accepted exec command policy"

cat >"$tmp/status-source-clock-device.conf" <<'EOC'
status = { sources = { clock = { device = "default"; }; }; };
EOC
expect_fail "$tmp/status-source-clock-device.conf"
grep -q "device is valid only for alsa" "$tmp/stderr" ||
  fail "non-ALSA provider accepted ALSA device policy"

echo "config tests: ok"
