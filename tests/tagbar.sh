#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
helper=${2:-./tests/tagbar-client}
tmp=${TMPDIR:-/tmp}/rawm-tagbar-test.$$
mkdir -p "$tmp"
wm=
cleanup()
{
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "tag bar tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "tag bar test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$helper" ] || skip "$helper not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

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
    selected = { border = "#204060"; foreground = "#ffffff"; background = "#304050"; };
    urgent = { border = "#806000"; foreground = "#ffffff"; background = "#806000"; };
    error = { border = "#800000"; foreground = "#ffffff"; background = "#800000"; };
  };
};
status = { enabled = false; };
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
