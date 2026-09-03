#!/usr/bin/env bash
#
# Renderer-facing subdivision coverage: lusdrender's legacy render path must
# refine Catmull-Clark, bilinear, loop, and authored-crease meshes at shallow and
# deeper levels.
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

write_quad() {
  local path="$1" scheme="$2" extra="${3:-}"
  cat > "$path" <<USDA
#usda 1.0
def Mesh "World"
{
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0, 1, 2, 3]
    point3f[] points = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    uniform token subdivisionScheme = "$scheme"
$extra
}
USDA
}

write_loop() {
  local path="$1"
  cat > "$path" <<'USDA'
#usda 1.0
def Mesh "World"
{
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
    uniform token subdivisionScheme = "loop"
}
USDA
}

run_case() {
  local label="$1" asset="$2" level="$3" expected_tris="$4"
  local out="$TMP/${label}_l${level}.png" log
  log="$("$LUSDRENDER" "$asset" "$out" -w 48 -height 48 -subdiv "$level" -stats 2>&1)"
  echo "$log"
  if ! echo "$log" | grep -q "subdivision level: $level"; then
    echo "FAIL: $label did not report subdivision level $level"
    exit 1
  fi
  if ! echo "$log" | grep -q "triangles: $expected_tris"; then
    echo "FAIL: $label expected triangles: $expected_tris"
    exit 1
  fi
  if [ ! -s "$out" ]; then
    echo "FAIL: $label did not produce an output image"
    exit 1
  fi
  echo "PASS: $label level $level refined to $expected_tris triangles"
}

write_grid() {
  local path="$1" extra="${2:-}"
  cat > "$path" <<USDA
#usda 1.0
def Mesh "World"
{
    int[] faceVertexCounts = [4, 4, 4, 4, 4, 4, 4, 4, 4]
    int[] faceVertexIndices = [
        0, 1, 5, 4, 1, 2, 6, 5, 2, 3, 7, 6,
        4, 5, 9, 8, 5, 6, 10, 9, 6, 7, 11, 10,
        8, 9, 13, 12, 9, 10, 14, 13, 10, 11, 15, 14
    ]
    point3f[] points = [
        (0, 0, 0), (1, 0, 1.8), (2, 0, -1.2), (3, 0, 0),
        (0, 1, -1.2), (1, 1, 0), (2, 1, 2.2), (3, 1, -1.2),
        (0, 2, 1.8), (1, 2, -1.2), (2, 2, 0), (3, 2, 1.8),
        (0, 3, 0), (1, 3, 2.2), (2, 3, -1.2), (3, 3, 0)
    ]
    uniform token subdivisionScheme = "catmullClark"
$extra
}
USDA
}

render_grid() {
  local label="$1" asset="$2"
  "$LUSDRENDER" "$asset" "$TMP/${label}.png" -w 96 -height 96 -subdiv 2 \
    -viewDir 1.2,-0.8,0.45 -ambient 0.2 -samples 1 >"$TMP/${label}.log" 2>&1
  local rc=$?
  if [ "$rc" -ne 0 ] || [ ! -s "$TMP/${label}.png" ]; then
    echo "FAIL: $label grid render failed"
    cat "$TMP/${label}.log"
    exit 1
  fi
}

compare_grid_images() {
  python3 - "$REPO_ROOT/examples/lusdview/tests" "$TMP/grid_plain.png" "$TMP/grid_crease.png" <<'PY'
import sys

sys.path.insert(0, sys.argv[1])
import asset_fingerprint

a = asset_fingerprint._read_image(sys.argv[2])
b = asset_fingerprint._read_image(sys.argv[3])
if a is None or b is None:
    raise SystemExit("could not decode PNG")
if a[0] != b[0] or a[1] != b[1]:
    raise SystemExit("image sizes differ")
rgb_a = a[2]
rgb_b = b[2]
diff = sum(abs(x - y) for x, y in zip(rgb_a, rgb_b))
changed = sum(1 for i in range(0, len(rgb_a), 3)
              if abs(rgb_a[i] - rgb_b[i]) +
                 abs(rgb_a[i + 1] - rgb_b[i + 1]) +
                 abs(rgb_a[i + 2] - rgb_b[i + 2]) > 12)
print(diff, changed)
if diff < 500 or changed < 4:
    raise SystemExit(f"crease visual signal too small: diff={diff} changed={changed}")
PY
}

write_quad "$TMP/catmull.usda" "catmullClark"
write_quad "$TMP/bilinear.usda" "bilinear"
write_loop "$TMP/loop.usda"
write_quad "$TMP/crease.usda" "catmullClark" '    int[] creaseIndices = [0, 1]
    int[] creaseLengths = [2]
    float[] creaseSharpnesses = [10]'

run_case "catmull" "$TMP/catmull.usda" 1 8
run_case "bilinear" "$TMP/bilinear.usda" 1 8
run_case "loop" "$TMP/loop.usda" 1 4
run_case "crease" "$TMP/crease.usda" 1 8

run_case "catmull" "$TMP/catmull.usda" 2 32
run_case "bilinear" "$TMP/bilinear.usda" 2 32
run_case "loop" "$TMP/loop.usda" 2 16
run_case "crease" "$TMP/crease.usda" 2 32

write_grid "$TMP/grid_plain.usda"
write_grid "$TMP/grid_crease.usda" '    int[] creaseIndices = [5, 6]
    int[] creaseLengths = [2]
    float[] creaseSharpnesses = [1000]'
run_case "grid_plain" "$TMP/grid_plain.usda" 2 288
run_case "grid_crease" "$TMP/grid_crease.usda" 2 288
render_grid "grid_plain" "$TMP/grid_plain.usda"
render_grid "grid_crease" "$TMP/grid_crease.usda"
COMPARE="$(compare_grid_images)" || {
  echo "FAIL: non-planar crease grid did not produce a stable visual difference"
  exit 1
}
read -r DIFF CHANGED <<< "$COMPARE"
echo "PASS: non-planar level-2 crease visual diff=$DIFF changed_pixels=$CHANGED"
