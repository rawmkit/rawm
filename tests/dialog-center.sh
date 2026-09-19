#!/bin/sh
set -eu

rawm=${1:-./rawm}
helper=${2:-./tests/dialog-center-client}

for tool in Xvfb xwininfo awk sed; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "dialog center tests: skipped ($tool not found)"
    exit 0
  }
done

[ -x "$helper" ] || {
  echo "dialog center tests: skipped ($helper not built)"
  exit 0
}

tmp=${TMPDIR:-/tmp}/rawm-dialog-center-test.$$
mkdir -p "$tmp"
server_pid=
wm_pid=
client_pid=
cleanup()
{
  [ -z "$client_pid" ] || kill "$client_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

fail()
{
  echo "dialog center test failed: $*" >&2
  exit 1
}

DISPLAY=:97
export DISPLAY
Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$tmp/xvfb.log" 2>&1 &
server_pid=$!

n=0
while ! xdpyinfo >/dev/null 2>&1; do
  n=$((n + 1))
  [ "$n" -lt 50 ] || fail "Xvfb did not become ready"
  sleep 0.05
done

write_config()
{
  mode=$1
  override=${2:-}
  cat >"$tmp/rawm.conf" <<EOC
ui = {
  border_width = 2;
  bar = { show = false; };
  systray = { show = false; };
};
behavior = {
  dialog_center = "$mode";
  size_hints = "floating";
};
rules = (
  { class = "RawmCenterParent"; floating = true; }$override
);
EOC
}

geometry()
{
  xwininfo -id "$1" | awk '
    /Absolute upper-left X:/ { x=$NF }
    /Absolute upper-left Y:/ { y=$NF }
    /^  Width:/ { w=$NF }
    /^  Height:/ { h=$NF }
    END { print x, y, w, h }
  '
}

run_case()
{
  mode=$1
  override=$2
  expected_x=$3
  expected_y=$4
  helper_arg=${5:-}

  write_config "$mode" "$override"
  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm_pid=$!
  sleep 0.15
  kill -0 "$wm_pid" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited during $mode case"
  }

  if [ -n "$helper_arg" ]; then
    "$helper" "$helper_arg" >"$tmp/windows" 2>"$tmp/client.err" &
  else
    "$helper" >"$tmp/windows" 2>"$tmp/client.err" &
  fi
  client_pid=$!
  n=0
  while [ ! -s "$tmp/windows" ]; do
    n=$((n + 1))
    [ "$n" -lt 50 ] || {
      cat "$tmp/client.err" >&2
      fail "client did not publish window ids"
    }
    sleep 0.05
  done

  read parent dialog <"$tmp/windows"
  set -- $(geometry "$parent")
  px=$1; py=$2; pw=$3; ph=$4
  set -- $(geometry "$dialog")
  dx=$1; dy=$2; dw=$3; dh=$4

  [ "$dx" -eq "$expected_x" ] && [ "$dy" -eq "$expected_y" ] ||
    fail "$mode dialog at ${dx},${dy}, expected ${expected_x},${expected_y}; parent=${px},${py},${pw}x${ph}, dialog=${dw}x${dh}"

  kill "$client_pid" 2>/dev/null || true
  wait "$client_pid" 2>/dev/null || true
  client_pid=
  kill "$wm_pid" 2>/dev/null || true
  wait "$wm_pid" 2>/dev/null || true
  wm_pid=
  rm -f "$tmp/windows" "$tmp/client.err" "$tmp/rawm.log"
}

# Parent outer frame is 404x304 at 100,100. Dialog outer frame is 204x104.
# Parent-centered position is therefore 200,200.
run_case parent '' 200 200

# 800x600 monitor center for a 204x104 dialog.
run_case monitor '' 298 248

# Parent mode falls back to the monitor when there is no managed owner.
run_case parent '' 298 248 --orphan

# Rule policy overrides the dialog default for transient dialogs.
run_case parent ', { class = "RawmCenterDialog"; floating = true; center = "monitor"; }' 298 248
run_case monitor ', { class = "RawmCenterDialog"; floating = true; center = "parent"; }' 200 200
run_case parent ', { class = "RawmCenterDialog"; floating = true; center = "none"; }' 17 23

echo "dialog center tests: ok"
