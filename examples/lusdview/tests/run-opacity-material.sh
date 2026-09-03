#!/usr/bin/env bash
# T12 regression: separate (including UDIM) opacity masks in GL/VK raster, and
# varying displayOpacity in GL/VK raster + Vulkan/CUDA/HIP ray tracing.
# Exit 77 when no backend can run; 1 on an observed regression.
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
if [ -n "${LUSDVIEW:-}" ]; then BIN="$LUSDVIEW"
elif [ -x "$REPO_ROOT/build_ninja/lusdview" ]; then BIN="$REPO_ROOT/build_ninja/lusdview"
else BIN="$REPO_ROOT/build/lusdview"; fi
[ -x "$BIN" ] || { echo "SKIP: lusdview not found"; exit "$SKIP"; }

OUT="${LUSDVIEW_TEST_OUT:-$(mktemp -d)}"
mkdir -p "$OUT"
if [ -z "${LUSDVIEW_TEST_OUT:-}" ]; then trap 'rm -rf "$OUT"' EXIT; fi
CONFIG="$OUT/config.json"
cat > "$CONFIG" <<'JSON'
{"window_size":{"width":256,"height":256}}
JSON
XVFB=""
DISPLAY_OK=0
if [ -n "${DISPLAY:-}" ]; then
  DISPLAY_OK=1
  if command -v xdpyinfo >/dev/null 2>&1 &&
     ! xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
    DISPLAY_OK=0
  fi
fi
if [ "$DISPLAY_OK" -eq 0 ] && command -v xvfb-run >/dev/null 2>&1; then
  if command -v timeout >/dev/null 2>&1; then
    timeout 10s xvfb-run -a xdpyinfo >/dev/null 2>&1 && DISPLAY_OK=1
  else
    xvfb-run -a xdpyinfo >/dev/null 2>&1 && DISPLAY_OK=1
  fi
  [ "$DISPLAY_OK" -ne 0 ] && XVFB="xvfb-run -a"
fi

# A broken graphics stack must not wedge the whole ctest run. Keep the timeout
# around each viewer process (rather than the script as a whole) so available
# backends still get exercised after one backend fails to start or shut down.
run_viewer() {
  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5s "${LUSDVIEW_RENDER_TIMEOUT:-60s}" "$@"
  else
    "$@"
  fi
}

# Tiny PPM masks avoid external image-library dependencies. Tile 1001 is fully
# transparent; 1002 fully opaque.
python3 - "$OUT" <<'PY'
import os, sys
for tile, value in ((1001, 0), (1002, 255)):
    p = os.path.join(sys.argv[1], f"mask.{tile}.ppm")
    with open(p, "wb") as f:
        f.write(b"P6\n4 4\n255\n" + bytes([value, value, value]) * 16)
PY

cat > "$OUT/opacity-udim.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Back" {
    uniform bool doubleSided = 1
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-1,-1,0), (1,-1,0), (1,1,0), (-1,1,0)]
    rel material:binding = </World/Red>
  }
  def Mesh "Mask" {
    uniform bool doubleSided = 1
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-1,-1,0.05), (1,-1,0.05), (1,1,0.05), (-1,1,0.05)]
    texCoord2f[] primvars:st = [(0,0), (2,0), (2,1), (0,1)] (interpolation = "vertex")
    rel material:binding = </World/GreenMask>
  }
  def Material "Red" {
    token outputs:surface.connect = </World/Red/P.outputs:surface>
    def Shader "P" { uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (1,0,0) float inputs:roughness = 1
      token outputs:surface }
  }
  def Material "GreenMask" {
    token outputs:surface.connect = </World/GreenMask/P.outputs:surface>
    def Shader "P" { uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0,1,0)
      float inputs:opacity.connect = </World/GreenMask/Mask.outputs:r>
      float inputs:opacityThreshold = 0.5
      float inputs:roughness = 1 token outputs:surface }
    def Shader "ST" { uniform token info:id = "UsdPrimvarReader_float2"
      token inputs:varname = "st" float2 outputs:result }
    def Shader "Mask" { uniform token info:id = "UsdUVTexture"
      asset inputs:file = @./mask.<UDIM>.ppm@
      float2 inputs:st.connect = </World/GreenMask/ST.outputs:result>
      float outputs:r }
  }
}
USDA

