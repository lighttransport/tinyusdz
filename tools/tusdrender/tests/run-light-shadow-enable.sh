#!/usr/bin/env bash
# UsdLuxShadowAPI inputs:shadow:enable must disable this light's occlusion rays
# without requiring the global -noShadows switch.
set -u
TUSDRENDER="${1:-}"
if [ ! -x "$TUSDRENDER" ] || ! command -v python3 >/dev/null; then exit 77; fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

make_scene() {
  local enabled="$1" out="$2"
  sed "s/@SHADOW@/$enabled/" > "$out" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Wall" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-3,-3,0), (3,-3,0), (3,3,0), (-3,3,0)]
    normal3f[] normals = [(0,0,1), (0,0,1), (0,0,1), (0,0,1)] (interpolation = "vertex")
    color3f[] primvars:displayColor = [(0.8,0.8,0.8)]
    uniform token subdivisionScheme = "none"
  }
  def Mesh "Blocker" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-0.8,-0.8,1), (0.8,-0.8,1), (0.8,0.8,1), (-0.8,0.8,1)]
    normal3f[] normals = [(0,0,1), (0,0,1), (0,0,1), (0,0,1)] (interpolation = "vertex")
    uniform token subdivisionScheme = "none"
  }
  def SphereLight "Key" (
    prepend apiSchemas = ["ShadowAPI"]
  ) {
    float inputs:radius = 0.01
    float inputs:intensity = 100
    bool inputs:shadow:enable = @SHADOW@
    double3 xformOp:translate = (0,0,4)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }
}
USDA
}
make_scene true "$TMP/on.usda"
make_scene false "$TMP/off.usda"
for mode in on off; do
  "$TUSDRENDER" "$TMP/$mode.usda" "$TMP/$mode.png" -rtPreview \
    -viewDir 0,0,1 -bg 0,0,0 -ambient 0 -w 96 -height 96 -samples 1 \
    >"$TMP/$mode.log" 2>&1 || { cat "$TMP/$mode.log"; exit 1; }
done

python3 - "$TMP/on.png" "$TMP/off.png" <<'PY'
import struct, sys, zlib
def mean(path):
    d=open(path,'rb').read(); p=8; raw=b''
    while p+8<=len(d):
        n=struct.unpack('>I',d[p:p+4])[0]; t=d[p+4:p+8]; b=d[p+8:p+8+n]; p+=12+n
        if t==b'IHDR': w,h,depth,color=struct.unpack('>IIBB',b[:10])
        elif t==b'IDAT': raw+=b
    raw=zlib.decompress(raw); channels={0:1,2:3,4:2,6:4}[color]; stride=w*channels
    prev=bytearray(stride); values=[]; q=0
    for _ in range(h):
        f=raw[q]; q+=1; row=bytearray(raw[q:q+stride]); q+=stride
        for i in range(stride):
            a=row[i-channels] if i>=channels else 0; b=prev[i]; c=prev[i-channels] if i>=channels else 0
            if f==1: row[i]=(row[i]+a)&255
            elif f==2: row[i]=(row[i]+b)&255
            elif f==3: row[i]=(row[i]+((a+b)//2))&255
            elif f==4:
                x=a+b-c; pa=abs(x-a); pb=abs(x-b); pc=abs(x-c)
                row[i]=(row[i]+(a if pa<=pb and pa<=pc else b if pb<=pc else c))&255
        values.extend(sum(row[i:i+3]) for i in range(0,stride,channels)); prev=row
    return sum(values)/len(values)
on,off=mean(sys.argv[1]),mean(sys.argv[2])
if off <= on*1.04:
    raise SystemExit(f'FAIL: disabling per-light shadows did not brighten scene ({on:.2f} -> {off:.2f})')
print(f'PASS: per-light shadow enable ({on:.2f} -> {off:.2f})')
PY
