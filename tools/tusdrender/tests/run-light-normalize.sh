#!/usr/bin/env bash
#
# Regression test: UsdLux `inputs:normalize` on analytic lights (R10).
#
# A normalized light holds its POWER fixed as its size changes: its radiance is
# the intensity divided by the shape's surface area (sphere 4*pi*r^2, rect w*h).
# tusdrender ignored the flag entirely -- and worse, a SphereLight was ALWAYS
# implicitly normalized by pi*r^2, a convention matching neither UsdLux, the
# other shapes, nor tusdview.
#
# Asserted as equivalences a constant factor cannot fake, on both the next
# (-rtPreview) and legacy paths:
#   normalize=true, intensity=I  ==  normalize=false, intensity=I/area
# for a sphere and a rect, and that dropping the divisor changes the image.
#
# Exit codes: 0 = pass, 1 = fail, 77 = skip.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDRENDER="${1:-${TUSDRENDER:-$REPO_ROOT/build/tools/tusdrender/tusdrender}}"

if [ ! -x "$TUSDRENDER" ]; then
  echo "SKIP: tusdrender binary not found at $TUSDRENDER"; exit $SKIP
fi
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 missing"; exit $SKIP; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; exit 1; }

# $1 light def -> a wall lit only by that light.
scene() {
cat <<USDA
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
  def Mesh "Wall" {
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-3,-3,0), (3,-3,0), (3,3,0), (-3,3,0)]
    normal3f[] normals = [(0,0,1), (0,0,1), (0,0,1), (0,0,1)] (interpolation = "vertex")
    color3f[] primvars:displayColor = [(0.8, 0.8, 0.8)]
    uniform token subdivisionScheme = "none"
  }
  $1
}
USDA
}

# Sphere radius 2: area = 4*pi*4 = 16*pi ~ 50.2655
scene 'def SphereLight "L" {
    float inputs:radius = 2.0
    float inputs:intensity = 40.0
    bool inputs:normalize = true
    double3 xformOp:translate = (0, 0, 5)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }' > "$TMP/sphere_norm.usda"
scene 'def SphereLight "L" {
    float inputs:radius = 2.0
    float inputs:intensity = 0.795775
    double3 xformOp:translate = (0, 0, 5)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }' > "$TMP/sphere_div.usda"      # 40 / (16*pi)

# Rect 2x0.5: area = 1.0 -- normalize must be an exact no-op at area 1, so use
# a rect whose area is NOT 1 to make the divisor observable: 4 x 2 -> area 8.
scene 'def RectLight "L" {
    float inputs:width = 4.0
    float inputs:height = 2.0
    float inputs:intensity = 16.0
    bool inputs:normalize = true
    double3 xformOp:translate = (0, 0, 5)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }' > "$TMP/rect_norm.usda"
scene 'def RectLight "L" {
    float inputs:width = 4.0
    float inputs:height = 2.0
    float inputs:intensity = 2.0
    double3 xformOp:translate = (0, 0, 5)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }' > "$TMP/rect_div.usda"        # 16 / 8

mean() {
python3 - "$1" <<'PY'
import sys, struct, zlib
d=open(sys.argv[1],"rb").read(); pos=8; w=h=None; idat=b""; color=None
while pos+8<=len(d):
    ln=struct.unpack(">I",d[pos:pos+4])[0]; t=d[pos+4:pos+8]; b=d[pos+8:pos+8+ln]
    if t==b"IHDR": w,h,_,color=struct.unpack(">IIBB",b[:10])
    elif t==b"IDAT": idat+=b
    elif t==b"IEND": break
    pos+=12+ln
nch=3 if color==2 else 4; raw=zlib.decompress(idat); stride=w*nch
prev=bytearray(stride); p=0; s=0; n=0
for _ in range(h):
    f=raw[p]; p+=1; line=bytearray(raw[p:p+stride]); p+=stride
    for i in range(stride):
        a=line[i-nch] if i>=nch else 0; bb=prev[i]
        if f==1: line[i]=(line[i]+a)&0xff
        elif f==2: line[i]=(line[i]+bb)&0xff
        elif f==3: line[i]=(line[i]+((a+bb)>>1))&0xff
        elif f==4:
            c=prev[i-nch] if i>=nch else 0
            p_=a+bb-c; pa,pb,pc=abs(p_-a),abs(p_-bb),abs(p_-c)
            pr=a if (pa<=pb and pa<=pc) else (bb if pb<=pc else c)
            line[i]=(line[i]+pr)&0xff
    prev=line
    for i in range(0,stride,nch): s+=line[i]+line[i+1]+line[i+2]; n+=3
print(s/max(n,1))
PY
}

# $1 = label, $2..: extra args (path selection)
check_pair() {
  local label="$1" a="$2" b="$3"; shift 3
  "$TUSDRENDER" "$a" "$TMP/a.png" -w 64 -height 64 -samples 4 -autoframe "$@" \
      >/dev/null 2>&1 || fail "$label: render A failed"
  "$TUSDRENDER" "$b" "$TMP/b.png" -w 64 -height 64 -samples 4 -autoframe "$@" \
      >/dev/null 2>&1 || fail "$label: render B failed"
  local ma mb msg
  ma=$(mean "$TMP/a.png"); mb=$(mean "$TMP/b.png")
  msg=$(python3 -c "
import sys
a, b = float('$ma'), float('$mb')
if a < 3.0: sys.exit('$label: normalized render is black (mean %.2f) -- divisor applied twice or light dropped' % a)
if abs(a - b) > 0.05 * max(a, b) + 1.0:
    sys.exit('$label: normalize=true intensity=I (mean %.2f) != normalize=false intensity=I/area (mean %.2f)' % (a, b))
print('  %s: %.2f ~ %.2f' % ('$label', a, b))
" 2>&1) || fail "$msg"
  echo "$msg"
}

echo "next (-rtPreview) path:"
check_pair "sphere/next" "$TMP/sphere_norm.usda" "$TMP/sphere_div.usda" -rtPreview
check_pair "rect/next"   "$TMP/rect_norm.usda"   "$TMP/rect_div.usda"   -rtPreview
echo "legacy path:"
check_pair "sphere/legacy" "$TMP/sphere_norm.usda" "$TMP/sphere_div.usda"
check_pair "rect/legacy"   "$TMP/rect_norm.usda"   "$TMP/rect_div.usda"

# The flag must DO something: normalize=true at the same intensity is dimmer
# than normalize=false by ~area (16*pi for this sphere).
scene 'def SphereLight "L" {
    float inputs:radius = 2.0
    float inputs:intensity = 40.0
    double3 xformOp:translate = (0, 0, 5)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }' > "$TMP/sphere_raw.usda"
"$TUSDRENDER" "$TMP/sphere_raw.usda" "$TMP/raw.png" -w 64 -height 64 -samples 4 \
    -autoframe -rtPreview >/dev/null 2>&1 || fail "unnormalized render failed"
"$TUSDRENDER" "$TMP/sphere_norm.usda" "$TMP/norm.png" -w 64 -height 64 -samples 4 \
    -autoframe -rtPreview >/dev/null 2>&1 || fail "normalized render failed"
mr=$(mean "$TMP/raw.png"); mn=$(mean "$TMP/norm.png")
python3 -c "
import sys
raw, norm = float('$mr'), float('$mn')
if not raw > norm * 1.5: sys.exit('FAIL')
" || fail "normalize is inert: same intensity renders raw=$mr vs normalized=$mn (raw must be far brighter)"

echo "PASS: inputs:normalize divides by surface area on both paths"
exit 0
