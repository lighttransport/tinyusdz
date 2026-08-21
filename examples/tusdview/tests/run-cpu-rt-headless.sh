#!/usr/bin/env bash
# Headless CPU-RT screenshot and stacked-alpha regression.
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${TUSDVIEW:-$REPO_ROOT/build_ninja/tusdview}"
ASSET="${ASSET:-$REPO_ROOT/models/tusdview-transparency.usda}"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }

OUT="${TUSDVIEW_TEST_OUT:-$(mktemp -d)}"
mkdir -p "$OUT"
if [ -z "${TUSDVIEW_TEST_OUT:-}" ]; then trap 'rm -rf "$OUT"' EXIT; fi
IMAGE="$OUT/cpu-rt.ppm"
LOG="$OUT/cpu-rt.log"

if command -v timeout >/dev/null 2>&1; then
  timeout --kill-after=5s "${TUSDVIEW_RENDER_TIMEOUT:-120s}" \
    "$BIN" --headless --backend vk --cpu-rt --frames 1 \
    --view-dir 0,0,-1 --screenshot "$IMAGE" \
    "$ASSET" >"$LOG" 2>&1
else
  "$BIN" --headless --backend vk --cpu-rt --frames 1 \
    --view-dir 0,0,-1 --screenshot "$IMAGE" \
    "$ASSET" >"$LOG" 2>&1
fi

grep -q 'CPU RT wrote' "$LOG" || {
  echo "FAIL: --cpu-rt did not own the headless screenshot"
  tail -30 "$LOG"
  exit 1
}
[ -s "$IMAGE" ] || { echo "FAIL: CPU RT screenshot is missing"; exit 1; }

python3 - "$IMAGE" <<'PY'
import re, sys
d = open(sys.argv[1], "rb").read()
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", d)
if not m or int(m.group(3)) != 255:
    sys.exit(2)
p = d[m.end():]
colored = mixed = 0
for i in range(0, len(p) - 2, 3):
    r, g, b = p[i:i + 3]
    if max(r, g, b) - min(r, g, b) > 20:
        colored += 1
    if sum(v > 35 for v in (r, g, b)) >= 2 and max(r, g, b) - min(r, g, b) > 15:
        mixed += 1
print(f"cpu-rt colored={colored} mixed={mixed}")
if colored < 500 or mixed < 100:
    sys.exit(1)
PY

echo "PASS: headless CPU RT stacked transparency"

# The same deterministic fixture also exercises Vulkan's ray-query path when
# available, or its compute-BVH/SWBVH fallback on software Vulkan devices.
# Keep this opt-out for minimal CI images that only build the CPU tracer.
if [ "${TUSDVIEW_STACKED_VKRT:-0}" = "1" ]; then
  VK_IMAGE="$OUT/vk-rt.ppm"
  VK_LOG="$OUT/vk-rt.log"
  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5s "${TUSDVIEW_RENDER_TIMEOUT:-120s}" \
      "$BIN" --headless --backend vk --rt --frames 1 \
      --view-dir 0,0,-1 --screenshot "$VK_IMAGE" \
      "$ASSET" >"$VK_LOG" 2>&1
  else
    "$BIN" --headless --backend vk --rt --frames 1 \
      --view-dir 0,0,-1 --screenshot "$VK_IMAGE" \
      "$ASSET" >"$VK_LOG" 2>&1
  fi
  grep -q 'wrote .*vk-rt.ppm' "$VK_LOG" || {
    echo "FAIL: Vulkan RT did not own the stacked-transparency screenshot"
    tail -30 "$VK_LOG"
    exit 1
  }
  [ -s "$VK_IMAGE" ] || { echo "FAIL: Vulkan RT screenshot is missing"; exit 1; }
  python3 - "$VK_IMAGE" <<'PY'
import re, sys
d = open(sys.argv[1], "rb").read()
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", d)
if not m or int(m.group(3)) != 255:
    sys.exit(2)
p = d[m.end():]
colored = sum(1 for i in range(0, len(p) - 2, 3)
             if max(p[i:i+3]) - min(p[i:i+3]) > 20)
if colored < 500:
    print(f"vk-rt colored={colored}")
    sys.exit(1)
print(f"vk-rt colored={colored}")
PY
  echo "PASS: Vulkan RT/SWBVH stacked transparency"
fi
