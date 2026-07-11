#!/usr/bin/env bash
#
# Embed a TrueType font into a committed C header (imgui compressed-TTF format),
# so tusdview ships the font inside the binary — no runtime .ttf dependency.
# This regenerates examples/common/cascadia_mono.h, which app.cc loads via
# ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(CascadiaMono_compressed_*).
#
# Uses imgui's binary_to_compressed_c tool (vendored under
# imgui/misc/fonts/). Default output matches the existing header: a static
# uint32 array, stb-compressed, symbol prefix "CascadiaMono".
#
# Usage:
#   examples/common/embed-font.sh <font.ttf> [symbol] [output.h]
# Examples:
#   examples/common/embed-font.sh ~/Downloads/ttf/CascadiaMono.ttf
#   examples/common/embed-font.sh MyFont.ttf MyFont my_font.h
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOL_SRC="$HERE/imgui/misc/fonts/binary_to_compressed_c.cpp"

TTF="${1:-}"
SYMBOL="${2:-CascadiaMono}"
OUT="${3:-$HERE/cascadia_mono.h}"

if [ -z "$TTF" ] || [ ! -f "$TTF" ]; then
  echo "usage: $(basename "$0") <font.ttf> [symbol] [output.h]" >&2
  echo "  (font .ttf files are not committed; point this at a local copy)" >&2
  exit 1
fi
if [ ! -f "$TOOL_SRC" ]; then
  echo "error: $TOOL_SRC not found (imgui binary_to_compressed_c tool)." >&2
  exit 1
fi

CXX="${CXX:-c++}"
TOOL_BIN="$(mktemp -d)/binary_to_compressed_c"
echo "==> building binary_to_compressed_c ($CXX)"
"$CXX" -O2 -std=c++11 "$TOOL_SRC" -o "$TOOL_BIN"

echo "==> embedding $(basename "$TTF") as '${SYMBOL}_compressed_*' -> $OUT"
# Default flags = static + uint32 array + stb compression (matches the existing
# header). The tool writes to stdout.
"$TOOL_BIN" "$TTF" "$SYMBOL" > "$OUT"

echo "==> done. $(grep -m1 "_compressed_size" "$OUT" || true)"
echo "    Commit $OUT (and keep the font's license note alongside it)."
