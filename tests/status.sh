#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
helper=${2:-./tests/status-client}
tmp=${TMPDIR:-/tmp}/rawm-status-test.$$
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
  echo "status protocol tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "status protocol test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$helper" ] || skip "$helper not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

run_case()
{
  enabled=$1
  helper_arg=${2:-}
  cat >"$tmp/rawm.conf" <<EOC
ui = {
  font = "monospace:size=9";
  bar = { show = true; position = "bottom"; height = 24; };
  systray = { show = false; };
  colors = {
    normal = { border = "#000000"; foreground = "#ffffff"; background = "#000000"; };
    selected = { border = "#000000"; foreground = "#ffffff"; background = "#000000"; };
    urgent = { border = "#000000"; foreground = "#ffff00"; background = "#000000"; };
    error = { border = "#000000"; foreground = "#ff0000"; background = "#000000"; };
  };
};
status = { enabled = $enabled; protocol = "rawm-v2"; separator = " | "; };
input = { keys = (); buttons = (); };
EOC
  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm=$!
  sleep 0.15
  kill -0 "$wm" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited during status test"
  }
  if [ -n "$helper_arg" ]; then
    "$helper" "$helper_arg" || fail "status client rejected $enabled case"
  else
    "$helper" || fail "status client rejected $enabled case"
  fi
  kill -KILL "$wm" 2>/dev/null || true
  wait "$wm" 2>/dev/null || true
  wm=
}

run_case true
run_case false --disabled

echo "status protocol tests: ok"
