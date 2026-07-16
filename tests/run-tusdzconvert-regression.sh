#!/usr/bin/env bash
#
# Driver-level regression coverage for the native tusdzconvert executable.
#
# Usage: run-tusdzconvert-regression.sh <tusdzconvert> <tusdcat> <project-source-dir>

set -u

TUSDZCONVERT="${1:?usage: run-tusdzconvert-regression.sh <tusdzconvert> <tusdcat> <srcdir>}"
TUSDCAT="${2:?usage: run-tusdzconvert-regression.sh <tusdzconvert> <tusdcat> <srcdir>}"
SRCDIR="${3:?usage: run-tusdzconvert-regression.sh <tusdzconvert> <tusdcat> <srcdir>}"
FIXTURE="${SRCDIR}/tests/unit/fixtures/usdzconvert-regression-textured-two-materials.usda"

if [ ! -x "${TUSDZCONVERT}" ]; then
  echo "FAIL: tusdzconvert not executable: ${TUSDZCONVERT}"
  exit 1
fi
if [ ! -x "${TUSDCAT}" ]; then
  echo "FAIL: tusdcat not executable: ${TUSDCAT}"
  exit 1
fi
if [ ! -f "${FIXTURE}" ]; then
  echo "FAIL: fixture not found: ${FIXTURE}"
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cp "${FIXTURE}" "${WORK}/scene.usda" || exit 1

# 1x1 transparent PNG, generated at test time to keep the fixture text-only.
if command -v base64 >/dev/null 2>&1; then
  printf '%s' 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNgYGD4DwABBAEAgi7R9wAAAABJRU5ErkJggg==' \
    | base64 -d > "${WORK}/Texture.png" 2>/dev/null
else
  echo "FAIL: base64 command is required by this regression"
  exit 1
fi

OUT_USDZ="${WORK}/out.usdz"
OUT_USDA="${WORK}/out.usda"

if ! "${TUSDZCONVERT}" "${WORK}/scene.usda" "${OUT_USDZ}" \
    -textureFormat jpeg -optimizeMaterials preview >/dev/null 2>&1; then
  echo "FAIL: tusdzconvert conversion failed"
  exit 1
fi

if [ ! -s "${OUT_USDZ}" ]; then
  echo "FAIL: tusdzconvert produced no output"
  exit 1
fi

if ! "${TUSDCAT}" "${OUT_USDZ}" -o "${OUT_USDA}" >/dev/null 2>&1; then
  echo "FAIL: tusdcat failed to read converted USDZ"
  exit 1
fi

if ! grep -q 'Texture.jpg' "${OUT_USDA}"; then
  echo "FAIL: converted layer did not remap texture asset to JPEG"
  grep 'inputs:file' "${OUT_USDA}" || true
  exit 1
fi

if grep -q 'Texture.png' "${OUT_USDA}"; then
  echo "FAIL: converted layer retained stale PNG texture reference"
  grep 'inputs:file' "${OUT_USDA}" || true
  exit 1
fi

material_count="$(grep -c 'def Material' "${OUT_USDA}")"
if [ "${material_count}" -ne 1 ]; then
  echo "FAIL: expected material preview dedupe to leave 1 material, got ${material_count}"
  grep 'def Material' "${OUT_USDA}" || true
  exit 1
fi

echo "PASS: tusdzconvert texture remap and material dedupe regression"
