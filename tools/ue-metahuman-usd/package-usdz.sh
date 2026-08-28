#!/usr/bin/env bash
# Package the portable MetaHuman hero layer for the browser WebGL2 demo.
# Keep the USDA root and reference arcs: the web next backend receives the
# archive's internal layers and composes them in WASM.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"
input="${1:-$script_dir/output/MetaHuman_Hero.usda}"
output="${2:-$script_dir/output/MetaHuman_Hero.usdz}"
converter="${TINYUSDZCONVERT:-$repo_root/build_ninja/tools/tusdzconvert/tusdzconvert}"

if [ ! -f "$input" ]; then
  echo "Missing MetaHuman hero layer: $input" >&2
  exit 1
fi
if [ ! -x "$converter" ]; then
  echo "Missing tusdzconvert: $converter" >&2
  exit 1
fi

"$converter" "$input" "$output" -noFlatten --rootLayerFormat usda -noReencode
echo "Wrote $output"
