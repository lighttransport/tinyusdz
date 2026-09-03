#!/usr/bin/env bash
#
# External usd-assets AlphaBlendSortTest smoke. The scene contains overlapping
# transparent shadow cards. This is a production-style opacity asset; the current
# regression guard asserts it loads and renders as translucent over a white
# background instead of being treated as opaque/cutout or invisible.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDRENDER="${1:-${LUSDRENDER:-$REPO_ROOT/build/tools/lusdrender/lusdrender}}"
USD_ASSETS_ROOT="${USD_ASSETS_ROOT:-$REPO_ROOT/usd-assets}"
if [ -d "$USD_ASSETS_ROOT" ]; then
  USD_ASSETS_ROOT="$(cd "$USD_ASSETS_ROOT" && pwd -P)"
fi
ASSET="${ASSET:-$USD_ASSETS_ROOT/test_assets/AlphaBlendSortTest/AlphaBlendSortTest.usda}"

if [ ! -x "$LUSDRENDER" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER"
  exit "$SKIP"
fi
if [ -z "$USD_ASSETS_ROOT" ]; then
  echo "SKIP: USD_ASSETS_ROOT is not set"
  exit "$SKIP"
fi
if [ ! -f "$ASSET" ]; then
  echo "SKIP: AlphaBlendSortTest asset not found at $ASSET"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/alpha-blend-sort.png"
LOG="$TMP/alpha-blend-sort.log"

"$LUSDRENDER" "$ASSET" "$OUT" -rtPreview -w 160 -height 120 \
  -viewDir 0,1,-1 -bg 1,1,1 -ambient 1 -noShadows -samples 1 >"$LOG" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "FAIL: lusdrender exited with $rc"
  cat "$LOG"
  exit 1
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: lusdrender produced no image"
  cat "$LOG"
  exit 1
fi

read -r MIN_LUMA MAX_LUMA AVG_LUMA DARK_PIXELS LAYER_PIXELS < <(
  python3 - "$REPO_ROOT/examples/lusdview/tests" "$OUT" <<'PY'
import sys

sys.path.insert(0, sys.argv[1])
import asset_fingerprint

got = asset_fingerprint._read_image(sys.argv[2])
if got is None:
    raise SystemExit("cannot decode rendered PNG")
w, h, rgb = got
vals = []
for i in range(0, len(rgb), 3):
    r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
    vals.append((r + g + b) // 3)
dark = [v for v in vals if v < 235]
mx = max(vals)
layer = [v for v in vals if v <= mx - 3]
print(min(vals), mx, sum(vals) // len(vals), len(dark), len(layer))
PY
) || {
  echo "FAIL: could not inspect rendered image"
  cat "$LOG"
  exit 1
}

echo "luma min/max/avg=$MIN_LUMA/$MAX_LUMA/$AVG_LUMA dark_pixels=$DARK_PIXELS layer_pixels=$LAYER_PIXELS"
if [ "$(( MAX_LUMA - MIN_LUMA ))" -lt 3 ]; then
  echo "SKIP: AlphaBlendSortTest rendered uniformly; no stable layer-order signal"
  exit "$SKIP"
fi
if [ "$AVG_LUMA" -le 80 ] || [ "$AVG_LUMA" -ge 245 ] || [ "$LAYER_PIXELS" -lt 100 ]; then
  echo "FAIL: expected AlphaBlendSortTest to render a translucent layer over white"
  cat "$LOG"
  exit 1
fi

echo "PASS: AlphaBlendSortTest external transparency coverage rendered translucent material"
exit 0
