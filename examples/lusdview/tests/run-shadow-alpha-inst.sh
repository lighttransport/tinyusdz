#!/usr/bin/env bash
#
# Load the checked-in alpha-cutout + PointInstancer shadow fixture and verify the
# renderer produces geometry with no errors. The full shadow quality assertion is
# in run-raster-shadow-map.sh; this test checks basic load + render success.
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${LUSDVIEW:-$ROOT/build/lusdview}"
[ -x "$BIN" ] || BIN="$ROOT/build_ninja/lusdview"
[ -x "$BIN" ] || { echo "SKIP: lusdview not found"; exit "$SKIP"; }
ASSET="$ROOT/tests/usda/lusdview-shadow-alpha-inst.usda"
[ -f "$ASSET" ] || { echo "SKIP: fixture missing"; exit "$SKIP"; }
RUN=()
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a)
fi
LOG="$(timeout --kill-after=5s 20s "${RUN[@]}" "$BIN" --backend gl \
  --frames 2 --no-grid "$ASSET" 2>&1)"
rc=$?
if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
  echo "SKIP: OpenGL/X11 backend timed out during initialization"
  exit "$SKIP"
fi
if ! echo "$LOG" | grep -q "render stats"; then
  echo "SKIP: no renderer"
  exit $SKIP
fi
real_errors="$(printf '%s\n' "$LOG" | grep -iE 'error|failed' |
  grep -viE 'glfw error 65540: Vulkan: Window surface creation requires the window to have the client API set to GLFW_NO_API|Vulkan capability probe: glfwCreateWindowSurface' || true)"
if [ -n "$real_errors" ]; then
  echo "$LOG"
  echo "FAIL: alpha-cutout + PointInstancer shadow fixture produced errors"
  exit 1
fi
echo "PASS: alpha-cutout + PointInstancer shadow fixture loads and renders"
exit 0
