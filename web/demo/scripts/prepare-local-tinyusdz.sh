#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WEB_DIR="$(cd "${DEMO_DIR}/.." && pwd)"
LEGACY_BUILD_DIR="${WEB_DIR}/build_ninja"
NEXT_BUILD_DIR="${WEB_DIR}/build_next_ninja"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-MinSizeRel}"
JOBS="${JOBS:-16}"

for tool in emcmake cmake ninja; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "error: ${tool} is required for local TinyUSDZ demo development." >&2
    exit 1
  fi
done

configure_build() {
  local build_dir="$1"
  local target="$2"
  shift 2
  emcmake cmake -S "${WEB_DIR}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" "$@"
  cmake --build "${build_dir}" --target "${target}" --parallel "${JOBS}"
}

configure_build "${LEGACY_BUILD_DIR}" tinyusdz \
  -DTINYUSDZ_WASM_PRODUCT=legacy \
  -DTINYUSDZ_WASM_DEMODEV=OFF \
  -DTINYUSDZ_WASM64=OFF
configure_build "${NEXT_BUILD_DIR}" tinyusdz_next_wasm \
  -DTINYUSDZ_WASM_PRODUCT=next \
  -DTINYUSDZ_WASM_DEMODEV=OFF \
  -DTINYUSDZ_WASM64=OFF

for artifact in tinyusdz.js tinyusdz.wasm tinyusdz_next.js tinyusdz_next.wasm; do
  if [[ ! -f "${WEB_DIR}/js/src/tinyusdz/${artifact}" ]]; then
    echo "error: local TinyUSDZ build did not produce ${artifact}." >&2
    exit 1
  fi
done

echo "Local TinyUSDZ legacy and next WASM modules are ready."
