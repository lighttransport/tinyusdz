#!/usr/bin/env bash
# List files that the tusdview texture benchmark considers real corpus inputs.
set -euo pipefail

root="${1:-${TUSDVIEW_TEXTURE_GPU_ROOT:-}}"
if [[ -z "$root" || ! -e "$root" ]]; then
  echo "usage: $0 TEXTURE_FILE_OR_DIRECTORY" >&2
  exit 2
fi

if [[ -f "$root" ]]; then
  [[ "$(basename "$root")" != ._* ]] && printf '%s\n' "$root"
  exit 0
fi

find "$root" -type f \
  ! -name '._*' \
  \( -iname '*.exr' -o -iname '*.hdr' -o -iname '*.tif' \
     -o -iname '*.tiff' -o -iname '*.tex' -o -iname '*.ptx' \
     -o -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' \
     -o -iname '*.bmp' -o -iname '*.tga' -o -iname '*.ppm' \) \
  -print | LC_ALL=C sort
