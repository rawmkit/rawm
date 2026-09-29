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
  osd = { show = true; geometry = true; duration_ms = 650; };
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

for legacy_ui in bar systray; do
  cat >"$tmp/legacy-ui.conf" <<EOC
ui = { $legacy_ui = {}; };
EOC
  expect_fail "$tmp/legacy-ui.conf"
  grep -q "unknown setting '$legacy_ui'" "$tmp/stderr" ||
    fail "ui.$legacy_ui authority was not rejected"
done

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

cat >"$tmp/label-limits.conf" <<'EOC'
layout = { symbols = { tile = "12345678901234567890123456789012"; }; };
tags = { default = ({ name = "abcdefghijklmnopqrstuv"; layout = "tile"; }); };
EOC
expect_ok "$tmp/label-limits.conf"

cat >"$tmp/tag-name-too-long.conf" <<'EOC'
tags = { default = ({ name = "abcdefghijklmnopqrstuvw"; layout = "tile"; }); };
EOC
expect_fail "$tmp/tag-name-too-long.conf"
grep -q "tag name is too long (maximum 22 bytes)" "$tmp/stderr" ||
  fail "tag-name byte-limit diagnostic missing"

cat >"$tmp/layout-symbol-too-long.conf" <<'EOC'
layout = { symbols = { tile = "123456789012345678901234567890123"; }; };
EOC
expect_fail "$tmp/layout-symbol-too-long.conf"
grep -q "layout symbol 'tile' exceeds 32 bytes" "$tmp/stderr" ||
  fail "layout-symbol byte-limit diagnostic missing"

cat >"$tmp/layout-symbol-reference-too-long.conf" <<'EOC'
ui = { font = "123456789012345678901234567890123"; };
layout = { symbols = { tile = "${ui.font}"; }; };
EOC
expect_fail "$tmp/layout-symbol-reference-too-long.conf"
grep -q "layout symbol 'tile' exceeds 32 bytes" "$tmp/stderr" ||
  fail "expanded layout-symbol byte-limit diagnostic missing"

cat >"$tmp/ui-range.conf" <<'EOC'
ui = { border_width = 65; };
EOC
expect_fail "$tmp/ui-range.conf"

grep -q "border_width" "$tmp/stderr" || fail "border-width range diagnostic missing"

cat >"$tmp/ui-opacity.conf" <<'EOC'
ui = { opacity = 1.1; };
EOC
expect_fail "$tmp/ui-opacity.conf"

grep -q "opacity" "$tmp/stderr" || fail "opacity range diagnostic missing"

cat >"$tmp/ui-font.conf" <<'EOC'
ui = { font = ""; };
EOC
expect_fail "$tmp/ui-font.conf"
grep -q "ui.font must not be empty" "$tmp/stderr" || fail "empty-font diagnostic missing"

cat >"$tmp/osd-type.conf" <<'EOC'
ui = { osd = { geometry = "yes"; }; };
EOC
expect_fail "$tmp/osd-type.conf"
grep -q "geometry" "$tmp/stderr" || fail "osd geometry type diagnostic missing"

cat >"$tmp/osd-position.conf" <<'EOC'
ui = { osd = { position = "middle"; }; };
EOC
expect_fail "$tmp/osd-position.conf"
grep -q "invalid osd position" "$tmp/stderr" || fail "osd position diagnostic missing"

