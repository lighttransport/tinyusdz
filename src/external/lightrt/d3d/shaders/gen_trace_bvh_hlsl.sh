#!/usr/bin/env bash
#
# Regenerate trace_bvh.hlsl + trace_bvh_hlsl.h from the committed Vulkan SPIR-V
# (../../vk/shaders/trace_bvh.spv.h) by decompiling it with SPIRV-Cross.
#
# The D3D11 backend (lightrt_c_d3d11.cpp) reuses the Vulkan trace_bvh compute
# shader verbatim — decompiling the SPIR-V guarantees the HLSL traverses the
# serialized BVH bit-for-bit like the GLSL/SPIR-V and the scalar CPU kernel, so
# there is no second shader to keep in sync. Run this after editing the Vulkan
# shader and regenerating vk/shaders/trace_bvh.spv.h (compile_shaders.sh).
#
# Needs spirv-cross on PATH or in $SPIRV_CROSS:
#   git clone https://github.com/KhronosGroup/SPIRV-Cross && cmake build it,
#   or install the Vulkan SDK (ships spirv-cross.exe).
#
# Usage:  SPIRV_CROSS=/path/to/spirv-cross  ./gen_trace_bvh_hlsl.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SPV_H="${HERE}/../../vk/shaders/trace_bvh.spv.h"
OUT_HLSL="${HERE}/trace_bvh.hlsl"
OUT_H="${HERE}/trace_bvh_hlsl.h"

SC="${SPIRV_CROSS:-$(command -v spirv-cross || true)}"
[ -z "${SC}" ] && { echo "error: spirv-cross not found (set \$SPIRV_CROSS)" >&2; exit 1; }

# Reconstruct the raw .spv from the C uint32 array in the header (little-endian).
TMP_SPV="$(mktemp -t trace_bvh.XXXXXX.spv)"
trap 'rm -f "${TMP_SPV}"' EXIT
grep -oE '0x[0-9a-fA-F]+' "${SPV_H}" | \
  perl -ne 'chomp; print pack("V", hex($_));' > "${TMP_SPV}"

# Decompile to HLSL shader model 5.0 (D3D11). Spec constants W / STACK become
# SPIRV_CROSS_CONSTANT_ID_0 / _1 macros (set per-scene by D3DCompile at runtime).
PROVENANCE="$(sed -n '1,12p' "${OUT_HLSL}" 2>/dev/null | grep -c '^//' || true)"
{
  cat <<'EOF'
// trace_bvh.hlsl — Direct3D 11 (SM5.0) compute BVH traversal for LightRT.
//
// DECOMPILED from vk/shaders/trace_bvh.spv (the committed Vulkan SPIR-V) with
// SPIRV-Cross (--hlsl --shader-model 50). It mirrors the scalar CPU kernel and
// the Vulkan trace_bvh compute shader bit-for-bit. Regenerate after editing the
// GLSL/SPIR-V via d3d/shaders/gen_trace_bvh_hlsl.sh.
//
// Bindings: t0 = BVH nodes, t1 = leaf blocks, t2 = rays, u3 = hits (RW),
// cbuffer PC = {root, node_count, block_count, ray_count}. Spec constants
// W (BVH width 4/8) and STACK are set via SPIRV_CROSS_CONSTANT_ID_0/_1 macros.
EOF
  "${SC}" --hlsl --shader-model 50 "${TMP_SPV}"
} > "${OUT_HLSL}"

# Embed as a raw string literal (the source is < 16 KB, the MSVC literal limit).
{
  echo "// Auto-generated from trace_bvh.hlsl. Do not edit by hand."
  echo "#pragma once"
  echo "static const char trace_bvh_hlsl[] = R\"HLSLSRC("
  cat "${OUT_HLSL}"
  echo ")HLSLSRC\";"
} > "${OUT_H}"

echo "generated ${OUT_HLSL} and ${OUT_H}"