cat > "$OUT/display-opacity.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Back" {
    uniform bool doubleSided = 1 int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    point3f[] points = [(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)]
    rel material:binding = </World/Red>
  }
  def Mesh "Varying" {
    uniform bool doubleSided = 1 int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    point3f[] points = [(-1,-1,0.05),(1,-1,0.05),(1,1,0.05),(-1,1,0.05)]
    float[] primvars:displayOpacity = [0, 1, 1, 0] (interpolation = "vertex")
    rel material:binding = </World/Green>
  }
  def Material "Red" { token outputs:surface.connect = </World/Red/P.outputs:surface>
    def Shader "P" { uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (1,0,0) token outputs:surface } }
  def Material "Green" { token outputs:surface.connect = </World/Green/P.outputs:surface>
    def Shader "P" { uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0,1,0) float inputs:opacity = 0.8
      token outputs:surface } }
}
USDA

cat > "$OUT/instance-opacity.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def PointInstancer "Instances" {
    rel prototypes = [</World/Instances/Protos/Quad>]
    point3f[] positions = [(-0.7,0,0), (0.7,0,0)]
    int[] protoIndices = [0, 0]
    quatf[] orientations = [(1,0,0,0), (1,0,0,0)]
    float3[] scales = [(1,1,1), (1,1,1)]
    float[] primvars:displayOpacity = [0.25, 1.0] (interpolation = "vertex")
    def Scope "Protos" {
      def Mesh "Quad" {
        uniform bool doubleSided = 1
        point3f[] points = [(-0.5,-0.5,0), (0.5,-0.5,0),
                            (0.5,0.5,0), (-0.5,0.5,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0,1,2,3]
        uniform token subdivisionScheme = "none"
      }
    }
  }
}
USDA

cat > "$OUT/instance-vertex-opacity.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Back" {
    uniform bool doubleSided = 1
    point3f[] points = [(-1,-1,0), (1,-1,0), (1,1,0), (-1,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    rel material:binding = </World/Red>
  }
  def PointInstancer "Instances" {
    rel prototypes = [</World/Instances/Protos/Quad>]
    point3f[] positions = [(0,0,0.05)]
    int[] protoIndices = [0]
    quatf[] orientations = [(1,0,0,0)]
    float3[] scales = [(1,1,1)]
    def Scope "Protos" {
      def Mesh "Quad" {
        uniform bool doubleSided = 1
        point3f[] points = [(-1,-1,0), (1,-1,0), (1,1,0), (-1,1,0)]
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0,1,2,3]
        color3f[] primvars:displayColor = [(0,1,0), (0,1,0),
                                           (0,1,0), (0,1,0)] (
          interpolation = "vertex"
        )
        float[] primvars:displayOpacity = [0,1,1,0] (interpolation = "vertex")
        uniform token subdivisionScheme = "none"
      }
    }
  }
  def Material "Red" { token outputs:surface.connect = </World/Red/P.outputs:surface>
    def Shader "P" { uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (1,0,0) token outputs:surface } }
}
USDA

