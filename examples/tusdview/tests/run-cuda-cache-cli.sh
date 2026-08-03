#!/usr/bin/env bash
# GPU-independent parser coverage for --cuda-cache-dir.
set -uo pipefail
BIN="${TUSDVIEW:-}"
[ -x "$BIN" ] || { echo "FAIL: TUSDVIEW is not executable: $BIN"; exit 1; }

check_missing() {
  local log rc
  if log="$("$BIN" "$@" 2>&1)"; then rc=0; else rc=$?; fi
  [ "$rc" -ne 0 ] || {
    echo "FAIL: $* unexpectedly succeeded"
    exit 1
  }
  grep -q -- '--cuda-cache-dir requires a non-empty path' <<<"$log" || {
    echo "FAIL: $* did not report the specific missing-path diagnostic"
    echo "$log"
    exit 1
  }
}

check_missing --cuda-cache-dir
check_missing --cuda-cache-dir=

help_log="$($BIN --cuda-cache-dir=/tmp/tusdview-cache-cli --help 2>&1)"
grep -q -- '--cuda-cache-dir PATH' <<<"$help_log" || {
  echo "FAIL: equals-form cache directory was not accepted"
  exit 1
}

echo "PASS: CUDA cache directory CLI forms and diagnostics"
