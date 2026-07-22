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

mkdir -p "$OUT"

# --- raster shaders (mesh / line) ------------------------------------------
# --vn <sym> emits 'static const uint32_t <sym>[] = {...}', matching the names
# vk_renderer.cc includes (mesh_vert.spv.h -> mesh_vert_spv, etc.).
for sh in mesh.vert mesh.frag mesh_tess.vert mesh_tess.tesc mesh_tess.tese mesh_inst.vert mesh_inst.frag line.vert line.frag volume.vert volume.frag; do
  sym="${sh/./_}"          # mesh.vert -> mesh_vert
  echo "==> $sh -> embedded/${sym}.spv.h (${sym}_spv)"
  "$GLSLANG" -V --vn "${sym}_spv" -o "$OUT/${sym}.spv.h" "$HERE/$sh"
done

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
if ! "$GLSLANG" -V --target-env vulkan1.2 \
      -o "$RAYTRACE_TMP" "$HERE/raytrace.comp" 2>/dev/null; then
  RAYTRACE_VALID=0
elif command -v spirv-val >/dev/null 2>&1 &&
     ! spirv-val --target-env vulkan1.2 "$RAYTRACE_TMP" >/dev/null; then
  RAYTRACE_VALID=0
fi

if [ "$RAYTRACE_VALID" -eq 1 ] &&
   "$GLSLANG" -V --target-env vulkan1.2 --vn raytrace_comp_spv \
      -o "$OUT/raytrace_comp.spv.h" "$HERE/raytrace.comp" 2>/dev/null; then
  echo "==> raytrace.comp -> embedded/raytrace_comp.spv.h (ray query ENABLED)"
else
  rm -f "$OUT/raytrace_comp.spv.h"
  echo "==> WARNING: '$GLSLANG' cannot produce valid GL_EXT_ray_query SPIR-V"
  echo "    (compile or spirv-val failed) — RT shader omitted. Use a newer"
  echo "    glslang (examples/common/build-glslang.sh) to enable Vulkan RT."
fi
cleanup_raytrace_tmp
trap - EXIT

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
