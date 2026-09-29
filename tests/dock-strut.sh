#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
fixture=${2:-./tests/dock-strut-client}
control=${3:-./tests/control-client}
tmp=${TMPDIR:-/tmp}/rawm-dock-strut-test.$$
mkdir -p "$tmp"
wm_pid=
dock_pid=
client_pid=

cleanup()
{
  [ -z "$client_pid" ] || kill "$client_pid" 2>/dev/null || true
  [ -z "$dock_pid" ] || kill "$dock_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$client_pid" ] || wait "$client_pid" 2>/dev/null || true
  [ -z "$dock_pid" ] || wait "$dock_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || wait "$wm_pid" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "dock strut tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "dock strut test failed: $*" >&2
  [ ! -f "$tmp/rawm.log" ] || cat "$tmp/rawm.log" >&2
  exit 1
}

for tool in Xvfb xwininfo awk sed grep wc; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$fixture" ] || skip "dock-strut-client not built"
[ -x "$control" ] || skip "control-client not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
  border_width = 0;
  bar = { show = false; };
  osd = { show = false; };
  systray = { show = false; };
};
layout = { gaps = { inner = 0; outer = 0; smart = false; }; };
tags = { default = ({ name = "one"; layout = "tile"; }); };
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm_pid=$!
n=0
while ! "$control" --state >/dev/null 2>&1; do
  n=$((n + 1))
  [ "$n" -lt 100 ] || fail "rawm did not publish readiness"
  kill -0 "$wm_pid" 2>/dev/null || fail "rawm exited during startup"
  sleep 0.02
done

wait_line()
{
  file=$1
  line=$2
  n=0
  while [ "$(wc -l <"$file" 2>/dev/null || printf 0)" -lt "$line" ]; do
    n=$((n + 1))
    [ "$n" -lt 100 ] || fail "fixture did not publish line $line"
    sleep 0.02
  done
  sed -n "${line}p" "$file"
}

geometry()
{
  xwininfo -id "$1" 2>/dev/null | awk '
    /Absolute upper-left X:/ { x=$NF }
    /Absolute upper-left Y:/ { y=$NF }
    /^  Width:/ { w=$NF }
    /^  Height:/ { h=$NF }
    /^  Border width:/ { b=$NF }
    END { print x, y, w, h, b }
  '
}

wait_geometry()
{
  window=$1
  expected=$2
  n=0
  while :; do
    actual=$(geometry "$window" || true)
    [ "$actual" = "$expected" ] && return 0
    n=$((n + 1))
    [ "$n" -lt 100 ] || fail "geometry $actual; expected $expected"
    kill -0 "$wm_pid" 2>/dev/null || fail "rawm exited while waiting for geometry"
    sleep 0.02
  done
}

stop_pair()
{
  [ -z "$client_pid" ] || kill "$client_pid" 2>/dev/null || true
  [ -z "$client_pid" ] || wait "$client_pid" 2>/dev/null || true
  client_pid=
  [ -z "$dock_pid" ] || kill "$dock_pid" 2>/dev/null || true
  [ -z "$dock_pid" ] || wait "$dock_pid" 2>/dev/null || true
  dock_pid=
}

# A normal (non-override-redirect) EWMH dock is not admitted as a rawm client.
# Its partial top strut reserves work area, live property changes are observed,
# and destroying it restores the full monitor.
"$fixture" dock partial managed 40 60 >"$tmp/dock.out" 2>"$tmp/dock.err" &
dock_pid=$!
dock_window=$(wait_line "$tmp/dock.out" 1)
[ -n "$dock_window" ] || fail "managed dock did not publish its window"

"$fixture" client >"$tmp/client.out" 2>"$tmp/client.err" &
client_pid=$!
client_window=$(wait_line "$tmp/client.out" 1)
wait_geometry "$client_window" "0 40 800 560 0"

kill -USR1 "$dock_pid"
[ "$(wait_line "$tmp/dock.out" 2)" = "changed" ] || fail "dock did not update strut"
wait_geometry "$client_window" "0 60 800 540 0"

kill "$dock_pid"
wait "$dock_pid" 2>/dev/null || true
dock_pid=
wait_geometry "$client_window" "0 0 800 600 0"
kill "$client_pid"
wait "$client_pid" 2>/dev/null || true
client_pid=

# Override-redirect docks use the same standard reservation contract. Legacy
# _NET_WM_STRUT remains supported for small/simple panels.
: >"$tmp/dock.out"
: >"$tmp/client.out"
"$fixture" dock legacy override 25 >"$tmp/dock.out" 2>"$tmp/dock.err" &
dock_pid=$!
wait_line "$tmp/dock.out" 1 >/dev/null
"$fixture" client >"$tmp/client.out" 2>"$tmp/client.err" &
client_pid=$!
client_window=$(wait_line "$tmp/client.out" 1)
wait_geometry "$client_window" "0 25 800 575 0"
stop_pair

echo "dock strut tests: ok"
