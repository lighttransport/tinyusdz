#!/usr/bin/env bash
#
# lusdrender -vk (compute trace): the ray set must be traced in TILES.
#
# lrt_vk_trace_scene used to allocate the whole frame's rays and hits in one go
# -- 48 bytes * width * height * spp, which is 6.3 GB of VRAM for 1920x1080 at
# 64 spp, on top of the BVH -- and dispatch it as a single job. It now traces in
# fixed-size tiles (LRT_VK_RAY_TILE rays, default 4 M = a 192 MiB working set).
#
# Rays are independent, so tiling must be EXACTLY image-neutral. That is what
# this asserts: the same frame rendered with a small tile (forcing many tiles),
# with the default tile, and with tiling off (LRT_VK_RAY_TILE=0, the old
# whole-frame dispatch) must be byte-identical.
#
# The failure this guards against is an offset bug -- a tile writing its hits at
# the wrong place in the output, or re-tracing tile 0 -- which produces a
# plausible-looking but wrong image (banded, or the first tile repeated).
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDRENDER="${1:-${LUSDRENDER:-$REPO_ROOT/build/tools/lusdrender/lusdrender}}"
MODEL="${2:-$REPO_ROOT/models/suzanne-materialx.usdc}"

if [ ! -x "$LUSDRENDER" ]; then
  echo "SKIP: lusdrender binary not found at $LUSDRENDER"
  exit "$SKIP"
fi
if [ ! -f "$MODEL" ]; then
  echo "SKIP: model not found at $MODEL"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# 256x256x4 = 262144 rays: a 65536-ray tile forces 4 tiles, so an offset bug
# cannot hide in a single-tile render.
render() {  # $1 = out, $2 = LRT_VK_RAY_TILE
  LRT_VK_RAY_TILE="$2" "$LUSDRENDER" "$MODEL" "$1" -rtPreview -vk \
    -w 256 -height 256 -samples 4 > "$1.log" 2>&1
  return $?
}

if ! render "$TMP/tiled.png" 65536; then
  if grep -qiE "failed to create .*vulkan|no vulkan|vulkan trace failed" "$TMP/tiled.png.log"; then
    echo "SKIP: no usable Vulkan compute device"
    exit "$SKIP"
  fi
  echo "FAIL: tiled -vk render failed"
  cat "$TMP/tiled.png.log"
  exit 1
fi
if [ ! -s "$TMP/tiled.png" ]; then
  echo "SKIP: -vk produced no image (no usable Vulkan compute device?)"
  exit "$SKIP"
fi

render "$TMP/whole.png" 0 || { echo "FAIL: untiled -vk render failed"; cat "$TMP/whole.png.log"; exit 1; }
render "$TMP/default.png" "" || { echo "FAIL: default -vk render failed"; cat "$TMP/default.png.log"; exit 1; }

if ! cmp -s "$TMP/tiled.png" "$TMP/whole.png"; then
  echo "FAIL: the 4-tile render differs from the single whole-frame dispatch. "
  echo "      Tiling must be image-neutral; a difference means a tile's hits "
  echo "      landed at the wrong offset in the output array."
  exit 1
fi
if ! cmp -s "$TMP/default.png" "$TMP/whole.png"; then
  echo "FAIL: the default tile size changed the image."
  exit 1
fi

echo "PASS: -vk ray tiling is image-neutral (4 tiles == 1 whole-frame dispatch)"
exit 0
