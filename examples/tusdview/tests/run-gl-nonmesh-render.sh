#!/usr/bin/env bash
# Verify that the extracted Points/Curves carriers produce visible colored
# pixels in the OpenGL raster path, not merely successful loader diagnostics.
set -uo pipefail

TUSDVIEW="${1:?usage: $0 /path/to/tusdview /repo/root}"
ROOT="${2:?usage: $0 /path/to/tusdview /repo/root}"
SKIP=77
TMP="$(mktemp -d /tmp/tusdview-gl-nonmesh.XXXXXX)"
xvfb_pid=""
cleanup() {
  if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
  rm -rf "$TMP"
}
trap cleanup EXIT
printf '%s\n' '{"window_size":{"width":640,"height":480}}' >"$TMP/config.json"

if command -v Xvfb >/dev/null 2>&1; then
  # X11 display numbers are one byte in several client paths; stay below 256.
  display_num=$((200 + ($$ % 50)))
  Xvfb ":$display_num" -screen 0 800x600x24 >"$TMP/xvfb.log" 2>&1 &
  xvfb_pid=$!
  export DISPLAY=":$display_num"
  for _ in 1 2 3 4 5; do
    kill -0 "$xvfb_pid" 2>/dev/null || break
    [ -S "/tmp/.X11-unix/X$display_num" ] && break
    sleep 0.1
  done
fi
log="$(timeout 45s "$TUSDVIEW" --config "$TMP/config.json" --backend gl \
  --frames 4 --screenshot "$TMP/nonmesh.ppm" \
  "$ROOT/tests/usda/tusdview-nonmesh-points-curves.usda" 2>&1)"
rc=$?
if [ "$rc" -ne 0 ]; then
  if grep -Eqi \
      "OpenGL.*(unavailable|failed)|GLFW.*failed|failed to open display|DISPLAY|xvfb-run: error" \
      <<<"$log"; then
    echo "SKIP: OpenGL/X11 backend unavailable"
    exit "$SKIP"
  fi
  echo "$log"
  echo "FAIL: OpenGL non-mesh render failed"
  exit 1
fi

python3 - "$TMP/nonmesh.ppm" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
with path.open("rb") as f:
    if f.readline().strip() != b"P6":
        raise SystemExit("FAIL: expected a binary PPM screenshot")
    width, height = map(int, f.readline().split())
    if int(f.readline()) != 255:
        raise SystemExit("FAIL: unexpected PPM range")
    pixels = f.read()
if len(pixels) != width * height * 3:
    raise SystemExit("FAIL: truncated OpenGL screenshot")
colored = sum(
    max(pixels[i:i + 3]) > 80 and
    max(pixels[i:i + 3]) - min(pixels[i:i + 3]) > 50
    for i in range(0, len(pixels), 3)
)
# Grid axes contribute a few narrow lines; the fixture's three discs and broad
# multicolor ribbon contribute well over this conservative threshold.
if colored < 800:
    raise SystemExit(
        f"FAIL: non-mesh carriers appear absent ({colored} colored pixels)"
    )
print(f"PASS: OpenGL non-mesh carriers rendered ({colored} colored pixels)")
PY
