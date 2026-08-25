#!/usr/bin/env bash
# Populate the NVIDIA OpenGL driver shader cache once before parallel CTest
# shards launch tusdview. The full material fragment program can take about a
# minute to compile on a cold driver cache, while cached launches take under a
# second. Without an ordered warm-up, short capability guards report false
# skips and parallel processes redundantly compile the same program.
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build_ninja/tusdview}"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found ($BIN)"; exit "$SKIP"; }

OUT="$(mktemp -d /tmp/tusdview-gl-warmup.XXXXXX)"
trap 'rm -rf "$OUT"' EXIT

RUN=()
if [ -n "${DISPLAY:-}" ] && command -v xdpyinfo >/dev/null 2>&1 &&
   xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
  :
elif command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a -s "-screen 0 640x480x24")
else
  echo "SKIP: no usable X display and xvfb-run is unavailable"
  exit "$SKIP"
fi

LOG="$(timeout --kill-after=5s "${TUSDVIEW_GL_WARMUP_TIMEOUT:-150s}" \
  "${RUN[@]}" "$BIN" --backend gl --frames 1 --size 96x96 --no-grid \
  --screenshot "$OUT/warmup.ppm" \
  "$ROOT/tests/usda/tusdview-raster-multilight-links.usda" 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "$LOG"
  echo "FAIL: OpenGL shader-cache warm-up failed (exit $rc)"
  exit 1
fi
if ! grep -q 'renderer: OpenGL' <<<"$LOG" ||
   ! grep -q 'render stats' <<<"$LOG" || [ ! -s "$OUT/warmup.ppm" ]; then
  echo "$LOG"
  echo "FAIL: OpenGL shader-cache warm-up did not complete a render"
  exit 1
fi
if [ "${TUSDVIEW_NVIDIA_OFFLOAD:-0}" = "1" ] &&
   ! grep -Eqi 'renderer: OpenGL, GPU:.*(NVIDIA|GeForce|RTX)' <<<"$LOG"; then
  echo "$LOG"
  echo "FAIL: NVIDIA offload was required but warm-up used another OpenGL GPU"
  exit 1
fi

echo "PASS: OpenGL driver shader cache warmed"
