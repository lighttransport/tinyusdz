# texcomp web demo — Basis-free GPU texture compression

Demonstrates, in the browser, the same transcode-on-load strategy tusdview uses
natively: one RGBA8 texture → the private tinyexr **`uni`** intermediate →
transcoded per device to the GPU-native block format the browser advertises
(BC7 / ASTC / ETC2), uploaded as a `THREE.CompressedTexture`. Where no compressed
format is available it decodes `uni` back to RGBA8 (`THREE.DataTexture`). No
`basis_universal`, no `KTX2Loader`.

Pages: `../texcomp.html` + `../texcomp.js`. WASM: this directory.

## Build the WASM module (once)

```sh
source /path/to/emsdk/emsdk_env.sh          # activate emscripten
bash build.sh                                # -> texcomp_web.mjs + texcomp_web.wasm
```

`texcomp_web.{mjs,wasm}` are generated build artifacts (git-ignored). The module
is pure C11 compiled from `src/external/textools/texcomp` + `texcomp_web.c`; it
does not link lightusd.

## Run

```sh
cd ..            # web/js
npm run dev:texcomp        # Vite dev server, opens /texcomp.html
# or any static server:  python3 -m http.server 8199   then open /texcomp.html
```

The panel shows the detected device formats, the chosen GPU format, and the VRAM
saving (e.g. a 256×256 texture: 256 KiB RGBA8 → 64 KiB BC7 = 4×).

## ABI (`texcomp_web.c`)

All buffers are caller-managed (`Module._malloc` / `HEAPU8` / `_free`):
`tcw_uni_size`, `tcw_block_size`, `tcw_compress_uni`, `tcw_transcode`
(0=BC7, 1=ASTC 4×4, 2=ETC2_RGBA), `tcw_decompress_rgba8`.
