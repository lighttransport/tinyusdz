#!/usr/bin/env bash
#
# Driver-level regression for variant selection across a reference -> payload ->
# reference chain (the Pixar Kitchen_set Chair.usd shape).
#
# The C++ feat-variant-payload-chain test drives the COMPOSITION LIBRARY with its
# own deferred iteration loop; it does NOT exercise the tusdcat *driver*. This
# runs the tusdcat binary end-to-end so that reverting the variant-deferral in
# the driver loop (examples/tusdcat/main.cc) — or in the shared
# ShouldDeferVariantComposition() helper — is caught: without deferral the strong
# local selection "ChairB" is consumed against empty variant blocks and the deep
# default "ChairA" wins.
#
# Usage: run-variant-chain.sh <path-to-tusdcat> <project-source-dir>

set -u

TUSDCAT="${1:?usage: run-variant-chain.sh <tusdcat> <srcdir>}"
SRCDIR="${2:?usage: run-variant-chain.sh <tusdcat> <srcdir>}"
MAIN="${SRCDIR}/tests/usda/feat-variant-chain-main.usda"

if [ ! -x "${TUSDCAT}" ]; then
  echo "SKIP: tusdcat not found at ${TUSDCAT}"
  exit 0
fi
if [ ! -f "${MAIN}" ]; then
  echo "FAIL: fixture not found: ${MAIN}"
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
OUT="${WORK}/flattened.usda"

if ! "${TUSDCAT}" --flatten "${MAIN}" -o "${OUT}" >/dev/null 2>&1; then
  echo "FAIL: tusdcat --flatten failed on ${MAIN}"
  exit 1
fi

# The flattened output must carry the selected (non-default) variant's opinion
# and must NOT carry the default variant's opinion (variant sets are consumed on
# flatten, so the unselected block does not leak).
if grep -q 'I_am_ChairB' "${OUT}" && ! grep -q 'I_am_ChairA' "${OUT}"; then
  echo "PASS: tusdcat selected non-default variant ChairB across ref->payload->ref chain"
  exit 0
fi

echo "FAIL: expected I_am_ChairB (and not I_am_ChairA) in flattened output."
echo "--- 'which' opinions in flattened output ---"
grep 'which' "${OUT}" || echo "(none)"
exit 1
