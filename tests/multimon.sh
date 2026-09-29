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
probe=${4:-./tests/xinerama-probe}
tmp=${TMPDIR:-/tmp}/rawm-multimon-test.$$
mkdir -p "$tmp"
wm=
client_pid=

cleanup()
{
  [ -z "$client_pid" ] || kill "$client_pid" 2>/dev/null || true
  [ -z "$client_pid" ] || wait "$client_pid" 2>/dev/null || true
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  [ -z "$wm" ] || wait "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "multi-monitor tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "multi-monitor test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "compositor-state-client not built"
[ -x "$control" ] || skip "control-client not built"
[ -x "$probe" ] || skip "xinerama-probe not built"

# Different screen sizes keep both overlapping Xvfb heads distinct under
# rawm's duplicate-geometry filter. Physical placement is irrelevant here;
# monitor identity and routing are the behavior under test.
test_xvfb_start "$tmp" "800x600x24" "640x480x24" ||
  skip "two-screen Xvfb did not become ready"

"$probe" >/dev/null 2>&1 || {
  rc=$?
  [ "$rc" -ne 77 ] || skip "Xinerama did not expose two heads"
  fail "cannot query Xinerama fixture"
}

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
};
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; }
  );
};
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
}

"$client" >"$tmp/client.out" 2>"$tmp/client.err" &
client_pid=$!

n=0
while [ ! -s "$tmp/client.out" ]; do
  n=$((n + 1))
  [ "$n" -lt 60 ] || fail "client did not publish window id"
  sleep 0.05
done
win=$(sed -n '1p' "$tmp/client.out")

state()
{
  "$client" --inspect "$win"
}

command()
{
  "$control" --send "$@" || fail "cannot send rawm command: $*"
  sleep 0.12
}

case "$(state)" in
  "floating=0 selected=1 layout=tile monitor=0") ;;
  *) fail "client did not start selected on monitor zero: $(state)" ;;
esac

# Routing does not implicitly move monitor focus.
command _RAWM_CMD_TAG_MONITOR 1
case "$(state)" in
  "floating=0 selected=0 layout=tile monitor=1") ;;
  *) fail "tag_monitor +1 did not route client to monitor one: $(state)" ;;
esac

command _RAWM_CMD_FOCUS_MONITOR 1
case "$(state)" in
  "floating=0 selected=1 layout=tile monitor=1") ;;
  *) fail "focus_monitor +1 did not select routed client: $(state)" ;;
esac

command _RAWM_CMD_FOCUS_MONITOR -1
case "$(state)" in
  "floating=0 selected=0 layout=tile monitor=1") ;;
  *) fail "focus_monitor -1 did not leave client on monitor one: $(state)" ;;
esac

command _RAWM_CMD_FOCUS_MONITOR 1
case "$(state)" in
  "floating=0 selected=1 layout=tile monitor=1") ;;
  *) fail "focus_monitor +1 did not return to monitor one: $(state)" ;;
esac

command _RAWM_CMD_TAG_MONITOR -1
case "$(state)" in
  "floating=0 selected=0 layout=tile monitor=0") ;;
  *) fail "tag_monitor -1 did not route client back to monitor zero: $(state)" ;;
esac

command _RAWM_CMD_FOCUS_MONITOR -1
case "$(state)" in
  "floating=0 selected=1 layout=tile monitor=0") ;;
  *) fail "focus_monitor -1 did not return focus to monitor zero: $(state)" ;;
esac

kill -0 "$wm" 2>/dev/null || fail "rawm exited during monitor routing"

echo "multi-monitor tests: ok"
