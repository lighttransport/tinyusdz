#!/usr/bin/env bash
#
# Raster shadow-map regression (roadmap P6): the single deterministic 2048^2
# shadow map with 3x3 PCF must actually occlude direct light, identically on the
# GL and Vulkan raster backends.
#
# The probe: a bright floor with a blocker quad floating above it, lit by one
# shadow-enabled DistantLight pointing straight down. The blocker's silhouette
# must appear on the floor as a contiguous darker band.
#
# Two things this pins that a naive "is the image darker" check would miss:
#   * The dark band must be BOUNDED -- lit floor on both sides of it. A shadow
#     camera fitted wrongly (or a light-space transform that collapses) darkens
#     the whole floor, which is not a shadow.
#   * Row scanning is expressed in fractions of the actual image height, and the
#     shadow is searched for across a band of rows rather than one hard-coded
#     row. The two backends do not necessarily honor --size identically, so a
#     single absolute row can land off the shadow and silently pass/fail.
#
# The floor is intentionally untextured mid-gray so the only thing that can
# darken it is direct-light occlusion.
#
# Exit codes: 0 = pass, 1 = fail, 77 = skip.
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

BIN="${1:-}"
if [ -z "$BIN" ]; then
  if [ -n "${TUSDVIEW:-}" ]; then BIN="$TUSDVIEW"
  elif [ -x "$REPO_ROOT/build/tusdview" ]; then BIN="$REPO_ROOT/build/tusdview"
  else BIN="$REPO_ROOT/build_ninja/tusdview"; fi
fi
[ -x "$BIN" ] || { echo "SKIP: tusdview not found ($BIN)"; exit "$SKIP"; }
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 missing"; exit $SKIP; }

OUT="$(mktemp -d)"
mkdir -p "$OUT/config"
trap 'rm -rf "$OUT"' EXIT
XVFB=""
command -v xvfb-run >/dev/null 2>&1 && XVFB="xvfb-run -a"

cat > "$OUT/shadow.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Floor" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-6,0,-6), (6,0,-6), (6,0,6), (-6,0,6)]
    normal3f[] normals = [(0,1,0), (0,1,0), (0,1,0), (0,1,0)] (interpolation = "vertex")
    color3f[] primvars:displayColor = [(0.9, 0.9, 0.9)]
    uniform bool doubleSided = true
    uniform token subdivisionScheme = "none"
  }
  def Mesh "Blocker" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-1.5,2.5,-1.5), (1.5,2.5,-1.5), (1.5,2.5,1.5), (-1.5,2.5,1.5)]
    normal3f[] normals = [(0,1,0), (0,1,0), (0,1,0), (0,1,0)] (interpolation = "vertex")
    texCoord2f[] primvars:st = [(0,0), (1,0), (1,1), (0,1)] (interpolation = "vertex")
    color3f[] primvars:displayColor = [(0.2, 0.4, 0.9)]
    uniform bool doubleSided = true
    uniform token subdivisionScheme = "none"
    rel material:binding = </World/Cutout>
  }
  def Material "Cutout" {
    token outputs:surface.connect = </World/Cutout/Preview.outputs:surface>
    def Shader "Preview" {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0.2, 0.4, 0.9)
      float inputs:opacity = 1
      float inputs:opacityThreshold = 0.5
      token outputs:surface
    }
    def Shader "OpacityUv" {
      uniform token info:id = "UsdUVTexture"
      asset inputs:file = @opacity.<UDIM>.ppm@
      token inputs:sourceColorSpace = "raw"
      float2 inputs:st.connect = </World/Cutout/St.outputs:result>
      float outputs:r
    }
    def Shader "St" {
      uniform token info:id = "UsdPrimvarReader_float2"
      token inputs:varname = "st"
      float2 outputs:result
    }
  }
  def DistantLight "Sun" {
    float inputs:intensity = 3
    bool inputs:shadow:enable = 1
    float3 xformOp:rotateXYZ = (-90, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
  }
  def Camera "Cam" {
    double3 xformOp:translate = (0, 9, 12)
    float xformOp:rotateX = -35
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateX"]
  }
}
USDA

