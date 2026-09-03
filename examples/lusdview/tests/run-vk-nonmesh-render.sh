#!/usr/bin/env bash
# Verify Vulkan raster keeps native Points/Curves visible through the dedicated
# camera-facing triangle-list carrier path.
set -uo pipefail
LUSDVIEW="${1:?usage: $0 /path/to/lusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/lusdview /repo/root}"
SKIP=77
TMP="$(mktemp -d /tmp/lusdview-vk-nonmesh.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

render_case() {
  local output="$1"
  shift
  local log
  log="$(timeout 60s "$@" "$LUSDVIEW" --headless --backend vk --frames 2 \
    --curve-preview-prims 8 --screenshot "$output" \
    "$ROOT/tests/usda/lusdview-nonmesh-points-curves.usda" 2>&1)"
  local rc=$?
  if [ "$rc" -ne 0 ]; then
    if grep -Eqi 'Vulkan.*(unavailable|failed)|no Vulkan|renderer init failed' <<<"$log"; then
      echo "SKIP: Vulkan backend unavailable"
      exit "$SKIP"
    fi
    echo "$log"
    echo "FAIL: Vulkan non-mesh render failed"
    exit 1
  fi
}

render_case "$TMP/nonmesh.ppm" env
render_case "$TMP/nonmesh-cpu.ppm" env LUSDVIEW_VK_CPU_CARRIERS=1

python3 - "$TMP/nonmesh.ppm" "$TMP/nonmesh-cpu.ppm" <<'PY'
import sys
for path in sys.argv[1:]:
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
        raise SystemExit(
            f"FAIL: Vulkan non-mesh carriers absent in {path} "
            f"({colored} colored pixels)")
    print(f"PASS: Vulkan non-mesh carriers rendered in {path} "
          f"({colored} colored pixels)")
PY
