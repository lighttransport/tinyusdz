#!/usr/bin/env bash
# Local NVIDIA CUDA texture regression. It intentionally uses an external HDR
# corpus and is opt-in so CI does not require CUDA or proprietary assets.
set -euo pipefail

bench="${LUSDVIEW_TEXTURE_BENCH:-build_ninja/tools/lusdview-texture-bench/lusdview_texture_gpu_bench}"
root="${LUSDVIEW_CUDA_HDR_ROOT:-}"
device="${LUSDVIEW_CUDA_DEVICE:-NVIDIA GeForce RTX 5060 Ti}"
report="${LUSDVIEW_CUDA_TEXTURE_REPORT:-/tmp/lusdview-cuda-texture-formats.json}"

if [[ -z "$root" || ! -e "$root" ]]; then
  echo "SKIP: set LUSDVIEW_CUDA_HDR_ROOT to an EXR/HDR file or corpus"
  exit 77
fi
if [[ ! -x "$bench" ]]; then
  echo "FAIL: texture benchmark executable not found: $bench" >&2
  exit 1
fi

log="$(mktemp "${TMPDIR:-/tmp}/lusdview-cuda-textures.XXXXXX")"
trap 'rm -f "$log"' EXIT
CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-0}" \
  "$bench" --root "$root" --backend cuda --format bc6h,bc7 \
  --mips 3 --iterations 2 --warmup 1 --device "$device" \
  --report "$report" 2>&1 | tee "$log"

grep -F "cuda: $device" "$log" >/dev/null
grep -E '^  bc6h@mip[0-9]+ ' "$log" >/dev/null
grep -E '^  bc7@mip[0-9]+ ' "$log" >/dev/null

python3 - "$report" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    report = json.load(f)
records = report.get("records", [])
formats = {r.get("format", "").split("@", 1)[0] for r in records
           if r.get("status") == "pass"}
if not {"bc6h", "bc7"}.issubset(formats):
    raise SystemExit("missing passing BC6H/BC7 records")
if any(r.get("gpu_ms", 0) <= 0 for r in records):
    raise SystemExit("missing GPU timing in CUDA report")
PY

echo "PASS: CUDA BC6H/BC7 HDR texture regression ($device)"
