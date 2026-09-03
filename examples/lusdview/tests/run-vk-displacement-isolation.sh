#!/usr/bin/env bash
# Regression: Vulkan raster displacement is enabled per draw. A material with no
# displacement must never sample another material's displacement descriptor.
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${LUSDVIEW:-$ROOT/build_ninja/lusdview}"
[ -x "$BIN" ] || { echo "SKIP: lusdview not found"; exit "$SKIP"; }
OUT="${LUSDVIEW_TEST_OUT:-$(mktemp -d)}"
[ -n "${LUSDVIEW_TEST_OUT:-}" ] || trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT"
printf '%s\n' '{"window_size":{"width":320,"height":192}}' > "$OUT/config.json"

python3 - "$OUT" <<'PY'
import os, sys
for name, values in (("flat", (0, 0)), ("raised", (0, 255))):
    for tile, value in zip((1001, 1002), values):
        with open(os.path.join(sys.argv[1], f"{name}.{tile}.ppm"), "wb") as f:
            f.write(b"P6\n4 4\n255\n" + bytes((value, value, value)) * 16)
PY

write_scene() {
  local path="$1" texture="$2"
  cat > "$path" <<USDA
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  # Kept on the left half of the image. Its pixels must be identical between
  # the flat and raised renders even though another material is displaced.
  def Mesh "Ordinary" {
    uniform bool doubleSided = 1
    uniform token subdivisionScheme = "none"
    point3f[] points = [(-3,-1,0),(-1,-1,0),(-1,1,0),(-3,1,0)]
    normal3f[] normals = [(0,0,1),(0,0,1),(0,0,1),(0,0,1)] (interpolation = "vertex")
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    rel material:binding = </World/Plain>
  }
  def Mesh "Displaced" {
    uniform bool doubleSided = 1
    uniform token subdivisionScheme = "none"
    point3f[] points = [(1,-1,0),(2,-1,0),(3,-1,0),(1,1,0),(2,1,0),(3,1,0)]
    normal3f[] normals = [(0,0,1),(0,0,1),(0,0,1),(0,0,1),(0,0,1),(0,0,1)] (interpolation = "vertex")
    texCoord2f[] primvars:st = [(0,0),(1,0),(2,0),(0,1),(1,1),(2,1)] (interpolation = "vertex")
    int[] faceVertexCounts = [4,4]
    int[] faceVertexIndices = [0,1,4,3,1,2,5,4]
    rel material:binding = </World/Raised>
  }
  def Material "Raised" {
    token outputs:surface.connect = </World/Raised/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0.8,0.15,0.05)
      float inputs:roughness = 1
      float inputs:displacement.connect = </World/Raised/D.outputs:r>
      token outputs:surface
    }
    def Shader "ST" {
      uniform token info:id = "UsdPrimvarReader_float2"
      token inputs:varname = "st"
      float2 outputs:result
    }
    def Shader "D" {
      uniform token info:id = "UsdUVTexture"
      asset inputs:file = @./$texture.<UDIM>.ppm@
      token inputs:sourceColorSpace = "raw"
      float2 inputs:st.connect = </World/Raised/ST.outputs:result>
      float outputs:r
    }
  }
  def Material "Plain" {
    token outputs:surface.connect = </World/Plain/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0.1,0.7,0.2)
      float inputs:roughness = 1
      token outputs:surface
    }
  }
}
USDA
}
write_scene "$OUT/flat.usda" flat
write_scene "$OUT/raised.usda" raised

render() {
  "$BIN" --headless --backend vk --config "$OUT/config.json" --frames 4 \
    --no-grid --mode albedo --view-dir '0.6,0,-1' --screenshot "$OUT/$1.ppm" \
    "$OUT/$1.usda" >"$OUT/$1.log" 2>&1
}
render flat || { echo "SKIP: Vulkan raster unavailable"; exit "$SKIP"; }
render raised || { echo "FAIL: raised Vulkan raster render"; exit 1; }

python3 - "$OUT/flat.ppm" "$OUT/raised.ppm" <<'PY'
import re, sys
def ppm(path):
    data = open(path, "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not m: raise SystemExit("invalid PPM: " + path)
    return int(m.group(1)), int(m.group(2)), data[m.end():]
w0,h0,a = ppm(sys.argv[1]); w1,h1,b = ppm(sys.argv[2])
if (w0,h0) != (w1,h1): raise SystemExit("FAIL: image size mismatch")
def half_mad(lo, hi):
    total = count = 0
    for y in range(h0):
        beg = 3 * (y*w0 + lo); end = 3 * (y*w0 + hi)
        total += sum(abs(x-y) for x,y in zip(a[beg:end], b[beg:end]))
        count += end-beg
    return total / max(count, 1)
left = half_mad(0, w0//2)
right = half_mad(w0//2, w0)
print(f"ordinary-half MAD={left:.5f}, displaced-half MAD={right:.5f}")
if right < 0.05:
    raise SystemExit("FAIL: displacement did not affect the displaced draw")
if left > 0.02:
    raise SystemExit("FAIL: displacement leaked into the ordinary draw")
print("PASS: Vulkan displacement remains isolated to its material draw")
PY
