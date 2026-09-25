#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
rawmctl=${2:-./rawmctl}
client=${3:-./tests/rawmctl-client}
tmp=${TMPDIR:-/tmp}/rawmctl-test.$$
mkdir -p "$tmp/bin"
tab=$(printf "\t")
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
  echo "rawmctl tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "rawmctl test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$rawmctl" ] || skip "rawmctl not built"
[ -x "$client" ] || skip "rawmctl-client not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

cat >"$tmp/rawm.conf" <<'EOC'
ui = {
  border_width = 2;
  bar = { show = false; };
  osd = { show = false; };
  systray = { show = false; };
};
status = { enabled = false; protocol = "rawm-v2"; };
tags = {
  default = (
    { name = "one"; layout = "tile"; },
    { name = "two"; layout = "tile"; }
  );
};
input = { keys = (); buttons = (); };
EOC

"$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
wm=$!
sleep 0.15
kill -0 "$wm" 2>/dev/null || {
  cat "$tmp/rawm.log" >&2
  fail "rawm exited"
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

wait_listed()
{
  window=$1
  n=0
  while :; do
    if "$rawmctl" window list >"$tmp/list" 2>"$tmp/list.err" &&
       grep -q "^${window}${tab}" "$tmp/list"; then
      return 0
    fi
    n=$((n + 1))
    [ "$n" -lt 60 ] || {
      cat "$tmp/list.err" >&2 || true
      fail "window $window did not enter _NET_CLIENT_LIST"
    }
    sleep 0.05
  done
}

wait_active()
{
  expected=$1
  n=0
  while :; do
    actual=$("$client" --active)
    [ "$actual" = "$expected" ] && return 0
    n=$((n + 1))
    [ "$n" -lt 60 ] || fail "active window is $actual, expected $expected"
    sleep 0.05
  done
}

"$client" ClassA "duplicate title" >"$tmp/a.out" 2>"$tmp/a.err" &
a_pid=$!
a_win=$(wait_window "$tmp/a.out")
wait_listed "$a_win"

"$client" ClassB "duplicate title" >"$tmp/b.out" 2>"$tmp/b.err" &
b_pid=$!
b_win=$(wait_window "$tmp/b.out")
wait_listed "$b_win"

"$rawmctl" window list >"$tmp/list"
grep -q "^${a_win}${tab}ClassA${tab}duplicate title$" "$tmp/list" ||
  fail "window list did not preserve client A identity/class/title"
grep -q "^${b_win}${tab}ClassB${tab}duplicate title$" "$tmp/list" ||
  fail "window list did not preserve client B identity/class/title"

"$rawmctl" window activate "$a_win"
wait_active "$a_win"

if "$rawmctl" window activate 0x1 >"$tmp/unmanaged.out" 2>"$tmp/unmanaged.err"; then
  fail "unmanaged window activation succeeded"
fi
grep -q 'not managed' "$tmp/unmanaged.err" ||
  fail "unmanaged activation did not explain the failure"


cat >"$tmp/bin/rawmenu" <<'EOS'
#!/bin/sh
: > "$RAWMCTL_TEST_CAPTURE"
picked=
while IFS= read -r line; do
  printf '%s\n' "$line" >> "$RAWMCTL_TEST_CAPTURE"
  case $line in
    "$RAWMCTL_TEST_PICK  "*) picked=$line ;;
  esac
done
[ "${RAWMCTL_TEST_CANCEL:-0}" -eq 0 ] || exit 1
[ -n "$picked" ] || exit 2
if [ -n "${RAWMCTL_TEST_DESTROY:-}" ]; then
  "$RAWMCTL_TEST_DESTROY" --destroy "$RAWMCTL_TEST_PICK"
  sleep 0.10
fi
printf '%s\n' "$picked"
EOS
chmod +x "$tmp/bin/rawmenu"

RAWMCTL_TEST_CAPTURE="$tmp/menu" RAWMCTL_TEST_PICK="$b_win" \
PATH="$tmp/bin:$PATH" "$rawmctl" window select
wait_active "$b_win"
grep -q "^${a_win}  ClassA - duplicate title$" "$tmp/menu" ||
  fail "selector input omitted client A"
grep -q "^${b_win}  ClassB - duplicate title$" "$tmp/menu" ||
  fail "selector input omitted client B"

if RAWMCTL_TEST_CAPTURE="$tmp/menu-cancel" RAWMCTL_TEST_PICK="$a_win" \
   RAWMCTL_TEST_CANCEL=1 PATH="$tmp/bin:$PATH" \
   "$rawmctl" window select >"$tmp/cancel.out" 2>"$tmp/cancel.err"; then
  fail "selector cancellation returned success"
fi
wait_active "$b_win"

if RAWMCTL_TEST_CAPTURE="$tmp/menu-stale" RAWMCTL_TEST_PICK="$a_win" \
   RAWMCTL_TEST_DESTROY="$client" PATH="$tmp/bin:$PATH" \
   "$rawmctl" window select >"$tmp/stale.out" 2>"$tmp/stale.err"; then
  fail "selection of a destroyed window returned success"
fi
grep -q 'not managed' "$tmp/stale.err" ||
  fail "destroyed selection did not fail through managed-window authority"
wait_active "$b_win"

# A stale EWMH snapshot must not let a disappeared client turn an ordinary
# inspection race into a fatal Xlib BadWindow error.
"$client" --client-list "$a_win" "$b_win"
"$rawmctl" window list >"$tmp/stale-list" 2>"$tmp/stale-list.err" ||
  fail "stale _NET_CLIENT_LIST made window list fail"
if grep -q "^${a_win}${tab}" "$tmp/stale-list"; then
  fail "destroyed client survived stale-list filtering"
fi
grep -q "^${b_win}${tab}" "$tmp/stale-list" ||
  fail "live client disappeared during stale-list filtering"

case $("$rawmctl" keyboard get) in
  0|1|2|3) ;;
  *) fail "keyboard get returned an invalid group" ;;
esac
"$rawmctl" keyboard set 0
[ "$("$rawmctl" keyboard get)" = 0 ] ||
  fail "keyboard group zero was not admitted"
if "$rawmctl" keyboard set 4 >"$tmp/group.out" 2>"$tmp/group.err"; then
  fail "out-of-range keyboard group succeeded"
fi
grep -q 'range 0..3' "$tmp/group.err" ||
  fail "invalid keyboard group did not explain the range"

echo "rawmctl tests: ok"
