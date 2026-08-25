#!/usr/bin/env bash
#
# Regression test: hostile or unsupported CLI input must fail cleanly, never
# abort or silently no-op.
#
#   -fitScale abc          used std::stof -> uncaught std::invalid_argument -> SIGABRT.
#   -w / -samples <huge>   were accepted up to INT_MAX; the framebuffer/sample
#                          loop had no other guard, so they were an OOM/hang.
#   -frames on the non-next path (plain .usda, no -rtPreview) was silently
#                          ignored: one image named literally with the #### token.
#
# Exit codes: 0 = pass, 1 = fail, 77 = skip.
set -uo pipefail
SKIP=77

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDRENDER="${1:-${TUSDRENDER:-$REPO_ROOT/build/tools/tusdrender/tusdrender}}"

if [ ! -x "$TUSDRENDER" ]; then
  echo "SKIP: tusdrender binary not found at $TUSDRENDER"; exit $SKIP
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail() { echo "FAIL: $1"; exit 1; }

cat > "$TMP/tri.usda" <<'USDA'
#usda 1.0
(defaultPrim = "World" upAxis = "Y")
def Xform "World" {
    def Mesh "Tri" {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
    }
}
USDA

# Each hostile input must exit non-zero WITHOUT being killed by a signal
# (128+N = signal; 134 = the old SIGABRT from std::stof).
expect_clean_failure() {
  local desc="$1"; shift
  timeout 20 "$TUSDRENDER" "$@" > "$TMP/log" 2>&1
  local rc=$?
  if [ $rc -eq 0 ]; then fail "$desc was accepted (exit 0)"; fi
  if [ $rc -ge 128 ]; then fail "$desc crashed/hung (exit $rc): $(tail -1 "$TMP/log")"; fi
}

expect_clean_failure "-fitScale abc" "$TMP/tri.usda" "$TMP/x.png" -fitScale abc
expect_clean_failure "-fitScale 1e999" "$TMP/tri.usda" "$TMP/x.png" -fitScale 1e999
expect_clean_failure "-w 2000000000" "$TMP/tri.usda" "$TMP/x.png" -w 2000000000
expect_clean_failure "-height 2000000000" "$TMP/tri.usda" "$TMP/x.png" -height 2000000000
expect_clean_failure "-samples 2000000000" "$TMP/tri.usda" "$TMP/x.png" -samples 2000000000
expect_clean_failure "--pt-samples 2000000000" "$TMP/tri.usda" "$TMP/x.png" \
    -cuda --path-trace --pt-samples 2000000000
expect_clean_failure "--path-trace without a supported backend" \
    "$TMP/tri.usda" "$TMP/x.png" --path-trace
expect_clean_failure "-frames on the non-next path" \
    "$TMP/tri.usda" "$TMP/f.####.png" -frames 1:3 -w 16 -height 16
grep -qi "frames" "$TMP/log" \
  || fail "-frames rejection did not mention -frames: $(tail -1 "$TMP/log")"

# Sane inputs must still work.
timeout 60 "$TUSDRENDER" "$TMP/tri.usda" "$TMP/ok.png" -w 16 -height 16 \
    -samples 1 -fitScale 1.5 -autoframe > "$TMP/log" 2>&1 \
  || fail "a valid render regressed: $(cat "$TMP/log")"
[ -f "$TMP/ok.png" ] || fail "valid render wrote no image"

echo "PASS: hostile CLI inputs fail cleanly; valid ones still render"
exit 0
