#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
client=${2:-./tests/compositor-state-client}
control=${3:-./tests/control-client}
sendpointer=${4:-./tests/send-pointer}
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
[ -x "$control" ] || skip "control-client not built"
[ -x "$sendpointer" ] || skip "send-pointer not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<EOC
ui = {
  border_width = 2;
  bar = { show = false; };
  osd = { show = true; position = "bottom"; geometry = true; duration_ms = 300; padding = 6; offset = 12; min_width = 180; };
  systray = { show = false; };
};
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; }
  );
};
pointer = {
  bindings = (
    { button = 1; action = "move"; },
    { button = 3; action = "resize"; }
  );
};
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm_pid=$!
sleep 0.15
kill -0 "$wm_pid" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
}

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

command()
{
  "$control" --send "$@" || fail "cannot send rawm command: $*"
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

command _RAWM_CMD_TOGGLE_FLOATING
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

command _RAWM_CMD_SET_LAYOUT 1
[ "$(state "$a_win")" = "floating=1 selected=0 layout=floating monitor=0" ] ||
  fail "floating layout state missing from client A: $(state "$a_win")"
[ "$(state "$b_win")" = "floating=1 selected=1 layout=floating monitor=0" ] ||
  fail "floating layout state missing from client B: $(state "$b_win")"

command _RAWM_CMD_SET_LAYOUT 0
[ "$(state "$a_win")" = "floating=0 selected=0 layout=tile monitor=0" ] ||
  fail "client A did not return to tiled presentation: $(state "$a_win")"
[ "$(state "$b_win")" = "floating=1 selected=1 layout=tile monitor=0" ] ||
  fail "explicitly floating client B lost its presentation state: $(state "$b_win")"

sleep 0.35
[ "$("$client" --surface-state osd)" = "unmapped" ] ||
  fail "osd did not expire before workspace-feedback checks"

sleep 0.35
command _RAWM_CMD_VIEW -1
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "all-tags view did not map osd"

sleep 0.35
command _RAWM_CMD_TOGGLE_VIEW 2
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "toggle-view from all-tags view did not map osd"
kill -0 "$wm_pid" 2>/dev/null ||
  fail "rawm exited while leaving all-tags toggle-view"

sleep 0.35
command _RAWM_CMD_TAG 2
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "tag assignment did not map osd"

sleep 0.35
command _RAWM_CMD_VIEW 2
[ "$(state "$b_win")" = "floating=1 selected=1 layout=tile monitor=0" ] ||
  fail "tagged client did not become selected on tag two: $(state "$b_win")"
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "tag-two view did not map osd"

command _RAWM_CMD_CYCLE_LAYOUT 1
case "$(state "$b_win")" in
  *"layout=floating "*) ;;
  *) fail "forward layout cycle did not select floating: $(state "$b_win")" ;;
esac
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "forward layout cycle did not map osd"

command _RAWM_CMD_CYCLE_LAYOUT 1
case "$(state "$b_win")" in
  *"layout=monocle "*) ;;
  *) fail "second forward layout cycle did not select monocle: $(state "$b_win")" ;;
esac

command _RAWM_CMD_CYCLE_LAYOUT -1
case "$(state "$b_win")" in
  *"layout=floating "*) ;;
  *) fail "backward layout cycle did not return to floating: $(state "$b_win")" ;;
esac

sleep 0.35
set -- $("$client" --client-geometry "$b_win")
before_move="$1 $2 $3 $4"
sx=$(( $1 + $3 / 2 ))
sy=$(( $2 + $4 / 2 ))
"$sendpointer" 1 "$sx" "$sy" 48 32 || {
  rc=$?
  [ "$rc" -ne 77 ] || skip "XTest pointer functions unavailable"
  fail "cannot synthesize client move"
}
sleep 0.08
after_move=$("$client" --client-geometry "$b_win")
[ "$after_move" != "$before_move" ] ||
  fail "pointer drag did not move floating client"
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "interactive move did not map geometry osd"

sleep 0.35
set -- $("$client" --client-geometry "$b_win")
before_resize="$1 $2 $3 $4"
sx=$(( $1 + $3 / 2 ))
sy=$(( $2 + $4 / 2 ))
"$sendpointer" 3 "$sx" "$sy" 48 36 || {
  rc=$?
  [ "$rc" -ne 77 ] || skip "XTest pointer functions unavailable"
  fail "cannot synthesize client resize"
}
sleep 0.08
after_resize=$("$client" --client-geometry "$b_win")
[ "$after_resize" != "$before_resize" ] ||
  fail "pointer drag did not resize floating client"
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "interactive resize did not map geometry osd"

sleep 0.35
command _RAWM_CMD_CYCLE_VIEW 1
[ "$(state "$a_win")" = "floating=0 selected=1 layout=tile monitor=0" ] ||
  fail "forward view cycle did not wrap tag two to tag one: $(state "$a_win")"
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "forward view cycle did not map osd"

command _RAWM_CMD_CYCLE_VIEW -1
[ "$(state "$b_win")" = "floating=1 selected=1 layout=floating monitor=0" ] ||
  fail "backward view cycle did not wrap tag one to tag two: $(state "$b_win")"

command _RAWM_CMD_VIEW -1
command _RAWM_CMD_CYCLE_VIEW 1
[ "$(state "$a_win")" = "floating=0 selected=1 layout=tile monitor=0" ] ||
  fail "forward cycle from all-tags view did not select tag one: $(state "$a_win")"

command _RAWM_CMD_VIEW -1
command _RAWM_CMD_CYCLE_VIEW -1
[ "$(state "$b_win")" = "floating=1 selected=1 layout=floating monitor=0" ] ||
  fail "backward cycle from all-tags view did not select final tag: $(state "$b_win")"

sleep 0.35
command _RAWM_CMD_RENAME_TAG 0 0 desk
[ "$("$client" --surface-state osd)" = "mapped" ] ||
  fail "rename_tag command did not reach tag rename/OSD path"

echo "compositor state tests: ok"
