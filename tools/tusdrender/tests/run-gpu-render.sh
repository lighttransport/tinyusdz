#!/usr/bin/env bash
#
# Run test: tusdrender's GPU compute/raytrace backends must render a non-blank
# frame AND agree byte-for-byte with each other. -vk (Vulkan compute), -vkr
# (Vulkan hardware ray query) and -hip (HIP/ROCm compute) all traverse the SAME
# LightRT BVH with the same shading, so a correct render is byte-identical across
# whichever backends are available on this machine. This is the cross-backend
# correctness guard for the GPU paths (the smoke test only exercises the CPU
# -rtPreview path).
#
# Each backend is runtime-loaded and degrades gracefully: a backend that cannot
# create its device/engine prints a diagnostic and never emits the success line
# "backend: LightRT ...", so it is simply skipped here. If NO GPU backend is
# available the whole test SKIPs (exit 77, ctest SKIP_RETURN_CODE) so headless /
# non-GPU CI stays green.
#
# Exit codes: 0 = pass, 1 = fail (blank, or backends disagree), 77 = skip.
#
# Usage:  run-gpu-render.sh [TUSDRENDER_BINARY] [ASSET]
#   args override the TUSDRENDER / ASSET env vars; both have sensible defaults.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDRENDER="${1:-${TUSDRENDER:-$REPO_ROOT/build/tools/tusdrender/tusdrender}}"
ASSET="${2:-${ASSET:-$REPO_ROOT/models/suzanne-pbr.usda}}"

if [ ! -x "$TUSDRENDER" ]; then
  echo "SKIP: tusdrender binary not found at $TUSDRENDER"
  exit $SKIP
fi
if [ ! -f "$ASSET" ]; then
  echo "SKIP: asset not found at $ASSET"
  exit $SKIP
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Render one backend. Returns 0 and sets REPLY to the output path when the
# backend rendered successfully (printed the "backend: LightRT" success line and
# wrote a non-trivial file); returns 1 when the backend is unavailable.
render() {
  local flag="$1" name="$2"
  local out="$TMP/gpu_${name}.png"
  local log
  log="$("$TUSDRENDER" "$ASSET" "$out" "$flag" -w 200 -height 150 -autoframe \
        -samples 2 2>&1)"
  if ! echo "$log" | grep -q "backend: LightRT"; then
    echo "  $name: unavailable (skipped)"
    return 1
  fi
  if [ ! -s "$out" ] || [ "$(wc -c < "$out")" -lt 2000 ]; then
    echo "FAIL: $name produced a blank/trivial image"
    REPLY="FAIL"
    return 0
  fi
  echo "  $name: rendered $(wc -c < "$out") bytes"
  REPLY="$out"
  return 0
}

echo "=== tusdrender GPU backends ==="
ok=0
ref=""
fail=0
for spec in "-vk vk" "-vkr vkr" "-hip hip"; do
  set -- $spec
  if render "$1" "$2"; then
    [ "$REPLY" = "FAIL" ] && { fail=1; continue; }
    ok=$((ok + 1))
    if [ -z "$ref" ]; then
      ref="$REPLY"
    else
      if ! cmp -s "$ref" "$REPLY"; then
        echo "FAIL: $2 output differs from the first GPU backend (cross-backend mismatch)"
        fail=1
      fi
    fi
  fi
done

[ "$fail" -ne 0 ] && exit 1
if [ "$ok" -eq 0 ]; then
  echo "SKIP: no GPU backend available in this environment"
  exit $SKIP
fi

echo "PASS: $ok GPU backend(s) render non-blank and agree byte-for-byte"
exit 0
