#!/usr/bin/env bash
# Opt-in real-corpus texture processing test for lusdview's GPU texture path.
# The corpus is intentionally external; unset LUSDVIEW_TEXTURE_GPU_ROOT skips.
set -uo pipefail

bench="${LUSDVIEW_TEXTURE_BENCH:-}"
root="${LUSDVIEW_TEXTURE_GPU_HDR_ROOT:-${LUSDVIEW_TEXTURE_GPU_ROOT:-}}"
backend="${LUSDVIEW_TEXTURE_GPU_BACKEND:-vulkan}"
device="${LUSDVIEW_TEXTURE_GPU_DEVICE:-}"
formats="${LUSDVIEW_TEXTURE_GPU_FORMATS:-bc7}"
mips="${LUSDVIEW_TEXTURE_GPU_MIPS:-3}"
max_images="${LUSDVIEW_TEXTURE_GPU_MAX_IMAGES:-2}"
max_dimension="${LUSDVIEW_TEXTURE_GPU_MAX_DIMENSION:-0}"
iterations="${LUSDVIEW_TEXTURE_GPU_ITERATIONS:-1}"
warmup="${LUSDVIEW_TEXTURE_GPU_WARMUP:-0}"
metadata_only="${LUSDVIEW_TEXTURE_GPU_METADATA_ONLY:-0}"
min_psnr="${LUSDVIEW_TEXTURE_GPU_MIN_PSNR:-0}"
max_gpu_ms="${LUSDVIEW_TEXTURE_GPU_MAX_GPU_MS:-0}"
prefer_textures="${LUSDVIEW_TEXTURE_GPU_PREFER_TEXTURES:-0}"

if [[ "$prefer_textures" == "1" && -d "$root/textures" ]]; then
  root="$root/textures"
fi

if [[ -z "$root" || ! -e "$root" ]]; then
  echo "SKIP: set LUSDVIEW_TEXTURE_GPU_ROOT to an external texture file or corpus"
  exit 77
fi
if [[ -z "$bench" || ! -x "$bench" ]]; then
  echo "FAIL: texture benchmark executable not found: ${bench:-<unset>}" >&2
  exit 1
fi

cmd=("$bench" --root "$root" --backend "$backend" --format "$formats"
     --mips "$mips" --iterations "$iterations" --warmup "$warmup")
if [[ "$metadata_only" == "1" ]]; then cmd+=(--metadata-only); fi
if [[ "$max_images" != "0" ]]; then cmd+=(--max-images "$max_images"); fi
if [[ "$max_dimension" != "0" ]]; then cmd+=(--max-dimension "$max_dimension"); fi
if [[ -n "$device" ]]; then cmd+=(--device "$device"); fi

echo "RUN: ${cmd[*]}"
log="$(mktemp "${TMPDIR:-/tmp}/lusdview-texture-real.XXXXXX")"
trap 'rm -f "$log"' EXIT
"${cmd[@]}" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
if [[ $rc -eq 0 && "$min_psnr" != "0" && "$metadata_only" != "1" ]]; then
  if ! awk -v min="$min_psnr" '
    /GPU/ && /dB$/ { seen=1; value=$(NF-1); if (value+0 < min) bad=1 }
    END { exit (!seen || bad) }
  ' "$log"; then
    echo "FAIL: observed texture PSNR is below ${min_psnr} dB" >&2
    exit 1
  fi
fi
if [[ $rc -eq 0 && "$max_gpu_ms" != "0" && "$metadata_only" != "1" ]]; then
  if ! awk -v max="$max_gpu_ms" '
    /GPU/ && /dB$/ { seen=1; value=$(NF-4); if (value+0 > max) bad=1 }
    END { exit (!seen || bad) }
  ' "$log"; then
    echo "FAIL: observed GPU time exceeds ${max_gpu_ms} ms" >&2
    exit 1
  fi
fi
if [[ $rc -eq 77 ]]; then
  echo "SKIP: texture backend or corpus is unavailable"
elif [[ $rc -ne 0 ]]; then
  echo "FAIL: real texture benchmark exited with $rc" >&2
  exit "$rc"
fi
exit "$rc"
