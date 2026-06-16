#!/usr/bin/env bash
#
# Driver-level regression for variant selection across a reference -> payload ->
# reference chain (the Pixar Kitchen_set Chair.usd shape).
#
# The C++ feat-variant-payload-chain test drives the COMPOSITION LIBRARY with its
# own deferred iteration loop; it does NOT exercise the binary flatten *drivers*.
# This runs each driver binary end-to-end so that reverting the variant-deferral
# in a driver loop — or in the shared ShouldDeferVariantComposition() helper —
# is caught: without deferral the strong local selection "ChairB" is consumed
# against empty variant blocks and the deep default "ChairA" wins.
#
# Covers:
#   - tusdcat       (examples/tusdcat, the native flatten driver)
#   - tusdzconvert  (tools/tusdzconvert -> src/usdz-convert.cc flatten loop)
#
# Usage: run-variant-chain.sh <tusdcat> <project-source-dir> [tusdzconvert]

set -u

TUSDCAT="${1:?usage: run-variant-chain.sh <tusdcat> <srcdir> [tusdzconvert]}"
SRCDIR="${2:?usage: run-variant-chain.sh <tusdcat> <srcdir> [tusdzconvert]}"
TUSDZCONVERT="${3:-}"
MAIN="${SRCDIR}/tests/usda/feat-variant-chain-main.usda"

if [ ! -f "${MAIN}" ]; then
  echo "FAIL: fixture not found: ${MAIN}"
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# check_flattened <label> <flattened-usda>: the output must carry the selected
# (non-default) variant's opinion and must NOT carry the default variant's
# opinion (variant sets are consumed on flatten, so the unselected block does not
# leak). Returns 0 on success, 1 on failure.
check_flattened() {
  local label="$1" out="$2"
  if [ ! -s "${out}" ]; then
    echo "FAIL: ${label}: produced no/empty output"
    return 1
  fi
  if grep -q 'I_am_ChairB' "${out}" && ! grep -q 'I_am_ChairA' "${out}"; then
    echo "PASS: ${label}: selected non-default variant ChairB across ref->payload->ref chain"
    return 0
  fi
  echo "FAIL: ${label}: expected I_am_ChairB (and not I_am_ChairA) in flattened output."
  echo "--- 'which' opinions ---"
  grep 'which' "${out}" || echo "(none)"
  return 1
}

rc=0

# --- tusdcat ---
if [ -x "${TUSDCAT}" ]; then
  OUT="${WORK}/tusdcat.usda"
  if "${TUSDCAT}" --flatten "${MAIN}" -o "${OUT}" >/dev/null 2>&1; then
    check_flattened "tusdcat" "${OUT}" || rc=1
  else
    echo "FAIL: tusdcat --flatten failed on ${MAIN}"; rc=1
  fi
else
  echo "SKIP: tusdcat not found at ${TUSDCAT}"
fi

# --- tusdzconvert (src/usdz-convert.cc flatten path) ---
if [ -n "${TUSDZCONVERT}" ] && [ -x "${TUSDZCONVERT}" ]; then
  OUT="${WORK}/tusdzconvert.usda"
  if "${TUSDZCONVERT}" "${MAIN}" "${OUT}" --outputFormat usda >/dev/null 2>&1; then
    check_flattened "tusdzconvert" "${OUT}" || rc=1
  else
    echo "FAIL: tusdzconvert flatten failed on ${MAIN}"; rc=1
  fi
elif [ -n "${TUSDZCONVERT}" ]; then
  echo "SKIP: tusdzconvert not found at ${TUSDZCONVERT}"
fi

exit ${rc}
