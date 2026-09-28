#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
clicker=${2:-./tests/status-client}
checksum=${3:-./tests/status-bar-checksum}
tmp=${TMPDIR:-/tmp}/rawm-status-test.$$
mkdir -p "$tmp"
wm=
cleanup()
{
  [ -z "$wm" ] || kill -KILL "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "status tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "status test failed: $*" >&2
  test ! -f "$tmp/rawm.log" || cat "$tmp/rawm.log" >&2
  exit 1
}

wait_for()
{
  limit=$1
  shift
  n=0
  while ! "$@"; do
    n=$((n + 1))
    [ "$n" -lt "$limit" ] || return 1
    sleep 0.02
  done
}

checksum_changed()
{
  old=$1
  current=$($checksum 2>/dev/null || printf 0)
  [ "$current" != "$old" ]
}

worker_pid()
{
  file=/proc/$wm/task/$wm/children
  [ -r "$file" ] || return 1
  set -- $(cat "$file")
  [ "$#" -eq 1 ] || return 1
  printf '%s\n' "$1"
}

write_config()
{
  binding_id=${1:-}
  enabled=${2:-true}
  cat >"$tmp/rawm.conf" <<EOF2
ui = {
  font = "monospace:size=9";
  bar = { show = true; position = "bottom"; height = 24; };
  systray = { show = false; };
};
status = {
  enabled = $enabled;
  separator = " | ";
  sources = {
    clock = { enabled = false; };
    probe = {
      provider = "exec";
      command = ("$tmp/probe", "$tmp/value");
      period_ms = 100;
      timeout_ms = 500;
    };
  };
};
input = {
  keys = ();
  buttons = (
EOF2
  if [ -n "$binding_id" ]; then
    cat >>"$tmp/rawm.conf" <<EOF2
    { click = "status"; status_id = "$binding_id"; button = 1; action = "quit"; }
EOF2
  fi
  cat >>"$tmp/rawm.conf" <<'EOF2'
  );
};
EOF2
}

start_rawm()
{
  : >"$tmp/rawm.log"
  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.out" 2>"$tmp/rawm.log" &
  wm=$!
  wait_for 100 kill -0 "$wm" || fail "rawm exited during status test"
  wait_for 100 "$checksum" >/dev/null 2>&1 || fail "rawm bar was not created"
}

stop_rawm()
{
  [ -z "$wm" ] && return 0
  kill -TERM "$wm" 2>/dev/null || true
  wait "$wm" 2>/dev/null || true
  wm=
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$clicker" ] || skip "$clicker not built"
[ -x "$checksum" ] || skip "$checksum not built"
[ -d /proc/self/task ] || skip "/proc task children interface unavailable"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/probe" <<'EOS'
#!/bin/sh
cat "$1"
EOS
chmod +x "$tmp/probe"
printf 'normal\tone\n' >"$tmp/value"

write_config "" true
start_rawm
sleep 0.25
one_hash=$($checksum)
printf 'warning\ttwo\n' >"$tmp/value"
wait_for 100 checksum_changed "$one_hash" || fail "warning update did not redraw status"
warning_hash=$($checksum)
printf 'critical\tthree\n' >"$tmp/value"
wait_for 100 checksum_changed "$warning_hash" || fail "critical update did not redraw status"

old_worker=$(worker_pid) || fail "could not identify internal status worker"
kill -KILL "$old_worker"
kill -0 "$wm" 2>/dev/null || fail "status worker death terminated rawm"
new_worker=
tries=0
while [ "$tries" -lt 180 ]; do
  candidate=$(worker_pid 2>/dev/null || true)
  if [ -n "$candidate" ] && [ "$candidate" != "$old_worker" ]; then
    new_worker=$candidate
    break
  fi
  tries=$((tries + 1))
  sleep 0.02
done
[ -n "$new_worker" ] || fail "rawm did not restart the status worker"
restart_hash=$($checksum)
printf 'normal\tfour\n' >"$tmp/value"
wait_for 100 checksum_changed "$restart_hash" ||
  fail "restarted status worker did not resume publication"
stop_rawm

write_config "" false
start_rawm
sleep 0.20
if worker_pid >/dev/null 2>&1; then
  fail "status.enabled=false still created a status worker"
fi
stop_rawm

printf 'normal\tclick\n' >"$tmp/value"
write_config other true
start_rawm
sleep 0.20
"$clicker" || fail "could not click unmatched status segment"
kill -0 "$wm" 2>/dev/null || fail "unmatched status_id triggered binding"
stop_rawm

write_config probe true
start_rawm
sleep 0.20
"$clicker" || fail "could not click matched status segment"
tries=0
while kill -0 "$wm" 2>/dev/null && [ "$tries" -lt 50 ]; do
  tries=$((tries + 1))
  sleep 0.02
done
if kill -0 "$wm" 2>/dev/null; then
  fail "matched status_id did not trigger binding"
fi
wait "$wm" 2>/dev/null || true
wm=

echo "status tests: ok"
