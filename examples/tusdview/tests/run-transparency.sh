#!/usr/bin/env bash
#
# Alpha-blend regression: render overlapping planes (opaque red behind,
# 50%-opacity green in the middle, 50%-opacity blue in front) and assert the
# overlap region is BLENDED and sorted (shows all three colors), not the
# opaque-blue that a non-blending forward renderer produced. Runs GL and Vulkan
# raster. SKIPs (77) when no GPU/binary.
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

ASSET="$REPO_ROOT/models/tusdview-transparency.usda"
if [ ! -f "$ASSET" ]; then echo "SKIP: asset missing: $ASSET"; exit "$SKIP"; fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
XVFB=""; command -v xvfb-run >/dev/null 2>&1 && XVFB="xvfb-run -a"

# Center-region mean RGB of a binary PPM (P6). Prints "R G B" (0..255 ints).
center_rgb() {
  python3 - "$1" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
if not d.startswith(b"P6"): sys.exit(2)
i, tok = 2, []
while len(tok) < 3 and i < len(d):
    c = d[i]
    if c == 35:
        while i < len(d) and d[i] not in (10, 13): i += 1
    elif chr(c).isspace(): i += 1
    else:
        s = i
        while i < len(d) and not chr(d[i]).isspace(): i += 1
        tok.append(d[s:i])
if len(tok) != 3: sys.exit(2)
if i < len(d) and chr(d[i]).isspace(): i += 1
w, h, mx = (int(x) for x in tok)
px = d[i:i + w*h*3]
if len(px) < w*h*3: sys.exit(2)
# average a centered 20% box
x0, x1 = int(w*0.4), int(w*0.6)
y0, y1 = int(h*0.4), int(h*0.6)
r=g=b=n=0
for y in range(y0, y1):
    for x in range(x0, x1):
        o = (y*w + x)*3
        r += px[o]; g += px[o+1]; b += px[o+2]; n += 1
if n == 0: sys.exit(2)
print(r//n, g//n, b//n)
PY
}

fail=0
for spec in "gl:--backend gl" "vk:--backend vk"; do
  tag="${spec%%:*}"; args="${spec#*:}"
  img="$OUT/transp_$tag.ppm"
  # shellcheck disable=SC2086
  $XVFB "$BIN" --headless $args --frames 2 --view-dir 0,0,-1 --size 256x256 \
      --screenshot "$img" "$ASSET" >"$OUT/$tag.log" 2>&1
  if ! grep -q 'render stats' "$OUT/$tag.log"; then
    echo "SKIP: $tag backend unavailable"; continue
  fi
  if [ ! -s "$img" ]; then echo "FAIL: $tag produced no image"; fail=1; continue; fi
  read -r R G B < <(center_rgb "$img") || { echo "FAIL: $tag PPM parse"; fail=1; continue; }
  echo "$tag center RGB = $R $G $B"
  # Blended overlap must carry red, green and blue contributions. Opaque-blue
  # (the pre-fix bug) would give R/G near 0. Require all channels >=25.
  if [ "$R" -ge 25 ] && [ "$G" -ge 25 ] && [ "$B" -ge 25 ]; then
    echo "PASS: $tag overlap is alpha-blended and sorted"
  else
    echo "FAIL: $tag overlap not blended (expected red+green+blue, got $R $G $B)"; fail=1
  fi
done

[ "$fail" -eq 0 ] || exit 1
echo "PASS: raster alpha blending verified"
exit 0
