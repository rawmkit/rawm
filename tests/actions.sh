#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
client=${2:-./tests/compositor-state-client}
sendkey=${3:-./tests/send-key}
tmp=${TMPDIR:-/tmp}/rawm-actions-test.$$
mkdir -p "$tmp"
wm=
a_pid=
b_pid=

cleanup()
{
  for pid in "$a_pid" "$b_pid"; do
    [ -z "$pid" ] || kill "$pid" 2>/dev/null || true
    [ -z "$pid" ] || wait "$pid" 2>/dev/null || true
  done
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  [ -z "$wm" ] || wait "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

skip()
{
  echo "action tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "action test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$client" ] || skip "compositor-state-client not built"
[ -x "$sendkey" ] || skip "send-key not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/spawn-probe.sh" <<EOF
#!/bin/sh
: > "$tmp/spawn.marker"
EOF
chmod +x "$tmp/spawn-probe.sh"

cat >"$tmp/rawm.conf" <<EOF
ui = {
  border_width = 2;
  bar = { show = false; };
  osd = { show = false; };
  systray = { show = false; };
};
status = { enabled = false; protocol = "rawm-v2"; };
layout = {
  master_factor = 0.50;
  master_count = 1;
  gaps = { inner = 0; outer = 0; smart = false; };
};
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; }
  );
};
commands = {
  probe = ("sh", "$tmp/spawn-probe.sh");
};
input = {
  keys = (
    { key = "s"; action = "spawn"; command = "probe"; },
    { key = "F1"; action = "focus_stack"; number = 1; },
    { key = "F2"; action = "focus_stack"; number = -1; },
    { key = "F3"; action = "zoom"; },
    { key = "F4"; action = "inc_master"; number = 1; },
    { key = "F5"; action = "inc_master"; number = -1; },
    { key = "F6"; action = "set_master_factor"; delta = 0.10; },
    { key = "F7"; action = "set_master_factor"; delta = -0.10; },
    { key = "F8"; action = "toggle_tag"; tag = 2; },
    { key = "F9"; action = "view"; tag = 2; },
    { key = "F10"; action = "view"; tag = 1; },
    { key = "F11"; action = "toggle_fullscreen"; },
    { key = "F12"; action = "kill_client"; }
  );
  buttons = ();
};
EOF

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
}

press()
{
  "$sendkey" "$1" || {
    rc=$?
    [ "$rc" -ne 77 ] || skip "XTest unavailable"
    fail "cannot synthesize $1"
  }
  sleep 0.12
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

state()
{
  "$client" --inspect "$1"
}

geom()
{
  "$client" --client-geometry "$1"
}

selected()
{
  case "$(state "$1")" in
    *"selected=1 "*) return 0 ;;
    *) return 1 ;;
  esac
}

# Async spawn must execute without owning rawm's event loop.
rm -f "$tmp/spawn.marker"
press s
n=0
while [ ! -f "$tmp/spawn.marker" ]; do
  n=$((n + 1))
  [ "$n" -lt 40 ] || fail "spawn action did not execute configured command"
  sleep 0.025
done

"$client" >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")

"$client" >"$tmp/b.out" 2>"$tmp/b.err" &
b_pid=$!
b_win=$(wait_window "$tmp/b.out")
sleep 0.12
selected "$b_win" || fail "client B did not start selected"

# Relative focus follows the visible client sequence in both directions.
press F1
selected "$a_win" || fail "focus_stack forward did not select client A"
press F2
selected "$b_win" || fail "focus_stack backward did not return to client B"

# B starts as master. zoom must promote A and focus it.
press F3
selected "$a_win" || fail "zoom did not promote/focus the next tiled client"

# Increasing nmaster from one to two must make A consume the full layout width.
set -- $(geom "$a_win")
master_width=$3
press F4
set -- $(geom "$a_win")
two_master_width=$3
[ "$two_master_width" -gt "$master_width" ] ||
  fail "inc_master did not expand the first client: $master_width -> $two_master_width"

press F5
set -- $(geom "$a_win")
restored_master_width=$3
[ "$restored_master_width" -lt "$two_master_width" ] ||
  fail "inc_master -1 did not restore a split layout"

# Relative master-factor changes must alter admitted tile geometry.
press F6
set -- $(geom "$a_win")
larger_master_width=$3
[ "$larger_master_width" -gt "$restored_master_width" ] ||
  fail "positive master-factor delta did not widen master"

press F7
set -- $(geom "$a_win")
normal_master_width=$3
[ "$normal_master_width" -lt "$larger_master_width" ] ||
  fail "negative master-factor delta did not narrow master"

# Add tag two to A, switch there, and prove A remains visible while B (tag one
# only) is moved offscreen by rawm's normal hide path.
press F8
press F9
set -- $(geom "$a_win")
[ "$1" -ge 0 ] || fail "toggle_tag did not keep client A visible on tag two"
set -- $(geom "$b_win")
[ "$1" -lt 0 ] || fail "tag-one-only client B remained visible on tag two"

press F10
set -- $(geom "$a_win")
[ "$1" -ge 0 ] || fail "client A did not return visibly to tag one"
set -- $(geom "$b_win")
[ "$1" -ge 0 ] || fail "client B did not return visibly to tag one"
selected "$a_win" || fail "client A lost selection before fullscreen test"

# Fullscreen is temporary elevation and must restore the previous tiled state
# and rectangle.
before_fullscreen=$(geom "$a_win")
press F11
case "$(state "$a_win")" in
  "floating=1 selected=1 layout=tile monitor=0") ;;
  *) fail "toggle_fullscreen did not expose elevated selected state: $(state "$a_win")" ;;
esac
set -- $(geom "$a_win")
[ "$1" -eq 0 ] && [ "$2" -eq 0 ] &&
[ "$3" -eq 800 ] && [ "$4" -eq 600 ] ||
  fail "fullscreen geometry is not the monitor rectangle: $(geom "$a_win")"

press F11
case "$(state "$a_win")" in
  "floating=0 selected=1 layout=tile monitor=0") ;;
  *) fail "leaving fullscreen did not restore tiled state: $(state "$a_win")" ;;
esac
[ "$(geom "$a_win")" = "$before_fullscreen" ] ||
  fail "leaving fullscreen did not restore client geometry"

# kill_client must use the advertised WM_DELETE_WINDOW protocol. The helper
# destroys its own window and exits cleanly; rawm then selects the survivor.
press F12
n=0
while kill -0 "$a_pid" 2>/dev/null; do
  n=$((n + 1))
  [ "$n" -lt 60 ] || fail "kill_client did not close WM_DELETE-aware client"
  sleep 0.025
done
wait "$a_pid"
a_pid=
kill -0 "$wm" 2>/dev/null || fail "rawm exited after kill_client"
selected "$b_win" || fail "surviving client B was not selected after close"

echo "action tests: ok"
