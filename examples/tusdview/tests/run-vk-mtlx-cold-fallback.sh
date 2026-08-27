#!/usr/bin/env bash
# Verify that a cold, oversized MaterialX ray-query shader fails fast and
# leaves the compact Vulkan RT pipeline available for rendering.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDVIEW="${TUSDVIEW:-$REPO_ROOT/build_ninja/tusdview}"
ASSET="${ASSET:-$REPO_ROOT/usd-assets/full_assets/OpenChessSet/chess_set.usda}"

if [ ! -x "$TUSDVIEW" ]; then
  echo "SKIP: tusdview binary not found at $TUSDVIEW"
  exit "$SKIP"
fi
if [ ! -f "$ASSET" ]; then
  echo "SKIP: cold MaterialX fallback asset not found at $ASSET"
  exit "$SKIP"
fi
if ! command -v nvidia-smi >/dev/null 2>&1 ||
   ! nvidia-smi -L >/dev/null 2>&1; then
  echo "SKIP: NVIDIA GPU is not available"
  exit "$SKIP"
fi
if command -v vulkaninfo >/dev/null 2>&1; then
  if ! vulkaninfo --summary 2>/dev/null | grep -q 'NVIDIA'; then
    echo "SKIP: Vulkan NVIDIA device is not visible"
    exit "$SKIP"
  fi
else
  echo "SKIP: vulkaninfo is not available to identify the NVIDIA Vulkan device"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/full-fallback.png"
LOGFILE="$TMP/tusdview.log"

set +e
if command -v timeout >/dev/null 2>&1; then
  env TUSDVIEW_VK_PIPELINE_CACHE_DIR="$TMP/cache" \
    TUSDVIEW_NVIDIA_OFFLOAD=1 \
    timeout --kill-after=10s "${TUSDVIEW_MTLX_VK_FALLBACK_TIMEOUT:-60s}" \
    "$TUSDVIEW" --headless --backend vk --vk-device nvidia --rt \
    --frames 1 --size 32x32 --materialx-vk-shader-max-kib 256 \
    --screenshot "$OUT" "$ASSET" >"$LOGFILE" 2>&1
else
  env TUSDVIEW_VK_PIPELINE_CACHE_DIR="$TMP/cache" \
    TUSDVIEW_NVIDIA_OFFLOAD=1 \
    "$TUSDVIEW" --headless --backend vk --vk-device nvidia --rt \
    --frames 1 --size 32x32 --materialx-vk-shader-max-kib 256 \
    --screenshot "$OUT" "$ASSET" >"$LOGFILE" 2>&1
fi
RC=$?
set -e
cat "$LOGFILE"

if [ "$RC" -ne 0 ]; then
  echo "FAIL: cold MaterialX fallback command failed (exit $RC)"
  exit 1
fi
if ! grep -q 'device=discrete rt=hardware' "$LOGFILE"; then
  echo "FAIL: test did not run on a hardware ray-query device"
  exit 1
fi
if ! grep -q 'requires a cold driver compile' "$LOGFILE"; then
  echo "FAIL: full shader did not report compile-required fallback"
  exit 1
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: compact fallback did not produce a screenshot"
  exit 1
fi

echo "PASS: cold full MaterialX pipeline failed fast and compact RT rendered"
