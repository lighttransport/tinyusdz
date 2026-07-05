#!/usr/bin/env bash
#
# Run test: representative RenderMode AOVs must render non-blank frames on the
# Vulkan ray-query path. This requires actual RT support: if --rt falls back to
# raster on the first mode, the test SKIPs (exit 77).
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDVIEW="${TUSDVIEW:-$REPO_ROOT/build/tusdview}"
ASSET="${ASSET:-$REPO_ROOT/models/suzanne-pbr.usda}"
TUSDVIEW_RENDER_TIMEOUT="${TUSDVIEW_RENDER_TIMEOUT:-30s}"

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

COLOR_ASSET="$TMP/displaycolor_vertex.usda"
cat > "$COLOR_ASSET" <<'USD'
#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Mesh "Quad"
    {
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        color3f[] primvars:displayColor = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0)] (
            interpolation = "vertex"
        )
        uniform token subdivisionScheme = "none"
    }
}
USD

FEATURE_ASSET="$TMP/aov_features.usda"
cat > "$FEATURE_ASSET" <<'USD'
#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Mesh "Left" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </World/Materials/MatLow>
        point3f[] points = [(-1.2, -1, 0), (-0.1, -1, 0), (-0.1, 1, 0), (-1.2, 1, 0)]
        int[] faceVertexCounts = [3, 3]
        int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
        texCoord2f[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 0), (1, 1), (0, 1)] (
            interpolation = "faceVarying"
        )
        texCoord2f[] primvars:st1 = [(0.1, 0.2), (0.8, 0.2), (0.8, 0.9), (0.1, 0.2), (0.8, 0.9), (0.1, 0.9)] (
            interpolation = "faceVarying"
        )
        uniform token subdivisionScheme = "none"
    }

    def Mesh "Right" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </World/Materials/MatHigh>
        point3f[] points = [(0.1, -1, 0), (1.2, -1, 0), (1.2, 1, 0), (0.1, 1, 0)]
        int[] faceVertexCounts = [3, 3]
        int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
        texCoord2f[] primvars:st = [(1, 0), (1, 3), (4, 3), (1, 0), (4, 3), (4, 0)] (
            interpolation = "faceVarying"
        )
        texCoord2f[] primvars:st1 = [(0.9, 0.1), (0.2, 0.1), (0.2, 0.8), (0.9, 0.1), (0.2, 0.8), (0.9, 0.8)] (
            interpolation = "faceVarying"
        )
        uniform token subdivisionScheme = "none"
    }

    def Scope "Materials"
    {
        def Material "MatLow"
        {
            token outputs:surface.connect = </World/Materials/MatLow/Preview.outputs:surface>
            def Shader "Preview"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0.8, 0.1, 0.1)
                color3f inputs:emissiveColor = (0.0, 0.0, 0.0)
                float inputs:metallic = 0.0
                float inputs:opacity = 0.35
                float inputs:roughness = 0.15
                token outputs:surface
            }
        }

        def Material "MatHigh"
        {
            token outputs:surface.connect = </World/Materials/MatHigh/Preview.outputs:surface>
            def Shader "Preview"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0.1, 0.3, 0.9)
                color3f inputs:emissiveColor = (0.2, 0.8, 0.1)
                float inputs:metallic = 1.0
                float inputs:opacity = 1.0
                float inputs:roughness = 0.85
                token outputs:surface
            }
        }
    }
}
USD

MASK_ASSET="$TMP/aov_mask_opacity.usda"
cat > "$MASK_ASSET" <<'USD'
#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Mesh "Low" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </World/Materials/CutLow>
        point3f[] points = [(-1.2, -1, 0), (-0.1, -1, 0), (-0.1, 1, 0), (-1.2, 1, 0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        uniform token subdivisionScheme = "none"
    }
    def Mesh "High" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </World/Materials/CutHigh>
        point3f[] points = [(0.1, -1, 0), (1.2, -1, 0), (1.2, 1, 0), (0.1, 1, 0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        uniform token subdivisionScheme = "none"
    }
    def Scope "Materials"
    {
        def Material "CutLow"
        {
            token outputs:surface.connect = </World/Materials/CutLow/Preview.outputs:surface>
            def Shader "Preview"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0.8, 0.1, 0.1)
                float inputs:opacity = 0.25
                float inputs:opacityThreshold = 0.5
                token outputs:surface
            }
        }
        def Material "CutHigh"
        {
            token outputs:surface.connect = </World/Materials/CutHigh/Preview.outputs:surface>
            def Shader "Preview"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0.1, 0.3, 0.9)
                float inputs:opacity = 0.75
                float inputs:opacityThreshold = 0.5
                token outputs:surface
            }
        }
    }
}
USD

