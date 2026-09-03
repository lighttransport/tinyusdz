#!/usr/bin/env bash
#
# lusdrender texture decoder regression: a CMYK JPEG texture should decode to RGB
# rather than being skipped or interpreted as inverted/black.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDRENDER="${1:-${LUSDRENDER:-$REPO_ROOT/build/tools/lusdrender/lusdrender}}"

if [ ! -x "$LUSDRENDER" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SCENE="$TMP/cmyk_jpeg.usda"
TEX="$TMP/cmyk.jpg"
OUT="$TMP/cmyk_jpeg.png"
LOG="$TMP/cmyk_jpeg.log"

python3 - "$TEX" <<'PY'
import base64
import sys

# 4x4 solid red-orange CMYK JPEG generated once from an RGB source and embedded
# so the regression has no runtime dependency on ImageMagick/libjpeg tools.
DATA = (
    "/9j/4AAQSkZJRgABAQAAAAAAAAD/7gAOQWRvYmUAZAAAAAAC/9sAQwADAgICAgIDAgIC"
    "AwMDAwQGBAQEBAQIBgYFBgkICgoJCAkJCgwPDAoLDgsJCQ0RDQ4PEBAREAoMEhMSEBMP"
    "EBAQ/9sAQwEDAwMEAwQIBAQIEAsJCxAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQ"
    "EBAQEBAQEBAQEBAQEBAQEBAQEBAQ/8AAFAgABAAEBAERAAIRAQMRAQQRAP/EABUAAQEA"
    "AAAAAAAAAAAAAAAAAAYI/8QAFBABAAAAAAAAAAAAAAAAAAAAAP/EABUBAQEAAAAAAAAA"
    "AAAAAAAAAAcJ/8QAFBEBAAAAAAAAAAAAAAAAAAAAAP/aAA4EAQACEQMRBAAAPwBwfU5F"
    "6v/Z"
)
open(sys.argv[1], "wb").write(base64.b64decode(DATA))
PY

cat > "$SCENE" <<'USDA'
#usda 1.0
( upAxis = "Y" )
def Xform "World" {
  def Mesh "M" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    texCoord2f[] primvars:st = [(0,0),(1,0),(1,1),(0,1)] (
      interpolation = "vertex"
    )
    rel material:binding = </World/Mats/Mat>
  }
  def Scope "Mats" {
    def Material "Mat" {
      token outputs:surface.connect = </World/Mats/Mat/Preview.outputs:surface>
      def Shader "Preview" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor.connect = </World/Mats/Mat/Tex.outputs:rgb>
        token outputs:surface
      }
      def Shader "Tex" {
        uniform token info:id = "UsdUVTexture"
        asset inputs:file = @cmyk.jpg@
        token inputs:sourceColorSpace = "sRGB"
        color3f outputs:rgb
      }
    }
  }
}
USDA

"$LUSDRENDER" "$SCENE" "$OUT" -rtPreview -w 64 -height 64 -autoframe \
  -viewDir 0,0,-1 -ambient 1 -noShadows -samples 1 -stats >"$LOG" 2>&1
rc=$?
cat "$LOG"
if [ "$rc" -ne 0 ]; then
  echo "FAIL: lusdrender exited with $rc"
  exit 1
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: lusdrender produced no image"
  exit 1
fi
if ! grep -Eq '^triangles: 2$' "$LOG"; then
  echo "FAIL: expected textured quad to render as two triangles"
  exit 1
fi
if grep -Eq 'load summary:.*missing_textures=[1-9]' "$LOG"; then
  echo "FAIL: CMYK JPEG texture was reported missing"
  exit 1
fi

read -r R G B < <(
  python3 - "$OUT" <<'PY'
import struct
import sys
import zlib

data = open(sys.argv[1], "rb").read()
if not data.startswith(b"\x89PNG\r\n\x1a\n"):
    raise SystemExit("not a PNG")
pos = 8
width = height = bit_depth = color_type = None
idat = bytearray()
while pos + 8 <= len(data):
    n = struct.unpack(">I", data[pos:pos + 4])[0]
    typ = data[pos + 4:pos + 8]
    payload = data[pos + 8:pos + 8 + n]
    pos += 12 + n
    if typ == b"IHDR":
        width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
    elif typ == b"IDAT":
        idat.extend(payload)
    elif typ == b"IEND":
        break
if bit_depth != 8 or color_type != 6:
    raise SystemExit("expected 8-bit RGBA PNG")
raw = zlib.decompress(bytes(idat))
stride = width * 4
prev = bytearray(stride)
p = 0
rgba = bytearray()
for _ in range(height):
    filt = raw[p]
    p += 1
    row = bytearray(raw[p:p + stride])
    p += stride
    for i in range(stride):
        left = row[i - 4] if i >= 4 else 0
        up = prev[i]
        up_left = prev[i - 4] if i >= 4 else 0
        if filt == 1:
            row[i] = (row[i] + left) & 0xff
        elif filt == 2:
            row[i] = (row[i] + up) & 0xff
        elif filt == 3:
            row[i] = (row[i] + ((left + up) >> 1)) & 0xff
        elif filt == 4:
            pa = abs(up - up_left)
            pb = abs(left - up_left)
            pc = abs(left + up - 2 * up_left)
            pred = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
            row[i] = (row[i] + pred) & 0xff
        elif filt != 0:
            raise SystemExit(f"unsupported PNG filter {filt}")
    rgba.extend(row)
    prev = row
o = ((height // 2) * width + (width // 2)) * 4
print(rgba[o], rgba[o + 1], rgba[o + 2])
PY
)

echo "center RGB = $R $G $B"
if [ "$R" -gt 150 ] && [ "$G" -lt 100 ] && [ "$B" -lt 80 ]; then
  echo "PASS: CMYK JPEG texture decoded to expected RGB color"
  exit 0
fi

echo "FAIL: expected red-dominant CMYK JPEG decode, got $R $G $B"
exit 1