# One-sided finite area-light variant. It uses the same blocker/floor probe but
# places a RectLight above the scene; the single perspective shadow map should
# retain the same bounded-band invariant as the distant-light baseline.
python3 - "$OUT/shadow.usda" "$OUT/shadow-rect.usda" <<'PY'
import pathlib, sys
text = pathlib.Path(sys.argv[1]).read_text()
text = text.replace('def DistantLight "Sun"', 'def RectLight "Sun"')
text = text.replace('float inputs:intensity = 3',
                    'float inputs:intensity = 30')
text = text.replace(
    '    float3 xformOp:rotateXYZ = (-90, 0, 0)\n'
    '    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]',
    '    double3 xformOp:translate = (0, 6, 0)\n'
    '    float3 xformOp:rotateXYZ = (-90, 0, 0)\n'
    '    uniform token[] xformOpOrder = '
    '["xformOp:translate", "xformOp:rotateXYZ"]')
pathlib.Path(sys.argv[2]).write_text(text)
PY

# Same scene, but the blocker is a fully rejected alpha-mask material. The
# shadow band must disappear; this catches depth-only shadow shaders that ignore
# material cutouts while the visible pass correctly discards them.
sed 's/float inputs:opacity = 1/float inputs:opacity = 0/' \
  "$OUT/shadow.usda" > "$OUT/shadow-cutout.usda"
sed 's/float inputs:opacity = 1/float inputs:opacity.connect = <\/World\/Cutout\/OpacityUv.outputs:r>/' \
  "$OUT/shadow.usda" > "$OUT/shadow-udim.usda"
python3 - "$OUT" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
for tile, value in ((1001, 0), (1002, 255)):
    # Binary PGM is accepted by stb_image and keeps the fixture tiny. Tile 1001
    # is fully rejected; 1002 proves sparse tile discovery does not collapse to
    # a single representative image.
    (root / f"opacity.{tile}.ppm").write_bytes(
        b"P5\n4 4\n255\n" + bytes([value]) * 16)
PY

cat > "$OUT/shadow-instanced.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Floor" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-6,0,-6), (6,0,-6), (6,0,6), (-6,0,6)]
    normal3f[] normals = [(0,1,0), (0,1,0), (0,1,0), (0,1,0)] (interpolation = "vertex")
    color3f[] primvars:displayColor = [(0.9, 0.9, 0.9)]
    uniform bool doubleSided = true
    uniform token subdivisionScheme = "none"
  }
  def Scope "Prototypes" {
    def Mesh "Blocker" {
      int[] faceVertexCounts = [4]
      int[] faceVertexIndices = [0, 1, 2, 3]
      point3f[] points = [(-1.5,0,-1.5), (1.5,0,-1.5), (1.5,0,1.5), (-1.5,0,1.5)]
      normal3f[] normals = [(0,1,0), (0,1,0), (0,1,0), (0,1,0)] (interpolation = "vertex")
      color3f[] primvars:displayColor = [(0.2, 0.4, 0.9)]
      uniform bool doubleSided = true
      uniform token subdivisionScheme = "none"
    }
  }
  def PointInstancer "Blocks" {
    rel prototypes = [</World/Prototypes/Blocker>]
    int[] protoIndices = [0]
    point3f[] positions = [(0, 2.5, 0)]
  }
  def DistantLight "Sun" {
    float inputs:intensity = 3
    bool inputs:shadow:enable = 1
    float3 xformOp:rotateXYZ = (-90, 0, 0)
    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
  }
  def Camera "Cam" {
    double3 xformOp:translate = (0, 9, 12)
    float xformOp:rotateX = -35
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateX"]
  }
}
USDA

