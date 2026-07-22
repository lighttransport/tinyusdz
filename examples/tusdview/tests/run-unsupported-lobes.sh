#!/usr/bin/env bash
# Both scene loaders must preserve supported OpenPBR inputs while reporting
# advanced lobes that the real-time evaluators currently omit.
set -uo pipefail

TUSDVIEW="${1:?usage: $0 /path/to/tusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/tusdview /repo/root}"
ASSET="$ROOT/tests/usda/tusdview-unsupported-realtime-lobes.usda"
TMP="$(mktemp -d /tmp/tusdview-unsupported-lobes.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

for loader in next legacy; do
  args=()
  if [ "$loader" = legacy ]; then args+=(--legacy-load); fi
  log="$(timeout 30s "$TUSDVIEW" --headless --backend vk --frames 1 \
    --screenshot "$TMP/$loader.ppm" "${args[@]}" "$ASSET" 2>&1)"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "$log"
    echo "FAIL: $loader unsupported-lobe load failed"
    exit 1
  fi
  if ! grep -Eq 'load summary:.*unsupported_lobes=1' <<<"$log"; then
    echo "$log"
    echo "FAIL: $loader did not report one structured unsupported-lobe record"
    exit 1
  fi
  for lobe in transmission subsurface sheen/fuzz thin-film anisotropy; do
    if ! grep -Fq "$lobe" <<<"$log"; then
      echo "$log"
      echo "FAIL: $loader diagnostic omitted $lobe"
      exit 1
    fi
  done
done

echo "PASS: default and legacy loaders diagnose unsupported real-time lobes"
