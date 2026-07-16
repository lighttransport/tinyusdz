#!/usr/bin/env bash
# Fetch the AOUSD Core supplemental conformance corpus, pinned.
#
# The corpus is intentionally NOT vendored into this repository (it has its
# own license and release cadence). This script clones the public release
# repo at a pinned tag into a stable cache path and prints the suite root to
# pass as -DTINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT / AOUSD_CORE_SUPPLEMENTAL_ROOT:
#
#   ~/.cache/tinyusdz/core-spec-supplemental-release_dec2025/releases/1.0.1
#
# Pin: tag release/1.0.1.post0 = commit c15ae0cad3ed9e07a25dffd6699627d2c166cab0
# (the dec2025 release; its releases/1.0.1 payload dirs are byte-identical to
# the previously used checkout). Idempotent: verifies the pin if the clone
# already exists and only re-clones on mismatch.
set -euo pipefail

REPO_URL="https://github.com/aousd/core-spec-supplemental-public"
PIN_TAG="release/1.0.1.post0"
PIN_COMMIT="c15ae0cad3ed9e07a25dffd6699627d2c166cab0"
CACHE_DIR="${AOUSD_SUPPLEMENTAL_CACHE:-${HOME}/.cache/tinyusdz/core-spec-supplemental-release_dec2025}"
SUITE_ROOT="${CACHE_DIR}/releases/1.0.1"

current_commit() {
  git -C "${CACHE_DIR}" rev-parse HEAD 2>/dev/null || true
}

if [[ "$(current_commit)" != "${PIN_COMMIT}" ]]; then
  echo "[fetch-aousd-supplemental] Cloning ${REPO_URL} @ ${PIN_TAG}" >&2
  rm -rf "${CACHE_DIR}"
  mkdir -p "$(dirname "${CACHE_DIR}")"
  git clone --quiet --depth 1 --branch "${PIN_TAG}" "${REPO_URL}" "${CACHE_DIR}"
  if [[ "$(current_commit)" != "${PIN_COMMIT}" ]]; then
    echo "error: cloned HEAD $(current_commit) does not match pin ${PIN_COMMIT}" >&2
    exit 1
  fi
else
  echo "[fetch-aousd-supplemental] Pin ${PIN_COMMIT} already present" >&2
fi

for d in composition data_types file_formats value_resolution; do
  if [[ ! -d "${SUITE_ROOT}/${d}" ]]; then
    echo "error: expected corpus directory missing: ${SUITE_ROOT}/${d}" >&2
    exit 1
  fi
done

# run-aousd-supplemental.py sanity-checks <suite-root>/LICENSE; the release
# subdirectory does not carry its own copy, so mirror the repo-root LICENSE.
if [[ ! -f "${SUITE_ROOT}/LICENSE" ]]; then
  cp "${CACHE_DIR}/LICENSE" "${SUITE_ROOT}/LICENSE"
fi

echo "${SUITE_ROOT}"
