#!/usr/bin/env bash
#
# lusdrender RT-preview opacity regression. Translucent blue and green planes sit
# in front of an opaque red plane; the overlap must contain all three colors.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDRENDER="${1:-${LUSDRENDER:-$REPO_ROOT/build/tools/lusdrender/lusdrender}}"
ASSET="${2:-${ASSET:-$REPO_ROOT/models/lusdview-transparency.usda}}"

if [ ! -x "$LUSDRENDER" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER"
  exit "$SKIP"
fi
if [ ! -f "$ASSET" ]; then
  echo "SKIP: asset not found at $ASSET"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

png_center_rgb() {
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
    raise SystemExit(f"expected 8-bit RGBA PNG, got depth={bit_depth} color={color_type}")
raw = zlib.decompress(bytes(idat))
stride = width * 4
rows = []
prev = bytearray(stride)
p = 0
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
            row[i] = (row[i] + left) & 0xFF
        elif filt == 2:
            row[i] = (row[i] + up) & 0xFF
        elif filt == 3:
            row[i] = (row[i] + ((left + up) >> 1)) & 0xFF
        elif filt == 4:
            pa = abs(up - up_left)
            pb = abs(left - up_left)
            pc = abs(left + up - 2 * up_left)
            pred = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
            row[i] = (row[i] + pred) & 0xFF
        elif filt != 0:
            raise SystemExit(f"unsupported PNG filter {filt}")
    rows.append(bytes(row))
    prev = row
rgba = b"".join(rows)
x0, x1 = int(width * 0.44), int(width * 0.56)
y0, y1 = int(height * 0.44), int(height * 0.56)
r = g = b = n = 0
for y in range(y0, y1):
    for x in range(x0, x1):
        o = (y * width + x) * 4
        r += rgba[o]
        g += rgba[o + 1]
        b += rgba[o + 2]
        n += 1
print(r // n, g // n, b // n)
PY
}

render_case() {
  local label="$1" asset="$2"
  OUT="$TMP/${label}.png"
  LOG="$TMP/${label}.log"
  "$LUSDRENDER" "$asset" "$OUT" -rtPreview -w 96 -height 96 -autoframe \
    -viewDir 0,0,-1 -ambient 0.2 -samples 1 >"$LOG" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL: lusdrender exited with $rc for $label"
    cat "$LOG"
    exit 1
  fi
  if [ ! -s "$OUT" ]; then
    echo "FAIL: lusdrender produced no image for $label"
    cat "$LOG"
    exit 1
  fi
  read -r R G B < <(png_center_rgb) || {
    echo "FAIL: could not parse rendered PNG for $label"
    exit 1
  }
  echo "$label center RGB = $R $G $B"
}

write_reversed_order_asset() {
  local path="$1"
  cat > "$path" <<'USDA'
#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def Mesh "BackRed"
    {
        uniform bool doubleSided = 1
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        rel material:binding = </World/Looks/Red>
        normal3f[] normals = [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1)] (
            interpolation = "faceVarying"
        )
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
        uniform token subdivisionScheme = "none"
    }

    def Mesh "MiddleBlueHalfOpacity"
    {
        uniform bool doubleSided = 1
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        rel material:binding = </World/Looks/BlueHalfOpacity>
        normal3f[] normals = [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1)] (
            interpolation = "faceVarying"
        )
        point3f[] points = [(-0.75, -0.75, 0.025), (0.75, -0.75, 0.025), (0.75, 0.75, 0.025), (-0.75, 0.75, 0.025)]
        uniform token subdivisionScheme = "none"
    }

    def Mesh "FrontGreenHalfOpacity"
    {
        uniform bool doubleSided = 1
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        rel material:binding = </World/Looks/GreenHalfOpacity>
        normal3f[] normals = [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1)] (
            interpolation = "faceVarying"
        )
        point3f[] points = [(-0.65, -0.65, 0.05), (0.65, -0.65, 0.05), (0.65, 0.65, 0.05), (-0.65, 0.65, 0.05)]
        uniform token subdivisionScheme = "none"
    }

    def Scope "Looks"
    {
        def Material "Red"
        {
            token outputs:surface.connect = </World/Looks/Red/Preview.outputs:surface>
            def Shader "Preview"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (1, 0, 0)
                float inputs:opacity = 1
                token outputs:surface
            }
        }
        def Material "BlueHalfOpacity"
        {
            token outputs:surface.connect = </World/Looks/BlueHalfOpacity/Preview.outputs:surface>
            def Shader "Preview"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0, 0, 1)
                float inputs:opacity = 0.5
                token outputs:surface
            }
        }
        def Material "GreenHalfOpacity"
        {
            token outputs:surface.connect = </World/Looks/GreenHalfOpacity/Preview.outputs:surface>
            def Shader "Preview"
            {
                uniform token info:id = "UsdPreviewSurface"
                color3f inputs:diffuseColor = (0, 1, 0)
                float inputs:opacity = 0.5
                token outputs:surface
            }
        }
    }
}
USDA
}

render_case "blue-front" "$ASSET"
if [ "$R" -lt 25 ] || [ "$G" -lt 25 ] || [ "$B" -lt 25 ]; then
  echo "FAIL: expected blended red+green+blue overlap, got $R $G $B"
  cat "$LOG"
  exit 1
fi
if [ "$B" -le "$(( R + 30 ))" ] || [ "$B" -le "$(( G + 30 ))" ]; then
  echo "FAIL: expected front blue layer to dominate sorted overlap, got $R $G $B"
  cat "$LOG"
  exit 1
fi

REV="$TMP/reversed-order.usda"
write_reversed_order_asset "$REV"
render_case "green-front" "$REV"
if [ "$R" -lt 25 ] || [ "$G" -lt 25 ] || [ "$B" -lt 25 ]; then
  echo "FAIL: expected reversed blended red+green+blue overlap, got $R $G $B"
  cat "$LOG"
  exit 1
fi
if [ "$G" -le "$(( R + 30 ))" ] || [ "$G" -le "$(( B + 30 ))" ]; then
  echo "FAIL: expected front green layer to dominate sorted overlap, got $R $G $B"
  cat "$LOG"
  exit 1
fi

echo "PASS: lusdrender sorted opacity blend preserves layer order"
exit 0
