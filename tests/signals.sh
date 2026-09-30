#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
tmp=${TMPDIR:-/tmp}/rawm-signal-test.$$
mkdir -p "$tmp"
wm=
cleanup()
{
  [ -z "$wm" ] || kill -KILL "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

skip()
{
  echo "signal lifecycle tests: skipped ($*)"
  exit 77
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

for tool in Xvfb xwininfo grep; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done

write_valid_config()
{
  cat >"$tmp/rawm.conf" <<'EOC'
ui = {
};
EOC
}

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

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
