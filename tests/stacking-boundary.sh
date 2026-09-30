#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

if [ "$#" -ne 3 ]; then
  echo "usage: $0 RAWM STACKING-CLIENT CONTROL-CLIENT" >&2
  exit 2
fi
rawm=$1
client=$2
control=$3
tmp=${TMPDIR:-/tmp}/rawm-stacking-boundary.$$
mkdir -p "$tmp"
wm_pid=
a_pid=
b_pid=
focus_pid=
dock_pid=
surface1_pid=
surface2_pid=

cleanup()
{
  for pid in "$surface2_pid" "$surface1_pid" "$dock_pid" "$focus_pid" "$b_pid" "$a_pid" "$wm_pid"; do
    [ -z "$pid" ] || kill "$pid" 2>/dev/null || true
  done
  for pid in "$surface2_pid" "$surface1_pid" "$dock_pid" "$focus_pid" "$b_pid" "$a_pid" "$wm_pid"; do
    [ -z "$pid" ] || wait "$pid" 2>/dev/null || true
  done
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

skip()
{
  echo "stacking boundary tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "stacking boundary test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo grep sed; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "stacking-client not built"
[ -x "$control" ] || skip "control-client not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
  border_width = 2;
};
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; }
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

wait_xid()
{
  file=$1
  n=0
  while [ ! -s "$file" ]; do
    n=$((n + 1))
    [ "$n" -lt 60 ] || fail "fixture did not publish window id"
    sleep 0.05
  done
  sed -n '1p' "$file"
}

wait_admitted()
{
  xid=$1
  n=0
  pattern=$(printf 'client\t%s\t' "$xid")
  while ! "$control" --clients | grep -F "$pattern" >/dev/null; do
    n=$((n + 1))
    [ "$n" -lt 60 ] || fail "managed client $xid was not admitted"
    sleep 0.05
  done
}

send_command()
{
  command_name=$1
  "$control" --send "$@" || fail "cannot send $command_name"
  sleep 0.12
}

assert_above()
{
  lower=$1
  upper=$2
  message=$3
  "$client" above "$lower" "$upper" || fail "$message"
}

"$client" client StackA >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_xid "$tmp/a.out")
wait_admitted "$a_win"

"$client" dock >"$tmp/dock.out" 2>"$tmp/dock.err" &
dock_pid=$!
dock_win=$(wait_xid "$tmp/dock.out")
sleep 0.08
assert_above "$a_win" "$dock_win" \
  "dock did not start above the managed client"

"$client" notification >"$tmp/surface1.out" 2>"$tmp/surface1.err" &
surface1_pid=$!
surface1_win=$(wait_xid "$tmp/surface1.out")
sleep 0.08
assert_above "$a_win" "$surface1_win" \
  "external surface did not start above the managed client"

send_command _RAWM_CMD_TOGGLE_FULLSCREEN
assert_above "$dock_win" "$a_win" \
  "fullscreen did not retain its ability to cover dock chrome"
assert_above "$a_win" "$surface1_win" \
  "fullscreen raise crossed the external presentation layer"

# Fullscreen elevation is selected-client state, not a permanent managed-client
# ceiling. Returning to a tag must reassert selected fullscreen above docks; if
# another managed client becomes selected on that tag, it must remain visible
# above the fullscreen client while ordinary dock chrome stays above it.
send_command _RAWM_CMD_TAG 2
send_command _RAWM_CMD_VIEW 2
send_command _RAWM_CMD_VIEW 1
send_command _RAWM_CMD_VIEW 2
assert_above "$dock_win" "$a_win" \
  "fullscreen did not recover above dock after leaving and returning to its tag"
assert_above "$a_win" "$surface1_win" \
  "tag return raised fullscreen across the external presentation layer"

"$client" client StackFocus >"$tmp/focus.out" 2>"$tmp/focus.err" &
focus_pid=$!
focus_win=$(wait_xid "$tmp/focus.out")
wait_admitted "$focus_win"
assert_above "$a_win" "$focus_win" \
  "newly selected client remained behind visible fullscreen client"
assert_above "$focus_win" "$dock_win" \
  "ordinary selected client crossed dock chrome"

send_command _RAWM_CMD_FOCUS_STACK 1
assert_above "$dock_win" "$a_win" \
  "refocused fullscreen client did not recover above dock chrome"
assert_above "$a_win" "$surface1_win" \
  "refocused fullscreen client crossed the external presentation layer"

send_command _RAWM_CMD_FOCUS_STACK -1
assert_above "$a_win" "$focus_win" \
  "focus traversal selected a client that remained hidden behind fullscreen"
assert_above "$focus_win" "$dock_win" \
  "focus traversal raised ordinary client across dock chrome"

kill "$focus_pid" 2>/dev/null || true
wait "$focus_pid" 2>/dev/null || true
focus_pid=
sleep 0.08
send_command _RAWM_CMD_TOGGLE_FULLSCREEN
send_command _RAWM_CMD_TAG 1
send_command _RAWM_CMD_VIEW 1

"$client" client StackB >"$tmp/b.out" 2>"$tmp/b.err" &
b_pid=$!
b_win=$(wait_xid "$tmp/b.out")
wait_admitted "$b_win"

"$client" popup >"$tmp/surface2.out" 2>"$tmp/surface2.err" &
surface2_pid=$!
surface2_win=$(wait_xid "$tmp/surface2.out")
sleep 0.08
assert_above "$b_win" "$surface2_win" \
  "popup surface did not start above the selected client"

send_command _RAWM_CMD_TOGGLE_FLOATING
assert_above "$b_win" "$surface2_win" \
  "floating-client restack crossed the popup presentation layer"

send_command _RAWM_CMD_SET_LAYOUT 1
assert_above "$b_win" "$surface2_win" \
  "layout restack crossed the popup presentation layer"

printf '%s\n' 'stacking boundary tests: ok'
