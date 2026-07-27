#!/usr/bin/env bash
# Build the Basis-free texcomp transcoder WASM module for the web demo.
#
# Produces texcomp_web.mjs + texcomp_web.wasm in this directory (ES6 module,
# loaded by texcomp.js). Pure C11 — no tinyusdz, no basis_universal.
#
# Requires emscripten on PATH (e.g. `source /path/to/emsdk/emsdk_env.sh`).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
TEXCOMP="$ROOT/src/external/textools/texcomp"

if ! command -v emcc >/dev/null 2>&1; then
  echo "emcc not found. Activate emscripten first: source /path/to/emsdk/emsdk_env.sh" >&2
  exit 1
fi

# All pure-C11 texcomp sources (encoders + uni intermediate + decoders). The
# uni transcoder pulls in the ASTC / BC7 / BC1 / ETC2 paths.
SRCS=("$HERE/texcomp_web.c")
for f in "$TEXCOMP"/src/*.c; do
  case "$(basename "$f")" in
    texcomp_cli.c) ;;                 # CLI (needs stdio / tinyexr) — skip
    *) SRCS+=("$f") ;;
  esac
done

emcc -O3 -std=c11 \
  -I"$TEXCOMP/include" \
  "${SRCS[@]}" \
  -o "$HERE/texcomp_web.mjs" \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createTexcompModule \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_FUNCTIONS='["_malloc","_free"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAPU32"]'

echo "built: $HERE/texcomp_web.mjs (+ .wasm)"
