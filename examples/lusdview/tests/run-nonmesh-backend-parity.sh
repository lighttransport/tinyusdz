#!/usr/bin/env bash
# Compare the checked-in native Points/Curves fixture across GL and Vulkan.
# Exact pixels are intentionally not required: the backends use different
# rasterizers and lighting paths. The gate compares colored silhouette coverage.
set -uo pipefail

LUSDVIEW="${1:?usage: $0 /path/to/lusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/lusdview /repo/root}"
SKIP=77
TMP="$(mktemp -d /tmp/lusdview-nonmesh-parity.XXXXXX)"
xvfb_pid=""
cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null || true; rm -rf "$TMP"; }
trap cleanup EXIT

printf '%s\n' '{"window_size":{"width":640,"height":480}}' >"$TMP/config.json"
if ! command -v Xvfb >/dev/null 2>&1; then
  echo "SKIP: Xvfb unavailable"
  exit "$SKIP"
fi
display_num=$((200 + ($$ % 50)))
Xvfb ":$display_num" -screen 0 800x600x24 >"$TMP/xvfb.log" 2>&1 &
xvfb_pid=$!
export DISPLAY=":$display_num"
for _ in 1 2 3 4 5 6 7 8 9 10; do
  [ -S "/tmp/.X11-unix/X$display_num" ] && break
  sleep 0.1
done
if [ ! -S "/tmp/.X11-unix/X$display_num" ]; then
  echo "SKIP: Xvfb failed to start"
  exit "$SKIP"
fi

scene="$ROOT/tests/usda/lusdview-nonmesh-points-curves.usda"
timeout 60s "$LUSDVIEW" --config "$TMP/config.json" --backend gl --frames 4 \
  --screenshot "$TMP/gl.ppm" "$scene" >"$TMP/gl.log" 2>&1
gl_rc=$?
if [ "$gl_rc" -ne 0 ]; then
  if grep -Eqi 'OpenGL.*(unavailable|failed)|GLFW.*failed|failed to open display|DISPLAY' "$TMP/gl.log"; then
    echo "SKIP: OpenGL/X11 backend unavailable"
    exit "$SKIP"
  fi
  cat "$TMP/gl.log"
  echo "FAIL: GL parity render failed"
  exit 1
fi

timeout 60s "$LUSDVIEW" --config "$TMP/config.json" --headless --backend vk --frames 4 \
  --screenshot "$TMP/vk.ppm" "$scene" >"$TMP/vk.log" 2>&1
vk_rc=$?
if [ "$vk_rc" -ne 0 ]; then
  if grep -Eqi 'Vulkan.*(unavailable|failed)|no Vulkan|renderer init failed' "$TMP/vk.log"; then
    echo "SKIP: Vulkan backend unavailable"
    exit "$SKIP"
  fi
  cat "$TMP/vk.log"
  echo "FAIL: Vulkan parity render failed"
  exit 1
fi

python3 - "$TMP/gl.ppm" "$TMP/vk.ppm" <<'PY'
import sys

def read(path):
    with open(path, "rb") as f:
        if f.readline().strip() != b"P6": raise SystemExit("FAIL: non-PPM screenshot")
        w, h = map(int, f.readline().split())
        if int(f.readline()) != 255: raise SystemExit("FAIL: unexpected PPM range")
        data = f.read()
    if len(data) != w*h*3: raise SystemExit("FAIL: truncated screenshot")
    return w, h, data

images = [read(p) for p in sys.argv[1:]]
def mask(image):
    w, h, img = image
    out = set()
    for y in range(h):
        for x in range(w):
            i = (y*w+x)*3
            p = img[i:i+3]
            if max(p) > 80 and max(p)-min(p) > 50:
                out.add((x*32//w, y*24//h))
    return out
a, b = map(mask, images)
if len(a) < 8 or len(b) < 8:
    raise SystemExit(f"FAIL: carrier silhouette too small: GL={len(a)} VK={len(b)} cells")
iou = len(a & b) / float(len(a | b))
if iou < 0.20:
    raise SystemExit(f"FAIL: GL/Vulkan carrier silhouette IoU={iou:.3f} (<0.20)")
print(f"PASS: GL/Vulkan carrier silhouette parity IoU={iou:.3f} (GL={len(a)} VK={len(b)} cells)")
PY
