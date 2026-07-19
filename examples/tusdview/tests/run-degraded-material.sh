#!/usr/bin/env bash
#
# Regression test for the renderer-parity degraded-material policy: a material
# that fails to convert (here an unknown shader info:id) must NOT sink the whole
# load. The geometry should still render with the per-material degraded surface
# (loadable), preserve recognizable authored constants, and emit a load summary
# degraded_materials>=1 so the smoke harness can fail on it. Guards against
# either regressing: a silent full-load failure (mesh disappears) or the
# degraded material going unreported.
#
# Exit codes: 0 = pass, 1 = fail, 77 = skip.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TUSDVIEW="${TUSDVIEW:-$SCRIPT_DIR/../../../build/tusdview}"
BACKEND="${BACKEND:-gl}"

if [ ! -x "$TUSDVIEW" ]; then
  echo "SKIP: tusdview binary not found at $TUSDVIEW (set TUSDVIEW=...)"
  exit $SKIP
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
ASSET="$TMP_DIR/degraded_material.usda"

cat > "$ASSET" <<'USDA'
#usda 1.0
( upAxis = "Y" )
def Xform "World" {
  def Mesh "M" ( prepend apiSchemas = ["MaterialBindingAPI"] ) {
    point3f[] points = [(-1,-1,0),(1,-1,0),(1,1,0),(-1,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
    rel material:binding = </World/Mats/Broken>
  }
  def Scope "Mats" {
    def Material "Broken" {
      token outputs:surface.connect = </World/Mats/Broken/S.outputs:surface>
      def Shader "S" {
        uniform token info:id = "SomeUnknownShaderType_xyz"
        color3f inputs:baseColor = (0.8, 0.1, 0.05)
        float inputs:roughness = 0.25
        token outputs:surface
      }
    }
  }
}
USDA

RUN=()
if command -v xvfb-run >/dev/null 2>&1; then RUN=(xvfb-run -a); fi

log="$("${RUN[@]}" "$TUSDVIEW" --headless --backend "$BACKEND" --frames 2 \
       --screenshot "$TMP_DIR/out.ppm" "$ASSET" 2>&1)"
echo "$log"

if ! echo "$log" | grep -q "render stats"; then
  echo "SKIP: no renderer (backend '$BACKEND' unavailable in this environment)"
  exit $SKIP
fi

# The mesh must still render despite the broken material (degraded, not fatal).
if ! echo "$log" | grep -Eq "render stats: meshes 1/1 visible"; then
  echo "FAIL: geometry did not render — a broken material sank the whole load."
  exit 1
fi

# The degraded material must be reported in the structured load summary.
if ! echo "$log" | grep -Eq "load summary:.*degraded_materials=[1-9]"; then
  echo "FAIL: degraded material was not reported in the load summary."
  exit 1
fi
if ! echo "$log" | grep -Fq "/World/Mats/Broken/S" ||
   ! echo "$log" | grep -Fq "SomeUnknownShaderType_xyz"; then
  echo "FAIL: degraded-material diagnostic omitted its shader path or ID."
  exit 1
fi

# The recovered baseColor is strongly red. A shared gray/default fallback has
# no red-dominant surface pixels, so this also verifies that tusdview consumes
# the degraded material instead of merely keeping the geometry alive.
python3 - "$TMP_DIR/out.ppm" <<'PY'
import sys

data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    print("FAIL: screenshot is not a binary PPM")
    sys.exit(1)
i, tokens = 2, []
while len(tokens) < 3 and i < len(data):
    if data[i] == 35:
        while i < len(data) and data[i] not in (10, 13):
            i += 1
    elif chr(data[i]).isspace():
        i += 1
    else:
        start = i
        while i < len(data) and not chr(data[i]).isspace():
            i += 1
        tokens.append(int(data[start:i]))
if len(tokens) != 3:
    print("FAIL: malformed PPM header")
    sys.exit(1)
i += 1
pixels = data[i:]
red = 0
for p in range(0, len(pixels) - 2, 3):
    r, g, b = pixels[p], pixels[p + 1], pixels[p + 2]
    if r > 24 and r > 2 * g and r > 2 * b:
        red += 1
if red < 20:
    print(f"FAIL: degraded baseColor was not rendered ({red} red pixels)")
    sys.exit(1)
print(f"OK: recovered degraded baseColor rendered ({red} red pixels)")
PY
rc=$?
[ "$rc" -eq 0 ] || exit 1

echo "PASS: degraded material preserved constants, geometry, and diagnostics"
