#!/bin/sh
set -eu

rawmsession=${1:-./rawmsession}
case $rawmsession in
  /*) ;;
  *) rawmsession=$(cd "$(dirname "$rawmsession")" && pwd)/$(basename "$rawmsession") ;;
esac

tmp=${TMPDIR:-/tmp}/rawmsession-lifecycle-test.$$
mkdir -p "$tmp"
session_pid=
cleanup()
{
  [ -z "$session_pid" ] || kill -KILL "$session_pid" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

fail()
{
  echo "rawmsession lifecycle test failed: $*" >&2
  [ ! -f "$tmp/stderr" ] || cat "$tmp/stderr" >&2
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

cat >"$tmp/primary-wait" <<EOF2
#!/bin/sh
printf '%s\n' \$\$ >'$tmp/primary.pid'
trap 'printf term >"$tmp/primary.term"; exit 0' TERM INT HUP
while [ ! -f '$tmp/finish' ]; do sleep 0.02; done
exit 0
EOF2
chmod +x "$tmp/primary-wait"

cat >"$tmp/sidecar-wait" <<EOF2
#!/bin/sh
printf '%s\n' \$\$ >'$tmp/sidecar.pid'
trap 'printf term >"$tmp/sidecar.term"; exit 0' TERM INT HUP
while :; do sleep 1; done
EOF2
chmod +x "$tmp/sidecar-wait"

cat >"$tmp/normal.conf" <<EOF2
primary = ("$tmp/primary-wait");
sidecars = ({ name = "probe"; command = ("$tmp/sidecar-wait"); });
EOF2
: >"$tmp/stderr"
"$rawmsession" -c "$tmp/normal.conf" >"$tmp/stdout" 2>"$tmp/stderr" &
session_pid=$!
wait_for 100 test -s "$tmp/primary.pid" || fail "primary did not start"
wait_for 100 test -s "$tmp/sidecar.pid" || fail "sidecar did not start"
: >"$tmp/finish"
wait "$session_pid" || fail "normal primary exit did not preserve status zero"
session_pid=
wait_for 100 test -f "$tmp/sidecar.term" || fail "primary exit did not terminate sidecar"

# The primary's non-zero exit status is the session exit status.
cat >"$tmp/primary-fail" <<'EOF2'
#!/bin/sh
exit 23
EOF2
chmod +x "$tmp/primary-fail"
cat >"$tmp/fail.conf" <<EOF2
primary = ("$tmp/primary-fail");
sidecars = ();
EOF2
set +e
"$rawmsession" -c "$tmp/fail.conf" >/dev/null 2>"$tmp/stderr"
rc=$?
set -e
[ "$rc" -eq 23 ] || fail "primary status 23 became $rc"

# A sidecar exit is restarted without disturbing the primary.
rm -f "$tmp/finish" "$tmp/sidecar.term"
printf '0\n' >"$tmp/restart.count"
cat >"$tmp/restart-sidecar" <<EOF2
#!/bin/sh
n=\$(cat '$tmp/restart.count')
n=\$((n + 1))
printf '%s\n' "\$n" >'$tmp/restart.count'
if [ "\$n" -eq 1 ]; then exit 7; fi
trap 'exit 0' TERM INT HUP
while :; do sleep 1; done
EOF2
chmod +x "$tmp/restart-sidecar"
cat >"$tmp/restart.conf" <<EOF2
primary = ("$tmp/primary-wait");
sidecars = ({ name = "restart"; command = ("$tmp/restart-sidecar"); });
EOF2
: >"$tmp/stderr"
"$rawmsession" -c "$tmp/restart.conf" >/dev/null 2>"$tmp/stderr" &
session_pid=$!
wait_for 150 sh -c '[ "$(cat '"$tmp/restart.count"' 2>/dev/null || echo 0)" -ge 2 ]' ||
  fail "failed sidecar was not restarted"
[ ! -f "$tmp/finish" ] || fail "primary marker unexpectedly present"
: >"$tmp/finish"
wait "$session_pid" || fail "restart test session failed"
session_pid=

# Status zero is also unexpected for a declared long-lived sidecar.
rm -f "$tmp/finish"
printf '0\n' >"$tmp/zero.count"
cat >"$tmp/zero-sidecar" <<EOF2
#!/bin/sh
n=\$(cat '$tmp/zero.count')
n=\$((n + 1))
printf '%s\n' "\$n" >'$tmp/zero.count'
if [ "\$n" -eq 1 ]; then exit 0; fi
trap 'exit 0' TERM INT HUP
while :; do sleep 1; done
EOF2
chmod +x "$tmp/zero-sidecar"
cat >"$tmp/zero.conf" <<EOF2
primary = ("$tmp/primary-wait");
sidecars = ({ name = "zero"; command = ("$tmp/zero-sidecar"); });
EOF2
"$rawmsession" -c "$tmp/zero.conf" >/dev/null 2>"$tmp/stderr" &
session_pid=$!
wait_for 150 sh -c '[ "$(cat '"$tmp/zero.count"' 2>/dev/null || echo 0)" -ge 2 ]' ||
  fail "status-zero sidecar was not restarted"
: >"$tmp/finish"
wait "$session_pid" || fail "status-zero restart session failed"
session_pid=

# A permanent crash loop is bounded and then disabled while the primary lives.
rm -f "$tmp/finish"
printf '0\n' >"$tmp/crash.count"
cat >"$tmp/crash-sidecar" <<EOF2
#!/bin/sh
n=\$(cat '$tmp/crash.count')
n=\$((n + 1))
printf '%s\n' "\$n" >'$tmp/crash.count'
exit 9
EOF2
chmod +x "$tmp/crash-sidecar"
cat >"$tmp/crash.conf" <<EOF2
primary = ("$tmp/primary-wait");
sidecars = ({ name = "crash"; command = ("$tmp/crash-sidecar"); });
EOF2
: >"$tmp/stderr"
"$rawmsession" -c "$tmp/crash.conf" >/dev/null 2>"$tmp/stderr" &
session_pid=$!
wait_for 250 grep -q "disabling after 5 restarts" "$tmp/stderr" ||
  fail "crash loop was not disabled"
count=$(cat "$tmp/crash.count")
[ "$count" -eq 6 ] || fail "crash budget started $count instances instead of 6"
sleep 0.35
[ "$(cat "$tmp/crash.count")" -eq "$count" ] ||
  fail "disabled sidecar continued restarting"
kill -0 "$session_pid" 2>/dev/null || fail "sidecar crash killed primary session"
: >"$tmp/finish"
wait "$session_pid" || fail "crash-budget session failed"
session_pid=

# Supervisor termination is propagated to the complete managed set.
rm -f "$tmp/finish" "$tmp/primary.term" "$tmp/sidecar.term" "$tmp/primary.pid" "$tmp/sidecar.pid"
: >"$tmp/stderr"
"$rawmsession" -c "$tmp/normal.conf" >/dev/null 2>"$tmp/stderr" &
session_pid=$!
wait_for 100 test -s "$tmp/primary.pid" || fail "signal-test primary did not start"
wait_for 100 test -s "$tmp/sidecar.pid" || fail "signal-test sidecar did not start"
kill -TERM "$session_pid"
set +e
wait "$session_pid"
rc=$?
set -e
session_pid=
[ "$rc" -eq 143 ] || fail "SIGTERM session status was $rc instead of 143"
wait_for 100 test -f "$tmp/primary.term" || fail "SIGTERM did not reach primary"
wait_for 100 test -f "$tmp/sidecar.term" || fail "SIGTERM did not reach sidecar"

# SIGHUP restarts the complete infrastructure set in place while preserving
# rawmsession's PID. This is distinct from sending SIGHUP directly to rawm,
# which remains the WM-only configuration reload path.
rm -f "$tmp/finish" "$tmp/primary.term" "$tmp/sidecar.term"
printf '0\n' >"$tmp/primary.starts"
printf '0\n' >"$tmp/sidecar.starts"
cat >"$tmp/reload-primary" <<EOF2
#!/bin/sh
n=\$(cat '$tmp/primary.starts')
n=\$((n + 1))
printf '%s\n' "\$n" >'$tmp/primary.starts'
trap 'exit 0' TERM INT HUP
while [ ! -f '$tmp/finish' ]; do sleep 0.02; done
EOF2
chmod +x "$tmp/reload-primary"
cat >"$tmp/reload-sidecar" <<EOF2
#!/bin/sh
n=\$(cat '$tmp/sidecar.starts')
n=\$((n + 1))
printf '%s\n' "\$n" >'$tmp/sidecar.starts'
trap 'exit 0' TERM INT HUP
while :; do sleep 1; done
EOF2
chmod +x "$tmp/reload-sidecar"
cat >"$tmp/reload.conf" <<EOF2
primary = ("$tmp/reload-primary");
sidecars = ({ name = "reload"; command = ("$tmp/reload-sidecar"); });
EOF2
"$rawmsession" -c "$tmp/reload.conf" >/dev/null 2>"$tmp/stderr" &
session_pid=$!
original_session_pid=$session_pid
wait_for 100 sh -c '[ "$(cat '"$tmp/primary.starts"' 2>/dev/null || echo 0)" -eq 1 ]' ||
  fail "reload primary did not start"
wait_for 100 sh -c '[ "$(cat '"$tmp/sidecar.starts"' 2>/dev/null || echo 0)" -eq 1 ]' ||
  fail "reload sidecar did not start"
kill -HUP "$session_pid"
wait_for 200 sh -c '[ "$(cat '"$tmp/primary.starts"' 2>/dev/null || echo 0)" -eq 2 ]' ||
  fail "SIGHUP did not restart primary"
wait_for 200 sh -c '[ "$(cat '"$tmp/sidecar.starts"' 2>/dev/null || echo 0)" -eq 2 ]' ||
  fail "SIGHUP did not restart sidecar"
kill -0 "$original_session_pid" 2>/dev/null || fail "SIGHUP changed rawmsession PID"
: >"$tmp/finish"
wait "$session_pid" || fail "reloaded session did not terminate cleanly"
session_pid=

echo "rawmsession lifecycle tests: ok"
