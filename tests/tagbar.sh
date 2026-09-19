#!/bin/sh
set -eu

rawm=${1:-./rawm}
helper=${2:-./tests/tagbar-client}
tmp=${TMPDIR:-/tmp}/rawm-tagbar-test.$$
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
  echo "tag bar tests: skipped ($*)"
  exit 0
}

fail()
{
  echo "tag bar test failed: $*" >&2
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
  visibility=$1
  cat >"$tmp/rawm.conf" <<EOC
ui = {
  font = "monospace:size=9";
  bar = {
    show = true;
    position = "bottom";
    height = 24;
    tags = { visibility = "$visibility"; };
  };
  systray = { show = false; };
  colors = {
    normal = { border = "#101010"; foreground = "#eeeeee"; background = "#101010"; };
    selected = { border = "#204060"; foreground = "#ffffff"; background = "#204060"; };
    urgent = { border = "#806000"; foreground = "#ffffff"; background = "#806000"; };
    error = { border = "#800000"; foreground = "#ffffff"; background = "#800000"; };
  };
};
status = { enabled = false; protocol = "rawm-v2"; };
behavior = { floating_placement = "client"; };
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; },
    { name = "three"; layout = "tile"; }
  );
};
rules = (
  { title = "rawm-tagbar-one"; tags = (1); },
  { title = "rawm-tagbar-three"; tags = (3); }
);
input = {
  keys = ();
  buttons = (
    { click = "tag_bar"; button = 1; action = "view"; }
  );
};
EOC

  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm=$!
  sleep 0.15
  kill -0 "$wm" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited in $visibility mode"
  }

  "$helper" "$visibility" || {
    cat "$tmp/rawm.log" >&2
    fail "$visibility helper failed"
  }

  kill "$wm" 2>/dev/null || true
  wait "$wm" 2>/dev/null || true
  wm=
}

run_case occupied
run_case all

echo "tag bar tests: ok"
