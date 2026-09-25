#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
client=${2:-./tests/systray-client}
tmp=${TMPDIR:-/tmp}/rawm-systray-test.$$
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
  echo "systray tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "systray test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "systray-client not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
  font = "monospace:size=9";
  bar = {
    show = true;
    position = "bottom";
    height = 24;
  };
  systray = {
    show = true;
    spacing = 5;
    icon_size = 16;
  };
};
status = { enabled = false; protocol = "rawm-v2"; };
input = { keys = (); buttons = (); };
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm_pid=$!
sleep 0.15
kill -0 "$wm_pid" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
}

"$client" || {
  cat "$tmp/rawm.log" >&2
  fail "systray client failed"
}

echo "systray tests: ok"
