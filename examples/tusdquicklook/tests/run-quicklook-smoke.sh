#!/usr/bin/env bash
# tusdquicklook headless smoke test.
#
# Renders a set of assets with --screenshot and checks that each PNG exists, is
# a real PNG, and is not a blank frame. Blank-frame detection matters: a broken
# tracer still writes a perfectly valid all-background PNG, so file existence
# alone proves nothing.
#
# usage: run-quicklook-smoke.sh <tusdquicklook-binary> <repo-root> [outdir]
set -euo pipefail

BIN="${1:?usage: run-quicklook-smoke.sh <binary> <repo-root> [outdir]}"
ROOT="${2:?missing repo root}"
OUT="${3:-${TMPDIR:-/tmp}/tusdquicklook-smoke.$$}"

mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

is_png() {
  head -c 8 "$1" | od -An -tx1 | tr -d ' \n' | grep -qi '^89504e470d0a1a0a$'
}

check_png() {
  local png="$1" label="$2"
  [ -s "$png" ] || fail "$label: no output at $png"
  is_png "$png" || fail "$label: $png is not a PNG"
}

# Proving geometry actually rendered needs a reference, not a colour histogram:
# the viewport gradient alone yields hundreds of distinct colours, so a "unique
# colours" threshold passes even when nothing was drawn. Instead, render an
# empty folder at the same size and require the asset frame to differ from it
# over a meaningful share of the viewport.
DIFF_PY="$(dirname "$0")/pngdiff.py"

check_renders_geometry() {
  local png="$1" ref="$2" min_frac="$3" label="$4"
  check_png "$png" "$label"
  command -v python3 >/dev/null 2>&1 || { echo "$label: python3 missing, skipping pixel check"; return; }
  [ -f "$DIFF_PY" ] || fail "$label: missing $DIFF_PY"
  local frac
  frac="$(python3 "$DIFF_PY" "$png" "$ref")" || fail "$label: pixel compare failed"
  awk -v f="$frac" -v m="$min_frac" 'BEGIN { exit !(f >= m) }' \
    || fail "$label: only ${frac} of the viewport differs from an empty render (need >= ${min_frac}) -- geometry did not draw"
  echo "$label: ok (${frac} of viewport differs from background)"
}

echo "== tusdquicklook smoke: $BIN"

# 1. UI chrome only: an empty directory still has to render and exit cleanly.
empty_dir="$OUT/empty"
mkdir -p "$empty_dir"
"$BIN" "$empty_dir" --screenshot "$OUT/empty.png" --size 480x320 >/dev/null \
  || fail "empty directory: non-zero exit"
check_png "$OUT/empty.png" "empty-dir"

# Background reference at the geometry-test size.
"$BIN" "$empty_dir" --screenshot "$OUT/bg.png" --size 480x360 >/dev/null \
  || fail "background reference: non-zero exit"
check_png "$OUT/bg.png" "background-reference"

# 2. Real geometry: each asset must visibly change the viewport.
i=0
rendered=0
for asset in \
    "$ROOT/models/cube-previewsurface.usda" \
    "$ROOT/models/suzanne-pbr.usda" \
    "$ROOT/data/ball_basketball_realistic.usdz" ; do
  [ -f "$asset" ] || continue
  i=$((i + 1))
  name="asset$i"
  "$BIN" "$asset" --screenshot "$OUT/$name.png" --size 480x360 --frames 4 \
    >/dev/null || fail "$asset: non-zero exit"
  check_renders_geometry "$OUT/$name.png" "$OUT/bg.png" 0.03 "$(basename "$asset")"
  rendered=$((rendered + 1))
done
[ "$rendered" -gt 0 ] || fail "no test assets found under $ROOT"

# 3. Budget degradation: a tight cap must still exit 0 and stay under the cap,
#    reporting the refusal rather than crashing or being OOM-killed.
if [ -f "$ROOT/data/ball_basketball_realistic.usdz" ]; then
  "$BIN" "$ROOT/data/ball_basketball_realistic.usdz" --max-mem 8 \
    --screenshot "$OUT/tight.png" --size 400x240 >/dev/null \
    || fail "tight budget: non-zero exit (should degrade, not fail)"
  check_png "$OUT/tight.png" "tight-budget"
  echo "tight budget: ok (exited cleanly at --max-mem 8)"
fi

