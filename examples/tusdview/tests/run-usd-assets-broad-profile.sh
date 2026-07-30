#!/usr/bin/env bash
# Self-contained regression for broad-profile selection and time-code forwarding.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

ROOT="$TMP/usd-assets"
mkdir -p "$ROOT/docs/PrimvarInterpolation"
mkdir -p "$ROOT/test_assets/USDZ/AnimatedCube"
: > "$ROOT/docs/PrimvarInterpolation/primvar_interpolation.usda"
: > "$ROOT/test_assets/USDZ/AnimatedCube/AnimatedCube.usdz"

FAKE="$TMP/tusdview"
cat > "$FAKE" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$TUSDVIEW_FAKE_ARGS"
out=""
time=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --screenshot) out="$2"; shift 2 ;;
    --time) time="$2"; shift 2 ;;
    *) shift ;;
  esac
done
[ -n "$out" ] || exit 2
if [ "$time" = "0" ]; then
  printf 'P6\n2 1\n255\n\000\000\000\377\377\377' > "$out"
else
  printf 'P6\n2 1\n255\n\377\000\000\000\377\000' > "$out"
fi
echo 'render stats: meshes 1/1 visible, instances 0/0 visible, drawn tris 1'
SH
chmod +x "$FAKE"

ARGS="$TMP/args.txt"
OUT="$TMP/out"
TUSDVIEW="$FAKE" \
TUSDVIEW_FAKE_ARGS="$ARGS" \
TUSDVIEW_XVFB=0 \
USD_ASSETS_ROOT="$ROOT" \
  bash "$SCRIPT_DIR/run-usd-assets-render-smoke.sh" \
    --profile usd-assets-broad --modes vk-raster --time 7 --out "$OUT"

[ "$(awk 'NR > 1 && $2 == "rendered" { n++ } END { print n+0 }' "$OUT/results.tsv")" -eq 2 ]
[ "$(grep -c -- '--time 7' "$ARGS")" -eq 2 ]
grep -q 'docs/PrimvarInterpolation/primvar_interpolation.usda' "$OUT/results.tsv"
grep -q 'test_assets/USDZ/AnimatedCube/AnimatedCube.usdz' "$OUT/results.tsv"

if TUSDVIEW="$FAKE" TUSDVIEW_FAKE_ARGS="$ARGS" TUSDVIEW_XVFB=0 \
   USD_ASSETS_ROOT="$ROOT" \
   bash "$SCRIPT_DIR/run-usd-assets-render-smoke.sh" \
     --profile usd-assets-broad --require-profile-complete \
     --modes vk-raster --out "$TMP/incomplete" >"$TMP/incomplete.log" 2>&1; then
  echo 'FAIL: incomplete broad profile was accepted' >&2
  exit 1
fi
grep -q "profile 'usd-assets-broad' is incomplete" "$TMP/incomplete.log"

TUSDVIEW="$FAKE" \
TUSDVIEW_FAKE_ARGS="$ARGS" \
TUSDVIEW_XVFB=0 \
USD_ASSETS_ROOT="$ROOT" \
TUSDVIEW_USD_ASSETS_ANIM_MODES=vk-raster \
TUSDVIEW_USD_ASSETS_ANIM_OUT="$TMP/animation" \
  bash "$SCRIPT_DIR/run-usd-assets-animation-smoke.sh"
grep -q $'vk-raster\tchanged\ttest_assets/USDZ/AnimatedCube/AnimatedCube.usdz' \
  "$TMP/animation/animation-summary.tsv"

echo 'PASS: broad usd-assets profile and cross-time animation coverage'
