#!/usr/bin/env bash
# Opt-in viewer texture upload/sampling smoke under Vulkan GPU-AV.
set -uo pipefail

viewer="${TUSDVIEW:-build_ninja/tusdview}"
root="${TUSDVIEW_ROOT:-.}"
if [[ -z "${VK_LAYER_PATH:-}" || ! -d "$VK_LAYER_PATH" ]]; then
  echo "SKIP: set VK_LAYER_PATH to a Vulkan ValidationLayers build"
  exit 77
fi
if [[ ! -x "$viewer" ]]; then
  echo "FAIL: tusdview executable not found: $viewer" >&2
  exit 1
fi
if ! find "$VK_LAYER_PATH" -name VkLayer_khronos_validation.json -print -quit |
     grep -q .; then
  echo "SKIP: VK_LAYER_PATH has no Khronos validation layer"
  exit 77
fi

out="$(mktemp -d "${TMPDIR:-/tmp}/tusdview-vk-texture-validation.XXXXXX")"
trap 'rm -rf "$out"' EXIT
cat >"$out/vk_layer_settings.txt" <<'EOF'
khronos_validation.validate_gpu_based = GPU_BASED_GPU_ASSISTED
khronos_validation.gpuav_descriptor_checks = true
khronos_validation.gpuav_buffer_oob = true
EOF

log="$out/viewer.log"
if ! timeout 120s env \
    VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    VK_LAYER_SETTINGS_PATH="$out/vk_layer_settings.txt" \
    "$viewer" --headless --backend vk --frames 2 --size 256x192 \
    --screenshot "$out/texture.ppm" \
    "$root/models/texture-cat-plane.usdz" >"$log" 2>&1; then
  if grep -Eqi 'no compatible Vulkan|no Vulkan device|renderer init failed' "$log"; then
    cat "$log" >&2
    echo "SKIP: Vulkan backend unavailable"
    exit 77
  fi
  cat "$log" >&2
  echo "FAIL: validated Vulkan texture render failed" >&2
  exit 1
fi
if grep -Eqi 'VUID-|validation error|index out of bounds|descriptor.*(invalid|unbound)' "$log"; then
  cat "$log" >&2
  echo "FAIL: Vulkan validation reported a texture-path error" >&2
  exit 1
fi
[[ -s "$out/texture.ppm" ]] || {
  echo "FAIL: Vulkan texture render produced no screenshot" >&2
  exit 1
}
echo "PASS: Vulkan texture upload and sampling are clean under GPU-AV"
