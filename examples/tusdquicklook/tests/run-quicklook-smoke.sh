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
    "$ROOT/models/suzanne-pbr.usda" ; do
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
#
#    The asset is generated rather than checked in. It has to be big enough to
#    bust an 8 MB budget, and the largest tracked model (suzanne-pbr, 623 KB)
#    fits inside one -- which would leave this check passing while proving
#    nothing. A ~3 MB grid projects to ~39 MB and is refused pre-open, which is
#    the rung of the ladder worth asserting.
BIG="$OUT/big-grid.usda"
if command -v python3 >/dev/null 2>&1; then
  python3 - "$BIG" <<'PY'
import sys
N = 180  # N*N verts, (N-1)^2*2 tris
pts, sts, idx = [], [], []
for j in range(N):
    for i in range(N):
        x = i / (N - 1) * 2 - 1
        z = j / (N - 1) * 2 - 1
        y = 0.025 * ((x * 3) ** 2 - (z * 3) ** 2)
        pts.append(f"({x:.5f}, {y:.5f}, {z:.5f})")
        sts.append(f"({i/(N-1):.5f}, {j/(N-1):.5f})")
for j in range(N - 1):
    for i in range(N - 1):
        a = j * N + i; b = a + 1; c = a + N; d = c + 1
        idx += [a, b, d, a, d, c]
open(sys.argv[1], "w").write(f"""#usda 1.0
(defaultPrim = "W" upAxis = "Y")
def Xform "W" {{
  def Mesh "Grid" {{
    int[] faceVertexCounts = [{", ".join(["3"] * (len(idx)//3))}]
    int[] faceVertexIndices = [{", ".join(map(str, idx))}]
    point3f[] points = [{", ".join(pts)}]
    texCoord2f[] primvars:st = [{", ".join(sts)}] (interpolation = "vertex")
  }}
}}
""")
PY
fi

if [ -f "$BIG" ]; then
  "$BIN" "$BIG" --max-mem 8 \
    --screenshot "$OUT/tight.png" --size 400x240 >/dev/null \
    || fail "tight budget: non-zero exit (should degrade, not fail)"
  check_png "$OUT/tight.png" "tight-budget"
  echo "tight budget: ok (exited cleanly at --max-mem 8)"
fi

# 3b. The budget has to bound real memory, not just a counter. Measure peak RSS
#     and require it to stay within a generous multiple of the cap -- generous
#     because the cap governs tracked preview data, while RSS also carries the
#     binary, the allocator and the embedded font.
if command -v /usr/bin/time >/dev/null 2>&1 && [ -f "$BIG" ]; then
  /usr/bin/time -f '%M' -o "$OUT/rss.txt" \
    "$BIN" "$BIG" --max-mem 128 \
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

  # The app must always report which backend is actually live, so a demotion to
  # the CPU renderer can never be mistaken for a deliberate choice.
  "$BIN" "$ROOT/models/suzanne-pbr.usda" --backend gl --verbose \
    --screenshot "$OUT/b_report.png" --size 480x360 --frames 4 \
    >/dev/null 2>"$OUT/b_report.err" \
    || fail "--backend gl --verbose: non-zero exit"
  grep -Eq '^\[tusdquicklook\] renderer: (gl|cpu) ' "$OUT/b_report.err" \
    || fail "--verbose did not report the live renderer"
  echo "backend reporting: ok ($(grep -Eo 'renderer: [a-z]+' "$OUT/b_report.err" | head -1))"

  # 4c. Debug AOVs. These carry no lighting, so the GLSL and shade.cc are
  #     computing the same arithmetic or they are not -- which makes them a
  #     tighter parity signal than the shaded image. They must also each differ
  #     from the others, or a mode that silently renders nothing would pass.
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ]; then
    prev=""
    for mode in albedo normal uv roughness metallic depth; do
      for backend in cpu gl; do
        "$BIN" "$ROOT/models/suzanne-pbr.usda" --backend "$backend" \
          --shading-mode "$mode" --screenshot "$OUT/aov_${mode}_$backend.png" \
          --size 480x360 --frames 4 >/dev/null 2>&1 \
          || fail "--shading-mode $mode --backend $backend: non-zero exit"
      done
      frac="$(python3 "$DIFF_PY" "$OUT/aov_${mode}_cpu.png" \
                                 "$OUT/aov_${mode}_gl.png")"
      awk -v f="$frac" 'BEGIN { exit !(f <= 0.05) }' \
        || fail "shading mode $mode: cpu and gl disagree over ${frac}"
      if [ -n "$prev" ]; then
        d="$(python3 "$DIFF_PY" "$OUT/aov_${prev}_cpu.png" \
                                "$OUT/aov_${mode}_cpu.png")"
        awk -v f="$d" 'BEGIN { exit !(f > 0.0) }' \
          || fail "shading modes $prev and $mode render identically"
      fi
      prev="$mode"
    done
    echo "shading modes: ok (6 AOVs, cpu/gl parity and all distinct)"
  fi

  # 4d. TEXTURED parity. The backend parity check above uses an untextured
  #     asset, which is how a vertical UV flip in the GL path went unnoticed:
  #     every textured render was mirrored relative to the tracer. Albedo is
  #     used because it is the texture with no lighting on top of it.
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ] &&
     [ -f "$ROOT/models/texture-cat-plane.usda" ]; then
    for backend in cpu gl; do
      "$BIN" "$ROOT/models/texture-cat-plane.usda" --backend "$backend" \
        --shading-mode albedo --screenshot "$OUT/tex_$backend.png" \
        --size 480x360 --frames 4 >/dev/null 2>&1 \
        || fail "textured parity --backend $backend: non-zero exit"
    done
    frac="$(python3 "$DIFF_PY" "$OUT/tex_cpu.png" "$OUT/tex_gl.png")"
    awk -v f="$frac" 'BEGIN { exit !(f <= 0.05) }' \
      || fail "textured cpu/gl disagree over ${frac} (UV orientation? filtering?)"
    echo "textured parity: ok (${frac} of viewport differs)"
  fi

  # 4e. Image-based lighting. The environment is projected to SH and prefiltered
  #     on the CPU and handed to GL verbatim, so the two must agree closely. The
  #     map is generated here rather than checked in, which also keeps the case
  #     independent of what test assets happen to exist.
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ]; then
    python3 - "$OUT/env.png" <<'PY'
