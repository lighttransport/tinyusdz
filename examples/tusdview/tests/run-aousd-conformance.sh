#!/usr/bin/env bash
#
# AOUSD conformance suite: load every AOUSD fixture through both loaders and
# assert no crashes.  External data — skip when the suite is not mounted.
set -uo pipefail
SKIP=77

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build/tusdview}"
[ -x "$BIN" ] || BIN="$ROOT/build_ninja/tusdview"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }

AOUSD_ROOT="${AOUSD_ROOT:-$ROOT/aousd}"
[ -d "$AOUSD_ROOT" ] || { echo "SKIP: AOUSD_ROOT=$AOUSD_ROOT not found"; exit "$SKIP"; }
[ -f "$AOUSD_ROOT/README.md" ] || { echo "SKIP: $AOUSD_ROOT does not look like the AOUSD suite"; exit "$SKIP"; }

RUN=()
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a)
fi

find "$AOUSD_ROOT" -type f \( -name '*.usda' -o -name '*.usdc' \) \
  ! -path '*/OpenUSD/*' ! -path '*/dist_minimal/*' ! -path '*/cpp_cmake/*' \
  ! -path '*/cpp_makefile/*' ! -path '*/build/*' -print0 | while IFS= read -r -d '' f; do
  base="$(basename "$f")"
  # Skip the crate writer output (test_data.usdc is a known-off spec example).
  [[ "$base" == "test_output.usdc" ]] && continue
  for loader in next legacy; do
    args=()
    [ "$loader" = legacy ] && args+=(--legacy-load)
    LOG="$("${RUN[@]}" "$BIN" --headless --backend vk --frames 1 "${args[@]}" "$f" 2>&1)"
    rc=$?
    if [ "$rc" -ne 0 ]; then
      # Backend unavailable = skip, not fail.
      if grep -Eiq 'no Vulkan|Vulkan.*unavailable|renderer init failed' <<<"$LOG"; then
        echo "SKIP: $loader $f (no Vulkan backend)"
        exit "$SKIP"
      fi
      echo "FAIL: $loader $f (exit $rc)"
      echo "$LOG"
      exit 1
    fi
  done
  echo "PASS: $base (both loaders)"
done

echo "PASS: AOUSD conformance suite"
exit 0
