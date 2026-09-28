#!/bin/sh
set -eu

rawmstat=${1:-./rawmstat}
tmp=${TMPDIR:-/tmp}/rawmstat-protocol-test.$$
pid=
trap 'test -z "${pid:-}" || kill "$pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

fail()
{
  echo "rawmstat protocol test failed: $*" >&2
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
cat >"$tmp/rawmstat.conf" <<EOF2
sources = {
  probe = {
    provider = "exec";
    command = ("$tmp/probe", "$tmp/state");
    period_ms = 100;
    timeout_ms = 500;
  };
};
EOF2

"$rawmstat" -p -c "$tmp/rawmstat.conf" >"$tmp/stdout" 2>"$tmp/stderr" &
pid=$!
wait_for grep -q 'probe[[:space:]]warning[[:space:]]one' "$tmp/stdout" || fail "warning sample was not published"
wait_for grep -q 'probe[[:space:]]stale[[:space:]]one' "$tmp/stdout" || fail "failed refresh did not mark previous value stale"
wait_for grep -q "marking stale" "$tmp/stderr" || fail "stale transition diagnostic missing"
kill -TERM "$pid"
wait "$pid" || fail "rawmstat did not terminate cleanly"
pid=

echo "rawmstat protocol tests: ok"
