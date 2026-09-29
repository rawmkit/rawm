#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
client=${2:-./tests/control-client}
tmp=${TMPDIR:-/tmp}/rawm-control-membrane-test.$$
mkdir -p "$tmp"
wm_pid=

cleanup()
{
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || wait "$wm_pid" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "control membrane tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "control membrane test failed: $*" >&2
  [ ! -f "$tmp/rawm.log" ] || cat "$tmp/rawm.log" >&2
  exit 1
}

for tool in Xvfb xwininfo grep; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "control-client not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"
tab=$(printf '\t')

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
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
commands = {};
input = { keys = (); buttons = (); };
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm_pid=$!

n=0
while ! "$client" --state >"$tmp/state" 2>/dev/null; do
  n=$((n + 1))
  [ "$n" -lt 100 ] || fail "authoritative state did not appear"
  kill -0 "$wm_pid" 2>/dev/null || fail "rawm exited before publishing state"
  sleep 0.02
done

[ "$("$client" --version)" = "1" ] ||
  fail "unexpected control protocol version"
"$client" --supports _RAWM_CMD_VIEW || fail "view command not advertised"
"$client" --supports _RAWM_CMD_RENAME_TAG || fail "rename-tag command not advertised"
if "$client" --supports _RAWM_CMD_TOGGLE_BAR; then
  fail "panel command leaked into rawm control authority"
fi
if "$client" --supports _RAWM_CMD_VOLUME_UP; then
  fail "volume command leaked into rawm control authority"
fi

state()
{
  "$client" --state
}

wait_state()
{
  pattern=$1
  n=0
  while :; do
    current=$(state) || fail "cannot read authoritative state"
    printf '%s\n' "$current" | grep -F "$pattern" >/dev/null && return 0
    n=$((n + 1))
    [ "$n" -lt 100 ] || fail "state did not reach: $pattern"
    kill -0 "$wm_pid" 2>/dev/null || fail "rawm exited while waiting for state"
    sleep 0.02
  done
}

initial=$(state)
printf '%s\n' "$initial" | grep -Fx 'rawm-state-v1' >/dev/null ||
  fail "state version marker missing"
printf '%s\n' "$initial" | grep -F "selected-monitor	0" >/dev/null ||
  fail "selected monitor missing"
printf '%s\n' "$initial" | grep -F "monitor	0	1	0	0	800	600	00000001" >/dev/null ||
  fail "initial monitor authority is incorrect"
printf '%s\n' "$initial" | grep -F "tag	0	1	one" >/dev/null ||
  fail "tag one authority missing"
printf '%s\n' "$initial" | grep -F "tag	0	2	two" >/dev/null ||
  fail "tag two authority missing"

"$client" --send _RAWM_CMD_VIEW 2
wait_state "monitor	0	1	0	0	800	600	00000002"

"$client" --send _RAWM_CMD_SET_LAYOUT 2
wait_state "	monocle	"

"$client" --send _RAWM_CMD_SET_MASTER_FACTOR 650000 1
wait_state "	1	0.650000"

"$client" --send _RAWM_CMD_RENAME_TAG 0 0 'desk%work'
wait_state "tag	0	2	2/desk%25work"

before=$(state)
"$client" --send _RAWM_CMD_TOGGLE_BAR
sleep 0.05
after=$(state)
[ "$before" = "$after" ] || fail "unknown/non-WM command changed rawm state"
kill -0 "$wm_pid" 2>/dev/null || fail "rawm exited after unknown command"

kill "$wm_pid"
wait "$wm_pid" 2>/dev/null || true
wm_pid=
if "$client" --state >/dev/null 2>&1; then
  fail "control authority survived rawm exit"
fi

printf '%s\n' "control membrane tests: ok"
