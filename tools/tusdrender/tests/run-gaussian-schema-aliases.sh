#!/usr/bin/env bash
set -uo pipefail

TUSDRENDER="${1:?usage: $0 /path/to/tusdrender}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

log="$("$TUSDRENDER" "$ROOT/tests/usda/tusdview-gaussian-half.usda" \
  "$TMP/half.png" -rtPreview -w 64 -height 64 -autoframe -stats 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "$log"
  echo "FAIL: half-precision Gaussian CPU render failed"
  exit 1
fi
echo "$log"
if ! grep -q 'native Gaussian ellipses: 2 in 1 chunk(s)' <<<"$log"; then
  echo "FAIL: half-precision Gaussian properties were not selected"
  exit 1
fi
if [ ! -s "$TMP/half.png" ]; then
  echo "FAIL: half-precision Gaussian render wrote no image"
  exit 1
fi
echo "PASS: float/half ParticleField schema selection reached tusdrender"
