#!/usr/bin/env bash
# Verify that Vulkan keeps native analytic Gaussian rendering for a pure splat
# stage, but routes Gaussian data through the bounded flat fallback when native
# Points are present as well.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDRENDER="${1:-${TUSDRENDER:-$REPO_ROOT/build/tools/tusdrender/tusdrender}}"

if [ ! -x "$TUSDRENDER" ]; then
  echo "SKIP: tusdrender binary not found at $TUSDRENDER"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

run() {
  "$TUSDRENDER" "$1" "$2" -vkr -w 32 -height 32 -autoframe -stats \
    >"$3" 2>&1
}

run "$REPO_ROOT/tests/usda/tusdview-gaussian-splat.usda" \
    "$TMP/pure.png" "$TMP/pure.log"
if [ "$?" -ne 0 ]; then
  if grep -qiE "failed to create .*vulkan|no vulkan|vulkan.*unavailable|requested gpu backend not built" \
      "$TMP/pure.log"; then
    echo "SKIP: no usable Vulkan LightRT backend"
    exit "$SKIP"
  fi
  cat "$TMP/pure.log"
  echo "FAIL: pure Gaussian Vulkan render failed"
  exit 1
fi
if ! grep -q "native Gaussian ellipses: 2 in 1 Vulkan chunk(s)" "$TMP/pure.log"; then
  cat "$TMP/pure.log"
  echo "FAIL: pure Gaussian scene did not use native Vulkan ellipses"
  exit 1
fi

run "$REPO_ROOT/tests/usda/tusdview-gaussian-points.usda" \
    "$TMP/mixed.png" "$TMP/mixed.log"
if [ "$?" -ne 0 ]; then
  cat "$TMP/mixed.log"
  echo "FAIL: Points+Gaussian Vulkan render failed"
  exit 1
fi
if ! grep -q "triangles: 144" "$TMP/mixed.log"; then
  cat "$TMP/mixed.log"
  echo "FAIL: mixed scene did not retain Points and Gaussian fallback geometry"
  exit 1
fi

echo "PASS: pure native Gaussian and mixed-carrier Vulkan dispatch verified"
