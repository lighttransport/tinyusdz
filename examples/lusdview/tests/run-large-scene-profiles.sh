#!/usr/bin/env bash
#
# Optional local smoke/performance harness for public large-scene profiles.
#
# The large scene assets are not part of the repo, so this script is deliberately
# not registered as a required ctest. Set any of CALDERA, ISLAND, or ALAB to a
# root USD path and it will run lusdview headless with the matching realtime
# profile, writing one screenshot and one log per scene.
#
# Usage:
#   LUSDVIEW=./build_ninja/lusdview \
#   CALDERA=/path/to/caldera.usda \
#   ISLAND=/path/to/island/root.usd \
#   ALAB=/path/to/ALab/root.usda \
#     bash examples/lusdview/tests/run-large-scene-profiles.sh
#
# Overrides:
#   OUT_DIR=/tmp/lusdview-large-scenes
#   FRAMES=8
#   LUSDVIEW_EXTRA_ARGS="--rt --mode albedo"
#   LUSDVIEW_SCENE_TIMEOUT=10m
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDVIEW="${LUSDVIEW:-$REPO_ROOT/build_ninja/lusdview}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/lusdview-large-scene-profiles}"
FRAMES="${FRAMES:-8}"
LUSDVIEW_EXTRA_ARGS="${LUSDVIEW_EXTRA_ARGS:-}"
LUSDVIEW_SCENE_TIMEOUT="${LUSDVIEW_SCENE_TIMEOUT:-10m}"
SKIP=77

if [ ! -x "$LUSDVIEW" ]; then
  echo "SKIP: lusdview binary not found at $LUSDVIEW (set LUSDVIEW=...)"
  exit "$SKIP"
fi

mkdir -p "$OUT_DIR"
SUMMARY="$OUT_DIR/summary.tsv"
printf 'profile\tasset\texit_code\telapsed_sec\timage_bytes\tlog\n' >"$SUMMARY"

ran=0
failed=0

run_lusdview_scene() {
  if command -v timeout >/dev/null 2>&1; then
    # shellcheck disable=SC2086 # intentional user-supplied extra CLI flags
    timeout --kill-after=15s "$LUSDVIEW_SCENE_TIMEOUT" "$LUSDVIEW" \
      --headless --large-scene-profile "$1" $LUSDVIEW_EXTRA_ARGS \
      --frames "$FRAMES" --screenshot "$2" "$3"
  else
    # shellcheck disable=SC2086 # intentional user-supplied extra CLI flags
    "$LUSDVIEW" --headless --large-scene-profile "$1" \
      $LUSDVIEW_EXTRA_ARGS --frames "$FRAMES" --screenshot "$2" "$3"
  fi
}

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
  local img="$base.ppm"
  local log="$base.log"
  echo "=== lusdview large-scene profile: $profile ==="
  echo "asset: $asset"
  echo "log:   $log"
  echo "image: $img"
  ran=1

  local start end elapsed bytes
  start="$(date +%s)"
  run_lusdview_scene "$profile" "$img" "$asset" >"$log" 2>&1
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
    echo "FAIL: $profile did not write a screenshot"
    failed=1
    return 0
  fi
  if ! grep -q "large-scene-profile $profile resolved" "$log"; then
    echo "FAIL: $profile log did not include resolved profile settings"
    failed=1
    return 0
  fi
  if ! grep -q "render stats" "$log"; then
    echo "WARN: $profile log did not include render stats"
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

echo "PASS: large-scene profile smoke runs completed"
echo "summary: $SUMMARY"
