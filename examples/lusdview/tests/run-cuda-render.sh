#!/usr/bin/env bash
#
# Run test: the CUDA ray-tracing backend must come up on a real NVIDIA GPU and
# render a non-blank frame.
#
# lusdview's --cuda path loads the CUDA driver API + NVRTC at runtime via cuew
# (no link-time CUDA toolkit needed) and traces the loaded scene's BVH on the
# GPU, owning the screenshot. Verified working on NVIDIA GeForce RTX 5060 Ti /
# Linux (see doc/lusdview.md). It is run under --headless so no window/X server
# is required.
#
# If CUDA is unavailable (no NVIDIA device/driver, no libcuda/libnvrtc) lusdview
# logs "CUDA ray tracing unavailable" and never writes the CUDA screenshot; the
# test then SKIPs (exit 77, ctest SKIP_RETURN_CODE) rather than failing, so CI
# without an NVIDIA GPU does not report a failure.
#
# Exit codes: 0 = pass, 1 = fail (backend up but frame blank), 77 = skip.
#
# Usage (from the repo root, after building build/lusdview):
#   examples/lusdview/tests/run-cuda-render.sh
# Overrides: LUSDVIEW=<binary>  ASSET=<usd file>
# Also run via ctest: `ctest -R lusdview-cuda-render`.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDVIEW="${LUSDVIEW:-$REPO_ROOT/build/lusdview}"
ASSET="${ASSET:-$REPO_ROOT/models/suzanne-pbr.usda}"

if [ ! -x "$LUSDVIEW" ]; then
  echo "SKIP: lusdview binary not found at $LUSDVIEW (set LUSDVIEW=...)"
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

OUT="$TMP/cuda.ppm"
echo "=== lusdview CUDA pass ==="
log="$("$LUSDVIEW" --headless --cuda --frames 4 --screenshot "$OUT" "$ASSET" 2>&1)"
echo "$log"

# "CUDA RT wrote ..." is printed only when the GPU trace succeeded and owns the
# screenshot; any "unavailable"/"failed" diagnostic means no usable CUDA device.
if ! echo "$log" | grep -q "CUDA RT wrote"; then
  echo "SKIP: CUDA ray tracing unavailable in this environment"
  exit $SKIP
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: no CUDA screenshot written"
  exit 1
fi
if ! nonblank "$OUT"; then
  echo "FAIL: CUDA screenshot is blank (backend came up but rendered nothing)"
  exit 1
fi

echo "PASS: CUDA ray tracing renders a non-blank frame on this GPU"
exit 0