# Look for a bounded dark band on the gray floor: lit / dark / lit across a row,
# in any of the rows spanning the floor. Prints "<rows_with_band> <max_contrast>".
scan_band() {
python3 - "$1" "${2:-0.6}" <<'PY'
import re, sys
d = open(sys.argv[1], "rb").read()
m = re.match(rb'P6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+(\d+)\s', d)
if not m: sys.exit(2)
w, h = int(m.group(1)), int(m.group(2)); px = d[m.end():]
if len(px) < w*h*3: sys.exit(2)

def lum(x, y):
    o = (y*w + x)*3
    return (px[o]*299 + px[o+1]*587 + px[o+2]*114) // 1000

ratio = float(sys.argv[2]) if len(sys.argv) > 2 else 0.6
rows, best = 0, 0
for fy in [i/100 for i in range(50, 96)]:
    y = int(h*fy)
    vals = [lum(x, y) for x in range(w)]
    hi = max(vals)
    if hi < 60:            # row isn't on the lit floor at all
        continue
    dark = [i for i, v in enumerate(vals) if v < hi*ratio]
    if not dark:
        continue
    lo, hiIdx = dark[0], dark[-1]
    # the band must be interior (lit floor on BOTH sides) and contiguous-ish
    if lo == 0 or hiIdx == w-1:
        continue
    if len(dark) < (hiIdx - lo + 1)*0.8:
        continue
    if not (w*0.05 < (hiIdx - lo) < w*0.9):
        continue
    rows += 1
    best = max(best, hi - min(vals[lo:hiIdx+1]))
print(rows, best)
PY
}

fail=0
ran=0
vk_ran=0
for spec in "gl:--backend gl:" "vk:--backend vk:--headless"; do
  tag="${spec%%:*}"; rest="${spec#*:}"; args="${rest%%:*}"; hl="${rest#*:}"
  img="$OUT/$tag.ppm"; log="$OUT/$tag.log"
  # shellcheck disable=SC2086
  $XVFB env XDG_CONFIG_HOME="$OUT/config" \
      "$BIN" $hl $args --frames 2 --size 640x480 --no-grid --camera Cam \
      --screenshot "$img" "$OUT/shadow.usda" >"$log" 2>&1
  if ! grep -q 'render stats' "$log"; then
    echo "SKIP: $tag backend unavailable"; continue
  fi
  [ -s "$img" ] || { echo "FAIL: $tag produced no image"; fail=1; continue; }
  read -r rows contrast < <(scan_band "$img") || {
    echo "FAIL: $tag PPM parse"; fail=1; continue; }
  ran=$((ran+1))
  [ "$tag" = "vk" ] && vk_ran=1
  echo "$tag: shadow-band rows=$rows contrast=$contrast"
  if [ "$rows" -lt 3 ]; then
    echo "FAIL: $tag has no bounded shadow band on the floor (shadow map not applied)"
    fail=1
  elif [ "$contrast" -lt 30 ]; then
    echo "FAIL: $tag shadow band too faint (contrast=$contrast)"
    fail=1
  fi
done

# Exercise the newly supported one-sided finite-light projection on Vulkan.
rect_img="$OUT/vk-rect.ppm"
rect_log="$OUT/vk-rect.log"
$XVFB env XDG_CONFIG_HOME="$OUT/config" \
    "$BIN" --headless --backend vk --frames 2 --size 640x480 --no-grid \
    --camera Cam --screenshot "$rect_img" "$OUT/shadow-rect.usda" \
    >"$rect_log" 2>&1
if grep -q 'render stats' "$rect_log"; then
  read -r rows contrast < <(scan_band "$rect_img" 0.75) || {
    echo "FAIL: vk RectLight shadow PPM parse"; exit 1; }
  echo "vk: RectLight shadow-band rows=$rows contrast=$contrast"
  if [ "$rows" -lt 3 ] || [ "$contrast" -lt 30 ]; then
    echo "FAIL: Vulkan RectLight did not produce a bounded shadow"
    cp "$rect_img" /tmp/tusdview-vk-rect-shadow-failed.ppm
    cp "$rect_log" /tmp/tusdview-vk-rect-shadow-failed.log
    fail=1
  fi
