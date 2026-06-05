#!/usr/bin/env bash
# Compile and run the WASM tstring_view heap-growth regression test in node.
# Verifies that a tstring_view's backing std::string buffer is not invalidated
# by WASM linear-memory growth (-sALLOW_MEMORY_GROWTH=1).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
src_root="$(cd "$here/../.." && pwd)/src"   # tinyusdz src/ (for tiny-string.hh)

# em++ must be on PATH (source emsdk_env.sh), or set EMXX.
EMXX="${EMXX:-em++}"

out="$(mktemp -d)/wasm-stringview-heapgrowth.js"
"$EMXX" -std=c++17 -O2 -I "$src_root" \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16MB -sENVIRONMENT=node \
  "$here/wasm-stringview-heapgrowth.cc" -o "$out"

node "$out"
