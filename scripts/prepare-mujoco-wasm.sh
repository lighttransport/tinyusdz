#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CACHE_DIR="${LIGHTUSD_VERIFY_CACHE:-${ROOT_DIR}/.cache/lightusd-verification}"
SRC_DIR="${CACHE_DIR}/mujoco"
BUILD_DIR="${CACHE_DIR}/mujoco-build"
MANIFEST="${LIGHTUSD_VERIFY_MANIFEST:-${ROOT_DIR}/tests/verification/manifest.json}"
CHECKOUT_ONLY=0
BUILD_ONLY=0
OFFLINE="${LIGHTUSD_VERIFY_OFFLINE:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --checkout-only) CHECKOUT_ONLY=1; shift ;;
    --build-only) BUILD_ONLY=1; shift ;;
    --cache-dir) CACHE_DIR="$2"; SRC_DIR="${CACHE_DIR}/mujoco"; BUILD_DIR="${CACHE_DIR}/mujoco-build"; shift 2 ;;
    --manifest) MANIFEST="$2"; shift 2 ;;
    --offline) OFFLINE=1; shift ;;
    -h|--help)
      echo "Usage: scripts/prepare-mujoco-wasm.sh [--checkout-only|--build-only] [--offline] [--cache-dir DIR] [--manifest FILE]"
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

REPO="$(node -e 'console.log(require(process.argv[1]).repositories.mujoco.url)' "${MANIFEST}")"
REF="$(node -e 'console.log(require(process.argv[1]).repositories.mujoco.ref)' "${MANIFEST}")"

if [[ ! -d "${SRC_DIR}/.git" ]]; then
  [[ "${BUILD_ONLY}" == 1 ]] && { echo "build-only: missing MuJoCo checkout ${SRC_DIR}" >&2; exit 2; }
  [[ "${OFFLINE}" == 1 ]] && { echo "offline: missing MuJoCo checkout ${SRC_DIR}" >&2; exit 2; }
  mkdir -p "$(dirname "${SRC_DIR}")"
  git clone --no-tags "${REPO}" "${SRC_DIR}"
fi
if [[ "${BUILD_ONLY}" != 1 && "${OFFLINE}" != 1 ]]; then
  git -C "${SRC_DIR}" fetch --no-tags origin "${REF}"
fi
if ! git -C "${SRC_DIR}" cat-file -e "${REF}^{commit}" 2>/dev/null; then
  echo "MuJoCo revision is not cached: ${REF}" >&2
  exit 2
fi
git -C "${SRC_DIR}" checkout --quiet --detach "${REF}"

if [[ "${CHECKOUT_ONLY}" == 1 ]]; then
  echo "MuJoCo checkout ready: ${SRC_DIR} (${REF})"
  exit 0
fi

# A CMake build tree is permanently associated with its absolute source path.
# Preserve a tree configured from another checkout and select a source/ref-
# specific fallback instead of deleting user artifacts or failing CMake's
# source-directory check.
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  CACHED_SOURCE="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" | tail -n 1)"
  CURRENT_SOURCE="$(cd "${SRC_DIR}" && pwd -P)"
  if [[ -d "${CACHED_SOURCE}" ]]; then
    CACHED_SOURCE="$(cd "${CACHED_SOURCE}" && pwd -P)"
  fi
  if [[ -n "${CACHED_SOURCE}" && "${CACHED_SOURCE}" != "${CURRENT_SOURCE}" ]]; then
    BUILD_DIR="${SRC_DIR}-build-${REF:0:12}"
    echo "MuJoCo build cache source mismatch (${CACHED_SOURCE}); using ${BUILD_DIR}"
  fi
fi

CMAKE_FETCHCONTENT_DISCONNECTED=OFF
if [[ "${OFFLINE}" == 1 ]]; then
  CMAKE_FETCHCONTENT_DISCONNECTED=ON
fi

emcmake cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DMUJOCO_BUILD_TESTS_WASM=OFF \
  -DMUJOCO_WASM_THREADS=OFF \
  -DMUJOCO_WASM_EMIT_TSD=OFF \
  -DMUJOCO_ENABLE_PNG=OFF \
  -DMUJOCO_ENABLE_FETCHCONTENT=ON \
  -DMUJOCO_PHYSICS_ENABLE_THREADS=OFF \
  -DMUJOCO_PHYSICS_ENABLE_EXCEPTIONS=OFF \
  -DMUJOCO_PHYSICS_ENABLE_PLUGINS=OFF \
  -DFETCHCONTENT_FULLY_DISCONNECTED="${CMAKE_FETCHCONTENT_DISCONNECTED}"
cmake --build "${BUILD_DIR}" --target mujoco_physics_wasm --parallel "${JOBS:-8}"

DIST_DIR="${SRC_DIR}/wasm/dist"
for artifact in mujoco_physics.js mujoco_physics.wasm; do
  [[ -s "${DIST_DIR}/${artifact}" ]] || { echo "missing MuJoCo artifact: ${DIST_DIR}/${artifact}" >&2; exit 1; }
done
echo "MuJoCo physics WASM ready: ${DIST_DIR}"
