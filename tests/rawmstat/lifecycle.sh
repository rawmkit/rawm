#!/bin/sh
set -eu

rawmstat=${1:-./rawmstat}
tmp=${TMPDIR:-/tmp}/rawmstat-lifecycle-test.$$
pid=
trap 'test -z "${pid:-}" || kill "$pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

fail()
{
  echo "rawmstat lifecycle test failed: $*" >&2
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

cat >"$tmp/reload.conf" <<'EOC'
sources = { probe = { provider = "exec"; command = ("printf", "one\\n"); period_ms = 100; timeout_ms = 500; }; };
EOC
"$rawmstat" -p -c "$tmp/reload.conf" >"$tmp/stdout" 2>"$tmp/stderr" &
pid=$!
wait_for grep -q 'probe[[:space:]]normal[[:space:]]one' "$tmp/stdout" || fail "initial source was not published"

cat >"$tmp/reload.conf" <<'EOC'
sources = { probe = { provider = "exec"; command = ("printf", "two\\n"); period_ms = 100; timeout_ms = 500; }; };
EOC
kill -HUP "$pid"
wait_for grep -q 'probe[[:space:]]normal[[:space:]]two' "$tmp/stdout" || fail "SIGHUP did not reload sources"

cat >"$tmp/reload.conf" <<'EOC'
sources = { probe = { provider = "broken"; }; };
EOC
kill -HUP "$pid"
wait_for grep -q 'reload rejected' "$tmp/stderr" || fail "invalid reload was not rejected"
kill -0 "$pid" 2>/dev/null || fail "invalid reload terminated rawmstat"

kill -TERM "$pid"
wait "$pid" || fail "rawmstat did not terminate cleanly"
pid=

echo "rawmstat lifecycle tests: ok"
