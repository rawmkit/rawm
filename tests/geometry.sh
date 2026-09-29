#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
client=${2:-./tests/geometry-client}
control=${3:-./tests/control-client}

tmp=${TMPDIR:-/tmp}/rawm-geometry-test.$$
mkdir -p "$tmp"
wm_pid=
a_pid=
b_pid=
cleanup()
{
  [ -z "$a_pid" ] || kill "$a_pid" 2>/dev/null || true
  [ -z "$b_pid" ] || kill "$b_pid" 2>/dev/null || true
  [ -z "$wm_pid" ] || kill "$wm_pid" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "geometry tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "geometry test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo awk sed wc; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
for helper in "$client" "$control"; do
  [ -x "$helper" ] || skip "$helper not built"
done

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

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

command()
{
  "$control" --send "$@" || fail "cannot send rawm command: $*"
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
ui = { border_width = 2; };
behavior = { size_hints = "$mode"; floating_placement = "client"; };
tags = { default = ({ name = "one"; layout = "tile"; }); };
EOC
}

# Switching the monitor to the floating layout releases the current arranged
# rectangles in place. It must not teleport clients back to historical free
# geometry or apply floating-only size hints merely because the arranger was
# removed. Accepted geometry changes while free still become saved free state.
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
a_tiled="$1 $2 $3 $4 $5"
b_tiled=$(geometry "$b_win")
command _RAWM_CMD_SET_LAYOUT 1
[ "$(geometry "$a_win")" = "$a_tiled" ] ||
  fail "floating layout moved client A: $(geometry "$a_win"); expected $a_tiled"
[ "$(geometry "$b_win")" = "$b_tiled" ] ||
  fail "floating layout moved client B: $(geometry "$b_win"); expected $b_tiled"
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
command _RAWM_CMD_SET_LAYOUT 0
a_tiled_again=$(geometry "$a_win")
[ "$a_tiled_again" != "111 123 350 240 2" ] ||
  fail "client A remained in free geometry after returning to tile: $a_tiled_again"
command _RAWM_CMD_SET_LAYOUT 1
[ "$(geometry "$a_win")" = "$a_tiled_again" ] ||
  fail "second floating layout transition restored historical geometry: $(geometry "$a_win"); expected $a_tiled_again"
stop_case

# Toggling an individual client floating is different: it restores that
# client's saved free rectangle. The floating-only hint policy is applied when
# materializing that explicit client transition.
write_config floating
start_wm
"$client" --name rawm-geometry-toggle --geometry 40 60 323 217 --increment 10 10 \
  >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
sleep 0.12
command _RAWM_CMD_TOGGLE_FLOATING
set -- $(geometry "$a_win")
[ "$1" -eq 40 ] && [ "$2" -eq 60 ] && [ "$3" -eq 320 ] && [ "$4" -eq 210 ] && [ "$5" -eq 2 ] ||
  fail "individual floating restore with hints was $*; expected 40 60 320 210 2"
stop_case

# "never" restores the same per-client free geometry byte-for-byte rather
# than rounding it to application increments.
write_config never
start_wm
"$client" --name rawm-geometry-never --geometry 40 60 323 217 --increment 10 10 \
  >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
sleep 0.12
command _RAWM_CMD_TOGGLE_FLOATING
set -- $(geometry "$a_win")
[ "$1" -eq 40 ] && [ "$2" -eq 60 ] && [ "$3" -eq 323 ] && [ "$4" -eq 217 ] && [ "$5" -eq 2 ] ||
  fail "size_hints=never restore was $*; expected 40 60 323 217 2"
stop_case

# A normal client created while the monitor is already in floating layout keeps
# its admitted position unless an explicit centering policy says otherwise.
write_config floating
start_wm
command _RAWM_CMD_SET_LAYOUT 1
"$client" --name rawm-geometry-floating-manage --geometry 73 91 321 207 --increment 1 1 \
  >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
sleep 0.12
set -- $(geometry "$a_win")
[ "$1" -eq 73 ] && [ "$2" -eq 91 ] && [ "$3" -eq 321 ] && [ "$4" -eq 207 ] && [ "$5" -eq 2 ] ||
  fail "floating-layout manage implicitly centered/resized client: $*"
stop_case


# A ConfigureRequest from a client on a non-visible tag may update canonical
# geometry, but it must not materialize that geometry on the current tag.
cat >"$tmp/rawm.conf" <<'EOC'
ui = { border_width = 2; };
behavior = { size_hints = "never"; floating_placement = "client"; };
tags = { default = ({ name = "one"; }, { name = "two"; }); };
rules = (
  { class = "RawmGeometry"; tags = (2); floating = true; }
);
EOC
start_wm
"$client" --name rawm-geometry-hidden-request --geometry 40 60 323 217 \
  --resize-after 300 500 200 100 100 >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
n=0
while [ "$(wc -l <"$tmp/a.out")" -lt 2 ]; do
  n=$((n + 1))
  [ "$n" -lt 50 ] || fail "hidden client did not issue ConfigureRequest"
  sleep 0.05
done
sleep 0.08
set -- $(geometry "$a_win")
[ "$1" -eq -208 ] && [ "$2" -eq 200 ] && [ "$3" -eq 100 ] && [ "$4" -eq 100 ] && [ "$5" -eq 2 ] ||
  fail "hidden ConfigureRequest materialized as $*; expected -208 200 100 100 2"
stop_case

# Property-driven recentering follows the same visibility projection. Updating
# the EWMH window type must not pull a centered tag-2 client onto tag 1.
cat >"$tmp/rawm.conf" <<'EOC'
ui = { border_width = 2; };
behavior = { size_hints = "never"; floating_placement = "client"; };
tags = { default = ({ name = "one"; }, { name = "two"; }); };
rules = (
  { class = "RawmGeometry"; tags = (2); floating = true; center = "monitor"; }
);
EOC
start_wm
"$client" --name rawm-geometry-hidden-property --geometry 40 60 323 217 \
  --dialog-after 300 >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
n=0
while [ "$(wc -l <"$tmp/a.out")" -lt 2 ]; do
  n=$((n + 1))
  [ "$n" -lt 50 ] || fail "hidden client did not update its window type"
  sleep 0.05
done
sleep 0.08
set -- $(geometry "$a_win")
[ "$1" -eq -654 ] && [ "$2" -eq 189 ] && [ "$3" -eq 323 ] && [ "$4" -eq 217 ] && [ "$5" -eq 2 ] ||
  fail "hidden property recenter materialized as $*; expected -654 189 323 217 2"
stop_case


# ConfigureRequest monitor-bound checks operate on the outer client frame. A
# request whose content box fits but whose configured border does not must be
# pulled back inside the monitor rather than leaving the frame clipped.
cat >"$tmp/rawm.conf" <<'EOC'
ui = { border_width = 2; };
behavior = { size_hints = "never"; floating_placement = "client"; };
rules = (
  { class = "RawmGeometry"; floating = true; }
);
EOC
start_wm
"$client" --name rawm-geometry-frame-bounds --geometry 40 60 323 217 \
  --resize-after 300 699 200 100 100 >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
n=0
while [ "$(wc -l <"$tmp/a.out")" -lt 2 ]; do
  n=$((n + 1))
  [ "$n" -lt 50 ] || fail "frame-bounds client did not issue ConfigureRequest"
  sleep 0.05
done
sleep 0.08
set -- $(geometry "$a_win")
[ "$1" -eq 348 ] && [ "$2" -eq 200 ] && [ "$3" -eq 100 ] && [ "$4" -eq 100 ] && [ "$5" -eq 2 ] ||
  fail "border-overflow ConfigureRequest was $*; expected 348 200 100 100 2"
stop_case

echo "geometry tests: ok"
