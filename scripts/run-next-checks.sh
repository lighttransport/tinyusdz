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
  RUN_BENCH            Run opt-in memory/perf benchmarks when nonzero
  RUN_CORPUS           Run opt-in usd-wg asset corpus gate when nonzero
  USD_WG_ASSETS_DIR    usd-wg/assets checkout for RUN_CORPUS
  RUN_DETERMINISM      Run opt-in flatten-output determinism gate when nonzero
  DET_SCENES           Space-separated scene files for RUN_DETERMINISM
                       (flattened DET_RUNS times each; all sha256 must match)
  DET_RUNS             Flatten repetitions per scene (default: 3)
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
RUN_DETERMINISM="${RUN_DETERMINISM:-0}"
DET_SCENES="${DET_SCENES:-}"
DET_RUNS="${DET_RUNS:-3}"
THREADS="${THREADS:-OFF}"
BENCH_LAZY_FILE_GENERATED=0
DET_TMP=""

cleanup_bench_file() {
  if [ "$BENCH_LAZY_FILE_GENERATED" = "1" ] && [ "${KEEP_BENCH_TMP:-0}" = "0" ]; then
    rm -f "$BENCH_LAZY_FILE"
  fi
  if [ -n "$DET_TMP" ]; then
    rm -f "$DET_TMP"
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

# Flatten-output determinism gate: flatten each scene DET_RUNS times and
# require every run's sha256 to match. Guards the class of bugs where unstable
# interning order or uninitialized bytes leak into the crate output (see the
# PropIndex slot-sort and double[] scalar-encode fixes).
if [ "$RUN_DETERMINISM" != "0" ]; then
  if [ -z "$DET_SCENES" ]; then
    echo "RUN_DETERMINISM=1 requires DET_SCENES=\"<scene.usd[acz]> ...\"" >&2
    exit 1
  fi
  DET_TMP="${TMPDIR:-/tmp}/tinyusdz-next-det-$$.usdc"
  for scene in $DET_SCENES; do
    ref_hash=""
    for i in $(seq 1 "$DET_RUNS"); do
      "$BUILD_DIR/next_usdcat" -f "$scene" -o "$DET_TMP" >/dev/null
      run_hash="$(sha256sum "$DET_TMP" | cut -d' ' -f1)"
      if [ -z "$ref_hash" ]; then
        ref_hash="$run_hash"
      elif [ "$run_hash" != "$ref_hash" ]; then
        echo "DETERMINISM FAILURE: $scene run $i sha256 $run_hash != $ref_hash" >&2
        exit 1
      fi
    done
    echo "determinism OK: $scene ($DET_RUNS runs, sha256 $ref_hash)"
  done
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
