#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/support/xvfb.sh"

client=${1:-./tests/color-visual-test}
tmp=${TMPDIR:-/tmp}/rawm-color-visual-test.$$
mkdir -p "$tmp"

cleanup()
{
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

skip()
{
  echo "color visual tests: skipped ($*)"
  exit 77
}

command -v Xvfb >/dev/null 2>&1 || skip "Xvfb not found"
[ -x "$client" ] || skip "color-visual-test not built"

test_xvfb_start "$tmp" || skip "Xvfb did not become ready"

set +e
"$client"
rc=$?
set -e
[ "$rc" -ne 77 ] || skip "Xvfb exposes no alpha TrueColor visual"
[ "$rc" -eq 0 ] || exit "$rc"
