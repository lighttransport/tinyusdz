#!/usr/bin/env bash
#
# tusdrender RT-preview purpose classification regression. The next-loader path
# must preserve default/render/proxy/guide purpose bits into renderer stats so
# CLI purpose masks and viewer parity checks can diagnose filtered geometry.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
TUSDRENDER="${1:-${TUSDRENDER:-$REPO_ROOT/build/tools/tusdrender/tusdrender}}"

if [ ! -x "$TUSDRENDER" ]; then
  echo "SKIP: tusdrender binary not found at $TUSDRENDER"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
ASSET="$TMP/purpose.usda"
OUT="$TMP/purpose.png"
LOG="$TMP/purpose.log"

cat > "$ASSET" <<'USD'
#usda 1.0
def Xform "Root" {
  def Mesh "DefaultQuad" {
    point3f[] points = [(-1,-1,0),(0,-1,0),(0,0,0),(-1,0,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
  def Mesh "RenderQuad" {
    uniform token purpose = "render"
    point3f[] points = [(0,-1,0),(1,-1,0),(1,0,0),(0,0,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
  def Mesh "ProxyQuad" {
    uniform token purpose = "proxy"
    point3f[] points = [(-1,0,0),(0,0,0),(0,1,0),(-1,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
  def Mesh "GuideQuad" {
    uniform token purpose = "guide"
    point3f[] points = [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
}
USD

"$TUSDRENDER" "$ASSET" "$OUT" -rtPreview -autoframe -samples 1 \
  -w 32 -height 32 -stats -purpose default,render,proxy,guide \
  >"$LOG" 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "FAIL: tusdrender exited with $rc"
  cat "$LOG"
  exit 1
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: tusdrender produced no image"
  cat "$LOG"
  exit 1
fi

require_log() {
  local pattern="$1"
  if ! grep -Eq "$pattern" "$LOG"; then
    echo "FAIL: missing log pattern: $pattern"
    cat "$LOG"
    exit 1
  fi
}

require_log '^rt meshes: 4$'
require_log '^triangles: 8$'
require_log '^rt purpose default triangles: 2$'
require_log '^rt purpose render triangles: 2$'
require_log '^rt purpose proxy triangles: 2$'
require_log '^rt purpose guide triangles: 2$'

echo "PASS: tusdrender purpose stats preserve default/render/proxy/guide"
