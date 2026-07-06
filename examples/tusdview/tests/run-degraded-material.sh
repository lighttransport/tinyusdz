#!/usr/bin/env bash
#
# Regression test for the renderer-parity degraded-material policy: a material
# that fails to convert (here an unknown shader info:id) must NOT sink the whole
# load. The geometry should still render with the substituted default material
# (loadable), and tusdview must emit a structured load summary reporting
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
        token outputs:surface
      }
    }
  }
}
USDA

RUN=()
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a)
fi

log="$("${RUN[@]}" "$TUSDVIEW" --backend "$BACKEND" --frames 2 \
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

echo "PASS: broken material rendered with default + reported as degraded_material"
