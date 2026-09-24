#!/bin/sh
set -eu

case $0 in
  */*) test_dir=${0%/*} ;;
  *) test_dir=. ;;
esac
. "$test_dir/xvfb.sh"

if [ "$#" -ne 2 ]; then
  echo "usage: $0 XVFB XWININFO" >&2
  exit 2
fi

RAWM_TEST_XVFB=$1
RAWM_TEST_XWININFO=$2
export RAWM_TEST_XVFB RAWM_TEST_XWININFO

tmp=${TMPDIR:-/tmp}/rawm-xvfb-probe.$$
mkdir -p "$tmp"
cleanup()
{
  test_xvfb_stop
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

if test_xvfb_start "$tmp"; then
  exit 0
fi

exit 1
