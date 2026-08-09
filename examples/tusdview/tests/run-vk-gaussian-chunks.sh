#!/usr/bin/env bash
set -uo pipefail
TUSDVIEW="${1:?usage: $0 /path/to/tusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/tusdview /repo/root}"
SKIP=77
TMP="$(mktemp -d /tmp/tusdview-vk-gaussian-chunks.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

log="$(timeout 90s env TUSDVIEW_GAUSSIAN_CHUNK=4 "$TUSDVIEW" \
  --headless --backend vk --rt --frames 2 \
  --screenshot "$TMP/gaussian.ppm" \
  "$ROOT/tests/usda/tusdview-gaussian-chunks.usda" 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
  if grep -Eqi 'Vulkan.*(unavailable|failed)|no Vulkan|renderer init failed' <<<"$log"; then
    echo "SKIP: Vulkan backend unavailable"
    exit "$SKIP"
  fi
  echo "$log"
  echo "FAIL: Vulkan Gaussian chunk render failed"
  exit 1
fi
echo "$log"
if ! grep -Eq 'Gaussian point BVH: 12 point\(s\) in 3 chunk\(s\)' <<<"$log"; then
  echo "FAIL: expected three Gaussian chunks"
  exit 1
fi
if ! grep -q 'render stats' <<<"$log"; then
  echo "SKIP: Vulkan renderer unavailable"
  exit "$SKIP"
fi
if [ ! -s "$TMP/gaussian.ppm" ]; then
  echo "FAIL: no Gaussian screenshot written"
  exit 1
fi
python3 - "$TMP/gaussian.ppm" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    raise SystemExit("FAIL: expected binary PPM")
parts = data.split(None, 4)
if len(parts) != 5:
    raise SystemExit("FAIL: malformed PPM")
w, h, maxval = map(int, parts[1:4])
pixels = parts[4]
if len(pixels) < w * h * 3:
    raise SystemExit("FAIL: truncated Gaussian screenshot")
colored = sum(max(pixels[i:i+3]) - min(pixels[i:i+3]) > 40
              for i in range(0, w * h * 3, 3))
if colored < 200:
    raise SystemExit(f"FAIL: Gaussian chunks rendered blank ({colored} colored pixels)")
print(f"PASS: Gaussian chunks rendered ({colored} colored pixels)")
PY
