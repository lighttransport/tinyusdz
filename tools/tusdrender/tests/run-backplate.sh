#!/usr/bin/env bash
set -euo pipefail

TUSDRENDER="${1:?tusdrender path required}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP="$(mktemp -d /tmp/tusdrender-backplate.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

VISIBLE="$ROOT/tests/usda/tusdrender-backplate-001.usda"
HIDDEN="$TMP/hidden.usda"
BEHIND="$TMP/behind.usda"
SINGLE="$TMP/single.usda"
sed 's/plateVisibility = "render"/plateVisibility = "invisible"/' \
  "$VISIBLE" > "$HIDDEN"
sed '/backPlate:hero:depth:/d' "$VISIBLE" > "$BEHIND"
sed -e 's/, "BackPlateAPI:accent"//' \
    -e '/backPlate:accent:/d' "$VISIBLE" > "$SINGLE"
mkdir -p "$TMP/textures"
cp "$ROOT/tests/usda/textures/alpha-billboard-bird.png" "$TMP/textures/"

COMMON=(-rtPreview -camera /World/Camera -w 64 -height 64 -samples 1 -threads 1)
"$TUSDRENDER" "$VISIBLE" "$TMP/visible.png" "${COMMON[@]}"
"$TUSDRENDER" "$HIDDEN" "$TMP/hidden.png" "${COMMON[@]}"
"$TUSDRENDER" "$BEHIND" "$TMP/behind.png" "${COMMON[@]}"
"$TUSDRENDER" "$SINGLE" "$TMP/single.png" "${COMMON[@]}"

if cmp -s "$TMP/visible.png" "$TMP/hidden.png"; then
  echo "FAIL: visible BackPlate produced the same image as an invisible plate"
  exit 1
fi
if cmp -s "$TMP/visible.png" "$TMP/behind.png"; then
  echo "FAIL: BackPlate depth did not change plate/geometry ordering"
  exit 1
fi
if cmp -s "$TMP/visible.png" "$TMP/single.png"; then
  echo "FAIL: second BackPlateAPI instance was not composited"
  exit 1
fi
echo "PASS: BackPlate color/alpha/depth compositing changes the offline render"
