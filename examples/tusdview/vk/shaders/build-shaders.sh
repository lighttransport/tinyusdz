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
if "$GLSLANG" -V --target-env vulkan1.2 --vn raytrace_comp_spv \
      -o "$OUT/raytrace_comp.spv.h" "$HERE/raytrace.comp" 2>/dev/null; then
  echo "==> raytrace.comp -> embedded/raytrace_comp.spv.h (ray query ENABLED)"
else
  rm -f "$OUT/raytrace_comp.spv.h"
  echo "==> WARNING: '$GLSLANG' cannot compile GL_EXT_ray_query — RT shader"
  echo "    omitted. Use a newer glslang (examples/common/build-glslang.sh) to"
  echo "    enable the Vulkan ray-query path."
fi

# --- RT GPU-skinning compute shader (optional) ------------------------------
# Skins the BLAS vertex stream on the GPU for the ray-query path (same
# vulkan1.2 / buffer_reference needs as raytrace.comp). Absent -> per-frame
# RT skinning falls back to the CPU path.
if "$GLSLANG" -V --target-env vulkan1.2 --vn skin_comp_spv \
      -o "$OUT/skin_comp.spv.h" "$HERE/skin.comp" 2>/dev/null; then
  echo "==> skin.comp -> embedded/skin_comp.spv.h (RT GPU skinning ENABLED)"
else
  rm -f "$OUT/skin_comp.spv.h"
  echo "==> WARNING: '$GLSLANG' cannot compile skin.comp — RT GPU compute"
  echo "    skinning omitted (per-frame CPU skinning fallback)."
fi

echo "==> done. Commit $OUT/*.spv.h"
