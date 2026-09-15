#!/bin/sh
set -eu

rawm=${1:-./rawm}
helper=${2:-./tests/status-client}
tmp=${TMPDIR:-/tmp}/rawm-status-test.$$
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
  echo "status protocol tests: skipped ($*)"
  exit 0
}

fail()
{
  echo "status protocol test failed: $*" >&2
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
  enabled=$1
  helper_arg=${2:-}
  cat >"$tmp/rawm.conf" <<EOC
ui = {
  font = "monospace:size=9";
  bar = { show = true; position = "bottom"; height = 24; };
  systray = { show = false; };
};
status = { enabled = $enabled; protocol = "rawm-v1"; };
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
