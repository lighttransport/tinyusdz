#!/usr/bin/env bash
set -euo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${TUSDVIEW:-$REPO_ROOT/build_ninja/tusdview}"
ASSET="$REPO_ROOT/tests/usda/tusdview-materialx-swizzle.usda"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }

OUT="${TUSDVIEW_TEST_OUT:-$(mktemp -d)}"
mkdir -p "$OUT"
if [ -z "${TUSDVIEW_TEST_OUT:-}" ]; then trap 'rm -rf "$OUT"' EXIT; fi
IMAGE="$OUT/cpu-rt-materialx-swizzle.ppm"
LOG="$OUT/cpu-rt-materialx-swizzle.log"

timeout --kill-after=5s "${TUSDVIEW_RENDER_TIMEOUT:-120s}" \
  "$BIN" --headless --backend vk --cpu-rt --frames 1 --size 192x96 \
  --view-dir 0,0,-1 --screenshot "$IMAGE" "$ASSET" >"$LOG" 2>&1

grep -q 'CPU RT wrote' "$LOG" || {
  echo "FAIL: CPU RT did not render the MaterialX swizzle fixture"
  tail -30 "$LOG"
  exit 1
}

python3 - "$IMAGE" <<'PY'
import re
import sys

data = open(sys.argv[1], "rb").read()
match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
if not match or int(match.group(3)) != 255:
    sys.exit("invalid CPU RT PPM")
width, height = int(match.group(1)), int(match.group(2))
rgb = data[match.end():]
pixels = [rgb[i:i + 3] for i in range(0, len(rgb) - 2, 3)]
sample = [p for p in pixels if max(p) >= 2]
if len(sample) < width * height // 8:
    sys.exit("CPU RT swizzle fixture has too little foreground")
mean = tuple(sum(p[c] for p in sample) / len(sample) for c in range(3))
print("CPU RT MaterialX bgr mean=%.1f,%.1f,%.1f" % mean)
if not (mean[0] > max(mean[1], mean[2]) + 8.0):
    sys.exit("CPU RT did not execute typed bgr swizzle")
PY

echo "PASS: CPU RT typed MaterialX swizzle"
