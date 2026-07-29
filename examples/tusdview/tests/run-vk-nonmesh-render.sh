#!/usr/bin/env bash
# Verify Vulkan raster keeps native Points/Curves visible through the dedicated
# camera-facing triangle-list carrier path.
set -uo pipefail
TUSDVIEW="${1:?usage: $0 /path/to/tusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/tusdview /repo/root}"
SKIP=77
TMP="$(mktemp -d /tmp/tusdview-vk-nonmesh.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

log="$(timeout 60s "$TUSDVIEW" --headless --backend vk --frames 2 \
  --screenshot "$TMP/nonmesh.ppm" \
  "$ROOT/tests/usda/tusdview-nonmesh-points-curves.usda" 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
  if grep -Eqi 'Vulkan.*(unavailable|failed)|no Vulkan|renderer init failed' <<<"$log"; then
    echo "SKIP: Vulkan backend unavailable"
    exit "$SKIP"
  fi
  echo "$log"
  echo "FAIL: Vulkan non-mesh render failed"
  exit 1
fi

python3 - "$TMP/nonmesh.ppm" <<'PY'
import sys
path = sys.argv[1]
with open(path, "rb") as f:
    if f.readline().strip() != b"P6":
        raise SystemExit("FAIL: expected binary PPM")
    width, height = map(int, f.readline().split())
    if int(f.readline()) != 255:
        raise SystemExit("FAIL: unexpected PPM range")
    pixels = f.read()
if len(pixels) != width * height * 3:
    raise SystemExit("FAIL: truncated Vulkan screenshot")
colored = sum(
    max(pixels[i:i + 3]) > 80 and
    max(pixels[i:i + 3]) - min(pixels[i:i + 3]) > 50
    for i in range(0, len(pixels), 3))
if colored < 800:
    raise SystemExit(f"FAIL: Vulkan non-mesh carriers absent ({colored} colored pixels)")
print(f"PASS: Vulkan non-mesh carriers rendered ({colored} colored pixels)")
PY
