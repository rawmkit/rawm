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
[ -x "$sendkey" ] || skip "send-key not built"

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
input = {
  keys = (
    { key = "F1"; action = "tag"; tag = "all"; },
    { key = "F2"; action = "view"; tag = "all"; },
    { key = "F3"; action = "set_layout"; layout = "floating"; },
    { key = "F4"; action = "view"; tag = 1; },
    { key = "F5"; action = "window_view"; }
  );
  buttons = ();
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

press()
{
  "$sendkey" "$1" || {
    rc=$?
    [ "$rc" -ne 77 ] || skip "XTest unavailable"
    fail "cannot synthesize $1"
  }
  sleep 0.12
}

# Make the selected client an all-tags client.
press F1

# Give the all-tags pertag slot a layout distinct from tag one.
press F2
case "$(state)" in
  *"layout=tile "*) ;;
  *) fail "all-tags view did not begin in tile layout: $(state)" ;;
esac

press F3
case "$(state)" in
  *"layout=floating "*) ;;
  *) fail "all-tags pertag layout did not become floating: $(state)" ;;
esac

# Return to tag one, whose own pertag layout remains tile.
press F4
case "$(state)" in
  *"layout=tile "*) ;;
  *) fail "tag-one layout was not restored: $(state)" ;;
esac

# window_view derives its target from the focused client's bounded TAGMASK.
# It must restore the all-tags pertag slot, not treat that mask as tag one.
press F5
case "$(state)" in
  *"layout=floating "*) ;;
  *) fail "window_view did not restore all-tags pertag state: $(state)" ;;
esac

echo "window view tests: ok"
