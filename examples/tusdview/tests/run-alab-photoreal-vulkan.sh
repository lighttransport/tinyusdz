#!/usr/bin/env bash
# External ALab quality smoke test. No ALab/HDR data or rendered output enters
# the repository; callers provide the two public-asset locations.
set -uo pipefail

SKIP=77
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build_ninja/tusdview}"
ALAB_GENERATED="${ALAB_GENERATED:-}"
HDR="${TUSDVIEW_HDR:-}"

if [ ! -x "$BIN" ]; then echo "SKIP: tusdview not found: $BIN"; exit "$SKIP"; fi
if [ -z "$ALAB_GENERATED" ] || [ ! -d "$ALAB_GENERATED/ALab" ]; then
  echo "SKIP: set ALAB_GENERATED to the ALab generated folder"; exit "$SKIP"
fi
if [ -z "$HDR" ] || [ ! -f "$HDR" ]; then
  echo "SKIP: set TUSDVIEW_HDR to an HDR/EXR environment"; exit "$SKIP"
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
LANTERN="$ALAB_GENERATED/alab-static-lantern.usda"
STOAT="$ALAB_GENERATED/alab-character-stoat.usda"
for asset in "$LANTERN" "$STOAT"; do
  if [ ! -f "$asset" ]; then echo "SKIP: missing $asset"; exit "$SKIP"; fi
done

render() {
  local tag="$1" asset="$2" quality="$3"
  timeout --kill-after=10s "${TUSDVIEW_ALAB_TIMEOUT:-240s}" \
    env TUSDVIEW_TIME_GPU=1 "$BIN" --headless --backend vk --next \
      --load-payloads --texture-fit never --frames 4 --size 1920x1080 \
      --no-grid --raster-quality "$quality" --envmap "$HDR" \
      --envmap-intensity "${TUSDVIEW_HDR_INTENSITY:-1}" \
      --screenshot "$OUT/$tag.ppm" "$asset" >"$OUT/$tag.log" 2>&1
  local rc=$?
  if [ "$rc" -ne 0 ] || [ ! -s "$OUT/$tag.ppm" ]; then
    if grep -qiE 'no vulkan|no suitable gpu|failed to create.*device' "$OUT/$tag.log"; then
      echo "SKIP: no usable Vulkan GPU"; exit "$SKIP"
    fi
    echo "FAIL: $tag render failed ($rc)"; sed -n '1,80p' "$OUT/$tag.log"; exit 1
  fi
}

render lantern-current "$LANTERN" current
render lantern-high "$LANTERN" high
render stoat-high "$STOAT" high

grep -qE '4 materials, 7 textures' "$OUT/lantern-high.log" || {
  echo "FAIL: Lantern did not select the full-purpose PBR texture set"; exit 1;
}
grep -q 'runtime dome IBL ready' "$OUT/lantern-high.log" || {
  echo "FAIL: runtime HDR environment was not baked"; exit 1;
}
grep -q 'high-quality screen-space refraction ready' "$OUT/lantern-high.log" || {
  echo "FAIL: high-quality weighted-OIT scene-color refraction was not ready"; exit 1;
}

python3 - "$OUT/lantern-current.ppm" "$OUT/lantern-high.ppm" <<'PY'
import sys

def pixels(path):
    data = open(path, 'rb').read()
    if not data.startswith(b'P6'):
        raise SystemExit(f'FAIL: {path} is not P6')
    i, tokens = 2, []
    while len(tokens) < 3:
        if data[i] == 35:
            while data[i] not in (10, 13): i += 1
        elif chr(data[i]).isspace(): i += 1
        else:
            start = i
            while not chr(data[i]).isspace(): i += 1
            tokens.append(int(data[start:i]))
    return data[i + 1:]

a, b = pixels(sys.argv[1]), pixels(sys.argv[2])
if len(a) != len(b): raise SystemExit('FAIL: image dimensions differ')
mad = sum(abs(x-y) for x, y in zip(a, b)) / len(a)
if mad < 1.0: raise SystemExit(f'FAIL: high-quality path had no visual effect (MAD {mad:.3f})')
print(f'PASS: full PBR/HDR high-quality output differs from current path (MAD {mad:.3f})')
PY

grep -h '\[gpu\].*total=' "$OUT"/*.log | tail -n 6 || true
echo "PASS: ALab Lantern and Stoat Vulkan high-quality smoke"
