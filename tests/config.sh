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

expect_ok rawm.conf

cat >"$tmp/valid.conf" <<'EOC'
ui = {
  font = "monospace:size=11";
  bar = { position = "top"; };
};
status = { enabled = true; protocol = "rawm-v2"; separator = " :: "; };
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

cat >"$tmp/status-disabled.conf" <<'EOC'
status = { enabled = false; protocol = "rawm-v2"; };
EOC
expect_ok "$tmp/status-disabled.conf"

cat >"$tmp/status-protocol.conf" <<'EOC'
status = { enabled = true; protocol = "root-name"; };
EOC
expect_fail "$tmp/status-protocol.conf"
grep -q "unsupported status protocol" "$tmp/stderr" ||
  fail "unsupported status protocol diagnostic missing"

cat >"$tmp/status-member.conf" <<'EOC'
status = { enabled = true; protcol = "rawm-v2"; };
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
  "$rawm" -C -c rawm.conf >/dev/null 2>"$tmp/stderr" || {
  cat "$tmp/stderr" >&2
  fail "explicit -c did not isolate the selected file from user configuration"
}

echo "config tests: ok"
