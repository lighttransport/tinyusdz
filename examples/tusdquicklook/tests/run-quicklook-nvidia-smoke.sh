#!/usr/bin/env bash
# tusdquicklook offscreen NVIDIA smoke test.
#
# Keep X virtual while selecting the NVIDIA EGL/GLX vendor explicitly. This is
# the constrained-GPU path used for the 2 GB development device; it is not a
# generic Mesa test and therefore skips when no live NVIDIA driver is present.
#
# usage: run-quicklook-nvidia-smoke.sh <tusdquicklook-binary> <repo-root>
set -euo pipefail

BIN="${1:?usage: run-quicklook-nvidia-smoke.sh <binary> <repo-root>}"
ROOT="${2:?missing repo root}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

skip() { echo "SKIP: $*"; exit 77; }
fail() { echo "FAIL: $*" >&2; exit 1; }

command -v xvfb-run >/dev/null 2>&1 || skip "xvfb-run not installed"
command -v nvidia-smi >/dev/null 2>&1 || skip "nvidia-smi not installed"
nvidia-smi -L >/dev/null 2>&1 || skip "NVIDIA driver is not live"

VENDOR_JSON=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
[ -f "$VENDOR_JSON" ] || skip "NVIDIA GLVND EGL vendor file not found"
[ -f "$ROOT/models/suzanne-pbr.usda" ] ||
  skip "test asset not found: $ROOT/models/suzanne-pbr.usda"

xvfb-run -a -s "-screen 0 800x600x24" env \
  __NV_PRIME_RENDER_OFFLOAD=1 \
  __GLX_VENDOR_LIBRARY_NAME=nvidia \
  __EGL_VENDOR_LIBRARY_FILENAMES="$VENDOR_JSON" \
  "$BIN" "$ROOT/models/suzanne-pbr.usda" --backend gl \
    --max-gpu-mem 512 --frames 4 --size 640x480 \
    --screenshot "$OUT/nvidia.png" --verbose \
    >"$OUT/output.txt" 2>&1 \
  || { cat "$OUT/output.txt" >&2; fail "NVIDIA GL render failed"; }

[ -s "$OUT/nvidia.png" ] || fail "NVIDIA GL render produced no PNG"
head -c 8 "$OUT/nvidia.png" | od -An -tx1 | tr -d ' \n' \
  | grep -qi '^89504e470d0a1a0a$' || fail "output is not a PNG"
grep -q 'renderer: gl ' "$OUT/output.txt" || {
  cat "$OUT/output.txt" >&2
  fail "GL request did not stay on the NVIDIA GL renderer"
}

echo "== tusdquicklook NVIDIA smoke: PASS"
