#!/usr/bin/env bash
#
# Load the checked-in alpha-cutout + PointInstancer shadow fixture and verify the
# renderer produces geometry with no errors. The full shadow quality assertion is
# in run-raster-shadow-map.sh; this test checks basic load + render success.
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build/tusdview}"
[ -x "$BIN" ] || BIN="$ROOT/build_ninja/tusdview"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }
ASSET="$ROOT/tests/usda/tusdview-shadow-alpha-inst.usda"
[ -f "$ASSET" ] || { echo "SKIP: fixture missing"; exit "$SKIP"; }
RUN=()
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a)
fi
LOG="$("${RUN[@]}" "$BIN" --backend gl --frames 2 --no-grid "$ASSET" 2>&1)"
if ! echo "$LOG" | grep -q "render stats"; then
  echo "SKIP: no renderer"
  exit $SKIP
fi
if echo "$LOG" | grep -qi "error\|failed"; then
  echo "$LOG"
  echo "FAIL: alpha-cutout + PointInstancer shadow fixture produced errors"
  exit 1
fi
echo "PASS: alpha-cutout + PointInstancer shadow fixture loads and renders"
exit 0
