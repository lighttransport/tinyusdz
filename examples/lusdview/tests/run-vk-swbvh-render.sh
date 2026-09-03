#!/usr/bin/env bash
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
LUSDVIEW="${LUSDVIEW:-$REPO_ROOT/build/lusdview}"
ASSET="${ASSET:-$REPO_ROOT/models/suzanne-pbr.usda}"

if [ ! -x "$LUSDVIEW" ]; then
  echo "SKIP: lusdview binary not found at $LUSDVIEW"
  exit "$SKIP"
fi
if [ ! -f "$ASSET" ]; then
  echo "SKIP: asset not found at $ASSET"
  exit "$SKIP"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/swbvh.ppm"

set +e
if command -v timeout >/dev/null 2>&1; then
  LOG="$(LUSDVIEW_RT_FORCE_SW=1 timeout --kill-after=5s \
      "${LUSDVIEW_RENDER_TIMEOUT:-30s}" "$LUSDVIEW" --headless --backend vk --rt \
      --frames 1 --size "${LUSDVIEW_SIZE:-64x64}" --screenshot "$OUT" "$ASSET" 2>&1)"
else
  LOG="$(LUSDVIEW_RT_FORCE_SW=1 "$LUSDVIEW" --headless --backend vk --rt \
      --frames 1 --size "${LUSDVIEW_SIZE:-64x64}" --screenshot "$OUT" "$ASSET" 2>&1)"
fi
RC=$?
set -e
echo "$LOG"

if [ "$RC" -ne 0 ]; then
  echo "SKIP: Vulkan compute-BVH backend unavailable (exit $RC)"
  exit "$SKIP"
fi
if ! grep -q "renderer: Vulkan (compute BVH)" <<<"$LOG"; then
  echo "FAIL: forced software RT did not select compute-BVH renderer"
  exit 1
fi
if ! grep -q "render stats:" <<<"$LOG" ||
   ! grep -Eq "drawn tris [1-9][0-9]*" <<<"$LOG"; then
  echo "FAIL: compute-BVH render produced no geometry"
  exit 1
fi
if [ ! -s "$OUT" ]; then
  echo "FAIL: compute-BVH screenshot was not written"
  exit 1
fi

if command -v python3 >/dev/null 2>&1; then
  python3 - "$OUT" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
if not data.startswith(b"P6"):
    raise SystemExit("FAIL: screenshot is not a PPM image")
payload = data[data.find(b"\n", data.find(b"\n") + 1) + 1:]
if not any(byte not in (0, 255) for byte in payload):
    raise SystemExit("FAIL: screenshot appears blank")
PY
fi

echo "PASS: forced compute-BVH RT render is non-blank"
