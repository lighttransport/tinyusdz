#!/usr/bin/env bash
# lusdquicklook INTERACTIVE smoke test (Xvfb).
#
# The headless --screenshot path never touches lui_window_create, the event loop
# or lui_window_present, so it cannot catch window-creation hangs or a loop that
# blocks before painting its first frame -- both of which happened during
# development. This drives the real GUI under Xvfb instead.
#
# Skips cleanly when Xvfb or ImageMagick's `import` is unavailable.
#
# usage: run-quicklook-gui-smoke.sh <lusdquicklook-binary> <repo-root>
set -euo pipefail

BIN="${1:?usage: run-quicklook-gui-smoke.sh <binary> <repo-root>}"
ROOT="${2:?missing repo root}"

fail() { echo "FAIL: $*" >&2; exit 1; }
skip() { echo "SKIP: $*"; exit 0; }

command -v Xvfb >/dev/null 2>&1 || skip "Xvfb not installed"
command -v import >/dev/null 2>&1 || skip "ImageMagick 'import' not installed"
command -v xwininfo >/dev/null 2>&1 || skip "xwininfo not installed"

ASSET="$ROOT/models/suzanne-pbr.usda"
[ -f "$ASSET" ] || skip "test asset not found: $ASSET"

OUT="$(mktemp -d)"
DISP=":$((90 + RANDOM % 8))"

XPID=""
APP=""
cleanup() {
  [ -n "$APP" ] && kill -9 "$APP" 2>/dev/null || true
  [ -n "$XPID" ] && kill "$XPID" 2>/dev/null || true
  rm -rf "$OUT"
}
trap cleanup EXIT

echo "== lusdquicklook GUI smoke on $DISP"

Xvfb "$DISP" -screen 0 1000x700x24 >/dev/null 2>&1 &
XPID=$!
sleep 2
kill -0 "$XPID" 2>/dev/null || skip "Xvfb failed to start on $DISP"

DISPLAY="$DISP" "$BIN" "$ASSET" --size 900x600 --backend cpu \
  >"$OUT/stdout.txt" 2>"$OUT/stderr.txt" &
APP=$!

# Give it time to create the window, load and render. A hang in
# lui_window_create shows up here as "no window".
sleep 8

kill -0 "$APP" 2>/dev/null || {
  cat "$OUT/stderr.txt" >&2
  fail "the app exited early"
}

WID="$(DISPLAY="$DISP" xwininfo -root -children 2>/dev/null \
       | grep lusdquicklook | awk '{print $1}' || true)"
[ -n "$WID" ] || fail "no lusdquicklook window was mapped (window-create hang?)"

DISPLAY="$DISP" import -window "$WID" "$OUT/gui.png" 2>/dev/null \
  || fail "could not capture the window"
[ -s "$OUT/gui.png" ] || fail "empty capture"

# The window must not be blank: a loop that blocks before its first paint still
# produces a perfectly valid all-black window.
if command -v python3 >/dev/null 2>&1; then
  python3 - "$OUT/gui.png" <<'PY'
import subprocess, sys
# Convert to a raw PPM via ImageMagick rather than decoding PNG by hand: the
# capture may be palette or 16-bit depending on the ImageMagick build.
raw = subprocess.run(['convert', sys.argv[1], '-depth', '8', 'ppm:-'],
                     capture_output=True).stdout
if not raw.startswith(b'P6'):
    print('could not convert capture; skipping blank check')
    sys.exit(0)
# Parse the PPM header (3 whitespace-separated fields after the magic).
fields, i = [], 2
while len(fields) < 3:
    while i < len(raw) and raw[i:i+1].isspace(): i += 1
    if raw[i:i+1] == b'#':
        while i < len(raw) and raw[i:i+1] != b'\n': i += 1
        continue
    j = i
    while j < len(raw) and not raw[j:j+1].isspace(): j += 1
    fields.append(int(raw[i:j])); i = j
i += 1
w, h, _ = fields
px = raw[i:]
colors = set()
for y in range(0, h, 4):
    row = y * w * 3
    for x in range(0, w, 4):
        o = row + x * 3
        colors.add(px[o:o+3])
if len(colors) < 20:
    print(f'window has only {len(colors)} distinct colours -- blank window',
          file=sys.stderr)
    sys.exit(1)
print(f'window painted: {len(colors)} distinct colours')
PY
fi

# A converged previewer must go idle, not spin. Sample CPU time over a window
# and require it to stay small.
read -r _ _ _ _ _ _ _ _ _ _ _ _ _ ut1 st1 _ < "/proc/$APP/stat"
sleep 3
if [ -r "/proc/$APP/stat" ]; then
  read -r _ _ _ _ _ _ _ _ _ _ _ _ _ ut2 st2 _ < "/proc/$APP/stat"
  ticks=$(( (ut2 + st2) - (ut1 + st1) ))
  hz="$(getconf CLK_TCK 2>/dev/null || echo 100)"
  # Allow up to ~35% of one core over the sample: the image may still be
  # refining. A busy-wait would sit at ~100%.
  limit=$(( 3 * hz * 35 / 100 ))
  if [ "$ticks" -gt "$limit" ]; then
    fail "idle CPU use too high: ${ticks} ticks over 3s (limit ${limit})"
  fi
  echo "idle CPU: ok (${ticks} ticks over 3s)"
fi

echo "== lusdquicklook GUI smoke: PASS"
