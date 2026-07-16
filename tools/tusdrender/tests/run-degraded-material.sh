#!/usr/bin/env bash
#
# tusdrender next-loader degraded-material regression. The shared Tydra material
# resolver is allowed to fall back to the legacy resolver for an unsupported
# shader, but the fallback must be reported in the structured load summary.
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
ASSET="$TMP/degraded_material.usda"
OUT="$TMP/degraded_material.png"
LOG="$TMP/degraded_material.log"

cat > "$ASSET" <<'USDA'
#usda 1.0
( upAxis = "Y" )
def Xform "World" {
  def Mesh "M" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
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
        token outputs:surface
      }
    }
  }
}
USDA

"$TUSDRENDER" "$ASSET" "$OUT" -rtPreview --materialResolver tydra-next \
  -w 64 -height 64 -autoframe -viewDir 0,0,-1 -ambient 1 -noShadows \
  -samples 1 -stats >"$LOG" 2>&1
rc=$?
cat "$LOG"
if [ "$rc" -ne 0 ]; then
  echo "FAIL: tusdrender exited with $rc"
  exit 1
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: tusdrender produced no image"
  exit 1
fi
if ! grep -Eq '^triangles: 2$' "$LOG"; then
  echo "FAIL: expected quad to render as two triangles"
  exit 1
fi
if ! grep -Eq 'load summary:.*degraded_materials=[1-9]' "$LOG"; then
  echo "FAIL: degraded material was not reported in the load summary"
  exit 1
fi

echo "PASS: tusdrender reports shared-resolver material fallback"
