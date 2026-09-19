#!/bin/sh
set -eu

rawm=${1:-./rawm}
client=${2:-./tests/geometry-client}
sendkey=${3:-./tests/send-key}

for tool in Xvfb xdpyinfo xwininfo awk sed; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "geometry tests: skipped ($tool not found)"
    exit 0
  }
done
for helper in "$client" "$sendkey"; do
  [ -x "$helper" ] || {
    echo "geometry tests: skipped ($helper not built)"
    exit 0
  }
done

tmp=${TMPDIR:-/tmp}/rawm-geometry-test.$$
mkdir -p "$tmp"
server_pid=
wm_pid=
a_pid=
b_pid=
cleanup()
{
  [ -z "$a_pid" ] || kill "$a_pid" 2>/dev/null || true
  [ -z "$b_pid" ] || kill "$b_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

fail()
{
  echo "geometry test failed: $*" >&2
  exit 1
}

DISPLAY=:94
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

press()
{
  if ! "$sendkey" "$1"; then
    rc=$?
    [ "$rc" -ne 77 ] || {
      echo "geometry tests: skipped (XTest unavailable)"
      exit 0
    }
    fail "cannot synthesize $1"
  fi
  sleep 0.12
}

stop_case()
{
  [ -z "$a_pid" ] || kill "$a_pid" 2>/dev/null || true
  [ -z "$b_pid" ] || kill "$b_pid" 2>/dev/null || true
  [ -z "$a_pid" ] || wait "$a_pid" 2>/dev/null || true
  [ -z "$b_pid" ] || wait "$b_pid" 2>/dev/null || true
  a_pid=
  b_pid=
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || wait "$wm_pid" 2>/dev/null || true
  wm_pid=
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

write_config()
{
  mode=$1
  cat >"$tmp/rawm.conf" <<EOC
ui = { border_width = 2; bar = { show = false; }; systray = { show = false; }; };
behavior = { size_hints = "$mode"; };
tags = { default = ({ name = "one"; layout = "tile"; }); };
input = {
  keys = (
    { key = "F1"; action = "set_layout"; layout = "floating"; },
    { key = "F2"; action = "set_layout"; layout = "tile"; }
  );
  buttons = ();
};
EOC
}

# In the default floating-only hint policy, arranged geometry does not destroy
# each client's admitted free rectangle.  Entering floating mode restores it
# and applies WM_NORMAL_HINTS.  A later free resize becomes the next restore
# point after another trip through an arranged layout.
write_config floating
start_wm
"$client" --name rawm-geometry-a --geometry 40 60 323 217 --increment 10 10 \
  --resize-after 1200 111 123 357 241 >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
"$client" --name rawm-geometry-b --geometry 420 80 281 183 --increment 1 1 \
  >"$tmp/b.out" 2>"$tmp/b.err" &
b_pid=$!
a_win=$(wait_window "$tmp/a.out")
b_win=$(wait_window "$tmp/b.out")
sleep 0.15
set -- $(geometry "$a_win")
[ "$1" -ne 40 ] || [ "$2" -ne 60 ] || [ "$3" -ne 323 ] || [ "$4" -ne 217 ] ||
  fail "client A was not arranged while tiled: $*"
press F1
set -- $(geometry "$a_win")
[ "$1" -eq 40 ] && [ "$2" -eq 60 ] && [ "$3" -eq 320 ] && [ "$4" -eq 210 ] && [ "$5" -eq 2 ] ||
  fail "floating restore with hints was $*; expected 40 60 320 210 2"
set -- $(geometry "$b_win")
[ "$1" -eq 420 ] && [ "$2" -eq 80 ] && [ "$3" -eq 281 ] && [ "$4" -eq 183 ] && [ "$5" -eq 2 ] ||
  fail "second floating restore was $*; expected 420 80 281 183 2"
n=0
while [ "$(wc -l <"$tmp/a.out")" -lt 2 ]; do
  n=$((n + 1))
  [ "$n" -lt 80 ] || fail "client A did not issue its free resize request"
  sleep 0.05
done
sleep 0.12
set -- $(geometry "$a_win")
[ "$1" -eq 111 ] && [ "$2" -eq 123 ] && [ "$3" -eq 350 ] && [ "$4" -eq 240 ] && [ "$5" -eq 2 ] ||
  fail "free ConfigureRequest was $*; expected 111 123 350 240 2"
press F2
set -- $(geometry "$a_win")
[ "$1" -ne 111 ] || [ "$2" -ne 123 ] || [ "$3" -ne 350 ] || [ "$4" -ne 240 ] ||
  fail "client A remained in free geometry after returning to tile: $*"
press F1
set -- $(geometry "$a_win")
[ "$1" -eq 111 ] && [ "$2" -eq 123 ] && [ "$3" -eq 350 ] && [ "$4" -eq 240 ] && [ "$5" -eq 2 ] ||
  fail "updated free geometry was not restored: $*"
stop_case

# "never" makes the distinction explicit: the same saved free geometry is
# restored byte-for-byte rather than being rounded to application increments.
write_config never
start_wm
"$client" --name rawm-geometry-never --geometry 40 60 323 217 --increment 10 10 \
  >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
sleep 0.12
press F1
set -- $(geometry "$a_win")
[ "$1" -eq 40 ] && [ "$2" -eq 60 ] && [ "$3" -eq 323 ] && [ "$4" -eq 217 ] && [ "$5" -eq 2 ] ||
  fail "size_hints=never restore was $*; expected 40 60 323 217 2"
stop_case

echo "geometry tests: ok"
