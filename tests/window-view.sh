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
tmp=${TMPDIR:-/tmp}/rawm-window-view-test.$$
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
  echo "window view tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "window view test failed: $*" >&2
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

# Make the selected client an all-tags client.
command _RAWM_CMD_TAG -1

# Give the all-tags pertag slot a layout distinct from tag one.
command _RAWM_CMD_VIEW -1
case "$(state)" in
  *"layout=tile "*) ;;
  *) fail "all-tags view did not begin in tile layout: $(state)" ;;
esac

command _RAWM_CMD_SET_LAYOUT 1
case "$(state)" in
  *"layout=floating "*) ;;
  *) fail "all-tags pertag layout did not become floating: $(state)" ;;
esac

# Return to tag one, whose own pertag layout remains tile.
command _RAWM_CMD_VIEW 1
case "$(state)" in
  *"layout=tile "*) ;;
  *) fail "tag-one layout was not restored: $(state)" ;;
esac

# window_view derives its target from the focused client's bounded TAGMASK.
# It must restore the all-tags pertag slot, not treat that mask as tag one.
command _RAWM_CMD_WINDOW_VIEW
case "$(state)" in
  *"layout=floating "*) ;;
  *) fail "window_view did not restore all-tags pertag state: $(state)" ;;
esac

echo "window view tests: ok"
