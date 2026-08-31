#!/usr/bin/env bash
#
# Compile tusdview's GLSL shaders to SPIR-V and embed them as committed C
# headers under vk/shaders/embedded/. vk/vulkan.cmake consumes those headers
# directly, so the normal build needs NO glslang and produces a single,
# self-contained binary on every platform.
#
# Run this only when a shader (vk/shaders/*.{vert,frag,comp}) changes, then
# commit the regenerated vk/shaders/embedded/*.spv.h.
#
# glslang selection (first hit wins):
#   1. $GLSLANG, or the path passed as $1
#   2. examples/common/glslang/bin/glslang(Validator)  (build-glslang.sh)
#   3. glslangValidator / glslang on PATH
#
# Usage:
#   examples/tusdview/vk/shaders/build-shaders.sh [path-to-glslang]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMON="$(cd "$HERE/../../../common" && pwd)"
OUT="$HERE/embedded"

# --- locate glslang --------------------------------------------------------
GLSLANG="${1:-${GLSLANG:-}}"
if [ -z "$GLSLANG" ]; then
  for cand in "$COMMON/glslang/bin/glslangValidator" "$COMMON/glslang/bin/glslang"; do
    [ -x "$cand" ] && GLSLANG="$cand" && break
  done
fi
[ -z "$GLSLANG" ] && GLSLANG="$(command -v glslangValidator || command -v glslang || true)"
if [ -z "$GLSLANG" ] || [ ! -x "$GLSLANG" ]; then
  echo "error: glslang not found. Build it via examples/common/build-glslang.sh,"
  echo "       install glslang-tools, or pass the path as the first argument." >&2
  exit 1
fi
echo "==> glslang: $GLSLANG"
"$GLSLANG" --version | head -1 || true
RT_IMAGE_CAP="${TUSDVIEW_RT_PTEXTURE_IMAGE_CAP:-256}"
RT_SHADER_DEFINE="-DTUSDVIEW_RT_PTEXTURE_IMAGE_CAP=${RT_IMAGE_CAP}"

mkdir -p "$OUT"

# --- raster shaders (mesh / line) ------------------------------------------
# --vn <sym> emits 'static const uint32_t <sym>[] = {...}', matching the names
# vk_renderer.cc includes (mesh_vert.spv.h -> mesh_vert_spv, etc.).
for sh in mesh.vert mesh.frag shadow.frag mesh_tess.vert mesh_tess.tesc mesh_tess.tese mesh_inst.vert mesh_inst.frag shadow_inst.frag line.vert line.frag line_depth.frag volume.vert volume.frag nonmesh.vert nonmesh.frag; do
  sym="${sh/./_}"          # mesh.vert -> mesh_vert
  echo "==> $sh -> embedded/${sym}.spv.h (${sym}_spv)"
  "$GLSLANG" -V --vn "${sym}_spv" -o "$OUT/${sym}.spv.h" "$HERE/$sh"
done

echo "==> mesh.frag (weighted OIT) -> embedded/mesh_oit_frag.spv.h"
"$GLSLANG" -V -DTUSDVIEW_OIT=1 --vn mesh_oit_frag_spv \
  -o "$OUT/mesh_oit_frag.spv.h" "$HERE/mesh.frag"
for sh in mesh_inst.frag nonmesh.frag volume.frag; do
  sym="${sh/./_}"
  echo "==> $sh (weighted OIT) -> embedded/${sym}_oit.spv.h"
  "$GLSLANG" -V -DTUSDVIEW_OIT=1 --vn "${sym}_oit_spv" \
    -o "$OUT/${sym}_oit.spv.h" "$HERE/$sh"
done
echo "==> oit_composite.comp -> embedded/oit_composite_comp.spv.h"
"$GLSLANG" -V --vn oit_composite_comp_spv \
  -o "$OUT/oit_composite_comp.spv.h" "$HERE/oit_composite.comp"

# --- ray-query compute shader (optional) -----------------------------------
# Needs a glslang new enough for GL_EXT_ray_query (SPIR-V 1.4 / vulkan1.2).
# If this glslang can't compile it, drop the header so the build compiles the
# RT path out (TUSDVIEW_HAVE_RT_SHADER=0) and Vulkan falls back to rasterization.
RAYTRACE_TMP="$(mktemp "${TMPDIR:-/tmp}/tusdview-raytrace.XXXXXX.spv")"
cleanup_raytrace_tmp() {
  rm -f "$RAYTRACE_TMP"
}
trap cleanup_raytrace_tmp EXIT

