#!/usr/bin/env bash
# Camera-visible Ptex demand regression: one in-view and one off-camera mesh
# share a generated Ptex source. Only the in-view mesh may enqueue its face.
set -uo pipefail

viewer="${TUSDVIEW:-build_ninja/tusdview}"
generator="${TUSDVIEW_PTEX_GENERATOR:-build_ninja/tusdview_ptex_atlas_test}"
out="$(mktemp -d "${TMPDIR:-/tmp}/tusdview-ptex-demand.XXXXXX")"
trap 'rm -rf "$out"' EXIT

if [[ ! -x "$viewer" || ! -x "$generator" ]]; then
  echo "FAIL: viewer or Ptex generator executable is missing" >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "SKIP: python3 unavailable"
  exit 77
fi

"$generator" --write-synthetic "$out/two-face.ptx" || exit 1
cat >"$out/scene.usda" <<EOF
#usda 1.0
def Camera "Camera" {
    float2 clippingRange = (0.1, 1000)
    double focalLength = 50
    double horizontalAperture = 20.955
    matrix4d xformOp:transform = ((1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,5,1))
    uniform token[] xformOpOrder = ["xformOp:transform"]
}
def Material "Mat" {
    token outputs:surface.connect = </Mat/Surface.outputs:surface>
    def Shader "Ptex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @${out}/two-face.ptx@
        float3 outputs:rgb
    }
    def Shader "Surface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor.connect = </Mat/Ptex.outputs:rgb>
        token outputs:surface
    }
}
def Mesh "Near" (prepend apiSchemas = ["MaterialBindingAPI"]) {
    rel material:binding = </Mat>
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    point3f[] points = [(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)]
    uniform token subdivisionScheme = "none"
}
def Mesh "Far" (prepend apiSchemas = ["MaterialBindingAPI"]) {
    rel material:binding = </Mat>
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    point3f[] points = [(99,-1,0),(101,-1,0),(101,1,0),(99,1,0)]
    uniform token subdivisionScheme = "none"
}
EOF

log="$out/run.log"
if ! env TUSDVIEW_PTEX_FORCE_RESIDENCY=1 timeout 60s \
    "$viewer" --headless --backend vk --next \
    --large-scene-profile island --frames 4 --size 192x128 --camera /Camera \
    --screenshot "$out/result.png" --render-report "$out/report.json" \
    "$out/scene.usda" >"$log" 2>&1; then
  if grep -Eq 'no compatible Vulkan|no Vulkan device|renderer init failed' "$log"; then
    echo "SKIP: Vulkan unavailable"
    exit 77
  fi
  cat "$log" >&2
  exit 1
fi

python3 - "$out/report.json" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as f:
    report = json.load(f)
s = report["scene_stats"]
r = report["render_stats"]
assert s["ptex_considered_meshes"] == 1, s
assert s["ptex_requested_meshes"] == 1, s
assert s["ptex_requested_faces"] == 1, s
assert s["ptex_async_jobs_launched"] >= 1, s
assert s["ptex_async_jobs_completed"] == s["ptex_async_jobs_launched"], s
assert s["ptex_gpu_page_uploads"] >= 1, s
assert r["visible_meshes"] == 1 and r["total_meshes"] == 2, r
PY

echo "PASS: off-camera mesh did not enqueue Ptex page demand"
