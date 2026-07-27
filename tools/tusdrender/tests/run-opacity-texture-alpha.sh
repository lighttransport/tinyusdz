#!/usr/bin/env bash
#
# tusdrender opacity texture regression. A generated RGBA texture drives
# UsdPreviewSurface inputs:opacity through outputs:a; the two halves of the quad
# must render differently over a blue background.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDRENDER="${1:-${TUSDRENDER:-$REPO_ROOT/build/tools/tusdrender/tusdrender}}"

if [ ! -x "$TUSDRENDER" ]; then
  echo "SKIP: tusdrender binary not found at $TUSDRENDER"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

python3 - "$TMP/alpha.png" <<'PY'
import struct
import sys
import zlib

w = h = 16
rows = []
for y in range(h):
    row = bytearray([0])
    for x in range(w):
        a = 0 if x < w // 2 else 255
        row.extend([0, 0, 0, a])
    rows.append(bytes(row))

def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload +
            struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

png = (b"\x89PNG\r\n\x1a\n" +
       chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
       chunk(b"IDAT", zlib.compress(b"".join(rows))) +
       chunk(b"IEND", b""))
open(sys.argv[1], "wb").write(png)
PY

cat > "$TMP/scene.usda" <<'USD'
#usda 1.0
(
  defaultPrim = "World"
  upAxis = "Y"
)

def Xform "World"
{
  def Mesh "Quad" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  )
  {
    uniform bool doubleSided = 1
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    float2[] primvars:st = [(0, 0), (1, 0), (1, 1), (0, 1)] (
      interpolation = "vertex"
    )
    uniform token subdivisionScheme = "none"
    rel material:binding = </World/Mat>
  }

  def Material "Mat"
  {
    token outputs:surface.connect = </World/Mat/Preview.outputs:surface>

    def Shader "Preview"
    {
      uniform token info:id = "UsdPreviewSurface"
      color3f inputs:diffuseColor = (1, 0, 0)
      float inputs:roughness = 1
      float inputs:opacity.connect = </World/Mat/Alpha.outputs:a>
      token outputs:surface
    }

    def Shader "St"
    {
      uniform token info:id = "UsdPrimvarReader_float2"
      token inputs:varname = "st"
      float2 outputs:result
    }

    def Shader "Alpha"
    {
      uniform token info:id = "UsdUVTexture"
      asset inputs:file = @alpha.png@
      float2 inputs:st.connect = </World/Mat/St.outputs:result>
      float4 inputs:scale = (1, 1, 1, 1)
      float outputs:a
    }
  }
}
USD

OUT="$TMP/out.png"
LOG="$TMP/render.log"
"$TUSDRENDER" "$TMP/scene.usda" "$OUT" -rtPreview -w 64 -height 64 \
  -viewDir 0,0,-1 -bg 0,0,1 -ambient 1 -noShadows -samples 1 \
  >"$LOG" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "FAIL: tusdrender exited with $rc"
  cat "$LOG"
  exit 1
fi

read -r LR LG LB RR RG RB < <(
  python3 - "$REPO_ROOT/examples/tusdview/tests" "$OUT" <<'PY'
import sys

sys.path.insert(0, sys.argv[1])
import asset_fingerprint

got = asset_fingerprint._read_image(sys.argv[2])
if got is None:
    raise SystemExit("cannot decode output")
w, h, rgb = got

def avg(x0, x1):
    s = [0, 0, 0]
    n = 0
    for y in range(h // 3, 2 * h // 3):
        for x in range(x0, x1):
            i = (y * w + x) * 3
            for c in range(3):
                s[c] += rgb[i + c]
            n += 1
    return tuple(v // n for v in s)

l = avg(w // 4, w // 3)
r = avg(2 * w // 3, 3 * w // 4)
print(l[0], l[1], l[2], r[0], r[1], r[2])
PY
) || {
  echo "FAIL: could not inspect render"
  cat "$LOG"
  exit 1
}

echo "left RGB=$LR $LG $LB right RGB=$RR $RG $RB"
if [ "$(( LR - RR ))" -lt 30 ] || [ "$(( RB - LB ))" -lt 25 ]; then
  echo "FAIL: expected opacity alpha texture to change red/blue mix across quad"
  cat "$LOG"
  exit 1
fi

echo "PASS: opacity texture outputs:a affects transparent shading"
exit 0
