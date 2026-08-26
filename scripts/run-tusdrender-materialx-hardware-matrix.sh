#!/usr/bin/env bash
# Strict headless MaterialX/OpenPBR gate for GPU CI workers.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
build_dir="${1:-${repo_root}/build_ninja}"
vulkan_devices="${TUSDR_CI_VULKAN_DEVICES:-0:NVIDIA,1:AMD}"

if [ ! -f "${build_dir}/CTestTestfile.cmake" ]; then
  echo "ERROR: ${build_dir} is not a configured CTest build" >&2
  exit 2
fi

run_gate() {
  TUSDR_PARITY_REQUIRE_BACKENDS=vkr,cuda,hip \
  TUSDR_PARITY_REQUIRE_HARDWARE=1 \
  TUSDR_PARITY_VULKAN_DEVICES="${vulkan_devices}" \
    ctest --test-dir "${build_dir}" \
      -R '^tool-tusdrender-materialx-openpbr-parity$' \
      --output-on-failure
  TINYUSDZ_MTLX_FLAKE_CUDA=1 \
  TINYUSDZ_MTLX_FLAKE_HIP=1 \
    ctest --test-dir "${build_dir}" \
      -R '^tool-tusdrender-materialx-flake-parity$' \
      --output-on-failure
  TINYUSDZ_MTLX_PROJECTION_CUDA=1 \
  TINYUSDZ_MTLX_PROJECTION_HIP=1 \
    ctest --test-dir "${build_dir}" \
      -R '^tool-tusdrender-materialx-projection-parity$' \
      --output-on-failure
}

if [ "${TUSDR_CI_CUDA_CACHE_CYCLE:-0}" = 1 ]; then
  cache_root="$(mktemp -d)"
  trap 'rm -rf "${cache_root}"' EXIT
  echo "=== strict hardware matrix: cold CUDA PTX cache ==="
  XDG_CACHE_HOME="${cache_root}" \
  TUSDR_PARITY_CUDA_CACHE_EXPECT=cold run_gate
  echo "=== strict hardware matrix: warm CUDA PTX cache ==="
  XDG_CACHE_HOME="${cache_root}" \
  TUSDR_PARITY_CUDA_CACHE_EXPECT=warm run_gate
else
  run_gate
fi
