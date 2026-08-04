#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: scripts/run-next-checks.sh

Configure, build, and run the standalone src/next smoke tests.

Environment overrides:
  ROOT_DIR             Repository root (default: git top-level)
  BUILD_DIR            Build directory (default: $ROOT_DIR/build-next)
  JOBS                 Build parallelism (default: 16)
  BUILD_TYPE           CMake build type (default: Debug)
  THREADS              TINYUSDZ_NEXT_ENABLE_THREAD value (default: OFF)
  ALSO_THREADS         Also build+test a THREADS=ON pass (default: 1; 0 skips)
  THREADS_BUILD_DIR    Build dir for that pass (default: $BUILD_DIR-threads)
  RUN_BENCH            Run opt-in memory/perf benchmarks when nonzero
  RUN_CORPUS           Run opt-in usd-wg asset corpus gate when nonzero
  USD_WG_ASSETS_DIR    usd-wg/assets checkout for RUN_CORPUS
  USDCAT_PATH           Optional OpenUSD usdcat used by usdcat roundtrip tests
  BENCH_LAZY_VERTS     bench_lazy_mem generated vertex count (default: 100000)
  BENCH_LAZY_CLONES    bench_lazy_mem clone count (default: 8)
  BENCH_LAZY_FILE      Reuse/write this USDC for bench_lazy_mem
  KEEP_BENCH_TMP       Keep generated benchmark USDC when nonzero
EOF
  exit 0
fi

ROOT_DIR="${ROOT_DIR:-$(git rev-parse --show-toplevel)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-next}"
JOBS="${JOBS:-16}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
RUN_BENCH="${RUN_BENCH:-0}"
RUN_CORPUS="${RUN_CORPUS:-0}"
THREADS="${THREADS:-OFF}"
ALSO_THREADS="${ALSO_THREADS:-1}"
THREADS_BUILD_DIR="${THREADS_BUILD_DIR:-$BUILD_DIR-threads}"
BENCH_LAZY_FILE_GENERATED=0

cleanup_bench_file() {
  if [ "$BENCH_LAZY_FILE_GENERATED" = "1" ] && [ "${KEEP_BENCH_TMP:-0}" = "0" ]; then
    rm -f "$BENCH_LAZY_FILE"
  fi
}
trap cleanup_bench_file EXIT

cd "$ROOT_DIR"

cmake -S "$ROOT_DIR/src/next" -B "$BUILD_DIR" \
  -DTINYUSDZ_NEXT_BUILD_TESTS=ON \
  -DTINYUSDZ_NEXT_ENABLE_THREAD="$THREADS" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cmake --build "$BUILD_DIR" -j"$JOBS"

ctest --test-dir "$BUILD_DIR" --output-on-failure -L next -LE 'benchmark|corpus'

if [ "$RUN_CORPUS" != "0" ]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure -L corpus
fi

if [ "$RUN_BENCH" != "0" ]; then
  "$BUILD_DIR/bench_pcp_compose" sizes
  "$BUILD_DIR/bench_pcp_compose" compose
  "$BUILD_DIR/bench_pcp_compose" deep

  if [ -z "${BENCH_LAZY_FILE:-}" ]; then
    BENCH_LAZY_FILE="${TMPDIR:-/tmp}/tinyusdz-next-lazy-bench-$$.usdc"
    BENCH_LAZY_FILE_GENERATED=1
  fi
  BENCH_LAZY_VERTS="${BENCH_LAZY_VERTS:-100000}"
  BENCH_LAZY_CLONES="${BENCH_LAZY_CLONES:-8}"
  "$BUILD_DIR/bench_lazy_mem" gen "$BENCH_LAZY_FILE" "$BENCH_LAZY_VERTS"
  "$BUILD_DIR/bench_lazy_mem" eager "$BENCH_LAZY_FILE" "$BENCH_LAZY_CLONES"
  "$BUILD_DIR/bench_lazy_mem" lazy "$BENCH_LAZY_FILE" "$BENCH_LAZY_CLONES"
  "$BUILD_DIR/bench_lazy_mem" flat-lazy-owned "$BENCH_LAZY_FILE"
fi

# Second pass with threading ON.
#
# The library default is OFF, and correctly so: web/CMakeLists.txt forces it
# off because the WASM build must not require Emscripten pthreads /
# SharedArrayBuffer. The consequence is that a normal run of this script never
# COMPILES the std::thread paths -- the PropNameTable freeze/intern
# synchronisation and the parallel PCP prewarm live entirely behind
# TINYUSDZ_ENABLE_THREAD, so a change that breaks them passes every default
# check and only fails for the tools/examples that force it ON
# (tusdrender/tusdview/tusdquicklook).
#
# Set ALSO_THREADS=0 to skip (e.g. a machine without a thread library).
if [ "$ALSO_THREADS" != "0" ] && [ "$THREADS" != "ON" ]; then
  echo
  echo "=== second pass: TINYUSDZ_NEXT_ENABLE_THREAD=ON ==="
  cmake -S "$ROOT_DIR/src/next" -B "$THREADS_BUILD_DIR" \
    -DTINYUSDZ_NEXT_BUILD_TESTS=ON \
    -DTINYUSDZ_NEXT_ENABLE_THREAD=ON \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  cmake --build "$THREADS_BUILD_DIR" -j"$JOBS"
  ctest --test-dir "$THREADS_BUILD_DIR" --output-on-failure -L next \
    -LE 'benchmark|corpus'
fi
