#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

rawm=${1:-./rawm}
helper=${2:-./tests/attention-rule-client}

tmp=${TMPDIR:-/tmp}/rawm-attention-rule-test.$$
mkdir -p "$tmp"
wm=
cleanup()
{
  [ -z "$wm" ] || kill "$wm" 2>/dev/null || true
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

skip()
{
  echo "attention rule tests: skipped ($*)"
  exit 77
}

fail()
{
  echo "attention rule test failed: $*" >&2
  exit 1
}

for tool in Xvfb xwininfo; do
  command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$helper" ] || skip "$helper not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

run_case()
{
  rules=$1
  mode=$2

  cat >"$tmp/rawm.conf" <<EOC
ui = {
  colors = {
    normal = "#111111";
    selected = "#222222";
    urgent = "#ffff00";
  };
};
tags = { default = ({ name = "one"; }, { name = "video"; }); };
rules = ($rules);
EOC

  "$rawm" -c "$tmp/rawm.conf" >"$tmp/rawm.log" 2>&1 &
  wm=$!
  sleep 0.15
  kill -0 "$wm" 2>/dev/null || {
    cat "$tmp/rawm.log" >&2
    fail "rawm exited during $mode case"
  }

  "$helper" "$mode" || {
    cat "$tmp/rawm.log" >&2
    fail "$mode case failed"
  }

  kill "$wm" 2>/dev/null || true
  wait "$wm" 2>/dev/null || true
  wm=
}

# WM-owned attention is latched for a client placed on a hidden tag. A later
# client WM_HINTS update cannot erase it, it does not forge XUrgencyHint, and
# focusing the client consumes the attention state.
run_case '{ class = "RawmAttentionTarget"; tags = (2); attention = "urgent"; }' wm

# An explicit later rule can cancel an earlier generic attention policy.
run_case '{ class = "RawmAttentionTarget"; tags = (2); attention = "urgent"; },
          { class = "RawmAttentionTarget"; title = "attention-target"; attention = "none"; }' none

# Application-owned ICCCM urgency remains independent of rule attention and is
# cleared when the application removes XUrgencyHint.
run_case '{ class = "RawmAttentionTarget"; tags = (2); attention = "none"; }' app

echo "attention rule tests: ok"