# Some old glslang releases accept GL_EXT_ray_query but mix the provisional
# RayQueryProvisionalKHR capability with final OpTypeRayQueryKHR opcodes. The
# resulting module compiles without diagnostics yet is invalid SPIR-V. Validate
# a raw module first when SPIRV-Tools is installed, then emit the C header.
RAYTRACE_VALID=1
if ! "$GLSLANG" -V --target-env vulkan1.2 "$RT_SHADER_DEFINE" \
      -o "$RAYTRACE_TMP" "$HERE/raytrace.comp" 2>/dev/null; then
  RAYTRACE_VALID=0
elif command -v spirv-val >/dev/null 2>&1 &&
     ! spirv-val --target-env vulkan1.2 "$RAYTRACE_TMP" >/dev/null; then
  RAYTRACE_VALID=0
fi

if [ "$RAYTRACE_VALID" -eq 1 ] &&
   "$GLSLANG" -V --target-env vulkan1.2 "$RT_SHADER_DEFINE" --vn raytrace_comp_spv \
      -o "$OUT/raytrace_comp.spv.h" "$HERE/raytrace.comp" 2>/dev/null; then
  echo "==> raytrace.comp -> embedded/raytrace_comp.spv.h (ray query ENABLED)"
else
  # glslc can support ray-query even when the installed glslangValidator is
  # too old. Emit a byte header and let createShader consume its byte size;
  # Vulkan requires pCode to be 4-byte aligned, which SPIR-V already is.
  GLSLC="${GLSLC:-$(command -v glslc || true)}"
  if [ -n "$GLSLC" ] && [ -x "$GLSLC" ] &&
     "$GLSLC" --target-env=vulkan1.2 "$RT_SHADER_DEFINE" -o "$RAYTRACE_TMP" "$HERE/raytrace.comp"; then
    xxd -i -c 12 "$RAYTRACE_TMP" "$OUT/raytrace_comp.spv.h"
    sed -i 's/unsigned char _tmp_.*_spv\[\]/const unsigned char raytrace_comp_spv[]/' "$OUT/raytrace_comp.spv.h"
    sed -i 's/unsigned int _tmp_.*_spv_len/const unsigned int raytrace_comp_spv_len/' "$OUT/raytrace_comp.spv.h"
    sed -i '1i#pragma once' "$OUT/raytrace_comp.spv.h"
    echo "==> raytrace.comp -> embedded/raytrace_comp.spv.h (ray query ENABLED via glslc)"
  else
    rm -f "$OUT/raytrace_comp.spv.h"
    echo "==> WARNING: '$GLSLANG' cannot produce valid GL_EXT_ray_query SPIR-V"
    echo "    (compile or spirv-val failed) — RT shader omitted. Use a newer"
    echo "    glslang or glslc to enable Vulkan RT."
  fi
fi

# A graph-free RT variant avoids compiling the large MaterialX interpreter for
# scenes that have no MaterialX node graphs. The descriptor ABI remains the
# same, so the renderer can select it per scene without another pipeline
# layout. Keep this on the glslc path because the bundled glslang may reject
# ray-query modules before preprocessing this variant.
RAYTRACE_FAST_TMP="$(mktemp "${TMPDIR:-/tmp}/tusdview-raytrace-fast.XXXXXX.spv")"
cleanup_raytrace_fast_tmp() {
  rm -f "$RAYTRACE_FAST_TMP"
}
trap cleanup_raytrace_fast_tmp EXIT
GLSLC_FAST="${GLSLC:-$(command -v glslc || true)}"
if [ -n "$GLSLC_FAST" ] && [ -x "$GLSLC_FAST" ] &&
  "$GLSLC_FAST" --target-env=vulkan1.2 "$RT_SHADER_DEFINE" \
      -DTUSDVIEW_RT_FAST_MATERIAL=1 \
      -DTUSDVIEW_RT_DISABLE_MTLX=1 -DTUSDVIEW_RT_DISABLE_DEBUG_RAYS=1 \
      -o "$RAYTRACE_FAST_TMP" \
      "$HERE/raytrace.comp"; then
  xxd -i -c 12 "$RAYTRACE_FAST_TMP" "$OUT/raytrace_fast_comp.spv.h"
  sed -i 's/unsigned char _tmp_.*_spv\[\]/const unsigned char raytrace_fast_comp_spv[]/' "$OUT/raytrace_fast_comp.spv.h"
  sed -i 's/unsigned int _tmp_.*_spv_len/const unsigned int raytrace_fast_comp_spv_len/' "$OUT/raytrace_fast_comp.spv.h"
  sed -i '1i#pragma once' "$OUT/raytrace_fast_comp.spv.h"
  echo "==> raytrace.comp (graph-free) -> embedded/raytrace_fast_comp.spv.h"
else
  rm -f "$OUT/raytrace_fast_comp.spv.h"
  echo "==> WARNING: graph-free RT shader omitted"
fi
cleanup_raytrace_fast_tmp
trap - EXIT
cleanup_raytrace_tmp
trap - EXIT

