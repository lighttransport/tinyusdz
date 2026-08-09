#!/usr/bin/env bash
# Flat/ribbon curves must not disappear when a GPU render also contains meshes.
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDRENDER="${1:-${TUSDRENDER:-$ROOT/build/tools/tusdrender/tusdrender}}"
ASSET="$ROOT/tests/usda/tusdrender-mixed-flat-curve.usda"
if [ ! -x "$TUSDRENDER" ]; then
  echo "SKIP: tusdrender binary not found at $TUSDRENDER"
  exit "$SKIP"
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$TUSDRENDER" "$ASSET" "$TMP/mixed.png" -vkr -w 48 -height 48 \
  -autoframe -stats >"$TMP/mixed.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  if grep -qiE 'failed to create .*vulkan|no vulkan|vulkan.*unavailable|requested gpu backend not built' "$TMP/mixed.log"; then
    echo "SKIP: no usable Vulkan LightRT backend"
    exit "$SKIP"
  fi
  cat "$TMP/mixed.log"
  echo "FAIL: mixed flat-curve render failed"
  exit 1
fi
if ! grep -q 'GPU flat/ribbon curves: camera-facing triangle carrier' "$TMP/mixed.log"; then
  cat "$TMP/mixed.log"
  echo "FAIL: mixed flat curve did not reach the GPU carrier"
  exit 1
fi
if ! grep -q 'backend: LightRT VK' "$TMP/mixed.log" ||
   ! grep -q 'triangles: 7' "$TMP/mixed.log" || [ ! -s "$TMP/mixed.png" ]; then
  cat "$TMP/mixed.log"
  echo "FAIL: GPU flat curve carrier did not render with the mesh"
  exit 1
fi
echo "PASS: mixed flat curves reach the bounded GPU triangle carrier"
