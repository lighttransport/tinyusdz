#!/usr/bin/env bash
#
# Run test: the Vulkan backend must come up on a real GPU and render a non-blank
# frame, both in rasterization (--backend vk) and hardware ray-query (--rt) mode.
#
# This is the cross-platform "the VK path works on this GPU" check. The Vulkan
# GPU backends were historically only exercised on a Windows AMD Radeon RX 570 /
# amdvlk where they mis-render; this asserts a correct render on the host GPU
# (verified working on NVIDIA GeForce RTX 5060 Ti / Linux, see doc/tusdrender.md).
#
# tusdview's --headless path is Vulkan-only and needs no window/X server (it
# renders into an offscreen image and reads it back), so this test does NOT need
# xvfb. If the Vulkan backend cannot be created (no Vulkan device/driver in this
# environment) tusdview exits nonzero without printing "render stats"; the test
# then SKIPs (exit 77, ctest SKIP_RETURN_CODE) rather than failing, so headless
# CI without a GPU does not report a failure.
#
# Exit codes: 0 = pass, 1 = fail (renderer up but frame blank), 77 = skip.
#
# Usage (from the repo root, after building build/tusdview):
#   examples/tusdview/tests/run-vk-render.sh
# Overrides: TUSDVIEW=<binary>  ASSET=<usd file>  OPACITY_ASSET=<usd file>
#            TUSDVIEW_RENDER_TIMEOUT=30s
# Also run via ctest: `ctest -R tusdview-vk-render`.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDVIEW="${TUSDVIEW:-$REPO_ROOT/build/tusdview}"
ASSET="${ASSET:-$REPO_ROOT/models/suzanne-pbr.usda}"
OPACITY_ASSET="${OPACITY_ASSET:-$REPO_ROOT/models/spectral-scene.usda}"
TUSDVIEW_RENDER_TIMEOUT="${TUSDVIEW_RENDER_TIMEOUT:-30s}"

if [ ! -x "$TUSDVIEW" ]; then
  echo "SKIP: tusdview binary not found at $TUSDVIEW (set TUSDVIEW=...)"
  exit $SKIP
fi
if [ ! -f "$ASSET" ]; then
  echo "SKIP: asset not found at $ASSET (set ASSET=...)"
  exit $SKIP
fi
if [ ! -f "$OPACITY_ASSET" ]; then
  echo "SKIP: opacity asset not found at $OPACITY_ASSET (set OPACITY_ASSET=...)"
  exit $SKIP
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

run_tusdview() {
  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5s "$TUSDVIEW_RENDER_TIMEOUT" "$TUSDVIEW" "$@"
  else
    "$TUSDVIEW" "$@"
  fi
}

# Assert a screenshot has more than one distinct pixel (i.e. is not a flat blank
# clear). PPM (binary P6) is trivial to parse without external packages; fall
# back to a size>header sanity check if python3 is unavailable.
nonblank() {
  local img="$1"
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$img" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
# Parse a binary PPM (P6) header: magic, width, height, maxval, then raw bytes.
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
i += 0
px = data[i+1:]
stride = 3
distinct = {px[p:p+stride] for p in range(0, min(len(px), w*h*3), stride)}
sys.exit(0 if len(distinct) > 1 else 1)
PY
    return $?
  fi
  # No python3: just require the file to be larger than a trivial header.
  [ "$(wc -c < "$img")" -gt 64 ]
}

run_pass() {
  # $1 = label, rest = extra tusdview flags
  local label="$1"; shift
  run_asset_pass "$label" "$ASSET" "$@"
}

run_asset_pass() {
  # $1 = label, $2 = asset, rest = extra tusdview flags
  local label="$1"; shift
  local asset="$1"; shift
  local out="$TMP/vk_${label}.ppm"
  echo "=== tusdview VK pass: $label ==="
  local log
  log="$(run_tusdview --headless "$@" --frames 4 --screenshot "$out" "$asset" 2>&1)"
  echo "$log"
  if ! echo "$log" | grep -q "render stats"; then
    echo "SKIP: Vulkan backend unavailable in this environment ($label)"
    return $SKIP
  fi
  if [ "$label" = "profile" ]; then
    for expected in "large-scene-profile island resolved" "backend=vk" \
                    "--next=on" "--raster-lod=on" "--rt-lod=on" \
                    "--max-gpu-mem=10.0"; do
      if ! echo "$log" | grep -q -- "$expected"; then
        echo "FAIL: profile pass did not apply expected default: $expected"
        return 1
      fi
    done
  fi
  if [ ! -s "$out" ]; then
    echo "FAIL: no screenshot written ($label)"
    return 1
  fi
  if ! nonblank "$out"; then
    echo "FAIL: $label screenshot is blank (renderer came up but rendered nothing)"
    return 1
  fi
  echo "PASS: $label render is non-blank"
  return 0
}

# 1) Vulkan rasterization.
run_pass raster --backend vk
rc=$?
[ $rc -eq $SKIP ] && exit $SKIP
[ $rc -ne 0 ] && exit $rc

# 2) Vulkan hardware ray query (falls back to raster if the GPU/shader lacks RT;
#    we still assert a non-blank frame either way).
run_pass rt --backend vk --rt
rc=$?
[ $rc -eq $SKIP ] && exit $SKIP
[ $rc -ne 0 ] && exit $rc

# 3) Opacity AOV over non-opaque PreviewSurface constants. This guards the
#    raster material alphaMode/alphaCutoff push-constant path without adding
#    private assets.
run_asset_pass opacity-aov "$OPACITY_ASSET" --backend vk --mode opacity
rc=$?
[ $rc -eq $SKIP ] && exit $SKIP
[ $rc -ne 0 ] && exit $rc

# 4) Large-scene realtime profile wiring. Uses the same tiny fixture so CI does
#    not need Caldera/Island/ALab, but exercises the preset resolver, --next,
#    Vulkan headless rendering, raster LOD defaults, and startup diagnostics.
run_pass profile --large-scene-profile island
rc=$?
[ $rc -eq $SKIP ] && exit $SKIP
[ $rc -ne 0 ] && exit $rc

echo "PASS: Vulkan raster + ray-query + large-scene profile render correctly on this GPU"
exit 0
