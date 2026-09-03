#!/usr/bin/env bash
#
# Optional local smoke/performance harness for lusdrender large-scene profiles.
#
# The large scene assets are not tracked in this repository. Set any of CALDERA,
# ISLAND, or ALAB to a root USD path and this script renders one PNG per scene
# with the matching profile.
#
# Usage:
#   LUSDRENDER=./build_ninja/tools/lusdrender/lusdrender \
#   CALDERA=/path/to/caldera.usda \
#   ISLAND=/path/to/island/root.usd \
#   ALAB=/path/to/ALab/root.usda \
#     bash tools/lusdrender/tests/run-large-scene-profiles.sh
#
# Overrides:
#   OUT_DIR=/tmp/lusdrender-large-scenes
#   WIDTH=960 HEIGHT=540
#   LUSDRENDER_EXTRA_ARGS="-stats -gpuShade preview"
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDRENDER="${LUSDRENDER:-$REPO_ROOT/build_ninja/tools/lusdrender/lusdrender}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/lusdrender-large-scene-profiles}"
WIDTH="${WIDTH:-960}"
HEIGHT="${HEIGHT:-540}"
LUSDRENDER_EXTRA_ARGS="${LUSDRENDER_EXTRA_ARGS:-}"
SKIP=77

if [ ! -x "$LUSDRENDER" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER (set LUSDRENDER=...)"
  exit "$SKIP"
fi

mkdir -p "$OUT_DIR"
SUMMARY="$OUT_DIR/summary.tsv"
printf 'profile\tasset\texit_code\telapsed_sec\timage_bytes\tlog\n' >"$SUMMARY"

ran=0
failed=0

run_scene() {
  local profile="$1"
  local asset="$2"
  if [ -z "$asset" ]; then
    echo "SKIP: $profile not set"
    return 0
  fi
  if [ ! -f "$asset" ]; then
    echo "FAIL: $profile asset not found: $asset"
    failed=1
    return 0
  fi

  local base="$OUT_DIR/$profile"
  local img="$base.png"
  local log="$base.log"
  echo "=== lusdrender large-scene profile: $profile ==="
  echo "asset: $asset"
  echo "log:   $log"
  echo "image: $img"
  ran=1

  local start end elapsed bytes
  start="$(date +%s)"
  # shellcheck disable=SC2086 # intentional user-supplied extra CLI flags
  "$LUSDRENDER" "$asset" "$img" -largeSceneProfile "$profile" \
    $LUSDRENDER_EXTRA_ARGS -w "$WIDTH" -height "$HEIGHT" >"$log" 2>&1
  local rc=$?
  end="$(date +%s)"
  elapsed=$((end - start))
  bytes=0
  [ -s "$img" ] && bytes="$(wc -c < "$img")"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$profile" "$asset" "$rc" "$elapsed" "$bytes" "$log" >>"$SUMMARY"
  cat "$log"
  if [ "$rc" -ne 0 ]; then
    echo "FAIL: $profile exited with $rc"
    failed=1
    return 0
  fi
  if [ ! -s "$img" ]; then
    echo "FAIL: $profile did not write an image"
    failed=1
    return 0
  fi
  if ! grep -q "largeSceneProfile $profile resolved" "$log"; then
    echo "FAIL: $profile log did not include resolved profile settings"
    failed=1
    return 0
  fi
  echo "PASS: $profile"
}

run_scene caldera "${CALDERA:-}"
run_scene island "${ISLAND:-}"
run_scene alab "${ALAB:-}"

if [ "$failed" -ne 0 ]; then
  exit 1
fi
if [ "$ran" -eq 0 ]; then
  echo "SKIP: set CALDERA, ISLAND, or ALAB to run local large-scene profiles"
  exit "$SKIP"
fi

echo "PASS: lusdrender large-scene profile smoke runs completed"
echo "summary: $SUMMARY"
