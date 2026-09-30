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
tmp=${TMPDIR:-/tmp}/rawm-actions-test.$$
mkdir -p "$tmp"
wm=
a_pid=
b_pid=

cleanup()
{
  for pid in "$a_pid" "$b_pid"; do
    [ -z "$pid" ] || kill "$pid" 2>/dev/null || true
    [ -z "$pid" ] || wait "$pid" 2>/dev/null || true
  done
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  [ -z "$wm" ] || wait "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

skip()
{
  echo "action tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "action test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "compositor-state-client not built"
[ -x "$control" ] || skip "control-client not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<EOF
ui = {
  border_width = 2;
};
layout = {
  master_factor = 0.50;
  master_count = 1;
  gaps = { inner = 0; outer = 0; smart = false; };
};
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; }
  );
};
EOF

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
}

command()
{
  "$control" --send "$@" || fail "cannot send rawm command: $*"
  sleep 0.12
}

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

geom()
{
  "$client" --client-geometry "$1"
}

selected()
{
  case "$(state "$1")" in
    *"selected=1 "*) return 0 ;;
    *) return 1 ;;
  esac
}

"$client" >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")

"$client" >"$tmp/b.out" 2>"$tmp/b.err" &
b_pid=$!
b_win=$(wait_window "$tmp/b.out")
sleep 0.12
selected "$b_win" || fail "client B did not start selected"

# Relative focus follows the visible client sequence in both directions.
command _RAWM_CMD_FOCUS_STACK 1
selected "$a_win" || fail "focus_stack forward did not select client A"
command _RAWM_CMD_FOCUS_STACK -1
selected "$b_win" || fail "focus_stack backward did not return to client B"

# B starts as master. zoom must promote A and focus it.
command _RAWM_CMD_ZOOM
selected "$a_win" || fail "zoom did not promote/focus the next tiled client"

# Increasing nmaster from one to two must make A consume the full layout width.
set -- $(geom "$a_win")
master_width=$3
command _RAWM_CMD_INC_MASTER 1
set -- $(geom "$a_win")
two_master_width=$3
[ "$two_master_width" -gt "$master_width" ] ||
  fail "inc_master did not expand the first client: $master_width -> $two_master_width"

command _RAWM_CMD_INC_MASTER -1
set -- $(geom "$a_win")
restored_master_width=$3
[ "$restored_master_width" -lt "$two_master_width" ] ||
  fail "inc_master -1 did not restore a split layout"

# Relative master-factor changes must alter admitted tile geometry.
command _RAWM_CMD_SET_MASTER_FACTOR 100000 0
set -- $(geom "$a_win")
larger_master_width=$3
[ "$larger_master_width" -gt "$restored_master_width" ] ||
  fail "positive master-factor delta did not widen master"

command _RAWM_CMD_SET_MASTER_FACTOR -100000 0
set -- $(geom "$a_win")
normal_master_width=$3
[ "$normal_master_width" -lt "$larger_master_width" ] ||
  fail "negative master-factor delta did not narrow master"

# Add tag two to A, switch there, and prove A remains visible while B (tag one
# only) is moved offscreen by rawm's normal hide path.
command _RAWM_CMD_TOGGLE_TAG 2
command _RAWM_CMD_VIEW 2
set -- $(geom "$a_win")
[ "$1" -ge 0 ] || fail "toggle_tag did not keep client A visible on tag two"
set -- $(geom "$b_win")
[ "$1" -lt 0 ] || fail "tag-one-only client B remained visible on tag two"

command _RAWM_CMD_VIEW 1
set -- $(geom "$a_win")
[ "$1" -ge 0 ] || fail "client A did not return visibly to tag one"
set -- $(geom "$b_win")
[ "$1" -ge 0 ] || fail "client B did not return visibly to tag one"
selected "$a_win" || fail "client A lost selection before fullscreen test"

# Fullscreen is temporary elevation and must restore the previous tiled state
# and rectangle.
before_fullscreen=$(geom "$a_win")
command _RAWM_CMD_TOGGLE_FULLSCREEN
case "$(state "$a_win")" in
  "floating=1 selected=1 layout=tile monitor=0") ;;
  *) fail "toggle_fullscreen did not expose elevated selected state: $(state "$a_win")" ;;
esac
set -- $(geom "$a_win")
[ "$1" -eq 0 ] && [ "$2" -eq 0 ] &&
[ "$3" -eq 800 ] && [ "$4" -eq 600 ] ||
  fail "fullscreen geometry is not the monitor rectangle: $(geom "$a_win")"

command _RAWM_CMD_TOGGLE_FULLSCREEN
case "$(state "$a_win")" in
  "floating=0 selected=1 layout=tile monitor=0") ;;
  *) fail "leaving fullscreen did not restore tiled state: $(state "$a_win")" ;;
esac
[ "$(geom "$a_win")" = "$before_fullscreen" ] ||
  fail "leaving fullscreen did not restore client geometry"

# kill_client must use the advertised WM_DELETE_WINDOW protocol. The helper
# destroys its own window and exits cleanly; rawm then selects the survivor.
command _RAWM_CMD_KILL_CLIENT
n=0
while kill -0 "$a_pid" 2>/dev/null; do
  n=$((n + 1))
  [ "$n" -lt 60 ] || fail "kill_client did not close WM_DELETE-aware client"
  sleep 0.025
done
wait "$a_pid"
a_pid=
kill -0 "$wm" 2>/dev/null || fail "rawm exited after kill_client"
selected "$b_win" || fail "surviving client B was not selected after close"

echo "action tests: ok"
