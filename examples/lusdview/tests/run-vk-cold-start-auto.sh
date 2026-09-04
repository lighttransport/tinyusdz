#!/usr/bin/env bash
# Verify that a fresh Vulkan pipeline-cache launch in transparency=auto keeps
# weighted OIT completely dormant. Timing is reported for hardware baselines,
# never used as a pass/fail threshold because driver JIT timing is variable.
set -uo pipefail

SKIP=77
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${LUSDVIEW:-$ROOT/build_ninja/lusdview}"
SCENE="$ROOT/models/lusdview-transparency.usda"

if [ ! -x "$BIN" ]; then
  echo "SKIP: lusdview executable not found: $BIN"
  exit "$SKIP"
fi
if [ ! -f "$SCENE" ]; then
  echo "SKIP: transparency fixture missing: $SCENE"
  exit "$SKIP"
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "SKIP: python3 is required to inspect the render report"
  exit "$SKIP"
fi

OUT="$(mktemp -d "${TMPDIR:-/tmp}/lusdview-vk-cold-auto.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
REPORT="$OUT/report.json"
LOG="$OUT/viewer.log"
START="$(date +%s)"

LUSDVIEW_VK_PIPELINE_CACHE_DIR="$OUT/cache" \
  "$BIN" --headless --backend vk --transparency auto --frames 1 --size 64x64 \
  --view-dir 0,0,-1 --render-report "$REPORT" "$SCENE" >"$LOG" 2>&1
RC=$?
ELAPSED=$(( $(date +%s) - START ))

if [ "$RC" -ne 0 ] || [ ! -s "$REPORT" ]; then
  if grep -Eqi 'no compatible Vulkan|no Vulkan device|renderer init failed' "$LOG"; then
    echo "SKIP: Vulkan backend unavailable"
    exit "$SKIP"
  fi
  cat "$LOG" >&2
  echo "FAIL: cold-cache Vulkan auto launch did not produce a report" >&2
  exit 1
fi

python3 - "$REPORT" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    backend = json.load(stream).get("backend", {})
if backend.get("transparency") != "auto":
    raise SystemExit(f"unexpected transparency mode: {backend!r}")
if backend.get("weighted_oit_active") is not False:
    raise SystemExit(f"auto mode activated weighted OIT: {backend!r}")
if backend.get("oit_attachment_bytes") != 0:
    raise SystemExit(f"auto mode allocated OIT attachments: {backend!r}")
PY

if grep -q 'Vulkan weighted OIT resources ready' "$LOG"; then
  echo "FAIL: auto mode created weighted OIT resources" >&2
  exit 1
fi
echo "PASS: cold-cache Vulkan auto launch kept OIT dormant (${ELAPSED}s; advisory)"