import struct, sys, zlib, math
w, h = 128, 64
rows = b''
for y in range(h):
    row = b'\x00'
    for x in range(w):
        v = 1 - (y + 0.5) / h
        el = (v - 0.5) * math.pi
        s = max(0.0, math.sin(el))
        r, g, b = int(40 + 180 * s), int(60 + 150 * s), int(90 + 140 * s)
        if abs(x / w - 0.25) < 0.06 and abs(v - 0.75) < 0.10:
            r, g, b = 255, 240, 200          # a sun, so direction matters
        row += bytes([r, g, b])
    rows += row
def chunk(t, d):
    return (struct.pack('>I', len(d)) + t + d +
            struct.pack('>I', zlib.crc32(t + d) & 0xffffffff))
open(sys.argv[1], 'wb').write(
    b'\x89PNG\r\n\x1a\n' +
    chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
    chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
PY
    for backend in cpu gl; do
      "$BIN" "$ROOT/models/suzanne-pbr.usda" --backend "$backend" \
        --env "$OUT/env.png" --screenshot "$OUT/ibl_$backend.png" \
        --size 480x360 --frames 4 >/dev/null 2>&1 \
        || fail "--env --backend $backend: non-zero exit"
      check_renders_geometry "$OUT/ibl_$backend.png" "$OUT/bg.png" 0.03 \
        "ibl backend=$backend"
    done
    frac="$(python3 "$DIFF_PY" "$OUT/ibl_cpu.png" "$OUT/ibl_gl.png")"
    awk -v f="$frac" 'BEGIN { exit !(f <= 0.10) }' \
      || fail "IBL: cpu and gl disagree over ${frac} of the viewport"

    # --no-ibl must actually turn it off, or the flag is decorative.
    "$BIN" "$ROOT/models/suzanne-pbr.usda" --backend cpu --env "$OUT/env.png" \
      --no-ibl --screenshot "$OUT/ibl_off.png" --size 480x360 --frames 4 \
      >/dev/null 2>&1 || fail "--no-ibl: non-zero exit"
    d="$(python3 "$DIFF_PY" "$OUT/ibl_cpu.png" "$OUT/ibl_off.png")"
    awk -v f="$d" 'BEGIN { exit !(f > 0.0) }' \
      || fail "--no-ibl rendered identically to IBL"
    echo "ibl: ok (${frac} cpu-vs-gl, --no-ibl changes ${d})"
  fi

  # 4f. Alpha modes. Two separate bars, because the two are not equally hard:
  #     non-overlapping surfaces are pure mode correctness and must match
  #     tightly, while overlapping blended ones are resolved by sorting in GL
  #     and by walking the ray on the CPU, which cannot agree pixel-for-pixel.
  ALPHA="$ROOT/models/tusdquicklook-alpha-modes.usda"
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ] && [ -f "$ALPHA" ]; then
    for backend in cpu gl; do
      "$BIN" "$ALPHA" --backend "$backend" --screenshot "$OUT/al_$backend.png" \
        --size 700x400 --frames 6 >/dev/null 2>&1 \
        || fail "alpha modes --backend $backend: non-zero exit"
    done
    frac="$(python3 "$DIFF_PY" "$OUT/al_cpu.png" "$OUT/al_gl.png")"
    awk -v f="$frac" 'BEGIN { exit !(f <= 0.05) }' \
      || fail "alpha modes: cpu and gl disagree over ${frac}"

    # A cutout below its threshold must render as if the surface were not in
    # the scene at all. Comparing against a variant with the mesh removed says
    # exactly that, with no dependence on where the quad lands on screen.
    #
    # The baseline is re-rendered from a copy inside $OUT: pngdiff excludes the
    # file list and the status bar but NOT the toolbar, which shows the current
    # directory, so comparing a render from models/ against one from $OUT would
    # differ by the caption alone.
    cp "$ALPHA" "$OUT/alpha-full.usda"
    "$BIN" "$OUT/alpha-full.usda" --backend cpu \
      --screenshot "$OUT/al_full.png" --size 700x400 --frames 6 \
      >/dev/null 2>&1 || fail "alpha baseline: non-zero exit"
    python3 - "$ALPHA" "$OUT/alpha-no-below.usda" "$OUT/alpha-below-opaque.usda" <<'PY'