for spec in duration_ms:99 padding:65 offset:513 min_width:4097; do
  name=${spec%%:*}
  value=${spec#*:}
  cat >"$tmp/osd-range.conf" <<EOC
ui = { osd = { $name = $value; }; };
EOC
  expect_fail "$tmp/osd-range.conf"
  grep -q "$name" "$tmp/stderr" || fail "osd $name range diagnostic missing"
done

cat >"$tmp/color-error.conf" <<'EOC'
ui = { colors = { error = { border = "#ff0000"; }; }; };
EOC
expect_fail "$tmp/color-error.conf"
grep -q "unknown setting 'error'" "$tmp/stderr" || fail "obsolete error color was accepted"

for value in never floating always; do
  cat >"$tmp/size-hints-ok.conf" <<EOC
behavior = { size_hints = "$value"; };
EOC
  expect_ok "$tmp/size-hints-ok.conf"
done

cat >"$tmp/size-hints-type.conf" <<'EOC'
behavior = { size_hints = true; };
EOC
expect_fail "$tmp/size-hints-type.conf"

cat >"$tmp/dialog-center-invalid.conf" <<'EOC'
behavior = { dialog_center = "screen-ish"; };
EOC
expect_fail "$tmp/dialog-center-invalid.conf"

cat >"$tmp/dialog-center-type.conf" <<'EOC'
behavior = { dialog_center = true; };
EOC
expect_fail "$tmp/dialog-center-type.conf"

cat >"$tmp/cascade-step.conf" <<'EOC'
behavior = { floating_placement = "cascade"; cascade_step = 0; };
EOC
expect_fail "$tmp/cascade-step.conf"

cat >"$tmp/master-factor.conf" <<'EOC'
layout = { master_factor = 0.99; };
EOC
expect_fail "$tmp/master-factor.conf"

cat >"$tmp/master-count.conf" <<'EOC'
layout = { master_count = 65; };
EOC
expect_fail "$tmp/master-count.conf"

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

cat >"$tmp/layout-name.conf" <<'EOC'
tags = { default = ({ name = "one"; layout = "spiral"; }); };
EOC
expect_fail "$tmp/layout-name.conf"

grep -q "unknown layout" "$tmp/stderr" || fail "unknown layout diagnostic missing"

cat >"$tmp/monitor-negative.conf" <<'EOC'
tags = {
  default = ({ name = "one"; });
  monitors = ({ monitor = -1; tags = ({ name = "one"; }); });
};
EOC
expect_fail "$tmp/monitor-negative.conf"

cat >"$tmp/monitor-duplicate.conf" <<'EOC'
tags = {
  default = ({ name = "one"; });
  monitors = (
    { monitor = 1; tags = ({ name = "one"; }); },
    { monitor = 1; tags = ({ name = "one"; }); }
  );
};
EOC
expect_fail "$tmp/monitor-duplicate.conf"
grep -q "duplicated" "$tmp/stderr" || fail "duplicate monitor diagnostic missing"

cat >"$tmp/rule-complete.conf" <<'EOC'
tags = { default = ({ name = "one"; }, { name = "two"; }); };
rules = ({
  class = "Class"; instance = "instance"; title = "title"; role = "role";
  tags = "all"; floating = true; center = "parent"; fullscreen = false;
  attention = "urgent"; monitor = -1;
});
EOC
expect_ok "$tmp/rule-complete.conf"

cat >"$tmp/rule-fullscreen-type.conf" <<'EOC'
rules = ({ class = "Broken"; fullscreen = "yes"; });
EOC
expect_fail "$tmp/rule-fullscreen-type.conf"

cat >"$tmp/rule-attention.conf" <<'EOC'
rules = ({ class = "Broken"; attention = "loud"; });
EOC
expect_fail "$tmp/rule-attention.conf"

cat >"$tmp/rule-center.conf" <<'EOC'
rules = ({ class = "Broken"; center = "screen-ish"; });
EOC
expect_fail "$tmp/rule-center.conf"

cat >"$tmp/pointer-type.conf" <<'EOC'
pointer = { bindings = "buttons"; };
EOC
expect_fail "$tmp/pointer-type.conf"

cat >"$tmp/pointer-entry.conf" <<'EOC'
pointer = { bindings = (1); };
EOC
expect_fail "$tmp/pointer-entry.conf"

cat >"$tmp/pointer-button-type.conf" <<'EOC'
pointer = { bindings = ({ button = "1"; action = "move"; }); };
EOC
expect_fail "$tmp/pointer-button-type.conf"

cat >"$tmp/pointer-modifier-type.conf" <<'EOC'
pointer = { bindings = ({ modifiers = (4); button = 1; action = "move"; }); };
EOC
expect_fail "$tmp/pointer-modifier-type.conf"

cat >"$tmp/unknown.conf" <<'EOC'
ui = { border_wdith = 7; };
EOC
expect_fail "$tmp/unknown.conf"
grep -q "unknown setting 'border_wdith'" "$tmp/stderr" || fail "unknown ui member diagnostic missing"

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
if HOME="$tmp/home" XDG_CONFIG_HOME=relative "$rawm" -C >/dev/null 2>"$tmp/stderr"; then
  fail "relative XDG_CONFIG_HOME did not fall back to HOME/.config"
fi

HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/home/.config" \
  "$rawm" -C -c config/rawm.conf >/dev/null 2>"$tmp/stderr" ||
  fail "explicit -c did not isolate the selected file from user configuration"

echo "config tests: ok"
