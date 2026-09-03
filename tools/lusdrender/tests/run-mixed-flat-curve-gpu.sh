#!/usr/bin/env bash
# Flat/ribbon curves must not disappear when a GPU render also contains meshes.
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDRENDER="${1:-${LUSDRENDER:-$ROOT/build/tools/lusdrender/lusdrender}}"
ASSET="$ROOT/tests/usda/lusdrender-mixed-flat-curve.usda"
PURE_ASSET="$ROOT/tests/usda/lusdrender-flat-curve-only.usda"
if [ ! -x "$LUSDRENDER" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER"
  exit "$SKIP"
fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$LUSDRENDER" "$ASSET" "$TMP/mixed.png" -vkr -w 48 -height 48 \
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

"$LUSDRENDER" "$PURE_ASSET" "$TMP/pure.png" -vkr -w 48 -height 48 \
  -autoframe -stats >"$TMP/pure.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  cat "$TMP/pure.log"
  echo "FAIL: flat-curve-only render failed"
  exit 1
fi
if ! grep -q 'GPU flat/ribbon curves: camera-facing triangle carrier' "$TMP/pure.log" ||
   ! grep -q 'triangles: 6' "$TMP/pure.log" || [ ! -s "$TMP/pure.png" ]; then
  cat "$TMP/pure.log"
  echo "FAIL: flat-curve-only scene was not framed/rendered"
  exit 1
fi
echo "PASS: flat-curve-only scene is framed and rendered on GPU"
