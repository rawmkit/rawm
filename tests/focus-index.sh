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
tmp=${TMPDIR:-/tmp}/rawm-focus-index-test.$$
mkdir -p "$tmp"
wm=
a_pid=
b_pid=
c_pid=

cleanup()
{
  for pid in "$a_pid" "$b_pid" "$c_pid"; do
    [ -z "$pid" ] || kill "$pid" 2>/dev/null || true
    [ -z "$pid" ] || wait "$pid" 2>/dev/null || true
  done
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  [ -z "$wm" ] || wait "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "focus index tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "focus index test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "compositor-state-client not built"
[ -x "$control" ] || skip "control-client not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
  border_width = 2;
  bar = { show = false; };
  osd = { show = false; };
  systray = { show = false; };
};
status = { enabled = false; };
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; }
  );
};
input = { keys = (); buttons = (); };
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
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

selected()
{
  case "$(state "$1")" in
    *"selected=1 "*) return 0 ;;
    *) return 1 ;;
  esac
}

command()
{
  "$control" --send "$@" || fail "cannot send rawm command: $*"
  sleep 0.12
}

"$client" >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")

"$client" >"$tmp/b.out" 2>"$tmp/b.err" &
b_pid=$!
b_win=$(wait_window "$tmp/b.out")
sleep 0.12
selected "$b_win" || fail "client B did not start selected"

# Move B to tag two. It remains at the head of the monitor client list but is
# hidden from the current tag-one view.
command _RAWM_CMD_TAG 2
selected "$a_win" || fail "client A did not become selected after hiding B"

# Create C on tag one. Client-list order is now C, B(hidden), A while visible
# order is C, A.
"$client" >"$tmp/c.out" 2>"$tmp/c.err" &
c_pid=$!
c_win=$(wait_window "$tmp/c.out")
sleep 0.12
selected "$c_win" || fail "client C did not start selected"

command _RAWM_CMD_FOCUS_INDEX 2
selected "$a_win" ||
  fail "focus_index 2 counted hidden client B instead of visible client A"

command _RAWM_CMD_FOCUS_INDEX 1
selected "$c_win" || fail "focus_index 1 did not select first visible client"

# Out-of-range direct selection is a no-op.
command _RAWM_CMD_FOCUS_INDEX 3
selected "$c_win" || fail "out-of-range focus_index changed selection"

echo "focus index tests: ok"
