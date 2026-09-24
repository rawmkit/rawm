#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
client=${2:-./tests/compositor-state-client}
sendkey=${3:-./tests/send-key}
tmp=${TMPDIR:-/tmp}/rawm-compositor-state-test.$$
mkdir -p "$tmp"
wm_pid=
a_pid=
b_pid=

cleanup()
{
  [ -z "$a_pid" ] || kill "$a_pid" 2>/dev/null || true
  [ -z "$b_pid" ] || kill "$b_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$a_pid" ] || wait "$a_pid" 2>/dev/null || true
  [ -z "$b_pid" ] || wait "$b_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || wait "$wm_pid" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "compositor state tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "compositor state test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "compositor-state-client not built"
[ -x "$sendkey" ] || skip "send-key not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
  border_width = 2;
  bar = { show = true; };
  osd = { show = true; position = "bottom"; duration_ms = 300; padding = 6; offset = 12; min_width = 180; };
  systray = { show = false; };
};
tags = { default = ({ name = "one"; layout = "tile"; }); };
input = {
  keys = (
    { key = "F1"; action = "toggle_floating"; },
    { key = "F2"; action = "set_layout"; layout = "floating"; },
    { key = "F3"; action = "set_layout"; layout = "tile"; }
  );
  buttons = ();
};
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm_pid=$!
sleep 0.15
kill -0 "$wm_pid" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
}

[ "$("$client" --surface bar)" = "surface=bar monitor=0 class=rawm instance=rawm-bar type=dock" ] ||
  fail "bar compositor surface metadata is incorrect"
[ "$("$client" --surface osd)" = "surface=osd monitor=0 class=rawm instance=rawm-osd type=dock" ] ||
  fail "osd compositor surface metadata is incorrect"
[ "$("$client" --surface-state osd)" = "unmapped" ] ||
  fail "osd must start unmapped"

wait_window()
{
  file=$1
  n=0
  while [ ! -s "$file" ]; do
    n=$((n + 1))
    [ "$n" -lt 60 ] || fail "client did not publish window id"
    sleep 0.05
  done
  sed -n '1p' "$file"
}

state()
{
  "$client" --inspect "$1"
}

press()
{
  "$sendkey" "$1" || {
    rc=$?
    [ "$rc" -ne 77 ] || skip "XTest unavailable"
    fail "cannot synthesize $1"
  }
  sleep 0.12
}

"$client" >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
sleep 0.12
[ "$(state "$a_win")" = "floating=0 selected=1 layout=tile monitor=0" ] ||
  fail "unexpected initial client A state: $(state "$a_win")"

"$client" >"$tmp/b.out" 2>"$tmp/b.err" &
b_pid=$!
b_win=$(wait_window "$tmp/b.out")
sleep 0.12
[ "$(state "$a_win")" = "floating=0 selected=0 layout=tile monitor=0" ] ||
  fail "client A selection did not clear: $(state "$a_win")"
[ "$(state "$b_win")" = "floating=0 selected=1 layout=tile monitor=0" ] ||
  fail "client B did not become selected: $(state "$b_win")"

press F1
[ "$(state "$b_win")" = "floating=1 selected=1 layout=tile monitor=0" ] ||
  fail "explicit floating state not published: $(state "$b_win")"
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "floating action did not map osd"
case "$("$client" --surface-geometry osd)" in
  "width=180 "*) ;;
  *) fail "osd did not honor configured minimum width: $("$client" --surface-geometry osd)" ;;
esac
sleep 0.35
[ "$("$client" --surface-state osd)" = "unmapped" ] ||
  fail "osd did not expire after configured timeout"

press F2
[ "$(state "$a_win")" = "floating=1 selected=0 layout=floating monitor=0" ] ||
  fail "floating layout state missing from client A: $(state "$a_win")"
[ "$(state "$b_win")" = "floating=1 selected=1 layout=floating monitor=0" ] ||
  fail "floating layout state missing from client B: $(state "$b_win")"

press F3
[ "$(state "$a_win")" = "floating=0 selected=0 layout=tile monitor=0" ] ||
  fail "client A did not return to tiled presentation: $(state "$a_win")"
[ "$(state "$b_win")" = "floating=1 selected=1 layout=tile monitor=0" ] ||
  fail "explicitly floating client B lost its presentation state: $(state "$b_win")"

echo "compositor state tests: ok"
