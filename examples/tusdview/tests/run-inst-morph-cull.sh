#!/usr/bin/env bash
#
# Regression test: a GPU-morphed instanced PointInstancer prototype must NOT be
# wrongly frustum-culled. The morph instance's REST-pose box is off-screen but
# its morphed geometry is in view; per-instance culling pads protoAabb by the
# morph displacement (DrawMeshCPU::morphExtent) so the instance survives.
#
# Asserts the render stats report all 4 instances visible. Without the morph
# protoAabb pad the morph instance culls and this drops to 3/4 -> test fails.
#
# Needs a GL (or VK) context: runs under xvfb-run when no $DISPLAY. Software GL
# (llvmpipe) is fine -- culling is CPU-side, independent of rasterization. If no
# renderer can be created the test SKIPs (exit 77, ctest SKIP_RETURN_CODE) rather
# than failing, so headless CI without a GPU/display does not report a failure.
#
# Exit codes: 0 = pass, 1 = fail (instance wrongly culled), 77 = skip.
#
# Usage (from the repo root, after building build/tusdview):
#   examples/tusdview/tests/run-inst-morph-cull.sh
# Overrides: TUSDVIEW=<binary>  BACKEND=gl|vk
# Also run via ctest: `ctest -R tusdview-inst-morph-cull`.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TUSDVIEW="${TUSDVIEW:-$SCRIPT_DIR/../../../build/tusdview}"
BACKEND="${BACKEND:-gl}"
ASSET="$SCRIPT_DIR/inst_morph_cull.usda"

if [ ! -x "$TUSDVIEW" ]; then
  echo "SKIP: tusdview binary not found at $TUSDVIEW (set TUSDVIEW=...)"
  exit $SKIP
fi

OUT="$(mktemp -d)/inst_morph_cull.png"
RUN=()
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a)
fi

# --frames loads synchronously (deterministic); the morph weight is 1.0 (static),
# so no timeline is needed -- this exercises the culling path at full morph.
LOG="$("${RUN[@]}" "$TUSDVIEW" --backend "$BACKEND" --next --frames 4 \
       --screenshot "$OUT" "$ASSET" 2>&1)"
echo "$LOG"

if ! echo "$LOG" | grep -q "render stats"; then
  echo "SKIP: no renderer (backend '$BACKEND' unavailable in this environment)"
  exit $SKIP
fi

if echo "$LOG" | grep -q "instances 4/4 visible"; then
  echo "PASS: all 4 instances visible (morphed prototype not culled)"
  exit 0
fi

echo "FAIL: expected 'instances 4/4 visible' (the morphed instance was culled --"
echo "      protoAabb is likely not padded by the GPU morph displacement)."
exit 1
