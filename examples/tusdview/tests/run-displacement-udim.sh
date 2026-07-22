#!/usr/bin/env bash
set -uo pipefail
SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build_ninja/tusdview}"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }
OUT="${TUSDVIEW_TEST_OUT:-$(mktemp -d)}"
[ -n "${TUSDVIEW_TEST_OUT:-}" ] || trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT"
printf '%s\n' '{"window_size":{"width":256,"height":256}}' > "$OUT/config.json"

python3 - "$OUT" <<'PY'
import os,sys
for stem,values in [('flat',(0,0)),('raised',(0,255))]:
 for tile,value in zip((1001,1002),values):
  with open(os.path.join(sys.argv[1],f'{stem}.{tile}.ppm'),'wb') as f:
   f.write(b'P6\n8 8\n255\n'+bytes((value,value,value))*64)
PY

write_scene() {
  local file="$1" stem="$2"
  cat > "$file" <<USDA
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Grid" {
    uniform bool doubleSided = 1
    uniform token subdivisionScheme = "none"
    point3f[] points = [(-1,-1,0),(0,-1,0),(1,-1,0),(-1,1,0),(0,1,0),(1,1,0)]
    normal3f[] normals = [(0,0,1),(0,0,1),(0,0,1),(0,0,1),(0,0,1),(0,0,1)] (interpolation = "vertex")
    texCoord2f[] primvars:st = [(0,0),(1,0),(2,0),(0,1),(1,1),(2,1)] (interpolation = "vertex")
    int[] faceVertexCounts = [4,4]
    int[] faceVertexIndices = [0,1,4,3,1,2,5,4]
    rel material:binding = </World/M>
  }
  def Material "M" {
    token outputs:surface.connect = </World/M/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "ND_standard_surface_surfaceshader"
      color3f inputs:base_color = (0.8,0.15,0.05)
      float inputs:specular_roughness = 0.7
      float inputs:displacement.connect = </World/M/D.outputs:out>
      token outputs:surface
    }
    def Shader "D" {
      uniform token info:id = "ND_image_float"
      asset inputs:file = @./$stem.<UDIM>.ppm@
      float outputs:out
    }
  }
}
USDA
}
write_scene "$OUT/flat.usda" flat
write_scene "$OUT/raised.usda" raised

render() {
  local name="$1" scene="$2"; shift 2
  "$BIN" --headless --backend vk --config "$OUT/config.json" --frames 4 \
    --no-grid --mode albedo --view-dir '1,0,-1' --screenshot "$OUT/$name.ppm" \
    "$@" "$scene" >"$OUT/$name.log" 2>&1
}
render flat "$OUT/flat.usda" || { echo "SKIP: Vulkan raster unavailable"; exit "$SKIP"; }
render raised "$OUT/raised.usda" || { echo "FAIL: default displacement UDIM render"; exit 1; }

python3 - "$OUT/flat.ppm" "$OUT/raised.ppm" <<'PY'
import re,sys
def ppm(path):
 d=open(path,'rb').read(); m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',d)
 if not m: raise SystemExit(f'invalid PPM: {path}')
 return tuple(map(int,m.groups())),d[m.end():]
def mad(a,b): return sum(abs(x-y) for x,y in zip(a,b))/len(a)
sa,a=ppm(sys.argv[1]); sb,b=ppm(sys.argv[2])
if sa!=sb: raise SystemExit('image size mismatch')
response=mad(a,b)
print(f'displacement response MAD={response:.4f}')
if response < 2.0: raise SystemExit('FAIL: UDIM displacement did not change silhouette')
print('PASS: displacement UDIM affects geometry')
PY
