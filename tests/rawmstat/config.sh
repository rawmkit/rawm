#!/bin/sh
set -eu

rawmstat=${1:-./rawmstat}
default_config=${2:-config/rawmstat.conf}
tmp=${TMPDIR:-/tmp}/rawmstat-config-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

fail()
{
  echo "rawmstat config test failed: $*" >&2
  exit 1
}

expect_ok()
{
  "$rawmstat" -C -c "$1" >/dev/null 2>"$tmp/stderr" || {
    cat "$tmp/stderr" >&2
    fail "expected valid configuration: $1"
  }
}

expect_fail()
{
  if "$rawmstat" -C -c "$1" >/dev/null 2>"$tmp/stderr"; then
    fail "expected invalid configuration: $1"
  fi
}

expect_ok "$default_config"

cat >"$tmp/valid.conf" <<'EOC'
sources = {
  battery = { provider = "battery"; period_ms = 15000; warning = 20; critical = 10; };
  cpu = { provider = "cpu"; period_ms = 2000; warning = 80; critical = 95; };
  memory = { provider = "memory"; period_ms = 5000; warning = 85; critical = 95; };
  disk = { provider = "filesystem"; path = "/"; period_ms = 60000; warning = 20; critical = 5; };
  network = { provider = "network"; interface = "default"; period_ms = 2000; };
  clock = { provider = "clock"; format = "%H:%M"; period_ms = 30000; };
  helper = { provider = "exec"; command = ("printf", "ok\\n"); period_ms = 1000; timeout_ms = 500; };
};
EOC
expect_ok "$tmp/valid.conf"

cat >"$tmp/legacy-blocks.conf" <<'EOC'
blocks = ({ name = "clock"; command = ("date"); interval = 5; signal = 1; });
EOC
expect_fail "$tmp/legacy-blocks.conf"
grep -q "unknown setting 'blocks'" "$tmp/stderr" || fail "legacy blocks were not rejected"

cat >"$tmp/legacy-protocol.conf" <<'EOC'
status = { protocol = "rawm-v2"; };
EOC
expect_fail "$tmp/legacy-protocol.conf"
grep -q "unknown setting 'status'" "$tmp/stderr" || fail "legacy protocol policy was not rejected"

cat >"$tmp/provider.conf" <<'EOC'
sources = { x = { provider = "not-a-provider"; }; };
EOC
expect_fail "$tmp/provider.conf"
grep -q "unknown provider" "$tmp/stderr" || fail "unknown provider diagnostic missing"

cat >"$tmp/exec.conf" <<'EOC'
sources = { x = { provider = "exec"; }; };
EOC
expect_fail "$tmp/exec.conf"
grep -q "has no command" "$tmp/stderr" || fail "missing exec command diagnostic missing"

cat >"$tmp/cpu-threshold.conf" <<'EOC'
sources = { cpu = { provider = "cpu"; warning = 95; critical = 80; }; };
EOC
expect_fail "$tmp/cpu-threshold.conf"
grep -q "warning < critical" "$tmp/stderr" || fail "CPU threshold diagnostic missing"

cat >"$tmp/battery-threshold.conf" <<'EOC'
sources = { battery = { provider = "battery"; warning = 10; critical = 20; }; };
EOC
expect_fail "$tmp/battery-threshold.conf"
grep -q "critical < warning" "$tmp/stderr" || fail "battery threshold diagnostic missing"

cat >"$tmp/period.conf" <<'EOC'
sources = { clock = { provider = "clock"; period_ms = 99; }; };
EOC
expect_fail "$tmp/period.conf"
grep -q "period_ms is out of range" "$tmp/stderr" || fail "period range diagnostic missing"

cat >"$tmp/command-native.conf" <<'EOC'
sources = { clock = { provider = "clock"; command = ("date"); }; };
EOC
expect_fail "$tmp/command-native.conf"
grep -q "command is valid only for exec" "$tmp/stderr" || fail "native command rejection missing"

cat >"$tmp/network-interface-long.conf" <<'EOC'
sources = { network = { provider = "network"; interface = "interface-name-is-too-long"; }; };
EOC
expect_fail "$tmp/network-interface-long.conf"
grep -q "invalid interface" "$tmp/stderr" || fail "overlong network interface was not rejected"

cat >"$tmp/network-interface-path.conf" <<'EOC'
sources = { network = { provider = "network"; interface = ".."; }; };
EOC
expect_fail "$tmp/network-interface-path.conf"
grep -q "invalid interface" "$tmp/stderr" || fail "path-like network interface was not rejected"

echo "rawmstat config tests: ok"