SKIN_ASSET="$TMP/aov_skin.usda"
cat > "$SKIN_ASSET" <<'USD'
#usda 1.0
(
    defaultPrim = "Root"
)
def SkelRoot "Root"
{
    def Skeleton "Skeleton"
    {
        uniform token[] joints = ["Root", "Root/Tip"]
        uniform matrix4d[] bindTransforms = [
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)),
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
        ]
        uniform matrix4d[] restTransforms = [
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)),
            ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
        ]
    }

    def Mesh "SkinnedQuad" (
        prepend apiSchemas = ["SkelBindingAPI"]
    )
    {
        rel skel:skeleton = </Root/Skeleton>
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
        int[] faceVertexCounts = [3, 3]
        int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
        int[] primvars:skel:jointIndices = [0, 1, 1, 0, 1, 0, 0, 1] (
            interpolation = "vertex"
            elementSize = 2
        )
        float[] primvars:skel:jointWeights = [1, 0, 1, 0, 1, 0, 1, 0] (
            interpolation = "vertex"
            elementSize = 2
        )
        uniform token subdivisionScheme = "none"
    }
}
USD

BLEND_ASSET="$TMP/aov_blend.usda"
cat > "$BLEND_ASSET" <<'USD'
#usda 1.0
(
    defaultPrim = "Root"
)
def SkelRoot "Root"
{
    def Mesh "BlendQuad" (
        prepend apiSchemas = ["SkelBindingAPI"]
    )
    {
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
        int[] faceVertexCounts = [3, 3]
        int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]
        uniform token[] skel:blendShapes = ["Lift"]
        rel skel:blendShapeTargets = </Root/BlendQuad/Lift>
        uniform token subdivisionScheme = "none"

        def BlendShape "Lift"
        {
            uniform vector3f[] offsets = [(0, 0, 0.75), (0, 0, 0.1), (0, 0, 0.5), (0, 0, 0.25)]
            uniform int[] pointIndices = [0, 1, 2, 3]
        }
    }
}
USD

run_tusdview() {
  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5s "$TUSDVIEW_RENDER_TIMEOUT" "$TUSDVIEW" "$@"
  else
    "$TUSDVIEW" "$@"
  fi
}

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
distinct = {px[p:p+3] for p in range(0, min(len(px), w*h*3), 3)}
sys.exit(0 if len(distinct) > 1 else 1)
PY
    return $?
  fi
  [ "$(wc -c < "$img")" -gt 64 ]
}

colorful() {
  local img="$1"
  python3 - "$img" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    sys.exit(2)
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
samples = [
    px[p:p+3] for p in range(0, min(len(px), w*h*3), 3)
    if len(px[p:p+3]) == 3 and max(px[p:p+3]) > 20
]
if not samples:
    sys.exit(1)
rs = [s[0] for s in samples]
gs = [s[1] for s in samples]
bs = [s[2] for s in samples]
sys.exit(0 if max(rs)-min(rs) > 32 and max(gs)-min(gs) > 32 and max(bs)-min(bs) > 32 else 1)
PY
}

binary_opacity() {
  local img="$1"
  python3 - "$img" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    sys.exit(2)
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
vals = [px[p] for p in range(0, min(len(px), w*h*3), 3)
        if len(px[p:p+3]) == 3]
dark = sum(1 for v in vals if v <= 16)
bright = sum(1 for v in vals if v >= 240)
sys.exit(0 if dark > 0 and bright > 0 else 1)
PY
}

VK_DEVICE_ARGS=()
if [ -n "${TUSDVIEW_VK_DEVICE:-}" ]; then
  VK_DEVICE_ARGS=(--vk-device "$TUSDVIEW_VK_DEVICE")
fi

MODES="shaded normals geom-normal depth uv material-id facing position barycentric ao soft-shadow albedo texel-density source-face-id"

first=1
fail=0
for mode in $MODES; do
  out="$TMP/vkrt_${mode}.ppm"
  log="$(run_tusdview --headless --backend vk "${VK_DEVICE_ARGS[@]}" --rt \
        --mode "$mode" --frames 4 --screenshot "$out" "$ASSET" 2>&1)"
  if ! echo "$log" | grep -q "render stats"; then
    if [ "$first" -eq 1 ]; then
      echo "SKIP: Vulkan backend unavailable in this environment"
      exit $SKIP
    fi
    echo "FAIL: mode '$mode' did not render (no render stats)"
    echo "$log"
    fail=1
    first=0
    continue
  fi
  if echo "$log" | grep -q "ray tracing is unavailable"; then
    if [ "$first" -eq 1 ]; then
      echo "SKIP: Vulkan ray tracing unavailable in this environment"
      exit $SKIP
    fi
    echo "FAIL: mode '$mode' fell back from Vulkan RT"
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

