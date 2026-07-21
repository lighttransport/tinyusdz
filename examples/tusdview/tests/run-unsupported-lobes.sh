#!/usr/bin/env bash
# Both scene loaders must preserve supported OpenPBR inputs while reporting
# advanced lobes that the real-time evaluators currently omit.
set -uo pipefail
SKIP=77

TUSDVIEW="${1:?usage: $0 /path/to/tusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/tusdview /repo/root}"
ASSET="$ROOT/tests/usda/tusdview-unsupported-realtime-lobes.usda"
TMP="$(mktemp -d /tmp/tusdview-unsupported-lobes.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

for loader in next legacy; do
  args=()
  if [ "$loader" = legacy ]; then args+=(--legacy-load); fi
  log="$(timeout 30s "$TUSDVIEW" --headless --backend vk --frames 1 \
    --screenshot "$TMP/$loader.ppm" "${args[@]}" "$ASSET" 2>&1)"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    if grep -Eiq 'no Vulkan|Vulkan.*unavailable|renderer init failed|no suitable GPU' \
        <<<"$log"; then
      echo "SKIP: Vulkan backend unavailable for $loader loader"
      exit "$SKIP"
    fi
    echo "$log"
    echo "FAIL: $loader unsupported-lobe load failed"
    exit 1
  fi
  if ! grep -Eq 'load summary:.*unsupported_lobes=1' <<<"$log"; then
    echo "$log"
    echo "FAIL: $loader did not report one structured unsupported-lobe record"
    exit 1
  fi
  for lobe in transmission subsurface sheen/fuzz thin-film anisotropy; do
    if ! grep -Fq "$lobe" <<<"$log"; then
      echo "$log"
      echo "FAIL: $loader diagnostic omitted $lobe"
      exit 1
    fi
  done
done

# The advanced lobes stay intentionally diagnosed/degraded, but their supported
# base material must still be extracted identically by both scene loaders. Keep
# this a modest decoded-pixel tolerance: it catches a material-record or texture
# binding drift without depending on byte-identical driver math.
python3 - "$TMP/next.ppm" "$TMP/legacy.ppm" <<'PY'
import re, sys

def read_ppm(path):
    data = open(path, "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m or int(m.group(3)) != 255:
        raise SystemExit(f"FAIL: invalid PPM {path}")
    w, h = int(m.group(1)), int(m.group(2))
    pix = data[m.end():]
    if len(pix) != w * h * 3:
        raise SystemExit(f"FAIL: truncated PPM {path}")
    return pix

a, b = read_ppm(sys.argv[1]), read_ppm(sys.argv[2])
mean = sum(abs(x - y) for x, y in zip(a, b)) / len(a)
if mean > 2.0:
    raise SystemExit(f"FAIL: next/legacy supported-material drift (mean={mean:.3f})")
print(f"PASS: next/legacy supported-material parity mean={mean:.3f}")
PY

echo "PASS: default and legacy loaders diagnose unsupported real-time lobes"
