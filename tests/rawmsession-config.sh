#!/bin/sh
set -eu

rawmsession=${1:-./rawmsession}
tmp=${TMPDIR:-/tmp}/rawmsession-config-test.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
  echo "rawmsession config test failed: $*" >&2
  [ ! -f "$tmp/stderr" ] || cat "$tmp/stderr" >&2
  exit 1
}

expect_fail()
{
  if "$rawmsession" -C -c "$1" >/dev/null 2>"$tmp/stderr"; then
    fail "$1 unexpectedly validated"
  fi
}

cat >"$tmp/valid.conf" <<'EOC'
primary = ("rawm", "-c", "/tmp/rawm.conf");
sidecars = (
  { name = "helper-main"; command = ("long-running-helper"); },
  { name = "picom.glx"; command = ("picom", "--backend", "glx"); }
);
EOC
"$rawmsession" -C -c "$tmp/valid.conf" >/dev/null 2>"$tmp/stderr" ||
  fail "valid configuration was rejected"

cat >"$tmp/empty-primary.conf" <<'EOC'
primary = ();
EOC
expect_fail "$tmp/empty-primary.conf"

cat >"$tmp/empty-arg.conf" <<'EOC'
primary = ("rawm");
sidecars = ({ name = "bad"; command = (""); });
EOC
expect_fail "$tmp/empty-arg.conf"

cat >"$tmp/duplicate.conf" <<'EOC'
sidecars = (
  { name = "same"; command = ("one"); },
  { name = "same"; command = ("two"); }
);
EOC
expect_fail "$tmp/duplicate.conf"

cat >"$tmp/bad-name.conf" <<'EOC'
sidecars = ({ name = "bad name"; command = ("one"); });
EOC
expect_fail "$tmp/bad-name.conf"

cat >"$tmp/unknown.conf" <<'EOC'
primary = ("rawm");
autostart = ("xterm");
EOC
expect_fail "$tmp/unknown.conf"

cat >"$tmp/unknown-sidecar.conf" <<'EOC'
sidecars = ({ name = "helper"; command = ("long-running-helper"); restart = true; });
EOC
expect_fail "$tmp/unknown-sidecar.conf"

# User policy may deliberately remove all sidecars.
cat >"$tmp/no-sidecars.conf" <<'EOC'
sidecars = ();
EOC
"$rawmsession" -C -c "$tmp/no-sidecars.conf" >/dev/null 2>"$tmp/stderr" ||
  fail "empty sidecar replacement was rejected"

echo "rawmsession config tests: ok"
