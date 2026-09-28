#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
sendkey=${2:-./tests/send-key}
tmp=${TMPDIR:-/tmp}/rawm-clipboard-binding-test.$$
mkdir -p "$tmp/bin"
wm=

cleanup()
{
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  [ -z "$wm" ] || wait "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "clipboard binding test: skipped ($*)"
  exit 77
}

fail()
{
  echo "clipboard binding test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$sendkey" ] || skip "send-key not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/bin/rawclip" <<EOF_INNER
#!/bin/sh
printf '%s\\n' "\$#" > "$tmp/argc"
printf '%s\\n' "\$@" > "$tmp/argv"
EOF_INNER
chmod +x "$tmp/bin/rawclip"

cat >"$tmp/rawm.conf" <<'EOF_CONFIG'
ui = {
  bar = { show = false; };
  osd = { show = false; };
  systray = { show = false; };
};
status = { enabled = false; };
EOF_CONFIG

PATH="$tmp/bin:$PATH" "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
}

set +e
"$sendkey" Super_L v
rc=$?
set -e
[ "$rc" -eq 0 ] || {
  [ "$rc" -ne 77 ] || skip "XTest unavailable"
  fail "cannot synthesize Mod4+v"
}

n=0
while [ ! -s "$tmp/argv" ]; do
  n=$((n + 1))
  [ "$n" -lt 60 ] || {
    cat "$tmp/rawm.log" >&2
    fail "Mod4+v did not execute rawclip"
  }
  sleep 0.025
done

[ "$(cat "$tmp/argc")" = "1" ] || fail "rawclip received unexpected argument count"
IFS= read -r arg < "$tmp/argv"
[ "$arg" = "menu" ] || fail "rawclip was not invoked with menu"
kill -0 "$wm" 2>/dev/null || fail "rawm exited after clipboard-history spawn"

echo "clipboard binding test: ok"