probe() {
  python3 - "$1" "$2" <<'PY'
import re, sys
d=open(sys.argv[1],"rb").read(); m=re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s",d)
if not m: sys.exit(2)
p=d[m.end():]; red=green=mix=0; green_values=[]
for i in range(0,len(p)-2,3):
    r,g,b=p[i:i+3]
    red += r > g+35 and r > b+35 and r > 35
    is_green = g > r+35 and g > b+35 and g > 35
    green += is_green
    if is_green: green_values.append(g)
    mix += r > 25 and g > 25 and b < max(r,g)*0.7
spread=0
if green_values:
    green_values.sort()
    spread=green_values[(len(green_values)*9)//10]-green_values[len(green_values)//10]
print(f"{sys.argv[2]} red={red} green={green} mixed={mix} green_spread={spread}")
label=sys.argv[2]
if label in ("vary-vkrt", "vary-cuda", "vary-hip"):
    # The RT preview paths are single-hit: Blend surfaces composite over the
    # environment rather than tracing the red mesh behind them. A broad green
    # intensity distribution verifies the authored opacity ramp.
    if green < 300 or spread < 8: sys.exit(1)
elif label.startswith("vary"):
    if red < 300 or green < 300 or mix < 200: sys.exit(1)
else:
    # Masked/cutout scenes must expose both the opaque green foreground and the
    # red surface behind rejected texels.
    if red < 300 or green < 300: sys.exit(1)
PY
}

# The opacity AOV must report the same composed opacity used by shaded raster:
# material/base alpha * separate mask * varying displayOpacity. Both fixtures
# increase from left to right, so a stale AOV that omits either new input is
# nearly flat and fails this regional comparison.
probe_opacity_aov() {
  python3 - "$1" "$2" <<'PY'
import re, sys
d=open(sys.argv[1],"rb").read(); m=re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s",d)
if not m: sys.exit(2)
w,h=map(int,m.groups()[:2]); p=d[m.end():]
def mean(x0,x1):
    values=[]
    for y in range(h*3//10,h*7//10):
        for x in range(w*x0//10,w*x1//10):
            i=(y*w+x)*3
            values.append(sum(p[i:i+3])/3.0)
    return sum(values)/max(len(values),1)
left=mean(2,4); right=mean(6,8)
print(f"{sys.argv[2]} opacity_aov_left={left:.1f} opacity_aov_right={right:.1f}")
if right-left < 35: sys.exit(1)
PY
}

# Shaded RT output is encoded from linear to sRGB. The UI clear colour is
# already authored in display space, so the RT shader must linearize it before
# that final encode; otherwise 0.12/0.13 becomes roughly (97,97,101) instead of
# the raster backend's (31,31,33). This fixture leaves ample clear background,
# making its most common pixel an exact, driver-independent regression probe.
probe_display_background() {
  python3 - "$1" <<'PY'
from collections import Counter
import re, sys
d = open(sys.argv[1], "rb").read()
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", d)
if not m or int(m.group(3)) != 255:
    sys.exit(2)
w, h = int(m.group(1)), int(m.group(2))
p = d[m.end():m.end() + w * h * 3]
if len(p) != w * h * 3:
    sys.exit(2)
colors = Counter(p[i:i + 3] for i in range(0, len(p), 3))
actual = colors.most_common(1)[0][0] if colors else b""
expected = bytes((31, 31, 33))
print(f"vkrt background={tuple(actual)} expected={tuple(expected)}")
sys.exit(0 if actual == expected else 1)
PY
}

ran=0 mask_ran=0 varying_ran=0 fail=0 vk_software=0
for spec in "gl:--backend gl" "vk:--backend vk"; do
  tag="${spec%%:*}"; args="${spec#*:}"
  if [ "$tag" = gl ] && [ "$DISPLAY_OK" -eq 0 ]; then
    echo "SKIP: GL unavailable (no working X server or Xvfb)"
    continue
  fi
  backend_ok=0
  # Run the texture-free varying fixture first; its device log lets the UDIM
  # probe skip known software rasterizers that cannot fetch textured attributes
  # reliably (the existing UV-routing test makes the same distinction).
  for scene in display-opacity opacity-udim instance-vertex-opacity; do
    if [ "$tag" = vk ] && [ "$scene" = opacity-udim ] &&
       [ "$vk_software" -ne 0 ]; then
      echo "SKIP: Vulkan opacity UDIM on software renderer"
      continue
    fi
    img="$OUT/${scene}-${tag}.ppm"
    if [ "$tag" = gl ]; then
      # GL needs a display. Do not pass --headless: that option is Vulkan-only
      # and intentionally forces the Vulkan renderer.
      # shellcheck disable=SC2086
      run_viewer $XVFB "$BIN" $args --config "$CONFIG" --frames 4 --view-dir 0,0,-1 \
        --screenshot "$img" "$OUT/$scene.usda" >"$OUT/$scene-$tag.log" 2>&1
    else
      # Windowless Vulkan must not inherit Xvfb's software-Vulkan selection.
      # shellcheck disable=SC2086
      run_viewer "$BIN" --headless $args --config "$CONFIG" --frames 4 --view-dir 0,0,-1 \
        --screenshot "$img" "$OUT/$scene.usda" >"$OUT/$scene-$tag.log" 2>&1
    fi
    if ! grep -q 'render stats' "$OUT/$scene-$tag.log" || [ ! -s "$img" ]; then
      echo "SKIP: $tag unavailable for $scene"
      tail -20 "$OUT/$scene-$tag.log"
      continue
    fi
    if [ "$tag" = vk ] && grep -Eqi 'llvmpipe|\(cpu, driver' "$OUT/$scene-$tag.log"; then
      vk_software=1
    fi
    if grep -Eqi 'llvmpipe|softpipe|lavapipe|software rasterizer|LLVM [0-9]' \
         "$OUT/$scene-$tag.log"; then
      echo "SKIP: $tag is a software renderer for $scene"
      continue
    fi
    ran=$((ran+1)); label="${scene}-${tag}"
    backend_ok=1
    if [ "$scene" = opacity-udim ]; then mask_ran=$((mask_ran+1));
    else varying_ran=$((varying_ran+1)); fi
    [ "$scene" = display-opacity ] && label="vary-${tag}"
    probe "$img" "$label" || { echo "FAIL: $label"; fail=1; }

    # VK raster sorts the two nearly coplanar blend layers independently of
    # GL; an opacity AOV writes opaque grayscale and can therefore be hidden by
    # the rear layer even though shaded blending is correct. The VK-RT AOV
    # below exercises the same non-instanced carrier without this raster-order
    # ambiguity.
    if [ "$tag" = vk ] && [ "$scene" = display-opacity ]; then
      continue
    fi
    aov="$OUT/${scene}-${tag}-opacity-aov.ppm"
    if [ "$tag" = gl ]; then
      # shellcheck disable=SC2086
      run_viewer $XVFB "$BIN" $args --config "$CONFIG" --mode opacity --frames 4 \
        --view-dir 0,0,-1 --screenshot "$aov" "$OUT/$scene.usda" \
        >"$OUT/$scene-$tag-opacity-aov.log" 2>&1
    else
      run_viewer "$BIN" --headless $args --config "$CONFIG" --mode opacity \
        --frames 4 --view-dir 0,0,-1 --screenshot "$aov" "$OUT/$scene.usda" \
        >"$OUT/$scene-$tag-opacity-aov.log" 2>&1
    fi
    if ! grep -q 'render stats' "$OUT/$scene-$tag-opacity-aov.log" ||
       [ ! -s "$aov" ]; then
      echo "FAIL: $scene-$tag opacity AOV did not render"
      tail -20 "$OUT/$scene-$tag-opacity-aov.log"
      fail=1
    else
      probe_opacity_aov "$aov" "$scene-$tag" || {
        echo "FAIL: $scene-$tag opacity AOV"; fail=1;
      }
    fi
  done

done

# RT samples the same opacity/UDIM inputs as raster. A transparent candidate
# must be rejected during traversal so the red mesh behind it is visible; the
# displayOpacity fixture separately exercises varying vertex alpha.
if [ "$vk_software" -ne 0 ]; then
  echo "SKIP: Vulkan ray query unavailable (software Vulkan only)"
else
img="$OUT/display-opacity-vkrt.ppm"
run_viewer "$BIN" --headless --backend vk --rt --config "$CONFIG" --frames 4 \
  --view-dir 0,0,-1 --screenshot "$img" "$OUT/display-opacity.usda" \
  >"$OUT/display-opacity-vkrt.log" 2>&1
if grep -q 'caps: v1 .*rt=hardware' "$OUT/display-opacity-vkrt.log" && [ -s "$img" ]; then
  ran=$((ran+1)); varying_ran=$((varying_ran+1))
  probe "$img" vary-vkrt || { echo "FAIL: vary-vkrt"; fail=1; }
  probe_display_background "$img" || {
    echo "FAIL: Vulkan RT clear colour was sRGB-encoded twice"; fail=1;
  }
  aov="$OUT/display-opacity-vkrt-opacity-aov.ppm"
  run_viewer "$BIN" --headless --backend vk --rt --config "$CONFIG" \
    --mode opacity --frames 4 --view-dir 0,0,-1 --screenshot "$aov" \
    "$OUT/display-opacity.usda" >"$OUT/display-opacity-vkrt-opacity-aov.log" 2>&1
  if grep -q 'caps: v1 .*rt=hardware' \
       "$OUT/display-opacity-vkrt-opacity-aov.log" && [ -s "$aov" ]; then
    probe_opacity_aov "$aov" display-opacity-vkrt || {
      echo "FAIL: display-opacity-vkrt opacity AOV"; fail=1;
    }
  else
    echo "FAIL: Vulkan ray-query opacity AOV did not render"
    fail=1
  fi
  mask_img="$OUT/opacity-udim-vkrt.ppm"
  run_viewer "$BIN" --headless --backend vk --rt --config "$CONFIG" --frames 4 \
    --view-dir 0,0,-1 --screenshot "$mask_img" "$OUT/opacity-udim.usda" \
    >"$OUT/opacity-udim-vkrt.log" 2>&1
  if grep -q 'caps: v1 .*rt=hardware' \
       "$OUT/opacity-udim-vkrt.log" && [ -s "$mask_img" ]; then
    mask_ran=$((mask_ran+1))
    probe "$mask_img" mask-vkrt || { echo "FAIL: mask-vkrt"; fail=1; }
  else
    echo "FAIL: Vulkan ray-query opacity/UDIM scene did not render"
    fail=1
  fi
else
  echo "SKIP: Vulkan ray query unavailable"
fi
fi

# The software-BVH Vulkan path uses the same UDIM fixture. Keep this separate
# from the hardware ray-query branch so llvmpipe/CPU Vulkan installations still
# exercise tile selection rather than silently skipping the regression.
sw_img="$OUT/opacity-udim-swbvh.ppm"
LUSDVIEW_RT_FORCE_SW=1 run_viewer "$BIN" --headless --backend vk --rt \
  --config "$CONFIG" --frames 2 --view-dir 0,0,-1 --screenshot "$sw_img" \
  "$OUT/opacity-udim.usda" >"$OUT/opacity-udim-swbvh.log" 2>&1
if grep -q 'renderer: Vulkan (compute BVH)' "$OUT/opacity-udim-swbvh.log" &&
   [ -s "$sw_img" ]; then
  mask_ran=$((mask_ran+1))
  probe "$sw_img" mask-swbvh || { echo "FAIL: mask-swbvh"; fail=1; }
else
  echo "SKIP: Vulkan software-BVH UDIM render unavailable"
fi

for rt in cuda hip; do
  img="$OUT/display-opacity-$rt.ppm"
  run_viewer "$BIN" --headless --"$rt" --config "$CONFIG" --frames 4 \
    --view-dir 0,0,-1 --screenshot "$img" "$OUT/display-opacity.usda" \
    >"$OUT/display-opacity-$rt.log" 2>&1
  upper="CUDA"; [ "$rt" = hip ] && upper="HIP"
  if grep -q "$upper RT wrote" "$OUT/display-opacity-$rt.log" && [ -s "$img" ]; then
    ran=$((ran+1)); varying_ran=$((varying_ran+1))
    probe "$img" "vary-$rt" || { echo "FAIL: vary-$rt"; fail=1; }
    aov="$OUT/display-opacity-$rt-opacity-aov.ppm"
    run_viewer "$BIN" --headless --"$rt" --config "$CONFIG" --mode opacity \
      --frames 4 --view-dir 0,0,-1 --screenshot "$aov" \
      "$OUT/display-opacity.usda" \
      >"$OUT/display-opacity-$rt-opacity-aov.log" 2>&1
    if grep -q "$upper RT wrote" "$OUT/display-opacity-$rt-opacity-aov.log" &&
       [ -s "$aov" ]; then
      probe_opacity_aov "$aov" "display-opacity-$rt" || {
        echo "FAIL: display-opacity-$rt opacity AOV"; fail=1;
      }
    else
      echo "FAIL: $upper opacity AOV did not render"
      fail=1
    fi
    mask_img="$OUT/opacity-udim-$rt.ppm"
    run_viewer "$BIN" --headless --"$rt" --config "$CONFIG" --frames 4 \
      --view-dir 0,0,-1 --screenshot "$mask_img" "$OUT/opacity-udim.usda" \
      >"$OUT/opacity-udim-$rt.log" 2>&1
    if grep -q "$upper RT wrote" "$OUT/opacity-udim-$rt.log" &&
       [ -s "$mask_img" ]; then
      mask_ran=$((mask_ran+1))
      probe "$mask_img" "mask-$rt" || { echo "FAIL: mask-$rt"; fail=1; }
    else
      echo "FAIL: $upper opacity/UDIM scene did not render"
      fail=1
    fi
  else
    echo "SKIP: $upper ray tracing unavailable"
  fi
done

[ "$ran" -gt 0 ] || exit "$SKIP"
[ "$mask_ran" -gt 0 ] || { echo "SKIP: no raster backend could test opacity UDIM"; exit "$SKIP"; }
[ "$varying_ran" -gt 0 ] || { echo "SKIP: no backend could test displayOpacity"; exit "$SKIP"; }
[ "$fail" -eq 0 ] || exit 1
echo "PASS: opacity texture/UDIM and varying displayOpacity"
