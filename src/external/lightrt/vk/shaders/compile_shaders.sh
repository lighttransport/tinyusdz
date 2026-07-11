#!/usr/bin/env bash
#
# Regenerate the checked-in SPIR-V C headers from the vk/shaders/*.comp sources.
#
# These headers (vk/shaders/*.spv.h) are committed, so a normal LightRT build
# needs NO shader toolchain — the runtime just hands the embedded uint32_t[]
# array to vkCreateShaderModule. Run this only after editing a .comp; it needs
# glslangValidator (Vulkan SDK) or, as a fallback, glslc (shaderc).
#
# Usage:  scripts/compile_shaders.sh
#         GLSLANG=/path/to/glslangValidator scripts/compile_shaders.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SH="${SCRIPT_DIR}/../vk/shaders"

# Prefer the locally built glslang (scripts/setup-and-build-glslang.sh): the
# trace_ray_query shader needs GL_EXT_ray_query, which older system glslang
# (Vulkan SDK < 1.2 / glslang < 11) does not support.
if [[ -z "${GLSLANG:-}" ]]; then
    if [[ -x "${REPO_ROOT}/tools/bin/glslangValidator" ]]; then
        GLSLANG="${REPO_ROOT}/tools/bin/glslangValidator"
    else
        GLSLANG="glslangValidator"
    fi
fi

if ! command -v "${GLSLANG}" >/dev/null 2>&1; then
    echo "error: ${GLSLANG} not found. Run scripts/setup-and-build-glslang.sh," >&2
    echo "       install the Vulkan SDK, or set GLSLANG=..." >&2
    exit 1
fi

# gen <basename> <symbol-name>
gen() {
    local base="$1" sym="$2"
    "${GLSLANG}" -V --target-env vulkan1.2 --vn "${sym}" \
        -o "${SH}/${base}.spv.h" "${SH}/${base}.comp"
    echo "generated ${SH}/${base}.spv.h  (${sym})"
}

gen trace_bvh        trace_bvh_spv
gen build_morton     build_morton_spv
gen trace_ray_query  trace_ray_query_spv  # needs VK_KHR_ray_query (SPIR-V 1.4)

echo "done."
