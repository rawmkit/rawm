#!/bin/sh
set -eu

rawm=${1:-./rawm}
tmp=${TMPDIR:-/tmp}/rawm-gaps-test.$$
mkdir -p "$tmp"

cleanup()
{
  if [ -n "${clients:-}" ]; then
    kill $clients 2>/dev/null || true
  fi
  if [ -n "${wm:-}" ]; then
    kill "$wm" 2>/dev/null || true
  fi
  if [ -n "${xvfb:-}" ]; then
    kill "$xvfb" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "gap geometry tests: skipped ($*)"
  exit 0
}

fail()
{
  echo "gap geometry test failed: $*" >&2
  exit 1
}

for tool in Xvfb xterm xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done

wait_for_file()
{
  file=$1
  tries=0
  while [ ! -s "$file" ]; do
    tries=$((tries + 1))
    [ "$tries" -lt 100 ] || return 1
    sleep 0.05
  done
}

window_geometry()
{
  name=$1
  DISPLAY=$display xwininfo -root -tree 2>/dev/null |
    sed -n "/(\"$name\" \"XTerm\")/s/.*)  \([^ ]*\).*/\1/p" |
    sed -n '1p'
}

wait_for_window()
{
  name=$1
  tries=0
  geometry=
  while [ -z "$geometry" ]; do
    geometry=$(window_geometry "$name")
    [ -n "$geometry" ] && break
    tries=$((tries + 1))
    [ "$tries" -lt 100 ] || return 1
    sleep 0.05
  done
  printf '%s\n' "$geometry"
}

start_server()
{
  display_file=$tmp/display
  : >"$display_file"
  Xvfb -displayfd 3 -screen 0 800x600x24 \
    >"$tmp/xvfb.log" 2>&1 3>"$display_file" &
  xvfb=$!
  wait_for_file "$display_file" || fail "Xvfb did not select a display"
  display=":$(cat "$display_file")"
}

stop_case()
{
  if [ -n "${clients:-}" ]; then
    kill $clients 2>/dev/null || true
    clients=
  fi
  if [ -n "${wm:-}" ]; then
    kill "$wm" 2>/dev/null || true
    wm=
  fi
  if [ -n "${xvfb:-}" ]; then
    kill "$xvfb" 2>/dev/null || true
    xvfb=
  fi
  sleep 0.05
}

write_config()
{
  layout=$1
  smart=$2
  cat >"$tmp/rawm.conf" <<EOC
ui = { bar = { show = false; }; };
layout = {
  master_factor = 0.50;
  master_count = 1;
  gaps = { inner = 10; outer = 20; smart = $smart; };
};
tags = { default = ({ name = "1"; layout = "$layout"; }); };
input = { keys = (); buttons = (); };
EOC
}

start_wm()
{
  DISPLAY=$display "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm=$!
  sleep 0.10
  kill -0 "$wm" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited during geometry test"
  }
}

spawn_clients()
{
  count=$1
  clients=
  i=1
  while [ "$i" -le "$count" ]; do
    DISPLAY=$display xterm -name "gap$i" >/dev/null 2>&1 &
    clients="$clients $!"
    wait_for_window "gap$i" >/dev/null || fail "xterm gap$i did not appear"
    i=$((i + 1))
  done
  sleep 0.10
}

assert_geometries()
{
  expected=$1
  count=$2
  actual=
  i=1
  while [ "$i" -le "$count" ]; do
    geometry=$(window_geometry "gap$i")
    [ -n "$geometry" ] || fail "cannot find gap$i geometry"
    actual="${actual}${geometry}\n"
    i=$((i + 1))
  done
  actual=$(printf '%b' "$actual" | sort)
  expected=$(printf '%s\n' "$expected" | sort)
  [ "$actual" = "$expected" ] || {
    printf 'expected:\n%s\nactual:\n%s\n' "$expected" "$actual" >&2
    fail "unexpected client geometry"
  }
}

run_layout_case()
{
  layout=$1
  expected=$2
  start_server
  write_config "$layout" false
  start_wm
  spawn_clients 4
  assert_geometries "$expected" 4
  stop_case
}

run_layout_case tile '371x176+405+20
371x176+405+210
371x176+405+400
371x556+20+20'

run_layout_case bstack '242x271+20+305
242x271+276+305
242x271+532+305
756x271+20+20'

run_layout_case bstack_horizontal '756x271+20+20
756x81+20+305
756x81+20+400
756x81+20+495'

run_layout_case grid '371x271+20+20
371x271+20+305
371x271+405+20
371x271+405+305'

run_layout_case monocle '760x560+20+20
760x560+20+20
760x560+20+20
760x560+20+20'

start_server
write_config tile true
start_wm
spawn_clients 1
assert_geometries '800x600+0+0' 1
stop_case

echo "gap geometry tests: ok"
