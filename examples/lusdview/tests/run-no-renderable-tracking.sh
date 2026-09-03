#!/usr/bin/env bash
#
# Regression guard for the usd-assets smoke harness' no_renderable bucket. This
# uses the lusdrender CPU path so the test does not require a GL/Vulkan context.
set -euo pipefail

SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

LUSDVIEW_BIN="${LUSDVIEW:-$REPO_ROOT/build/lusdview}"
LUSDRENDER_BIN="${LUSDRENDER:-$REPO_ROOT/build/tools/lusdrender/lusdrender}"

if [ ! -x "$LUSDVIEW_BIN" ]; then
  echo "SKIP: lusdview binary not found at $LUSDVIEW_BIN"
  exit "$SKIP"
fi
if [ ! -x "$LUSDRENDER_BIN" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER_BIN"
  exit "$SKIP"
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/lusdview-no-renderable.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT

cat > "$WORK_DIR/empty.usda" <<'USD'
#usda 1.0
def Xform "World" {
}
USD

OUT_DIR="$WORK_DIR/out"
mkdir -p "$OUT_DIR"
EXPECT="$WORK_DIR/expect.tsv"
printf 'lusdr-cpu\tempty.usda\tno_renderable\tfound no renderable Mesh triangles\t0\n' > "$EXPECT"

LUSDVIEW="$LUSDVIEW_BIN" \
LUSDRENDER="$LUSDRENDER_BIN" \
USD_ASSETS_ROOT="$WORK_DIR" \
LUSDVIEW_USD_ASSETS_OUT="$OUT_DIR" \
LUSDVIEW_USD_ASSETS_MODES=lusdr-cpu \
LUSDVIEW_USD_ASSETS_FILES=empty.usda \
LUSDVIEW_USD_ASSETS_TIMEOUT=15s \
bash "$SCRIPT_DIR/run-usd-assets-render-smoke.sh" --expectations "$EXPECT" \
  --fail-on "expectation_mismatch"

RESULTS="$OUT_DIR/results.tsv"
if ! awk -F'\t' 'NR > 1 && $1 == "lusdr-cpu" && $2 == "no_renderable" && $4 == "empty.usda" { found = 1 } END { exit found ? 0 : 1 }' "$RESULTS"; then
  echo "FAIL: expected lusdr-cpu/no_renderable result for empty.usda"
  cat "$RESULTS"
  exit 1
fi

# Prove that an unexpected status is promoted to a hard, machine-readable
# mismatch rather than merely appearing as another warning in the batch log.
printf 'lusdr-cpu\tempty.usda\trendered\t-\t0\n' > "$EXPECT"
BAD_OUT="$WORK_DIR/bad-out"
if LUSDVIEW="$LUSDVIEW_BIN" \
   LUSDRENDER="$LUSDRENDER_BIN" \
   USD_ASSETS_ROOT="$WORK_DIR" \
   LUSDVIEW_USD_ASSETS_OUT="$BAD_OUT" \
   LUSDVIEW_USD_ASSETS_MODES=lusdr-cpu \
   LUSDVIEW_USD_ASSETS_FILES=empty.usda \
   LUSDVIEW_USD_ASSETS_TIMEOUT=15s \
   bash "$SCRIPT_DIR/run-usd-assets-render-smoke.sh" --expectations "$EXPECT" \
     --fail-on "expectation_mismatch"; then
  echo "FAIL: mismatched per-asset expectation did not fail"
  exit 1
fi
if ! awk -F'\t' 'NR > 1 && $2 == "expectation_mismatch" { found = 1 } END { exit found ? 0 : 1 }' \
     "$BAD_OUT/results.tsv"; then
  echo "FAIL: expectation_mismatch was not recorded"
  cat "$BAD_OUT/results.tsv"
  exit 1
fi

echo "PASS: no_renderable bucket and per-asset expectations tracked"
