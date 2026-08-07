#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CACHE_DIR="${TINYUSDZ_VERIFY_CACHE:-${ROOT_DIR}/.cache/tinyusdz-verification}"
SRC_DIR="${CACHE_DIR}/mujoco"
BUILD_DIR="${CACHE_DIR}/mujoco-build"
MANIFEST="${TINYUSDZ_VERIFY_MANIFEST:-${ROOT_DIR}/tests/verification/manifest.json}"
CHECKOUT_ONLY=0
BUILD_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --checkout-only) CHECKOUT_ONLY=1; shift ;;
    --build-only) BUILD_ONLY=1; shift ;;
    --cache-dir) CACHE_DIR="$2"; SRC_DIR="${CACHE_DIR}/mujoco"; BUILD_DIR="${CACHE_DIR}/mujoco-build"; shift 2 ;;
    --manifest) MANIFEST="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: scripts/prepare-mujoco-wasm.sh [--checkout-only|--build-only] [--cache-dir DIR] [--manifest FILE]"
      exit 0
      ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

REPO="$(node -e 'console.log(require(process.argv[1]).repositories.mujoco.url)' "${MANIFEST}")"
REF="$(node -e 'console.log(require(process.argv[1]).repositories.mujoco.ref)' "${MANIFEST}")"

if [[ ! -d "${SRC_DIR}/.git" ]]; then
  [[ "${BUILD_ONLY}" == 1 ]] && { echo "build-only: missing MuJoCo checkout ${SRC_DIR}" >&2; exit 2; }
  [[ "${TINYUSDZ_VERIFY_OFFLINE:-0}" == 1 ]] && { echo "offline: missing MuJoCo checkout ${SRC_DIR}" >&2; exit 2; }
  mkdir -p "$(dirname "${SRC_DIR}")"
  git clone --no-tags "${REPO}" "${SRC_DIR}"
fi
if [[ "${BUILD_ONLY}" != 1 && "${TINYUSDZ_VERIFY_OFFLINE:-0}" != 1 ]]; then
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

emcmake cmake -S "${SRC_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DMUJOCO_BUILD_TESTS_WASM=OFF \
  -DMUJOCO_WASM_THREADS=OFF \
  -DMUJOCO_ENABLE_PNG=OFF \
  -DMUJOCO_ENABLE_FETCHCONTENT=ON \
  -DMUJOCO_PHYSICS_ENABLE_THREADS=OFF \
  -DMUJOCO_PHYSICS_ENABLE_EXCEPTIONS=OFF \
  -DMUJOCO_PHYSICS_ENABLE_PLUGINS=OFF
cmake --build "${BUILD_DIR}" --target mujoco_physics_wasm --parallel "${JOBS:-8}"

DIST_DIR="${SRC_DIR}/wasm/dist"
for artifact in mujoco_physics.js mujoco_physics.wasm; do
  [[ -s "${DIST_DIR}/${artifact}" ]] || { echo "missing MuJoCo artifact: ${DIST_DIR}/${artifact}" >&2; exit 1; }
done
echo "MuJoCo physics WASM ready: ${DIST_DIR}"