out="$TMP/vkrt_displaycolor_albedo.ppm"
log="$(run_tusdview --headless --backend vk "${VK_DEVICE_ARGS[@]}" --rt \
      --mode albedo --frames 4 --screenshot "$out" "$COLOR_ASSET" 2>&1)"
if ! echo "$log" | grep -q "render stats"; then
  echo "FAIL: displayColor albedo pass did not render (no render stats)"
  echo "$log"
  fail=1
elif echo "$log" | grep -q "ray tracing is unavailable"; then
  echo "FAIL: displayColor albedo pass fell back from Vulkan RT"
  echo "$log"
  fail=1
elif [ ! -s "$out" ] || ! colorful "$out"; then
  echo "FAIL: displayColor vertex interpolation did not produce varied albedo"
  fail=1
else
  echo "  ok: displayColor vertex interpolation in --mode albedo"
fi

FEATURE_MODES="roughness metallic emissive opacity tangent uv-checker udim uv1 curvature texel-density source-face-id"
for mode in $FEATURE_MODES; do
  out="$TMP/vkrt_feature_${mode}.ppm"
  log="$(run_tusdview --headless --backend vk "${VK_DEVICE_ARGS[@]}" --rt \
        --mode "$mode" --frames 4 --screenshot "$out" "$FEATURE_ASSET" 2>&1)"
  if ! echo "$log" | grep -q "render stats"; then
    echo "FAIL: feature mode '$mode' did not render (no render stats)"
    echo "$log"
    fail=1
    continue
  fi
  if echo "$log" | grep -q "ray tracing is unavailable"; then
    echo "FAIL: feature mode '$mode' fell back from Vulkan RT"
    echo "$log"
    fail=1
    continue
  fi
  if [ ! -s "$out" ] || ! nonblank "$out"; then
    echo "FAIL: feature mode '$mode' produced a blank/missing image"
    fail=1
  else
    echo "  ok: feature --mode $mode"
  fi
done

out="$TMP/vkrt_mask_opacity.ppm"
log="$(run_tusdview --headless --backend vk "${VK_DEVICE_ARGS[@]}" --rt \
      --mode opacity --frames 4 --screenshot "$out" "$MASK_ASSET" 2>&1)"
if ! echo "$log" | grep -q "render stats"; then
  echo "FAIL: masked opacity AOV did not render (no render stats)"
  echo "$log"
  fail=1
elif echo "$log" | grep -q "ray tracing is unavailable"; then
  echo "FAIL: masked opacity AOV fell back from Vulkan RT"
  echo "$log"
  fail=1
elif [ ! -s "$out" ] || ! binary_opacity "$out"; then
  echo "FAIL: masked opacity AOV was not binary"
  fail=1
else
  echo "  ok: masked opacity threshold in --mode opacity"
fi

declare -A SPECIAL_ASSETS
SPECIAL_ASSETS["skin-weights"]="$SKIN_ASSET"
SPECIAL_ASSETS["blend-influence"]="$BLEND_ASSET"
for mode in skin-weights blend-influence; do
  out="$TMP/vkrt_feature_${mode}.ppm"
  log="$(run_tusdview --headless --backend vk "${VK_DEVICE_ARGS[@]}" --rt \
        --mode "$mode" --frames 4 --screenshot "$out" "${SPECIAL_ASSETS[$mode]}" 2>&1)"
  if ! echo "$log" | grep -q "render stats"; then
    echo "FAIL: deformation AOV mode '$mode' did not render (no render stats)"
    echo "$log"
    fail=1
    continue
  fi
  if echo "$log" | grep -q "ray tracing is unavailable"; then
    echo "FAIL: deformation AOV mode '$mode' fell back from Vulkan RT"
    echo "$log"
    fail=1
    continue
  fi
  if [ ! -s "$out" ] || ! nonblank "$out"; then
    echo "FAIL: deformation AOV mode '$mode' produced a blank/missing image"
    fail=1
  else
    echo "  ok: deformation --mode $mode"
  fi
done

[ "$fail" -ne 0 ] && exit 1
echo "PASS: all Vulkan RT AOV modes render non-blank on this GPU"
exit 0
