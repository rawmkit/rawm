#!/bin/sh
set -eu

rawm=${1:-./rawm}
tmp=${TMPDIR:-/tmp}/rawm-signal-test.$$
mkdir -p "$tmp"
xvfb=
wm=
cleanup()
{
  [ -z "$wm" ] || kill -KILL "$wm" 2>/dev/null || true
  [ -z "$xvfb" ] || kill "$xvfb" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "signal lifecycle tests: skipped ($*)"
  exit 0
}

fail()
{
  echo "signal lifecycle test failed: $*" >&2
  [ ! -f "$tmp/rawm.log" ] || cat "$tmp/rawm.log" >&2
  exit 1
}

wait_dead()
{
  pid=$1
  n=0
  while kill -0 "$pid" 2>/dev/null; do
    n=$((n + 1))
    [ "$n" -lt 100 ] || return 1
    sleep 0.02
  done
}

command -v Xvfb >/dev/null 2>&1 || skip "Xvfb not found"

write_valid_config()
{
  cat >"$tmp/rawm.conf" <<'EOC'
ui = {
  font = "monospace:size=9";
  bar = { show = true; position = "bottom"; height = 24; };
  systray = { show = false; };
};
status = { enabled = false; protocol = "rawm-v2"; };
input = { keys = (); buttons = (); };
EOC
}

display_file=$tmp/display
: >"$display_file"
Xvfb -displayfd 3 -screen 0 800x600x24 -nolisten tcp \
  >"$tmp/xvfb.log" 2>&1 3>"$display_file" &
xvfb=$!
n=0
while [ ! -s "$display_file" ]; do
  n=$((n + 1))
  [ "$n" -lt 100 ] || fail "Xvfb did not select a display"
  sleep 0.02
done
DISPLAY=":$(cat "$display_file")"
export DISPLAY

# SIGTERM must wake an otherwise idle X event loop.
write_valid_config
"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || fail "rawm exited before SIGTERM test"
kill -TERM "$wm"
wait_dead "$wm" || fail "SIGTERM did not wake the X event loop"
wait "$wm" || fail "SIGTERM did not terminate rawm cleanly"
wm=

# SIGHUP must wake the loop and exec the original argv.  Replacing the
# selected configuration with an invalid file makes the restarted process
# exit, proving that configuration was re-read by the exec path.
write_valid_config
"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || fail "rawm exited before SIGHUP test"
printf '%s\n' 'this_is_invalid = true;' >"$tmp/rawm.conf"
kill -HUP "$wm"
wait_dead "$wm" || fail "SIGHUP did not wake/restart the X event loop"
if wait "$wm"; then
  fail "SIGHUP restart unexpectedly accepted invalid replacement config"
fi
wm=
grep -q "unknown setting 'this_is_invalid'" "$tmp/rawm.log" ||
  fail "SIGHUP did not reload the selected configuration"

echo "signal lifecycle tests: ok"
