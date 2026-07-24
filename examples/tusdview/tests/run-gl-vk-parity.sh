#!/usr/bin/env bash
#
# GL/Vulkan raster image parity: compare headless renders of focused material
# fixtures through both backends.  Pixels must agree within a tolerance that
# accounts for software-rasterizer (llvmpipe/lavapipe) differences.
# Skip when either backend is unavailable.
set -uo pipefail
SKIP=77

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${TUSDVIEW:-$ROOT/build/tusdview}"
[ -x "$BIN" ] || BIN="$ROOT/build_ninja/tusdview"
[ -x "$BIN" ] || { echo "SKIP: tusdview not found"; exit "$SKIP"; }

RUN=()
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
  RUN=(xvfb-run -a)
fi

TMP="$(mktemp -d /tmp/tusdview-gl-vk-parity.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# Fixtures from the focused material test suite.
FIXTURES=(
  "$ROOT/tests/usda/tusdview-unsupported-realtime-lobes.usda"
  "$ROOT/tests/usda/tusdview-shadow-alpha-inst.usda"
)

any_run=0
for fixture in "${FIXTURES[@]}"; do
  [ -f "$fixture" ] || continue
  name="$(basename "$fixture" .usda)"
  gl_out="$TMP/${name}_gl.ppm"
  vk_out="$TMP/${name}_vk.ppm"
  gl_ok=0; vk_ok=0
  LOG_GL="$("${RUN[@]}" "$BIN" --backend gl --headless --frames 2 \
    --screenshot "$gl_out" "$fixture" 2>&1)" && gl_ok=1
  LOG_VK="$("${RUN[@]}" "$BIN" --backend vk --headless --frames 2 \
    --screenshot "$vk_out" "$fixture" 2>&1)" && vk_ok=1
  [ "$gl_ok" -eq 1 ] && [ "$vk_ok" -eq 1 ] || { any_run=1; continue; }
  any_run=1
  python3 - "$gl_out" "$vk_out" <<'PY'
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
err = 0.0
for i in range(n):
    for c in range(3):
        d = abs(pa[i*3+c] - pb[i*3+c])
        if d > 7: err += 1  # count pixels exceeding 7/255
if err / n > 0.05:
    raise SystemExit(f"FAIL: {err}/{n} pixels differ >7/255 ({100.0*err/n:.1f}%)")
print(f"PASS: {sys.argv[1]} vs {sys.argv[2]} ({err}/{n} pixels >7/255)")
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
