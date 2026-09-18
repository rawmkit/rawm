#!/bin/sh
set -eu

rawm=${1:-./rawm}
helper=${2:-./tests/attention-rule-client}

tmp=${TMPDIR:-/tmp}/rawm-attention-rule-test.$$
mkdir -p "$tmp"
xvfb=
wm=
cleanup()
{
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  [ -z "$xvfb" ] || kill "$xvfb" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "attention rule tests: skipped ($*)"
  exit 0
}

fail()
{
  echo "attention rule test failed: $*" >&2
  exit 1
}

command -v Xvfb >/dev/null 2>&1 || skip "Xvfb not found"
[ -x "$helper" ] || skip "$helper not built"

display_file=$tmp/display
: >"$display_file"
Xvfb -displayfd 3 -screen 0 800x600x24 -nolisten tcp \
  >"$tmp/xvfb.log" 2>&1 3>"$display_file" &
xvfb=$!
n=0
while [ ! -s "$display_file" ]; do
  n=$((n + 1))
  [ "$n" -lt 100 ] || fail "Xvfb did not select a display"
  sleep 0.02
done
DISPLAY=":$(cat "$display_file")"
export DISPLAY

run_case()
{
  rules=$1
  mode=$2

  cat >"$tmp/rawm.conf" <<EOC
ui = {
  font = "monospace:size=9";
  bar = { show = true; position = "bottom"; height = 24; };
  systray = { show = false; };
  colors = {
    normal = { border = "#111111"; foreground = "#aaaaaa"; background = "#000000"; };
    selected = { border = "#222222"; foreground = "#ffffff"; background = "#000000"; };
    urgent = { border = "#ffff00"; foreground = "#ffff00"; background = "#000000"; };
    error = { border = "#ff0000"; foreground = "#ff0000"; background = "#000000"; };
  };
};
tags = { default = ({ name = "one"; }, { name = "video"; }); };
status = { enabled = false; protocol = "rawm-v2"; separator = " | "; };
input = { keys = (); buttons = (); };
rules = ($rules);
EOC

  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm=$!
  sleep 0.15
  kill -0 "$wm" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited during $mode case"
  }

  "$helper" "$mode" || {
    cat "$tmp/rawm.log" >&2
    fail "$mode case failed"
  }

  kill "$wm" 2>/dev/null || true
  wait "$wm" 2>/dev/null || true
  wm=
}

# WM-owned attention is latched for a client placed on a hidden tag. A later
# client WM_HINTS update cannot erase it, it does not forge XUrgencyHint, and
# focusing the client consumes the attention state.
run_case '{ class = "RawmAttentionTarget"; tags = (2); attention = "urgent"; }' wm

# An explicit later rule can cancel an earlier generic attention policy.
run_case '{ class = "RawmAttentionTarget"; tags = (2); attention = "urgent"; },
          { class = "RawmAttentionTarget"; title = "attention-target"; attention = "none"; }' none

# Application-owned ICCCM urgency remains independent of rule attention and is
# cleared when the application removes XUrgencyHint.
run_case '{ class = "RawmAttentionTarget"; tags = (2); attention = "none"; }' app

echo "attention rule tests: ok"
