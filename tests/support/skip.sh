#!/bin/sh
set -eu

printf 'skipped: %s\n' "${1:-test fixture unavailable}"
exit 77
