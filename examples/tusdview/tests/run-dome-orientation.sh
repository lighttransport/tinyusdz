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

python3 - "$OUT/env.ppm" <<'PY'
import sys
w,h=64,32
with open(sys.argv[1],'wb') as f:
 f.write(f'P6\n{w} {h}\n255\n'.encode())
 for y in range(h):
  for x in range(w):
   if x < w//4: c=(240,24,12)
   elif x < w//2: c=(16,220,32)
   elif x < 3*w//4: c=(12,32,240)
   else: c=(220,180,16)
   f.write(bytes(c))
PY

write_scene() {
  local file="$1" angle="$2"
  cat > "$file" <<USDA
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Sphere "Ball" {
    double radius = 1
    rel material:binding = </World/M>
  }
  def Material "M" {
    token outputs:surface.connect = </World/M/P.outputs:surface>
    def Shader "P" {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (0.65,0.65,0.65)
      float inputs:metallic = 0.15
      float inputs:roughness = 0.35
      token outputs:surface
    }
  }
  def DomeLight "Sky" {
    asset inputs:texture:file = @./env.ppm@
    token inputs:texture:format = "latlong"
    float inputs:intensity = 1
    float3 xformOp:rotateXYZ = (0,$angle,0)
    uniform token[] xformOpOrder = ["xformOp:rotateXYZ"]
  }
}
USDA
}
write_scene "$OUT/dome-0.usda" 0
write_scene "$OUT/dome-90.usda" 90

run() {
  if command -v timeout >/dev/null; then
    timeout --kill-after=5s "${TUSDVIEW_RENDER_TIMEOUT:-90s}" "$@"
  else
    "$@"
  fi
}
render() {
  local name="$1" scene="$2"; shift 2
  run "$BIN" --headless --backend vk --config "$OUT/config.json" --frames 4 \
    --no-grid --mode shaded --view-dir 0,0,-1 --screenshot "$OUT/$name.ppm" \
    "$@" "$scene" >"$OUT/$name.log" 2>&1
}
render next-0 "$OUT/dome-0.usda" || { echo "SKIP: Vulkan unavailable"; exit "$SKIP"; }
render next-90 "$OUT/dome-90.usda" || { echo "FAIL: rotated DomeLight render"; exit 1; }
render legacy-0 "$OUT/dome-0.usda" --legacy-load || { echo "FAIL: legacy DomeLight render"; exit 1; }
render legacy-90 "$OUT/dome-90.usda" --legacy-load || { echo "FAIL: legacy rotated DomeLight render"; exit 1; }

python3 - "$OUT/next-0.ppm" "$OUT/next-90.ppm" "$OUT/legacy-0.ppm" "$OUT/legacy-90.ppm" <<'PY'
import re,sys
def ppm(p):
 d=open(p,'rb').read(); m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',d)
 if not m: raise SystemExit(f'invalid PPM: {p}')
 return m.groups(),d[m.end():]
def mad(a,b): return sum(abs(x-y) for x,y in zip(a,b))/len(a)
imgs=[ppm(p) for p in sys.argv[1:]]
if len({x[0] for x in imgs}) != 1: raise SystemExit('image size mismatch')
n0,n90,l0,l90=(x[1] for x in imgs)
response=mad(n0,n90); parity0=mad(n0,l0); parity90=mad(n90,l90)
print(f'dome rotation MAD={response:.4f}, loader MAD 0={parity0:.4f} 90={parity90:.4f}')
if response < 2.0: raise SystemExit('FAIL: DomeLight rotation did not change lighting')
if max(parity0,parity90) > 2.0: raise SystemExit('FAIL: DomeLight loader parity')
print('PASS: DomeLight orientation changes IBL with loader parity')
PY
