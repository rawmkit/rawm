#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
helper=${2:-./tests/fullscreen-rule-client}

tmp=${TMPDIR:-/tmp}/rawm-fullscreen-rule-test.$$
mkdir -p "$tmp"
wm_pid=
client_pid=
cleanup()
{
  [ -z "$client_pid" ] || kill "$client_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "fullscreen rule tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "fullscreen rule test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo awk sed wc; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done

[ -x "$helper" ] || skip "$helper not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

geometry()
{
  xwininfo -id "$1" | awk '
    /Absolute upper-left X:/ { x=$NF }
    /Absolute upper-left Y:/ { y=$NF }
    /^  Width:/ { w=$NF }
    /^  Height:/ { h=$NF }
    /^  Border width:/ { b=$NF }
    END { print x, y, w, h, b }
  '
}

start_wm()
{
  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm_pid=$!
  sleep 0.15
  kill -0 "$wm_pid" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited"
  }
}

start_client()
{
  : >"$tmp/client.out"
  "$helper" "$@" >"$tmp/client.out" 2>"$tmp/client.err" &
  client_pid=$!
  n=0
  while [ ! -s "$tmp/client.out" ]; do
    n=$((n + 1))
    [ "$n" -lt 50 ] || {
      cat "$tmp/client.err" >&2
      fail "client did not publish window id"
    }
    sleep 0.05
  done
  win=$(sed -n '1p' "$tmp/client.out")
}

stop_case()
{
  kill "$client_pid" 2>/dev/null || true
  wait "$client_pid" 2>/dev/null || true
  client_pid=
  kill "$wm_pid" 2>/dev/null || true
  wait "$wm_pid" 2>/dev/null || true
  wm_pid=
  rm -f "$tmp/client.out" "$tmp/client.err" "$tmp/rawm.log"
}

# A fullscreen rule may place the client directly on a non-visible tag.  The
# client keeps fullscreen geometry but showhide() moves it two widths left.
cat >"$tmp/rawm.conf" <<'EOC'
ui = { border_width = 2; bar = { show = false; }; systray = { show = false; }; };
behavior = { floating_placement = "client"; };
tags = { default = ({ name = "one"; }, { name = "video"; }); };
input = { keys = (); buttons = (); };
rules = (
  { class = "RawmFullscreenRule"; tags = (2); fullscreen = true; }
);
EOC
start_wm
start_client
set -- $(geometry "$win")
[ "$1" -eq -1600 ] && [ "$2" -eq 0 ] && [ "$3" -eq 800 ] &&
[ "$4" -eq 600 ] && [ "$5" -eq 0 ] ||
  fail "tagged fullscreen geometry was $*; expected -1600 0 800 600 0"
stop_case

# Leaving rule-requested fullscreen restores the client's underlying floating
# state, original geometry, and configured border width.
cat >"$tmp/rawm.conf" <<'EOC'
ui = { border_width = 2; bar = { show = false; }; systray = { show = false; }; };
behavior = { floating_placement = "client"; };
rules = (
  { class = "RawmFullscreenRule"; floating = true; fullscreen = true; }
);
EOC
start_wm
start_client --remove-after 500
set -- $(geometry "$win")
[ "$1" -eq 0 ] && [ "$2" -eq 0 ] && [ "$3" -eq 800 ] &&
[ "$4" -eq 600 ] && [ "$5" -eq 0 ] ||
  fail "initial fullscreen geometry was $*; expected 0 0 800 600 0"
n=0
while [ "$(wc -l <"$tmp/client.out")" -lt 2 ]; do
  n=$((n + 1))
  [ "$n" -lt 50 ] || fail "client did not remove fullscreen"
  sleep 0.05
done
sleep 0.05
set -- $(geometry "$win")
[ "$1" -eq 50 ] && [ "$2" -eq 60 ] && [ "$3" -eq 320 ] &&
[ "$4" -eq 200 ] && [ "$5" -eq 2 ] ||
  fail "restored geometry was $*; expected 50 60 320 200 2"
stop_case

# An explicit false rule suppresses an application's initial fullscreen request
# without becoming a permanent policy.
cat >"$tmp/rawm.conf" <<'EOC'
ui = { border_width = 2; bar = { show = false; }; systray = { show = false; }; };
behavior = { floating_placement = "client"; };
rules = (
  { class = "RawmFullscreenRule"; floating = true; fullscreen = false; }
);
EOC
start_wm
start_client --initial-fullscreen
sleep 0.05
set -- $(geometry "$win")
[ "$1" -eq 50 ] && [ "$2" -eq 60 ] && [ "$3" -eq 320 ] &&
[ "$4" -eq 200 ] && [ "$5" -eq 2 ] ||
  fail "fullscreen=false geometry was $*; expected 50 60 320 200 2"
stop_case

echo "fullscreen rule tests: ok"
