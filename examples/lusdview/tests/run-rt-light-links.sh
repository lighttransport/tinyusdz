#!/usr/bin/env bash
# Prove Vulkan ray-query honors per-mesh light-link collections. The fixture's
# left panel receives red, center receives red+blue, and right receives blue.
set -uo pipefail

LUSDVIEW="${1:?usage: $0 /path/to/lusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/lusdview /repo/root}"
SKIP=77
TMP="$(mktemp -d /tmp/lusdview-rt-light-links.XXXXXX)"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT
printf '%s\n' '{"window_size":{"width":320,"height":240}}' >"$TMP/config.json"
asset="$ROOT/tests/usda/lusdview-raster-multilight-links.usda"

log="$(timeout 120s "$LUSDVIEW" --config "$TMP/config.json" --headless \
  --backend vk --rt --camera /World/Camera --frames 1 \
  --screenshot "$TMP/vkrt.ppm" "$asset" 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
  if grep -Eqi 'Vulkan.*(unavailable|failed)|no Vulkan|renderer init failed|ray query unavailable' <<<"$log"; then
    echo "SKIP: Vulkan ray query unavailable"
    exit "$SKIP"
  fi
  echo "$log"
  echo "FAIL: Vulkan RT linked-light render failed"
  exit 1
fi
if ! grep -q 'caps: v1 .*rt=hardware' <<<"$log"; then
  echo "SKIP: Vulkan ray query was not enabled"
  exit "$SKIP"
fi

python3 - "$TMP/vkrt.ppm" <<'PY'
import sys

path = sys.argv[1]
with open(path, "rb") as f:
    if f.readline().strip() != b"P6":
        raise SystemExit("FAIL: Vulkan RT output is not binary PPM")
    width, height = map(int, f.readline().split())
    if int(f.readline()) != 255:
        raise SystemExit("FAIL: Vulkan RT output has an unexpected range")
    data = f.read()
if len(data) != width * height * 3:
    raise SystemExit("FAIL: Vulkan RT output is truncated")

pixels = [tuple(data[i:i + 3]) for i in range(0, len(data), 3)]
groups = {"red": 0, "blue": 0, "magenta": 0}
for r, g, b in pixels:
    if r > 1.35 * g and r > 1.35 * b:
        groups["red"] += 1
    elif b > 1.35 * r and b > 1.35 * g:
        groups["blue"] += 1
    elif r > 1.35 * g and b > 1.35 * g:
        groups["magenta"] += 1
for name, count in groups.items():
    if count < 200:
        raise SystemExit(
            f"FAIL: Vulkan RT has only {count} {name} pixels; "
            "per-instance light linking is missing"
        )
print("PASS: Vulkan RT linked multi-light", groups)
PY
