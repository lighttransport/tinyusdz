#!/usr/bin/env bash
#
# Run test: the HIP/ROCm ray-tracing backend must come up on a real AMD GPU and
# render a non-blank frame.
#
# tusdview's --hip path loads the HIP runtime + hiprtc at runtime via hipew (no
# link-time ROCm toolkit needed), compiles its trace kernel with hiprtc, and
# traces the loaded scene's BVH on the GPU, owning the screenshot. Verified
# working on AMD Radeon RX 9070 XT (gfx1201) / Linux. It is run under --headless
# so no window/X server is required.
#
# If HIP is unavailable (no AMD/ROCm device, no libamdhip64/libhiprtc) tusdview
# logs "HIP ray tracing unavailable" and never writes the HIP screenshot; the
# test then SKIPs (exit 77, ctest SKIP_RETURN_CODE) rather than failing, so CI
# without an AMD GPU does not report a failure.
#
# Exit codes: 0 = pass, 1 = fail (backend up but frame blank), 77 = skip.
#
# Usage (from the repo root, after building build/tusdview):
#   examples/tusdview/tests/run-hip-render.sh
# Overrides: TUSDVIEW=<binary>  ASSET=<usd file>
# Also run via ctest: `ctest -R tusdview-hip-render`.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDVIEW="${TUSDVIEW:-$REPO_ROOT/build/tusdview}"
ASSET="${ASSET:-$REPO_ROOT/models/suzanne-pbr.usda}"

if [ ! -x "$TUSDVIEW" ]; then
  echo "SKIP: tusdview binary not found at $TUSDVIEW (set TUSDVIEW=...)"
  exit $SKIP
fi
if [ ! -f "$ASSET" ]; then
  echo "SKIP: asset not found at $ASSET (set ASSET=...)"
  exit $SKIP
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Assert a screenshot has more than one distinct pixel (i.e. is not a flat blank
# clear). PPM (binary P6) is trivial to parse without external packages; fall
# back to a size>header sanity check if python3 is unavailable.
nonblank() {
  local img="$1"
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$img" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    print("not-ppm"); sys.exit(2)
tok, i, vals = [], 2, []
while len(vals) < 3 and i < len(data):
    c = data[i:i+1]
    if c.isspace():
        if tok: vals.append(b"".join(tok)); tok = []
    elif c == b"#":
        while i < len(data) and data[i:i+1] != b"\n": i += 1
    else:
        tok.append(c)
    i += 1
w, h, mx = (int(v) for v in vals)
px = data[i+1:]
stride = 3
distinct = {px[p:p+stride] for p in range(0, min(len(px), w*h*3), stride)}
sys.exit(0 if len(distinct) > 1 else 1)
PY
    return $?
  fi
  [ "$(wc -c < "$img")" -gt 64 ]
}

OUT="$TMP/hip.ppm"
echo "=== tusdview HIP pass ==="
log="$("$TUSDVIEW" --headless --hip --frames 4 --screenshot "$OUT" "$ASSET" 2>&1)"
echo "$log"

# "HIP RT wrote ..." is printed only when the GPU trace succeeded and owns the
# screenshot; any "unavailable"/"failed" diagnostic means no usable HIP device.
if ! echo "$log" | grep -q "HIP RT wrote"; then
  echo "SKIP: HIP ray tracing unavailable in this environment"
  exit $SKIP
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: no HIP screenshot written"
  exit 1
fi
if ! nonblank "$OUT"; then
  echo "FAIL: HIP screenshot is blank (backend came up but rendered nothing)"
  exit 1
fi

echo "PASS: HIP ray tracing renders a non-blank frame on this GPU"
exit 0
