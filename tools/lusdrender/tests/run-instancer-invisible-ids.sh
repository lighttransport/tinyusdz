#!/usr/bin/env bash
#
# Regression test: PointInstancer invisibleIds are authored ids when an `ids`
# array exists. The tiny scene has four instances of a one-triangle prototype,
# with ids=[10,20,30,40], invisibleIds=[30], and inactiveIds=[40]. Correct
# expansion emits two instances / two triangles; index-only masking emits four.
# A second scene repeats this through a nested PointInstancer under an outer
# prototype to cover recursive expansion.
#
# Exit codes: 0 = pass, 1 = fail, 77 = skip.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDRENDER="${1:-${LUSDRENDER:-$REPO_ROOT/build_ninja/tools/lusdrender/lusdrender}}"

if [ ! -x "$LUSDRENDER" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER"
  exit $SKIP
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
TOP_ASSET="$TMP/pointinstancer_invisible_ids.usda"
NESTED_ASSET="$TMP/nested_pointinstancer_invisible_ids.usda"

cat > "$TOP_ASSET" <<'USDA'
#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Z"
)

def Xform "World"
{
    def PointInstancer "PI"
    {
        int64[] ids = [10, 20, 30, 40]
        int64[] invisibleIds = [30]
        int64[] inactiveIds = [40]
        point3f[] positions = [(-1.5, 0, 0), (-0.5, 0, 0), (0.5, 0, 0), (1.5, 0, 0)]
        int[] protoIndices = [0, 0, 0, 0]
        rel prototypes = [</World/PI/Proto>]

        def Mesh "Proto"
        {
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            point3f[] points = [(-0.2, -0.2, 0), (0.2, -0.2, 0), (0, 0.2, 0)]
        }
    }
}
USDA

cat > "$NESTED_ASSET" <<'USDA'
#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Z"
)

def Xform "World"
{
    def PointInstancer "Outer"
    {
        point3f[] positions = [(0, 0, 0)]
        int[] protoIndices = [0]
        rel prototypes = [</World/Outer/ProtoRoot>]

        def Xform "ProtoRoot"
        {
            def PointInstancer "Inner"
            {
                int64[] ids = [10, 20, 30, 40]
                int64[] invisibleIds = [30]
                int64[] inactiveIds = [40]
                point3f[] positions = [(-1.5, 0, 0), (-0.5, 0, 0), (0.5, 0, 0), (1.5, 0, 0)]
                int[] protoIndices = [0, 0, 0, 0]
                rel prototypes = [</World/Outer/ProtoRoot/Inner/Proto>]

                def Mesh "Proto"
                {
                    int[] faceVertexCounts = [3]
                    int[] faceVertexIndices = [0, 1, 2]
                    point3f[] points = [(-0.2, -0.2, 0), (0.2, -0.2, 0), (0, 0.2, 0)]
                }
            }
        }
    }
}
USDA

run_case() {
  local label="$1"
  local asset="$2"
  local nested_expected="$3"
  local out="$TMP/${label}.png"
  local log
  log="$("$LUSDRENDER" "$asset" "$out" -rtPreview -stats -w 64 -height 64 \
         -autoframe -samples 1 2>&1)"
  echo "$log"

  if ! echo "$log" | grep -q "rt loader: next"; then
    echo "FAIL: $label expected next rtPreview loader"
    exit 1
  fi

  if ! echo "$log" | grep -q "rt instances: 2"; then
    echo "FAIL: $label expected 'rt instances: 2'"
    exit 1
  fi

  if ! echo "$log" | grep -q "triangles: 2"; then
    echo "FAIL: $label expected 'triangles: 2'"
    exit 1
  fi

  if [ "$nested_expected" != "-" ] &&
     ! echo "$log" | grep -q "rt nested instances: $nested_expected"; then
    echo "FAIL: $label expected 'rt nested instances: $nested_expected'"
    exit 1
  fi

  if [ ! -s "$out" ]; then
    echo "FAIL: $label lusdrender did not write an output image"
    exit 1
  fi

  echo "PASS: $label hidden ids matched authored ids; emitted 2 instances"
}

run_case "top-level" "$TOP_ASSET" "-"
run_case "nested" "$NESTED_ASSET" "2"