# --- compute-BVH ray-tracing fallback (optional, no hardware RT needed) ----
# Milestone-1 debug shader (see raytrace_swbvh.comp): needs only baseline
# Vulkan compute + storage buffers, no GL_EXT_ray_query/SPIR-V 1.4, so this
# should compile with essentially any glslang -- absent only if glslang
# itself is missing or broken. Absent -> compute-BVH fallback compiled out
# (TUSDVIEW_HAVE_SWRT_SHADER=0); --rt then falls back to plain rasterization
# on GPUs without hardware ray query, same as before this fallback existed.
# Keep full-interpreter variants for automatic procedural-graph upgrades.
if "$GLSLANG" -V --target-env vulkan1.1 --vn raytrace_swbvh_full_comp_spv \
      -o "$OUT/raytrace_swbvh_full_comp.spv.h" "$HERE/raytrace_swbvh.comp" 2>/dev/null; then
  echo "==> raytrace_swbvh.comp (full MaterialX) -> embedded/raytrace_swbvh_full_comp.spv.h"
else
  rm -f "$OUT/raytrace_swbvh_full_comp.spv.h"
fi
if "$GLSLANG" -V --target-env vulkan1.1 -DTUSDVIEW_SW_DISABLE_MTLX=1 \
      --vn raytrace_swbvh_comp_spv \
      -o "$OUT/raytrace_swbvh_comp.spv.h" "$HERE/raytrace_swbvh.comp" 2>/dev/null; then
  echo "==> raytrace_swbvh.comp (graph-free) -> embedded/raytrace_swbvh_comp.spv.h (compute-BVH RT fallback ENABLED)"
else
  rm -f "$OUT/raytrace_swbvh_comp.spv.h"
  echo "==> WARNING: '$GLSLANG' cannot produce valid raytrace_swbvh.comp SPIR-V"
  echo "    (compile failed) — compute-BVH RT fallback omitted."
fi
if "$GLSLANG" -V --target-env vulkan1.1 -DTUSDVIEW_SW_PATH_ONLY=1 \
      --vn raytrace_swbvh_path_full_comp_spv \
      -o "$OUT/raytrace_swbvh_path_full_comp.spv.h" \
      "$HERE/raytrace_swbvh.comp" 2>/dev/null; then
  echo "==> raytrace_swbvh.comp (path-only full MaterialX) -> embedded/raytrace_swbvh_path_full_comp.spv.h"
else
  rm -f "$OUT/raytrace_swbvh_path_full_comp.spv.h"
fi
if "$GLSLANG" -V --target-env vulkan1.1 -DTUSDVIEW_SW_PATH_ONLY=1 \
      -DTUSDVIEW_SW_DISABLE_MTLX=1 \
      --vn raytrace_swbvh_path_comp_spv \
      -o "$OUT/raytrace_swbvh_path_comp.spv.h" \
      "$HERE/raytrace_swbvh.comp" 2>/dev/null; then
  echo "==> raytrace_swbvh.comp (path-only) -> embedded/raytrace_swbvh_path_comp.spv.h"
else
  rm -f "$OUT/raytrace_swbvh_path_comp.spv.h"
  echo "==> WARNING: compute-BVH production path shader omitted"
fi

# --- RT GPU-skinning compute shader (optional) ------------------------------
# Skins the BLAS vertex stream on the GPU for the ray-query path (same
# vulkan1.2 / buffer_reference needs as raytrace.comp). Absent -> per-frame
# RT skinning falls back to the CPU path.
SKIN_TMP="$(mktemp "${TMPDIR:-/tmp}/tusdview-skin.XXXXXX.spv")"
cleanup_skin_tmp() {
  rm -f "$SKIN_TMP"
}
trap cleanup_skin_tmp EXIT

SKIN_VALID=1
if ! "$GLSLANG" -V --target-env vulkan1.2 \
      -o "$SKIN_TMP" "$HERE/skin.comp" 2>/dev/null; then
  SKIN_VALID=0
elif command -v spirv-val >/dev/null 2>&1 &&
     ! spirv-val --target-env vulkan1.2 "$SKIN_TMP" >/dev/null; then
  SKIN_VALID=0
fi

if [ "$SKIN_VALID" -eq 1 ] &&
   "$GLSLANG" -V --target-env vulkan1.2 --vn skin_comp_spv \
      -o "$OUT/skin_comp.spv.h" "$HERE/skin.comp" 2>/dev/null; then
  echo "==> skin.comp -> embedded/skin_comp.spv.h (RT GPU skinning ENABLED)"
else
  rm -f "$OUT/skin_comp.spv.h"
  echo "==> WARNING: '$GLSLANG' cannot produce valid skin.comp SPIR-V — RT"
  echo "    GPU skinning omitted (per-frame CPU skinning fallback)."
fi
cleanup_skin_tmp
trap - EXIT

echo "==> done. Commit $OUT/*.spv.h"
