#!/usr/bin/env bash
#
# Clone + build the Khronos glslang reference compiler and install it locally to
# examples/common/glslang. tusdview's Vulkan ray-tracing shader (ray query) needs
# a glslang new enough to understand GL_EXT_ray_query, which many distro packages
# (and older Vulkan SDKs) predate. Build it once with this script, then configure
# tusdview normally: vk/vulkan.cmake auto-detects examples/common/glslang/bin and
# uses it in preference to the system compiler.
#
# Linux only for now. Requires: git, cmake, a C++17 compiler, python3.
#
# Usage:
#   examples/common/build-glslang.sh [git-ref]     # default ref: 14.3.0
#   FORCE=1 examples/common/build-glslang.sh        # rebuild even if installed
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GLSLANG_REF="${1:-14.3.0}"
SRC="$HERE/glslang-src"
BUILD="$SRC/build"
PREFIX="$HERE/glslang"
JOBS="$(nproc 2>/dev/null || echo 4)"

case "$(uname -s)" in
  Linux) ;;
  *) echo "build-glslang.sh: Linux only for now (saw $(uname -s)). Install a"
     echo "  glslangValidator with GL_EXT_ray_query support manually and pass"
     echo "  -DTUSDVIEW_GLSLANG=/path to CMake."; exit 1 ;;
esac

bin="$PREFIX/bin/glslangValidator"
[ -x "$bin" ] || bin="$PREFIX/bin/glslang"
if [ -x "$bin" ] && [ -z "${FORCE:-}" ]; then
  echo "glslang already installed: $bin"
  "$bin" --version || true
  exit 0
fi

for tool in git cmake python3; do
  command -v "$tool" >/dev/null 2>&1 || { echo "error: '$tool' not found in PATH"; exit 1; }
done

# Fetch the source (shallow). Fall back to the default branch if the tag is absent.
if [ ! -d "$SRC/.git" ]; then
  echo "==> cloning glslang ($GLSLANG_REF) ..."
  git clone --depth 1 --branch "$GLSLANG_REF" \
      https://github.com/KhronosGroup/glslang.git "$SRC" \
    || git clone --depth 1 https://github.com/KhronosGroup/glslang.git "$SRC"
fi

# Pull in the in-tree SPIRV-Tools/headers glslang expects (no system deps needed).
echo "==> fetching glslang's bundled sources ..."
( cd "$SRC" && python3 update_glslang_sources.py ) || \
  echo "    (update_glslang_sources.py failed; continuing with ENABLE_OPT=0)"

echo "==> configuring + building (install prefix: $PREFIX) ..."
cmake -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_OPT=0 \
  -DGLSLANG_TESTS=OFF \
  -DENABLE_CTEST=OFF \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD" -j"$JOBS" --target install

bin="$PREFIX/bin/glslangValidator"
[ -x "$bin" ] || bin="$PREFIX/bin/glslang"
if [ ! -x "$bin" ]; then
  echo "error: build finished but no glslang binary at $PREFIX/bin"; exit 1
fi
echo "==> installed: $bin"
"$bin" --version

# Quick self-check: can it compile the ray-query shader?
rt="$HERE/../tusdview/vk/shaders/raytrace.comp"
if [ -f "$rt" ]; then
  if "$bin" -V --target-env vulkan1.2 -o /tmp/_rq_check.spv "$rt" >/dev/null 2>&1; then
    echo "==> OK: this glslang compiles GL_EXT_ray_query. Reconfigure tusdview to enable RT."
    rm -f /tmp/_rq_check.spv
  else
    echo "==> WARNING: this glslang still cannot compile $rt — try a newer ref."
  fi
fi
