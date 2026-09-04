#!/usr/bin/env bash
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${LUSDVIEW:-$ROOT/build_ninja/lusdview}"
DEVICE="${LUSDVIEW_VK_DEVICE:-}"
[ -x "$BIN" ] || { echo "SKIP: lusdview not found at $BIN"; exit "$SKIP"; }
[ -n "$DEVICE" ] || { echo "SKIP: set LUSDVIEW_VK_DEVICE to a discrete GPU"; exit "$SKIP"; }

# Keep the selector usable from PRIME/offload systems as well as ordinary
# Vulkan hosts. These variables are harmless on systems where the selected
# ICD is already the default, and are intentionally scoped to this child.
GPU_ENV=()
case "${DEVICE,,}" in
  nvidia)
    GPU_ENV=(
      __NV_PRIME_RENDER_OFFLOAD=1
      __GLX_VENDOR_LIBRARY_NAME=nvidia
      __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
      LUSDVIEW_NVIDIA_OFFLOAD=1
    )
    ;;
  amd|radeon)
    GPU_ENV=(DRI_PRIME=1)
    ;;
esac
if [ "${#GPU_ENV[@]}" -gt 0 ]; then
  export "${GPU_ENV[@]}"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
for scene in authored-nested-closures authored-volume-material; do
  asset="$ROOT/examples/lusdview/tests/$scene.usda"
  out="$TMP/$scene.png"
  log="$TMP/$scene.log"
  timeout --kill-after=5s "${LUSDVIEW_AUTHORED_MATERIAL_TIMEOUT:-90s}" \
    "$BIN" --headless --backend vk --vk-device "$DEVICE" --rt \
    --frames 1 --size 64x64 --screenshot "$out" "$asset" >"$log" 2>&1
  rc=$?
  cat "$log"
  [ "$rc" -eq 0 ] || { echo "FAIL: $scene exited $rc"; exit 1; }
  grep -Eq 'caps: v1 .*rt=software|pipeline cache is cold' "$log" && {
    echo "SKIP: Vulkan hardware RT pipeline cache is cold"; exit "$SKIP";
  }
  grep -q 'device=discrete rt=hardware' "$log" || {
    echo "FAIL: $scene did not use hardware Vulkan RT"; exit 1;
  }
  [ -s "$out" ] || { echo "FAIL: $scene produced no screenshot"; exit 1; }
done
echo "PASS: authored nested-closure and volume-material scenes rendered on Vulkan RT"
