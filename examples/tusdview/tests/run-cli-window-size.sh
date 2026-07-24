#!/usr/bin/env bash
# Verify that --size controls headless capture dimensions and takes precedence
# over a conflicting saved window size.
set -uo pipefail

TUSDVIEW="${1:?usage: $0 /path/to/tusdview}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/config.json" <<'JSON'
{
  "window_size": {
    "width": 32,
    "height": 530
  }
}
JSON

cat >"$TMP/triangle.usda" <<'USDA'
#usda 1.0
def Mesh "Triangle"
{
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0, 1, 2]
    point3f[] points = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
}
USDA

log="$("$TUSDVIEW" --headless --backend vk --config "$TMP/config.json" \
  --frames 2 --size 64x48 --screenshot "$TMP/out.ppm" \
  "$TMP/triangle.usda" 2>&1)"
rc=$?

if [ "$rc" -ne 0 ] ||
   grep -Eiq 'renderer init failed|Vulkan backend unavailable|Vulkan unavailable|no Vulkan physical device|Failed to create Vulkan' <<<"$log"; then
  echo "SKIP: Vulkan headless rendering unavailable"
  exit 77
fi
if [ ! -s "$TMP/out.ppm" ]; then
  echo "FAIL: --size run did not write a screenshot"
  echo "$log"
  exit 1
fi

python3 - "$TMP/out.ppm" <<'PY'
import sys

with open(sys.argv[1], "rb") as f:
    if f.readline().strip() != b"P6":
        raise SystemExit("FAIL: screenshot is not a binary PPM")
    dims = f.readline().split()
if dims != [b"64", b"48"]:
    raise SystemExit(
        f"FAIL: --size did not override config: got {dims!r}, expected 64x48"
    )
PY

echo "PASS: --size overrides saved window dimensions"
