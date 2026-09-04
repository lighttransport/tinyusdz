#!/usr/bin/env bash
#
# Alpha-blend regression: render overlapping planes (opaque red behind,
# 50%-opacity green in the middle, 50%-opacity blue in front) and assert the
# overlap region is BLENDED and sorted (shows all three colors), not the
# opaque-blue that a non-blending forward renderer produced. Runs GL and Vulkan
# raster. SKIPs (77) when no GPU/binary.
#
# Env: LUSDVIEW=/path/to/lusdview
set -uo pipefail
export LUSDVIEW_LOG=debug
SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

if [ -n "${LUSDVIEW:-}" ]; then BIN="$LUSDVIEW"
elif [ -x "$REPO_ROOT/build_ninja/lusdview" ]; then BIN="$REPO_ROOT/build_ninja/lusdview"
else BIN="$REPO_ROOT/build/lusdview"; fi
if [ ! -x "$BIN" ]; then echo "SKIP: lusdview not found ($BIN)"; exit "$SKIP"; fi

ASSET="$REPO_ROOT/models/lusdview-transparency.usda"
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
for spec in "gl:--backend gl" \
            "vk-auto:--backend vk --transparency auto" \
            "vk-weighted:--backend vk --transparency weighted" \
            "vk-sorted:--backend vk --transparency sorted"; do
  tag="${spec%%:*}"; args="${spec#*:}"
  img="$OUT/transp_$tag.ppm"
  report_args=""
  if [ "$tag" != "gl" ]; then
    report_args="--render-report $OUT/$tag.json"
  fi
  # shellcheck disable=SC2086
  $XVFB "$BIN" --headless $args --frames 2 --view-dir 0,0,-1 --size 256x256 \
      --screenshot "$img" $report_args "$ASSET" >"$OUT/$tag.log" 2>&1
  if ! grep -q 'render stats' "$OUT/$tag.log"; then
    echo "SKIP: $tag backend unavailable"; continue
  fi
  if [ "$tag" = "vk-weighted" ] &&
     grep -q 'weighted OIT capability: available' "$OUT/$tag.log" &&
     ! grep -q 'Vulkan weighted OIT resources ready' "$OUT/$tag.log"; then
    echo "FAIL: weighted OIT was supported and requested but did not activate"
    fail=1
    continue
  fi
  if [ ! -s "$img" ]; then echo "FAIL: $tag produced no image"; fail=1; continue; fi
  if [ "$tag" != "gl" ]; then
    if ! python3 - "$OUT/$tag.json" "$tag" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    backend = json.load(f).get("backend", {})
tag = sys.argv[2]
for name in ("pipeline_binds", "descriptor_set_binds"):
    value = backend.get(name)
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise SystemExit(f"invalid backend.{name}: {value!r}")
expected_weighted = tag == "vk-weighted"
if backend.get("weighted_oit_active") is not expected_weighted:
    raise SystemExit(
        f"invalid backend.weighted_oit_active for {tag}: "
        f"{backend.get('weighted_oit_active')!r}")
PY
    then
      echo "FAIL: $tag render report has invalid Vulkan bind counters"
      fail=1
      continue
    fi
  fi
  read -r R G B < <(center_rgb "$img") || { echo "FAIL: $tag PPM parse"; fail=1; continue; }
  echo "$tag center RGB = $R $G $B"
  # Blended overlap must carry red, green and blue contributions. Opaque-blue
  # (the pre-fix bug) would give R/G near 0. Require all channels >=25.
  if [ "$R" -ge 25 ] && [ "$G" -ge 25 ] && [ "$B" -ge 25 ]; then
    echo "PASS: $tag overlap is alpha-blended"
  else
    echo "FAIL: $tag overlap not blended (expected red+green+blue, got $R $G $B)"; fail=1
  fi
done

# Weighted OIT must not change when the two equal-opacity transparent layers
# exchange depth order. The sorted reference intentionally may change.
if [ -s "$OUT/transp_vk-weighted.ppm" ] &&
   grep -q 'Vulkan weighted OIT resources ready' "$OUT/vk-weighted.log"; then
  REVERSED="$OUT/transparency-reversed.usda"
  sed -e 's/0\.05)/0.075)/g' \
      -e 's/0\.025)/0.05)/g' \
      -e 's/0\.075)/0.025)/g' "$ASSET" >"$REVERSED"
  $XVFB "$BIN" --headless --backend vk --transparency weighted --frames 2 \
      --view-dir 0,0,-1 --size 256x256 \
      --screenshot "$OUT/transp_vk-weighted-reversed.ppm" "$REVERSED" \
      >"$OUT/vk-weighted-reversed.log" 2>&1
  read -r WR WG WB < <(center_rgb "$OUT/transp_vk-weighted.ppm") || fail=1
  read -r RR RG RB < <(center_rgb "$OUT/transp_vk-weighted-reversed.ppm") || fail=1
  DR=$((WR > RR ? WR - RR : RR - WR))
  DG=$((WG > RG ? WG - RG : RG - WG))
  DB=$((WB > RB ? WB - RB : RB - WB))
  if [ "$DR" -le 3 ] && [ "$DG" -le 3 ] && [ "$DB" -le 3 ]; then
    echo "PASS: weighted overlap is depth-order independent"
  else
    echo "FAIL: weighted overlap changed after layer reversal: " \
         "$WR $WG $WB -> $RR $RG $RB"
    fail=1
  fi
fi

[ "$fail" -eq 0 ] || exit 1
echo "PASS: raster alpha blending verified"
exit 0
