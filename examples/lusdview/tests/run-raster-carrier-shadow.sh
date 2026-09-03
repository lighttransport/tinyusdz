#!/usr/bin/env bash
set -uo pipefail
SKIP=77
BIN="${1:-}"
ROOT="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
[ -x "$BIN" ] || { echo "SKIP: lusdview not found"; exit "$SKIP"; }
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT/config"
cp "$ROOT/tests/usda/lusdview-raster-carrier-shadow.usda" "$OUT/linked.usda"
cp "$OUT/linked.usda" "$OUT/all.usda"
sed -i '/collection:shadowLink/d' "$OUT/all.usda"
cp "$OUT/all.usda" "$OUT/off.usda"
sed -i 's/inputs:shadow:enable = 1/inputs:shadow:enable = 0/' "$OUT/off.usda"

XVFB=""
if { [ -z "${DISPLAY:-}" ] || ! command -v xdpyinfo >/dev/null 2>&1 ||
     ! xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; } &&
   command -v xvfb-run >/dev/null 2>&1; then
  XVFB="xvfb-run -a"
fi

compare_images() {
python3 - "$1" "$2" "$3" <<'PY'
import sys

def ppm(path):
    data = open(path, "rb").read()
    magic, sw, sh, maximum, pixels = data.split(None, 4)
    if magic != b"P6" or maximum != b"255":
        raise SystemExit(2)
    return int(sw), int(sh), pixels

wa, ha, a = ppm(sys.argv[1])
wb, hb, b = ppm(sys.argv[2])
if (wa, ha) != (wb, hb):
    raise SystemExit(2)
changed = 0
strongest = 0
for i in range(0, min(len(a), len(b)), 3):
    la = (a[i] * 299 + a[i + 1] * 587 + a[i + 2] * 114) // 1000
    lb = (b[i] * 299 + b[i + 1] * 587 + b[i + 2] * 114) // 1000
    delta = lb - la
    if delta >= 18:
        changed += 1
        strongest = max(strongest, delta)
print(changed, strongest, sys.argv[3])
if changed < 120 or strongest < 30:
    raise SystemExit(1)
PY
}

ran=0
fail=0
for spec in "gl:--backend gl:" "vk:--backend vk:--headless"; do
  tag="${spec%%:*}"; rest="${spec#*:}"; args="${rest%%:*}"; hl="${rest#*:}"
  available=1
  for variant in off linked all; do
    # shellcheck disable=SC2086
    timeout --kill-after=5s 30s $XVFB env XDG_CONFIG_HOME="$OUT/config" \
      "$BIN" $hl $args --frames 2 \
      --size 640x480 --no-grid --camera Cam --screenshot "$OUT/$tag-$variant.ppm" \
      "$OUT/$variant.usda" >"$OUT/$tag-$variant.log" 2>&1
    if ! grep -q 'render stats' "$OUT/$tag-$variant.log"; then
      available=0
      break
    fi
  done
  [ "$available" -eq 1 ] || { echo "SKIP: $tag backend unavailable"; continue; }
  ran=$((ran + 1))
  compare_images "$OUT/$tag-linked.ppm" "$OUT/$tag-off.ppm" \
    "$tag linked-point shadow" || fail=1
  compare_images "$OUT/$tag-all.ppm" "$OUT/$tag-linked.ppm" \
    "$tag excluded-curve shadowLink" || fail=1
done

[ "$ran" -gt 0 ] || exit "$SKIP"
[ "$fail" -eq 0 ] || exit 1
echo "PASS: raster point/curve carrier shadows and shadow links on $ran backend(s)"
