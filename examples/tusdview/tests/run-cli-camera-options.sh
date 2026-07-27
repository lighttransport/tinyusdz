#!/usr/bin/env bash
# Parser-level regression for deterministic capture controls. These checks run
# before renderer initialization, so they remain hermetic on headless builders.
set -uo pipefail

TUSDVIEW="${1:?usage: $0 /path/to/tusdview}"

expect_failure() {
  local expected="$1"
  shift
  local log rc
  log="$("$TUSDVIEW" "$@" 2>&1)"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "FAIL: command unexpectedly succeeded: $*"
    exit 1
  fi
  if ! grep -Fq -- "$expected" <<<"$log"; then
    echo "FAIL: command did not report '$expected': $*"
    echo "$log"
    exit 1
  fi
}

expect_failure "--view-dir must be non-zero" --view-dir 0,0,0
expect_failure "three finite comma-separated values" --view-dir 1,2
expect_failure "three finite comma-separated values" --view-dir nan,0,-1
expect_failure "--view-dir cannot be combined with --camera" \
  --view-dir 0,0,-1 --camera Cam
expect_failure "--camera-conform must be fit, crop, horizontal, vertical, or none" \
  --camera-conform stretch
expect_failure "--size must be WxH" --size 256
expect_failure "--size must be WxH" --size 0x256
expect_failure "--size must be WxH" --size 256x256junk
expect_failure "--size must be WxH" --size

help="$("$TUSDVIEW" --help 2>&1)" || {
  echo "FAIL: --help returned failure"
  exit 1
}
grep -Fq -- "--view-dir X,Y,Z" <<<"$help" || {
  echo "FAIL: --view-dir is missing from help"
  exit 1
}
grep -Fq -- "--camera-conform MODE" <<<"$help" || {
  echo "FAIL: --camera-conform is missing from help"
  exit 1
}
grep -Fq -- "--no-grid" <<<"$help" || {
  echo "FAIL: --no-grid is missing from help"
  exit 1
}
grep -Fq -- "--size WxH" <<<"$help" || {
  echo "FAIL: --size is missing from help"
  exit 1
}

echo "PASS: deterministic camera/grid/size CLI validation"