import re, sys
src = open(sys.argv[1]).read()
# Drop the CutoutBelow mesh entirely.
removed = re.sub(r'\n    def Mesh "CutoutBelow"\n    \{.*?\n    \}\n', '\n',
                 src, count=1, flags=re.S)
if removed == src:
    raise SystemExit('could not remove CutoutBelow mesh')
open(sys.argv[2], 'w').write(removed)
# Same asset, but that material's opacity now clears the threshold.
raised = src.replace('float inputs:opacity = 0.2', 'float inputs:opacity = 0.9')
if raised == src:
    raise SystemExit('could not raise CutoutBelow opacity')
open(sys.argv[3], 'w').write(raised)
PY
    for variant in no-below below-opaque; do
      "$BIN" "$OUT/alpha-$variant.usda" --backend cpu \
        --screenshot "$OUT/al_$variant.png" --size 700x400 --frames 6 \
        >/dev/null 2>&1 || fail "alpha variant $variant: non-zero exit"
    done
    d="$(python3 "$DIFF_PY" "$OUT/al_full.png" "$OUT/al_no-below.png")"
    awk -v f="$d" 'BEGIN { exit !(f == 0) }' \
      || fail "cutout below threshold is visible (differs from absent by ${d})"
    d2="$(python3 "$DIFF_PY" "$OUT/al_full.png" "$OUT/al_below-opaque.png")"
    awk -v f="$d2" 'BEGIN { exit !(f > 0.0) }' \
      || fail "opacityThreshold ignored: raising opacity changed nothing"
    echo "alpha modes: ok (${frac} cpu-vs-gl, cutout==absent, threshold honored)"
  fi

  # Overlapping blended surfaces: sort order vs layered accumulation.
  OVL="$ROOT/models/tusdview-transparency.usda"
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ] && [ -f "$OVL" ]; then
    for backend in cpu gl; do
      # 480x360 to match the background reference the geometry check uses.
      "$BIN" "$OVL" --backend "$backend" --screenshot "$OUT/ov_$backend.png" \
        --size 480x360 --frames 6 >/dev/null 2>&1 \
        || fail "overlapping blend --backend $backend: non-zero exit"
      check_renders_geometry "$OUT/ov_$backend.png" "$OUT/bg.png" 0.03 \
        "blend backend=$backend"
    done
    frac="$(python3 "$DIFF_PY" "$OUT/ov_cpu.png" "$OUT/ov_gl.png")"
    awk -v f="$frac" 'BEGIN { exit !(f <= 0.20) }' \
      || fail "overlapping blend: cpu and gl disagree over ${frac} (limit 0.20)"
    echo "overlapping blend: ok (${frac} of viewport differs, limit 0.20)"
  fi

  # The thread-count independence must hold in a debug mode too. Pin the CPU
  # backend: worker threads are a CPU-tracer concept, and letting `auto` choose
  # would compare two GL images (or one of each, if a GL context happens to
  # fail) instead of what this is testing. Compare the viewport only -- the
  # status bar reports live RSS, which legitimately differs run to run.
  if command -v python3 >/dev/null 2>&1 && [ -f "$DIFF_PY" ]; then
    "$BIN" "$ROOT/models/cube-previewsurface.usda" --backend cpu --threads 1 \
      --spp 4 --shading-mode normal --screenshot "$OUT/aov_t1.png" \
      --size 480x360 --frames 4 >/dev/null 2>&1 \
      || fail "aov --threads 1: non-zero exit"
    "$BIN" "$ROOT/models/cube-previewsurface.usda" --backend cpu --threads 8 \
      --spp 4 --shading-mode normal --screenshot "$OUT/aov_t8.png" \
      --size 480x360 --frames 4 >/dev/null 2>&1 \
      || fail "aov --threads 8: non-zero exit"
    frac="$(python3 "$DIFF_PY" "$OUT/aov_t1.png" "$OUT/aov_t8.png")"
    awk -v f="$frac" 'BEGIN { exit !(f == 0) }' \
      || fail "shading mode normal: --threads 1 and 8 differ over ${frac}"
    echo "aov thread determinism: ok"
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