elif [ "$vk_ran" -eq 1 ]; then
  echo "FAIL: Vulkan baseline ran but RectLight shadow fixture did not render"
  sed -n '1,80p' "$rect_log"
  fail=1
fi

for spec in "gl:--backend gl:" "vk:--backend vk:--headless"; do
  tag="${spec%%:*}"; rest="${spec#*:}"; args="${rest%%:*}"; hl="${rest#*:}"
  img="$OUT/$tag-udim.ppm"; log="$OUT/$tag-udim.log"
  # shellcheck disable=SC2086
  $XVFB env XDG_CONFIG_HOME="$OUT/config" \
      "$BIN" $hl $args --frames 2 --size 640x480 --no-grid --camera Cam \
      --screenshot "$img" "$OUT/shadow-udim.usda" >"$log" 2>&1
  grep -q 'render stats' "$log" || continue
  read -r rows contrast < <(scan_band "$img") || {
    echo "FAIL: $tag UDIM-cutout PPM parse"; fail=1; continue; }
  echo "$tag: sparse-UDIM cutout shadow rows=$rows contrast=$contrast"
  if [ "$rows" -ne 0 ]; then
    echo "FAIL: $tag sparse-UDIM opacity tile still casts a solid shadow"
    fail=1
  fi
done

# Vulkan's instanced pipeline has a separate shadow depth path. Require the same
# bounded band from a PointInstancer prototype so falling back to ordinary mesh
# caster coverage cannot satisfy this assertion.
inst_img="$OUT/vk-instanced.ppm"
inst_log="$OUT/vk-instanced.log"
$XVFB env XDG_CONFIG_HOME="$OUT/config" \
    "$BIN" --headless --backend vk --frames 2 --size 640x480 --no-grid \
    --camera Cam --screenshot "$inst_img" "$OUT/shadow-instanced.usda" \
    >"$inst_log" 2>&1
if grep -q 'render stats' "$inst_log"; then
  read -r rows contrast < <(scan_band "$inst_img") || {
    echo "FAIL: vk instanced-shadow PPM parse"; exit 1; }
  echo "vk: instanced shadow-band rows=$rows contrast=$contrast"
  if [ "$rows" -lt 3 ] || [ "$contrast" -lt 30 ]; then
    echo "FAIL: Vulkan PointInstancer prototype did not cast a bounded shadow"
    fail=1
  fi
elif [ "$vk_ran" -eq 1 ]; then
  echo "FAIL: Vulkan baseline ran but instanced shadow fixture did not render"
  sed -n '1,80p' "$inst_log"
  fail=1
fi

for spec in "gl:--backend gl:" "vk:--backend vk:--headless"; do
  tag="${spec%%:*}"; rest="${spec#*:}"; args="${rest%%:*}"; hl="${rest#*:}"
  img="$OUT/$tag-cutout.ppm"; log="$OUT/$tag-cutout.log"
  # shellcheck disable=SC2086
  $XVFB env XDG_CONFIG_HOME="$OUT/config" \
      "$BIN" $hl $args --frames 2 --size 640x480 --no-grid --camera Cam \
      --screenshot "$img" "$OUT/shadow-cutout.usda" >"$log" 2>&1
  grep -q 'render stats' "$log" || continue
  read -r rows contrast < <(scan_band "$img") || {
    echo "FAIL: $tag cutout PPM parse"; fail=1; continue; }
  echo "$tag: rejected-cutout shadow rows=$rows contrast=$contrast"
  if [ "$rows" -ne 0 ]; then
    echo "FAIL: $tag rejected alpha-mask still casts a solid shadow"
    fail=1
  fi
done

[ "$ran" -gt 0 ] || { echo "SKIP: no raster backend available"; exit "$SKIP"; }
[ "$fail" -eq 0 ] || exit 1
echo "PASS: raster shadow map and alpha-mask rejection on $ran backend(s)"
exit 0
