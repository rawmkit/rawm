#!/bin/sh
set -eu

rawm=${1:-./rawm}
client=${2:-./tests/geometry-client}

for tool in Xvfb xdpyinfo xwininfo awk sed; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "placement tests: skipped ($tool not found)"
    exit 0
  }
done
[ -x "$client" ] || {
  echo "placement tests: skipped ($client not built)"
  exit 0
}

tmp=${TMPDIR:-/tmp}/rawm-placement-test.$$
mkdir -p "$tmp"
server_pid=
wm_pid=
pids=
cleanup()
{
  for pid in $pids; do kill "$pid" 2>/dev/null || true; done
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

fail()
{
  echo "placement test failed: $*" >&2
  exit 1
}

DISPLAY=:93
export DISPLAY
Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$tmp/xvfb.log" 2>&1 &
server_pid=$!
n=0
while ! xdpyinfo >/dev/null 2>&1; do
  n=$((n + 1))
  [ "$n" -lt 50 ] || fail "Xvfb did not become ready"
  sleep 0.05
done

geometry()
{
  xwininfo -id "$1" | awk '
    /Absolute upper-left X:/ { x=$NF }
    /Absolute upper-left Y:/ { y=$NF }
    /^  Width:/ { w=$NF }
    /^  Height:/ { h=$NF }
    /^  Border width:/ { b=$NF }
    END { print x, y, w, h, b }
  '
}

wait_window()
{
  file=$1
  n=0
  while [ ! -s "$file" ]; do
    n=$((n + 1))
    [ "$n" -lt 60 ] || fail "client did not publish window id"
    sleep 0.05
  done
  sed -n '1p' "$file"
}

start_wm()
{
  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm_pid=$!
  sleep 0.15
  kill -0 "$wm_pid" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited"
  }
}

stop_case()
{
  for pid in $pids; do
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  pids=
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || wait "$wm_pid" 2>/dev/null || true
  wm_pid=
}

launch()
{
  id=$1
  shift
  "$client" "$@" >"$tmp/$id.out" 2>"$tmp/$id.err" &
  pid=$!
  pids="$pids $pid"
  last_win=$(wait_window "$tmp/$id.out")
}

write_config()
{
  policy=$1
  cat >"$tmp/rawm.conf" <<EOC
ui = { border_width = 2; bar = { show = false; }; systray = { show = false; }; };
behavior = {
  size_hints = "floating";
  floating_placement = "$policy";
  cascade_step = 32;
};
tags = { default = ({ name = "one"; layout = "floating"; }); };
input = { keys = (); buttons = (); };
EOC
}

# Cascade owns only otherwise-unplaced normal free clients. The first window is
# inset from the work-area origin, and the next visible free client is offset
# from the currently selected one.
write_config cascade
start_wm
launch one --name rawm-placement-one --geometry 0 0 200 120 --position none
w1=$last_win
sleep 0.08
[ "$(geometry "$w1")" = "32 32 200 120 2" ] ||
  fail "first cascade geometry was $(geometry "$w1"); expected 32 32 200 120 2"
launch two --name rawm-placement-two --geometry 0 0 200 120 --position none
w2=$last_win
sleep 0.08
[ "$(geometry "$w2")" = "64 64 200 120 2" ] ||
  fail "second cascade geometry was $(geometry "$w2"); expected 64 64 200 120 2"

# USPosition is user authority and must bypass the WM convenience policy.
launch user --name rawm-placement-user --geometry 300 210 200 120 --position user
w3=$last_win
sleep 0.08
[ "$(geometry "$w3")" = "300 210 200 120 2" ] ||
  fail "USPosition geometry was $(geometry "$w3"); expected 300 210 200 120 2"

# PPosition is only a program preference under cascade. The selected user-placed
# client is the reference, so this client is cascaded from it rather than kept
# at its requested 7,9 position.
launch program --name rawm-placement-program --geometry 7 9 200 120 --position program
w4=$last_win
sleep 0.08
[ "$(geometry "$w4")" = "332 242 200 120 2" ] ||
  fail "PPosition cascade was $(geometry "$w4"); expected 332 242 200 120 2"

# A cascade that would cross either work-area edge wraps to the first slot.
launch edge --name rawm-placement-edge --geometry 580 450 200 120 --position user
w5=$last_win
sleep 0.08
[ "$(geometry "$w5")" = "580 450 200 120 2" ] ||
  fail "edge USPosition geometry was $(geometry "$w5")"
launch wrap --name rawm-placement-wrap --geometry 0 0 200 120 --position none
w6=$last_win
sleep 0.08
[ "$(geometry "$w6")" = "32 32 200 120 2" ] ||
  fail "wrapped cascade geometry was $(geometry "$w6"); expected 32 32 200 120 2"
stop_case

# The explicit client policy preserves admitted coordinates, including an
# otherwise unspecified (0,0)-style application position.
write_config client
start_wm
launch client --name rawm-placement-client --geometry 73 91 200 120 --position none
w1=$last_win
sleep 0.08
[ "$(geometry "$w1")" = "73 91 200 120 2" ] ||
  fail "client placement geometry was $(geometry "$w1"); expected 73 91 200 120 2"
launch client-program --name rawm-placement-client-program --geometry 11 13 200 120 --position program
w2=$last_win
sleep 0.08
[ "$(geometry "$w2")" = "11 13 200 120 2" ] ||
  fail "client PPosition geometry was $(geometry "$w2"); expected 11 13 200 120 2"
stop_case

# Explicit rawm placement remains higher authority than the default cascade.
cat >"$tmp/rawm.conf" <<'EOC'
ui = { border_width = 2; bar = { show = false; }; systray = { show = false; }; };
behavior = { floating_placement = "cascade"; cascade_step = 32; };
tags = { default = ({ name = "one"; layout = "floating"; }); };
rules = (
  { title = "rawm-placement-centered"; floating = true; center = "monitor"; }
);
input = { keys = (); buttons = (); };
EOC
start_wm
launch centered --name rawm-placement-centered --geometry 0 0 200 120 --position none
w1=$last_win
sleep 0.08
[ "$(geometry "$w1")" = "298 238 200 120 2" ] ||
  fail "explicit center geometry was $(geometry "$w1"); expected 298 238 200 120 2"
stop_case

echo "placement tests: ok"
