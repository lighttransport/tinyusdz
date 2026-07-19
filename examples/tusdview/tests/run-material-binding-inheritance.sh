#!/usr/bin/env bash
#
# Two production material regressions, both of which make a scene render as
# untextured flat-shaded soup. Pass the scene to test as $1 (default: the binding
# fixture):
#
#   models/tusdview-material-binding-inheritance.usda
#     Binds its textured material PURPOSE-SCOPED (material:binding:preview / :full)
#     on an ANCESTOR Xform and never authors a plain `material:binding` on the
#     Mesh -- exactly how ALab binds. Reading only the Mesh's own
#     `material:binding` finds nothing and falls back to the default gray material.
#
#   models/nested-look-texture/root.usda
#     References a look layer in `look/` that reaches its texture with
#     `../tex/...`, correct RELATIVE TO THAT LAYER. Anchoring the path at the
#     stage root instead resolves it out of the asset and drops the texture.
#
# Asserts the loader (a) reports a decoded texture and (b) actually SAMPLES it:
# the render must be non-uniform (the checkerboard), not a flat color.
#
# Env: TUSDVIEW=/path/to/tusdview
set -uo pipefail
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

if [ -n "${TUSDVIEW:-}" ]; then BIN="$TUSDVIEW"
elif [ -x "$REPO_ROOT/build_ninja/tusdview" ]; then BIN="$REPO_ROOT/build_ninja/tusdview"
else BIN="$REPO_ROOT/build/tusdview"; fi
if [ ! -x "$BIN" ]; then echo "SKIP: tusdview not found ($BIN)"; exit "$SKIP"; fi

ASSET="${1:-$REPO_ROOT/models/tusdview-material-binding-inheritance.usda}"
if [ ! -f "$ASSET" ]; then echo "SKIP: asset missing: $ASSET"; exit "$SKIP"; fi
echo "scene: $ASSET"

OUT="$(mktemp -d)"
mkdir -p "$OUT/config"
trap 'rm -rf "$OUT"' EXIT
LOG="$OUT/render.log"
if command -v timeout >/dev/null 2>&1; then
  timeout --kill-after=5s "${TUSDVIEW_RENDER_TIMEOUT:-60s}" \
    env XDG_CONFIG_HOME="$OUT/config" \
    "$BIN" --headless --frames 4 --screenshot "$OUT/out.ppm" "$ASSET" \
    >"$LOG" 2>&1
else
  env XDG_CONFIG_HOME="$OUT/config" \
    "$BIN" --headless --frames 4 --screenshot "$OUT/out.ppm" "$ASSET" \
    >"$LOG" 2>&1
fi
rc=$?
if [ "$rc" -ne 0 ] || [ ! -s "$OUT/out.ppm" ]; then
  # No GPU / no Vulkan device in this environment -> skip rather than fail.
  if grep -qiE 'no vulkan|failed to (create|find).*(device|instance)|no suitable gpu' "$LOG"; then
    echo "SKIP: no usable GPU device"; sed -n '1,20p' "$LOG"; exit "$SKIP"
  fi
  echo "FAIL: render failed (rc=$rc)"; sed -n '1,40p' "$LOG"; exit 1
fi

# Mesa's software Vulkan path on this machine is useful for API/validation
# smoke tests but does not fetch the viewer's textured vertex attributes
# reliably. A flat software render therefore cannot distinguish bad asset
# anchoring from the device limitation; let a hardware backend answer instead.
if grep -Eqi 'llvmpipe|softpipe|lavapipe|software rasterizer|\(cpu, driver' "$LOG"; then
  echo "SKIP: software Vulkan cannot validate texture sampling"
  exit "$SKIP"
fi

# (a) The texture must survive material resolution + path anchoring, and decode.
if ! grep -qE '[0-9]+ materials, [1-9][0-9]* textures' "$LOG"; then
  echo "FAIL: no texture resolved -- the material binding was dropped, or the"
  echo "      texture's asset path was anchored at the stage root instead of at"
  echo "      the layer that authored it."
  grep -oE '[0-9]+ materials, [0-9]+ textures' "$LOG" || true
  exit 1
fi

# (b) The texture must actually be SAMPLED: a flat fallback material renders a
#     uniform surface, the checkerboard does not.
python3 - "$OUT/out.ppm" <<'PY'
import sys

data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    print("FAIL: not a binary PPM"); sys.exit(1)

# Parse the P6 header (whitespace-separated, '#' comments allowed).
i, tok = 2, []
while len(tok) < 3 and i < len(data):
    c = data[i]
    if c == 35:  # '#'
        while i < len(data) and data[i] not in (10, 13):
            i += 1
    elif chr(c).isspace():
        i += 1
    else:
        s = i
        while i < len(data) and not chr(data[i]).isspace():
            i += 1
        tok.append(int(data[s:i]))
w, h, _maxv = tok
i += 1  # single whitespace byte after maxval
px = data[i:]

# Collect the distinct colors of the rendered (non-background) surface.
colors = {}
for y in range(0, h, 2):
    for x in range(0, w, 2):
        o = (y * w + x) * 3
        if o + 2 >= len(px):
            continue
        c = (px[o], px[o + 1], px[o + 2])
        colors[c] = colors.get(c, 0) + 1

# The checkerboard gives many distinct shades across the quad; a dropped texture
# (default gray / flat diffuse) gives essentially background + one flat color.
surface = [c for c, n in colors.items() if n >= 8]
if len(surface) < 3:
    print(f"FAIL: render is flat ({len(surface)} dominant colors) -- "
          f"texture not sampled: {sorted(colors.items(), key=lambda kv: -kv[1])[:4]}")
    sys.exit(1)
print(f"OK: textured render, {len(surface)} dominant surface colors")
PY
rc=$?
[ "$rc" -eq 0 ] || exit 1

echo "PASS: material resolved and its texture is sampled"
grep -oE '[0-9]+ materials, [0-9]+ textures' "$LOG" || true
exit 0
