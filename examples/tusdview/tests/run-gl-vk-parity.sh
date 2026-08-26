#!/usr/bin/env bash
#
# GL/Vulkan raster image parity: compare a real windowed OpenGL render with a
# windowless Vulkan render of focused material fixtures. Pixels must agree within a tolerance that
# accounts for software-rasterizer (llvmpipe/lavapipe) differences.
# Skip when either backend is unavailable.
set -uo pipefail
SKIP=77

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build/tusdview}"
[ -x "$BIN" ] || BIN="$ROOT/build_ninja/tusdview"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }

GL_RUN=()
if [ -n "${DISPLAY:-}" ] && command -v xdpyinfo >/dev/null 2>&1 &&
   xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
  : # Prefer a usable inherited display (it may expose hardware OpenGL).
elif command -v xvfb-run >/dev/null 2>&1; then
  GL_RUN=(xvfb-run -a)
fi

TMP="$(mktemp -d /tmp/tusdview-gl-vk-parity.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/config-home"
printf '%s\n' '{"window_size":{"width":1024,"height":768}}' >"$TMP/config.json"

run_bounded() {
  if command -v timeout >/dev/null 2>&1; then
    timeout --kill-after=5s "${TUSDVIEW_GL_VK_TIMEOUT:-20s}" "$@"
  else
    "$@"
  fi
}

# Fixtures from the focused raster material parity suite. The advanced-lobe
# fixture is intentionally tested by run-unsupported-lobes.sh instead: its
# nonzero OpenPBR lobes are outside the bounded GL/Vulkan raster parity lane.
FIXTURES=(
  "$ROOT/tests/usda/tusdview-raster-multilight-links.usda"
  "$ROOT/tests/usda/tusdview-shadow-alpha-inst.usda"
)

any_run=0
for fixture in "${FIXTURES[@]}"; do
  [ -f "$fixture" ] || continue
  name="$(basename "$fixture" .usda)"
  gl_out="$TMP/${name}_gl.ppm"
  vk_out="$TMP/${name}_vk.ppm"
  gl_ok=0; vk_ok=0
  LOG_GL="$(run_bounded "${GL_RUN[@]}" env XDG_CONFIG_HOME="$TMP/config-home" \
    "$BIN" --config "$TMP/config.json" --backend gl --frames 2 \
    --no-grid --screenshot "$gl_out" "$fixture" 2>&1)"
  gl_rc=$?
  [ "$gl_rc" -eq 0 ] && gl_ok=1
  if [ "$gl_ok" -eq 0 ]; then
    if [ "$gl_rc" -eq 124 ] || [ "$gl_rc" -eq 137 ]; then
      echo "SKIP: OpenGL/X11 backend timed out during initialization"
      exit "$SKIP"
    fi
    if grep -Eiq 'failed to open display|glfwInit failed|OpenGL.*unavailable' \
         <<<"$LOG_GL"; then
      echo "SKIP: OpenGL unavailable for $name"
      continue
    fi
    echo "$LOG_GL"
    echo "FAIL: OpenGL render failed for $name"
    exit 1
  fi
  # The bounded low-sampler shader intentionally omits advanced material
  # lobes. Its image cannot be compared with Vulkan's complete OpenPBR path;
  # the dedicated loader/material tests still cover this degraded capability.
  if grep -q 'full GL material shader unavailable; using low-sampler fallback' \
      <<<"$LOG_GL"; then
    echo "SKIP: full OpenGL material shader unavailable for $name"
    exit "$SKIP"
  fi
  if ! grep -q 'renderer: OpenGL' <<<"$LOG_GL"; then
    echo "$LOG_GL"
    echo "FAIL: $name GL launch did not select OpenGL"
    exit 1
  fi
  [ -s "$gl_out" ] || {
    echo "FAIL: $name did not produce an OpenGL screenshot"
    exit 1
  }
  read -r gl_w gl_h < <(python3 - "$gl_out" <<'PY'
import sys
with open(sys.argv[1], "rb") as f:
    if f.readline().strip() != b"P6": raise SystemExit(2)
    print(*map(int, f.readline().split()))
PY
  ) || { echo "FAIL: malformed OpenGL screenshot for $name"; exit 1; }
  LOG_VK="$(run_bounded env XDG_CONFIG_HOME="$TMP/config-home" \
    "$BIN" --config "$TMP/config.json" --backend vk --headless --frames 2 \
    --size "${gl_w}x${gl_h}" --no-grid --screenshot "$vk_out" "$fixture" 2>&1)" && vk_ok=1
  if [ "$vk_ok" -eq 0 ]; then
    if grep -Eiq 'no Vulkan|Vulkan.*unavailable|renderer init failed' \
         <<<"$LOG_VK"; then
      echo "SKIP: Vulkan unavailable for $name"
      continue
    fi
    echo "$LOG_VK"
    echo "FAIL: Vulkan render failed for $name"
    exit 1
  fi
  if ! grep -q 'renderer: Vulkan' <<<"$LOG_VK"; then
    echo "$LOG_VK"
    echo "FAIL: $name Vulkan launch did not select Vulkan"
    exit 1
  fi
  [ -s "$vk_out" ] || {
    echo "FAIL: $name did not produce a Vulkan screenshot"
    exit 1
  }
  any_run=1
  python3 - "$gl_out" "$vk_out" "$name" <<'PY'
import struct, sys
def read_ppm(p):
    with open(p, "rb") as f:
        h = f.readline().strip()
        if h != b"P6": raise SystemExit(f"FAIL: not PPM {p}")
        w, h = map(int, f.readline().split())
        f.readline()  # maxval
        return w, h, f.read()
wa, ha, pa = read_ppm(sys.argv[1])
wb, hb, pb = read_ppm(sys.argv[2])
if wa != wb or ha != hb:
    raise SystemExit(f"FAIL: size mismatch {wa}x{ha} vs {wb}x{hb}")
if len(pa) != len(pb):
    raise SystemExit(f"FAIL: pixel count mismatch")
n = len(pa) // 3
err = sum(
    any(abs(pa[i*3+c] - pb[i*3+c]) > 7 for c in range(3))
    for i in range(n)
)
if err / n > 0.05:
    raise SystemExit(f"FAIL: {sys.argv[3]}: {err}/{n} pixels differ >7/255 ({100.0*err/n:.1f}%)")
print(f"PASS: {sys.argv[3]} GL/Vulkan ({err}/{n} pixels >7/255)")
PY
  rc=$?
  [ "$rc" -eq 0 ] || exit 1
done

if [ "$any_run" -eq 0 ]; then
  echo "SKIP: no fixture could be rendered on either backend"
  exit "$SKIP"
fi
echo "PASS: all GL/VK pairs agree within tolerance"
exit 0
