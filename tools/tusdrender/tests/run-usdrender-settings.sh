#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
RENDERER="${TUSDRENDER:-${1:-$ROOT/build_ninja/tools/tusdrender/tusdrender}}"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/tusdrender-settings.XXXXXX")"
trap 'rm -rf "$TMP_ROOT"' EXIT

SCENE="$TMP_ROOT/settings.usda"
OUTPUT="$TMP_ROOT/settings.png"
cat > "$SCENE" <<'USD'
#usda 1.0
(
    renderSettingsPrimPath = "/Render/Settings"
)
def Scope "Render" {
    def RenderSettings "Settings" {
        int2 resolution = (37, 29)
        token[] includedPurposes = ["default"]
        rel products = </Render/Product>
    }
    def RenderProduct "Product" {
        int2 resolution = (41, 31)
        token productType = "raster"
        rel orderedVars = </Render/Color>
    }
    def RenderVar "Color" {
        token dataType = "color3f"
        string sourceName = "Ci"
        token sourceType = "raw"
    }
    def RenderPass "Pass" {
        rel renderSource = </Render/Settings>
        bool collection:renderVisibility:includeRoot = false
        rel collection:renderVisibility:includes = </World/Keep>
    }
}
def Xform "World" {
    def Mesh "Keep" {
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
    }
    def Mesh "Drop" {
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        double3 xformOp:translate = (3, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
USD

LOG="$TMP_ROOT/render.log"
if ! "$RENDERER" "$SCENE" "$OUTPUT" -rtPreview -renderPass /Render/Pass -stats \
    >"$LOG" 2>&1; then
  cat "$LOG" >&2
  exit 1
fi
python3 - "$OUTPUT" 41 31 <<'PY'
import struct, sys
path, ew, eh = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(path, "rb") as f:
    sig = f.read(24)
if sig[:8] != b"\x89PNG\r\n\x1a\n":
    raise SystemExit("not a PNG")
w, h = struct.unpack(">II", sig[16:24])
if (w, h) != (ew, eh):
    raise SystemExit(f"RenderProduct resolution ignored: {(w, h)} != {(ew, eh)}")
PY
grep -Eq 'triangles: 1$|rt triangles: 1$' "$LOG"

"$RENDERER" "$SCENE" "$TMP_ROOT/override.png" -rtPreview \
  -renderPass /Render/Pass -w 23 -height 17 >/dev/null 2>&1
python3 - "$TMP_ROOT/override.png" 23 17 <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as f:
    data = f.read(24)
w, h = struct.unpack(">II", data[16:24])
if (w, h) != (int(sys.argv[2]), int(sys.argv[3])):
    raise SystemExit("CLI resolution did not override RenderProduct")
PY

echo "PASS: UsdRender settings/product/pass selection"
