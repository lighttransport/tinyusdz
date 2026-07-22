#!/usr/bin/env bash
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build_ninja/tusdview}"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }
command -v xvfb-run >/dev/null || { echo "SKIP: xvfb-run missing"; exit "$SKIP"; }
OUT="${TUSDVIEW_TEST_OUT:-$(mktemp -d)}"
[ -n "${TUSDVIEW_TEST_OUT:-}" ] || trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT"
printf '%s\n' '{"window_size":{"width":96,"height":96}}' > "$OUT/config.json"

cat > "$OUT/lights.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "A" {
    point3f[] points = [(-1,-1,0),(0,-1,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </World/MatA>
  }
  def Mesh "B" {
    point3f[] points = [(0,-1,0),(1,-1,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </World/MatB>
  }
  def Material "MatA" {
    token outputs:surface.connect = </World/MatA/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (1,0,0)
      token outputs:surface
    }
  }
  def Material "MatB" {
    token outputs:surface.connect = </World/MatB/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0,1,0)
      token outputs:surface
    }
  }
  def RectLight "Key" {
    color3f inputs:color = (0.25,0.5,1)
    float inputs:intensity = 4
    float inputs:exposure = 1
    float inputs:diffuse = 0.75
    float inputs:specular = 0.5
    bool inputs:normalize = 1
    float inputs:width = 2
    float inputs:height = 4
    float inputs:shaping:cone:angle = 30
    float inputs:shaping:cone:softness = 0.2
    float inputs:shaping:focus = 0.4
    bool inputs:shadow:enable = 1
    color3f inputs:shadow:color = (0.05,0.06,0.07)
    float inputs:shadow:distance = 12
    float inputs:shadow:falloff = 3
    float inputs:shadow:falloffGamma = 2
    rel collection:lightLink:includes = </World/A>
    uniform token collection:lightLink:expansionRule = "expandPrims"
    rel collection:shadowLink:includes = </World/B>
    uniform token collection:shadowLink:expansionRule = "expandPrims"
    double3 xformOp:translate = (0,4,0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }
  def SphereLight "Fill" {
    color3f inputs:color = (1,0.25,0.125)
    float inputs:intensity = 2
    float inputs:radius = 0.5
    bool inputs:normalize = 1
    bool inputs:shadow:enable = 0
    double3 xformOp:translate = (2,3,1)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }
  def DistantLight "Sun" {
    color3f inputs:color = (0.8,0.7,0.6)
    float inputs:intensity = 1.5
    float inputs:angle = 0.75
    float inputs:diffuse = 0.9
    float inputs:specular = 0.8
    bool inputs:shadow:enable = 1
    float3 xformOp:rotateXYZ = (15,25,5)
    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
  }
  def DiskLight "Rim" {
    color3f inputs:color = (0.2,0.4,1)
    float inputs:intensity = 3
    float inputs:radius = 1.25
    bool inputs:normalize = 1
    double3 xformOp:translate = (-2,2,1)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }
  def CylinderLight "Tube" {
    color3f inputs:color = (1,0.6,0.2)
    float inputs:intensity = 2.5
    float inputs:radius = 0.2
    float inputs:length = 3
    bool inputs:normalize = 1
    double3 xformOp:translate = (0,2,2)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }
}
USDA

run_loader() {
  local name="$1"; shift
  local attempt
  for attempt in 1 2; do
    TUSDVIEW_DUMP_LIGHT_RECORDS=1 xvfb-run -a "$BIN" --backend gl \
      --config "$OUT/config.json" --frames 1 --no-grid "$@" \
      "$OUT/lights.usda" >"$OUT/$name.log" 2>&1 && break
    [ "$attempt" = 1 ] || return 1
  done
  sed -n 's/^\[tusdview-light\] //p' "$OUT/$name.log" > "$OUT/$name.records"
  [ -s "$OUT/$name.records" ]
}
run_loader next || { echo "FAIL: default light extraction"; exit 1; }
run_loader legacy --legacy-load || { echo "FAIL: legacy light extraction"; exit 1; }

python3 - "$OUT/next.records" "$OUT/legacy.records" <<'PY'
import math,sys
def read(p): return [[float(x) for x in line.split()] for line in open(p) if line.strip()]
a,b=read(sys.argv[1]),read(sys.argv[2])
if len(a)!=5 or len(b)!=5 or any(len(x)!=len(y) for x,y in zip(a,b)):
 print('light record shape mismatch',a,b); raise SystemExit(1)
for li,(x,y) in enumerate(zip(a,b)):
 # Mesh indices are loader-layout-specific (the next loader batches geometry),
 # so collection membership is pinned separately by lighting_test. Compare the
 # shared authored/derived light record here, including the collection-all flags.
 for fi,(u,v) in enumerate(zip(x[:-2],y[:-2])):
  if not math.isclose(u,v,rel_tol=1e-5,abs_tol=1e-5):
   print(f'light {li} field {fi}: next={u} legacy={v}'); raise SystemExit(1)
print('PASS: default/legacy light records equivalent')
PY