# 3b. The budget has to bound real memory, not just a counter. Measure peak RSS
#     and require it to stay within a generous multiple of the cap -- generous
#     because the cap governs tracked preview data, while RSS also carries the
#     binary, the allocator and the embedded font.
if command -v /usr/bin/time >/dev/null 2>&1 && \
   [ -f "$ROOT/data/ball_basketball_realistic.usdz" ]; then
  /usr/bin/time -f '%M' -o "$OUT/rss.txt" \
    "$BIN" "$ROOT/data/ball_basketball_realistic.usdz" --max-mem 128 \
    --screenshot "$OUT/capped.png" --size 480x360 --frames 4 >/dev/null \
    || fail "capped run: non-zero exit"
  peak_kb="$(tail -n1 "$OUT/rss.txt" | tr -dc '0-9')"
  if [ -n "$peak_kb" ]; then
    limit_kb=$((512 * 1024))
    [ "$peak_kb" -le "$limit_kb" ] \
      || fail "peak RSS ${peak_kb} KB exceeded ${limit_kb} KB at --max-mem 128"
    echo "memory cap: ok (peak RSS $((peak_kb / 1024)) MB at --max-mem 128)"
  fi
fi

# 4. Thread-count determinism: the sample jitter is deterministic per
#    (pixel, sample), so the converged image must not depend on --threads.
#    Use a size where the viewport is actually large: the file-list pane has a
#    160px minimum, so a small window leaves almost no 3D area to compare.
if [ -f "$ROOT/models/cube-previewsurface.usda" ]; then
  "$BIN" "$ROOT/models/cube-previewsurface.usda" --threads 1 --spp 4 \
    --screenshot "$OUT/t1.png" --size 480x360 --frames 8 >/dev/null
  "$BIN" "$ROOT/models/cube-previewsurface.usda" --threads 8 --spp 4 \
    --screenshot "$OUT/t8.png" --size 480x360 --frames 8 >/dev/null
  # Compare the viewport only: the status bar reports live process RSS, which
  # legitimately differs run to run, so a whole-image cmp would be flaky.
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ]; then
    frac="$(python3 "$DIFF_PY" "$OUT/t1.png" "$OUT/t8.png")" \
      || fail "thread determinism: pixel compare failed"
    awk -v f="$frac" 'BEGIN { exit !(f == 0) }' \
      || fail "--threads 1 and --threads 8 rendered differently (${frac} of viewport differs)"
    echo "thread determinism: ok"
  fi
fi

# 4b. Backend selection. The GL backend is optional -- a machine with no libEGL
#     or no usable driver must fall back to the CPU renderer and still produce
#     an image, so this checks the outcome, not that GL was used.
if [ -f "$ROOT/models/suzanne-pbr.usda" ]; then
  for backend in cpu gl auto; do
    "$BIN" "$ROOT/models/suzanne-pbr.usda" --backend "$backend" \
      --screenshot "$OUT/b_$backend.png" --size 480x360 --frames 4 \
      >/dev/null 2>"$OUT/b_$backend.err" \
      || fail "--backend $backend: non-zero exit"
    check_renders_geometry "$OUT/b_$backend.png" "$OUT/bg.png" 0.03 \
      "backend=$backend"
  done
  # cpu and gl need not be pixel-identical (no shadow maps in the GL path, and
  # a flat background), but they must frame the same subject: require the two
  # to agree closely: the GL fragment shader mirrors shade.cc, so anything more
  # than a few percent means the two have drifted apart.
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ]; then
    frac="$(python3 "$DIFF_PY" "$OUT/b_cpu.png" "$OUT/b_gl.png")"
    awk -v f="$frac" 'BEGIN { exit !(f <= 0.10) }' \
      || fail "cpu and gl backends disagree over ${frac} of the viewport"
    echo "backend parity: ok (${frac} of viewport differs between cpu and gl)"
  fi
fi

# 5. Bad input must fail cleanly with a message, not a crash.
if "$BIN" "$OUT/does-not-exist.usda" --screenshot "$OUT/nope.png" \
     >/dev/null 2>"$OUT/err.txt"; then
  fail "missing file: expected non-zero exit"
fi
grep -q . "$OUT/err.txt" || fail "missing file: no diagnostic on stderr"
echo "missing-file diagnostic: ok"

echo "== tusdquicklook smoke: PASS"
