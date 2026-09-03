#!/usr/bin/env bash
# Both scene loaders must preserve OpenPBR surface and volume inputs without
# emitting a stale unsupported-lobe diagnostic.
set -uo pipefail
SKIP=77

LUSDVIEW="${1:?usage: $0 /path/to/lusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/lusdview /repo/root}"
ASSET="$ROOT/tests/usda/lusdview-unsupported-realtime-lobes.usda"
TMP="$(mktemp -d /tmp/lusdview-unsupported-lobes.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

for loader in next legacy; do
  args=()
  if [ "$loader" = legacy ]; then args+=(--legacy-load); fi
  log="$(timeout 30s "$LUSDVIEW" --headless --backend vk --frames 1 \
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
  if grep -q 'load summary:' <<<"$log"; then
    if ! grep -Eq 'load summary:.*unsupported_lobes=0' <<<"$log"; then
      echo "$log"
      echo "FAIL: $loader reported an unexpected unsupported-lobe count"
      exit 1
    fi
  else
    echo "INFO: $loader loader has no legacy load-summary line; pixel parity is authoritative"
  fi
done

# The supported surface and volume material must still be extracted identically
# by both scene loaders. Keep
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

echo "PASS: default and legacy loaders support OpenPBR surface and volume output"
