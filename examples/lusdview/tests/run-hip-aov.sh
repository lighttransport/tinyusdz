#!/usr/bin/env bash
#
# Run test: every representative RenderMode AOV must trace a non-blank frame on
# the HIP/ROCm backend. The --cuda and --hip screenshot tracers share one kernel
# (raytracer_kernel_src.txt), so this exercises the HIP-via-hiprtc compile + each AOV
# branch on a real AMD GPU (the plain lusdview-hip-render test covers only the
# default shaded view). Catches AOV branches that compile/run on NVIDIA/NVRTC but
# break under hiprtc, and any single mode that renders nothing.
#
# Modes cover the distinct AOV kinds: shaded, smooth + geometric normals, depth,
# uv, material-id, facing, world position, barycentric, and the stochastic
# ray-traced AOVs (ao, soft-shadow).
#
# HIP is runtime-loaded; if no AMD/ROCm device is available the first render
# prints no "HIP RT wrote" line and the whole test SKIPs (exit 77,
# SKIP_RETURN_CODE) so non-AMD CI stays green.
#
# Exit codes: 0 = pass, 1 = fail (a mode rendered blank / no file), 77 = skip.
#
# Usage:  LUSDVIEW=<binary> ASSET=<usd> run-hip-aov.sh
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

# Assert a screenshot has more than one distinct pixel (binary PPM / P6; no
# external packages). Falls back to a size check when python3 is absent.
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

MODES="shaded normals geom-normal depth uv material-id facing position barycentric ao soft-shadow"

# Probe the first mode to decide availability vs failure.
first=1
fail=0
for mode in $MODES; do
  out="$TMP/hip_${mode}.ppm"
  log="$("$LUSDVIEW" --headless --hip --mode "$mode" --frames 3 \
        --screenshot "$out" "$ASSET" 2>&1)"
  if ! echo "$log" | grep -q "HIP RT wrote"; then
    if [ "$first" -eq 1 ]; then
      echo "SKIP: HIP ray tracing unavailable in this environment"
      exit $SKIP
    fi
    echo "FAIL: mode '$mode' did not render (no 'HIP RT wrote')"
    echo "$log"
    fail=1
    first=0
    continue
  fi
  first=0
  if [ ! -s "$out" ] || ! nonblank "$out"; then
    echo "FAIL: mode '$mode' produced a blank/missing image"
    fail=1
  else
    echo "  ok: --mode $mode"
  fi
done

[ "$fail" -ne 0 ] && exit 1
echo "PASS: all HIP AOV modes render non-blank on this GPU"
exit 0
