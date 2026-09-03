#!/usr/bin/env bash
# Optional Vulkan validation/GPU-assisted validation smoke test.
set -uo pipefail

bench="${LUSDVIEW_TEXTURE_BENCH:-}"
if [[ -z "${VK_LAYER_PATH:-}" || ! -d "$VK_LAYER_PATH" ]]; then
  echo "SKIP: set VK_LAYER_PATH to a Vulkan ValidationLayers build"
  exit 77
fi
if [[ -z "$bench" || ! -x "$bench" ]]; then
  echo "FAIL: texture benchmark executable not found" >&2
  exit 1
fi

layer_json="$(find "$VK_LAYER_PATH" -name 'VkLayer_khronos_validation.json' -print -quit)"
if [[ -z "$layer_json" ]]; then
  echo "SKIP: VK_LAYER_PATH has no Khronos validation layer"
  exit 77
fi

settings="$(mktemp "${TMPDIR:-/tmp}/lusdview-vk-layer-settings.XXXXXX")"
trap 'rm -f "$settings"' EXIT
cat >"$settings" <<'EOF'
khronos_validation.validate_gpu_based = GPU_BASED_GPU_ASSISTED
khronos_validation.gpuav_descriptor_checks = true
khronos_validation.gpuav_buffer_oob = true
EOF

VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
VK_LAYER_SETTINGS_PATH="$settings" \
  "$bench" --backend vulkan --synthetic --format bc7 --mips 3 \
  --iterations 1 --warmup 0 --allow-software
