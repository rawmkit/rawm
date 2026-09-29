#!/bin/sh
set -eu

rawm=$1
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

expect_ok config/rawm.conf

cat >"$tmp/valid.conf" <<'EOC'
ui = {
  font = "monospace:size=11";
  bar = { position = "top"; padding = 6; tags = { visibility = "all"; }; };
  osd = { show = true; geometry = true; duration_ms = 650; };
  systray = { show = false; spacing = 7; icon_size = 19; };
};
behavior = { dialog_center = "parent"; size_hints = "always"; floating_placement = "client"; };
layout = { master_factor = 0.60; gaps = { inner = 8; outer = 12; smart = true; }; };
tags = { default = (
  { name = "dev"; layout = "tile"; },
  { name = "web"; layout = "monocle"; },
  { name = "chat"; layout = "tile"; }
); };
rules = ({ class = "Video"; tags = (2); floating = true; center = "monitor"; });
pointer = { bindings = (
  { modifiers = ("Mod4"); button = 1; action = "move"; },
  { modifiers = ("Mod4"); button = 2; action = "toggle_floating"; },
  { modifiers = ("Mod4"); button = 3; action = "resize"; }
); };
EOC
expect_ok "$tmp/valid.conf"

for legacy in status commands input; do
  printf '%s = {};\n' "$legacy" >"$tmp/legacy.conf"
  expect_fail "$tmp/legacy.conf"
  grep -q "unknown setting '$legacy'" "$tmp/stderr" ||
    fail "$legacy authority was not rejected"
done

cat >"$tmp/pointer-action.conf" <<'EOC'
pointer = { bindings = ({ button = 1; action = "spawn"; }); };
EOC
expect_fail "$tmp/pointer-action.conf"
grep -q "unknown pointer action" "$tmp/stderr" || fail "pointer action diagnostic missing"

cat >"$tmp/pointer-duplicate.conf" <<'EOC'
pointer = { bindings = (
  { modifiers = ("Mod4"); button = 1; action = "move"; },
  { modifiers = ("Mod4"); button = 1; action = "resize"; }
); };
EOC
expect_fail "$tmp/pointer-duplicate.conf"
grep -q "duplicate pointer binding" "$tmp/stderr" || fail "duplicate pointer diagnostic missing"

cat >"$tmp/pointer-button.conf" <<'EOC'
pointer = { bindings = ({ button = 0; action = "move"; }); };
EOC
expect_fail "$tmp/pointer-button.conf"

cat >"$tmp/pointer-modifier.conf" <<'EOC'
pointer = { bindings = ({ modifiers = ("Hyper"); button = 1; action = "move"; }); };
EOC
expect_fail "$tmp/pointer-modifier.conf"
grep -q "unknown modifier" "$tmp/stderr" || fail "modifier diagnostic missing"

cat >"$tmp/pointer-extra.conf" <<'EOC'
pointer = { bindings = ({ button = 1; action = "move"; command = "x"; }); };
EOC
expect_fail "$tmp/pointer-extra.conf"

cat >"$tmp/osd-duration.conf" <<'EOC'
ui = { osd = { duration_ms = 99; }; };
EOC
expect_fail "$tmp/osd-duration.conf"

cat >"$tmp/tag-visibility.conf" <<'EOC'
ui = { bar = { tags = { visibility = "smart"; }; }; };
EOC
expect_fail "$tmp/tag-visibility.conf"

cat >"$tmp/size-hints.conf" <<'EOC'
behavior = { size_hints = "sometimes"; };
EOC
expect_fail "$tmp/size-hints.conf"

cat >"$tmp/floating-placement.conf" <<'EOC'
behavior = { floating_placement = "smart"; };
EOC
expect_fail "$tmp/floating-placement.conf"

cat >"$tmp/monitor.conf" <<'EOC'
tags = {
  default = ({ name = "one"; }, { name = "two"; });
  monitors = ({ monitor = 1; tags = ({ name = "only-one"; }); });
};
EOC
expect_fail "$tmp/monitor.conf"

cat >"$tmp/rule-tag.conf" <<'EOC'
tags = { default = ({ name = "one"; }, { name = "two"; }); };
rules = ({ class = "Broken"; tags = (3); });
EOC
expect_fail "$tmp/rule-tag.conf"

cat >"$tmp/reference.conf" <<'EOC'
ui = {
  font = "${ui.colors.normal.foreground}";
  colors = {
    normal = { foreground = "monospace:size=12"; };
    selected = { background = "${ui.colors.normal.background}"; };
  };
};
layout = { symbols = { tile = "g${layout.gaps.inner}"; }; };
EOC
expect_ok "$tmp/reference.conf"

cat >"$tmp/reference-unknown.conf" <<'EOC'
ui = { font = "${ui.does_not_exist}"; };
EOC
expect_fail "$tmp/reference-unknown.conf"
grep -q "unknown reference" "$tmp/stderr" || fail "unknown reference diagnostic missing"

cat >"$tmp/reference-cycle.conf" <<'EOC'
ui = { colors = {
  normal = { foreground = "${ui.colors.selected.foreground}"; };
  selected = { foreground = "${ui.colors.normal.foreground}"; };
}; };
EOC
expect_fail "$tmp/reference-cycle.conf"
grep -q "cyclic reference" "$tmp/stderr" || fail "reference cycle diagnostic missing"

cat >"$tmp/reference-unclosed.conf" <<'EOC'
ui = { font = "${ui.colors.normal.foreground"; };
EOC
expect_fail "$tmp/reference-unclosed.conf"
grep -q "unterminated reference" "$tmp/stderr" || fail "unterminated reference diagnostic missing"

cat >"$tmp/include-body.conf" <<'EOC'
ui = { font = "monospace:size=10"; };
EOC
cat >"$tmp/include.conf" <<'EOC'
@include "include-body.conf"
EOC
expect_ok "$tmp/include.conf"

mkdir -p "$tmp/home/.config/rawm"
cat >"$tmp/home/.config/rawm/rawm.conf" <<'EOC'
this_setting_does_not_exist = true;
EOC
HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/home/.config" \
  "$rawm" -C -c config/rawm.conf >/dev/null 2>"$tmp/stderr" ||
  fail "explicit -c did not isolate the selected file from user configuration"

echo "config tests: ok"
