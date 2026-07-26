#!/usr/bin/env bash
# Verify persistent CUDA PTX caching. The cold run uses the platform-default
# XDG cache path; the warm run names that same directory through the CLI.
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${TUSDVIEW:-$REPO_ROOT/build_ninja/tusdview}"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found at $BIN"; exit "$SKIP"; }

OUT="${TUSDVIEW_TEST_OUT:-$(mktemp -d)}"
mkdir -p "$OUT/xdg-cache" "$OUT/config-home"
if [ -z "${TUSDVIEW_TEST_OUT:-}" ]; then trap 'rm -rf "$OUT"' EXIT; fi
CONFIG="$OUT/config.json"
printf '%s\n' '{"window_size":{"width":160,"height":160}}' >"$CONFIG"
SCENE="$REPO_ROOT/examples/tusdview/tests/deform-morph-skin.usda"
CACHE_DIR="$OUT/xdg-cache/tusdview/cuda"

run_viewer() {
  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5s "${TUSDVIEW_CUDA_CACHE_TIMEOUT:-90s}" "$@"
  else
    "$@"
  fi
}

cold_img="$OUT/cold.png"
cold_log="$OUT/cold.log"
run_viewer env XDG_CACHE_HOME="$OUT/xdg-cache" XDG_CONFIG_HOME="$OUT/config-home" \
  "$BIN" --next --headless --cuda --mode depth --camera Cam --frames 1 \
  --time 1 --config "$CONFIG" --no-skeleton --screenshot "$cold_img" "$SCENE" \
  >"$cold_log" 2>&1
cold_rc=$?
if ! grep -q 'CUDA RT wrote' "$cold_log" || [ ! -s "$cold_img" ]; then
  if grep -q 'CUDA kernel cache miss' "$cold_log"; then
    echo "FAIL: CUDA/NVRTC was available but the cold cache run failed (exit $cold_rc)"
    cat "$cold_log"
    exit 1
  fi
  echo "SKIP: CUDA/NVRTC unavailable for kernel-cache regression (exit $cold_rc)"
  tail -20 "$cold_log"
  exit "$SKIP"
fi
grep -q 'CUDA kernel cached:' "$cold_log" || {
  echo "FAIL: cold CUDA run did not populate the default tusdview cache"
  cat "$cold_log"
  exit 1
}
ptx_count="$(find "$CACHE_DIR" -maxdepth 1 -type f -name '*.ptx' | wc -l)"
[ "$ptx_count" -eq 1 ] || {
  echo "FAIL: expected one PTX entry under $CACHE_DIR, found $ptx_count"
  exit 1
}

warm_img="$OUT/warm.png"
warm_log="$OUT/warm.log"
run_viewer env XDG_CONFIG_HOME="$OUT/config-home" \
  "$BIN" --next --headless --cuda --cuda-cache-dir "$CACHE_DIR" --mode depth \
  --camera Cam --frames 1 --time 1 --config "$CONFIG" --no-skeleton \
  --screenshot "$warm_img" "$SCENE" >"$warm_log" 2>&1
warm_rc=$?
if [ "$warm_rc" -ne 0 ] || ! grep -q 'CUDA RT wrote' "$warm_log" ||
   [ ! -s "$warm_img" ]; then
  echo "FAIL: warm CUDA cache run failed (exit $warm_rc)"
  cat "$warm_log"
  exit 1
fi
grep -q "CUDA kernel cache hit: $CACHE_DIR/" "$warm_log" || {
  echo "FAIL: --cuda-cache-dir run did not reuse the default-path PTX"
  cat "$warm_log"
  exit 1
}
cmp -s "$cold_img" "$warm_img" || {
  echo "FAIL: cached PTX changed CUDA pixels"
  exit 1
}

echo "PASS: default CUDA kernel cache and --cuda-cache-dir reuse exact pixels"
