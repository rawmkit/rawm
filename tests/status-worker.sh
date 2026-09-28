#!/bin/sh
set -eu

rawm=${1:-./rawm}
tmp=${TMPDIR:-/tmp}/rawm-status-worker-test.$$
pid=
trap 'test -z "${pid:-}" || kill "$pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

fail()
{
  echo "status worker test failed: $*" >&2
  test ! -f "$tmp/stderr" || cat "$tmp/stderr" >&2
  exit 1
}

wait_for()
{
  n=0
  while ! "$@"; do
    n=$((n + 1))
    [ "$n" -lt 100 ] || return 1
    sleep 0.05
  done
}

cat >"$tmp/probe" <<'EOS'
#!/bin/sh
state=$1
if [ ! -e "$state" ]; then
  printf 'warning\tone\n'
  : >"$state"
else
  exit 1
fi
EOS
chmod +x "$tmp/probe"
cat >"$tmp/rawm.conf" <<EOF2
status = {
  sources = {
    clock = { enabled = false; };
    probe = {
      provider = "exec";
      command = ("$tmp/probe", "$tmp/state");
      period_ms = 100;
      timeout_ms = 500;
    };
  };
};
EOF2

"$rawm" --status-debug -c "$tmp/rawm.conf" >"$tmp/stdout" 2>"$tmp/stderr" &
pid=$!
wait_for grep -q 'probe[[:space:]]warning[[:space:]]one' "$tmp/stdout" ||
  fail "warning sample was not published"
wait_for grep -q 'probe[[:space:]]stale[[:space:]]one' "$tmp/stdout" ||
  fail "failed refresh did not mark the previous value stale"
wait_for grep -q "marking stale" "$tmp/stderr" ||
  fail "stale transition diagnostic missing"
kill -TERM "$pid"
wait "$pid" || fail "status debug worker did not terminate cleanly"
pid=

cat >"$tmp/hang" <<'EOS'
#!/bin/sh
trap '' TERM
sleep 60
EOS
chmod +x "$tmp/hang"
cat >"$tmp/hang.conf" <<EOF2
status = {
  sources = {
    clock = { enabled = false; };
    hang = {
      provider = "exec";
      command = ("$tmp/hang");
      period_ms = 100;
      timeout_ms = 60000;
    };
  };
};
EOF2
"$rawm" --status-debug -c "$tmp/hang.conf" >"$tmp/hang.out" 2>"$tmp/hang.err" &
pid=$!
sleep 0.10
kill -TERM "$pid"
n=0
while kill -0 "$pid" 2>/dev/null; do
  n=$((n + 1))
  if [ "$n" -ge 100 ]; then
    kill -KILL "$pid" 2>/dev/null || true
    fail "status debug shutdown waited for exec timeout"
  fi
  sleep 0.02
done
wait "$pid" || fail "status debug cancellation did not exit cleanly"
pid=

echo "status worker tests: ok"
