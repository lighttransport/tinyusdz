# usdzconvert (WebAssembly) — browser, Node CLI, and library

TinyUSDZ's WASM `usdzconvert` packages a USD scene (a `.usd`/`.usda`/`.usdc`
layer plus textures, or an existing `.usdz`) into a `.usdz` archive — entirely
in the browser or in Node, no native toolchain required. It is the WebAssembly
counterpart of the native [`tusdzconvert`](../../doc/tusdzconvert.md).

There are three entry points, all sharing one library
(`web/js/src/usdzconvert.js`):

| Entry point | File | Use |
|-------------|------|-----|
| **Browser demo** | `web/js/usdzconvert.js` | Drag-and-drop UI; upload a folder/files/`.usdz`, convert, download. |
| **Node CLI** | `web/js/cli/usdzconvert.js` | Batch/scripted conversion from the terminal. |
| **Library** | `web/js/src/usdzconvert.js` → `convertFolderToUSDZ()` | Programmatic API for your own JS/TS app. |

> PNG re-encoding uses portable **fpng** (fpnge needs x86 SIMD, not compiled for
> WASM). The native CLI uses fpnge. Output is otherwise equivalent.

## Build the WASM module

```bash
# 32-bit glue (default): web/js/src/tinyusdz/tinyusdz.js
ninja -C web/build_ninja tinyusdz.js
# 64-bit glue (MEMORY64): web/js/src/tinyusdz/tinyusdz_64.js
ninja -C web/build_64_ninja tinyusdz_64.js
```

---

## Browser demo

`web/js/usdzconvert.js` builds a small UI: drop or pick a **folder** (USD +
textures), multiple files, or a single **`.usdz`**, set options, **Convert** to
validate, then **Download**. It also includes a texture channel-repack tool.

Options map 1:1 to the library (see below): root layer, max texture size,
texture format (keep / PNG / JPEG / EXR), USDZ root layer format, flatten, ARKit
compatible, target total texture size + fit strategy, re-encode toggle, JPEG
quality, and the output filename.

The demo only attaches its canvas-based `textureProcessor` when textures
actually need work (resize / re-encode / format change / size-fit). When they
pass through unchanged (re-encode off + keep format, no resize/target), the
processor is omitted so a single self-contained `.usdz` can take the **low-heap**
path (below) and large scenes convert without exhausting the 2 GB wasm32 heap.

---

## Node CLI

Runs with plain `node` (the CLI loads the Emscripten glue directly) or
`vite-node`:

```text
node cli/usdzconvert.js <input-dir|input.usd|input.usdz> [options]
node cli/usdzconvert.js --repack <out.png> -packR <src> [-packG <src> ...] [options]
```

A directory is read recursively; a single `.usd` reads its sibling textures; a
`.usdz` is unpacked and repacked (textures pass through by default).

### Convert options

| Option | Description |
|--------|-------------|
| `-o`, `--output <file>` | Output `.usdz` path (default `<root>.usdz`). |
| `--root <relpath>` | Root USD layer within the input dir (default: auto). |
| `--resize <N>` | Cap each texture's longest edge to N pixels. |
| `--texture-format <keep\|png\|jpeg>` | Texture output (default `keep`). |
| `--root-layer-format <usdc\|usda>` | USDZ root layer (default `usdc`). |
| `--arkit-compatible` | Force an ARKit-friendly flattened USDC package. |
| `--no-flatten` | Accepted for parity; the JS/WASM export still flattens. |
| `--jpeg-quality <1-100>` | JPEG quality when re-encoding (default 90). |
| `--no-reencode` | Copy unmodified textures through unchanged. |
| `--max-usdc-mb <N>` | Raise the USDC root-layer write size cap to N MB (0 = ~100 MB default). Needed for very large scenes. |
| `--max-mem-mb <N>` | Raise the USDC writer memory cap to N MB (0 = default). |
| `--png-encoder <fpnge\|fpng\|auto>` | PNG encoder hint (WASM always uses fpng). |
| `-v`, `--verbose` | Verbose logging. |
| `-h`, `--help` | Show help. |

### Fit textures to a total size budget

| Option | Description |
|--------|-------------|
| `--target-size <size>` | Shrink textures so their total fits `<size>` (e.g. `100MB`). |
| `--fit-strategy <size\|quality>` | Reduce dimensions (`size`) or transcode to JPEG + lower quality (`quality`). Default `size`. |
| `--fit-min-size <N>` | Smallest longest-edge for the size search (default 64). |
| `--fit-min-quality <N>` | Lowest JPEG quality for the quality search (default 30). |

### Repack mode

| Option | Description |
|--------|-------------|
| `--repack <out.png>` | Enable repack mode; write the packed image here. |
| `-packR`/`-packG`/`-packB`/`-packA <src>` | `file.png:CH` (CH 0..3) or `const:VALUE`. |
| `--pack-channels <1-4>` | Output channel count (default: from `-pack*` flags). |

### CLI examples

```bash
# Folder → USDZ, cap textures at 1024 px
node cli/usdzconvert.js ./model_folder -o model.usdz --resize 1024 -v

# Repack a .usdz, passing textures through (fast)
node cli/usdzconvert.js in.usdz -o out.usdz --no-reencode

# Large scene, low-heap keep-textures flatten (raise the USDC cap)
node cli/usdzconvert.js big.usdz -o out.usdz --no-reencode --max-usdc-mb 1500

# ARKit flatten, textures kept (low-heap)
node cli/usdzconvert.js big.usdz -o out.usdz --arkit-compatible --no-reencode --max-usdc-mb 1500

# Build an ORM map
node cli/usdzconvert.js --repack orm.png -packR ao.png:0 -packG rough.png:0 -packB metal.png:0 --pack-channels 3
```

---

## Library API

```js
import { loadWasm, convertFolderToUSDZ } from './src/usdzconvert.js';

const native = await loadWasm(() => import('./src/tinyusdz/tinyusdz.js'));

// assetMap: Map<string, Uint8Array> of every file (USD layers + textures),
// keyed by relative path. A single self-contained .usdz is one entry.
const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, {
  rootPath: 'scene.usdz',
  reencode: false,        // keep textures (passthrough)
  textureFormat: 'keep',
  arkitCompatible: true,
  maxUsdcMb: 1500,
  log: (m) => console.log(m),
});
// usdz: Uint8Array; stats: { textures, resized, reencoded, rootPath, ... }
```

### Options (`opts`)

| Key | Default | Description |
|-----|---------|-------------|
| `rootPath` | auto | Root USD layer's key in `assetMap`. |
| `flatten` | `true` | Flatten composition before writing. |
| `arkitCompatible` | `false` | ARKit-friendly flattened USDC package. |
| `rootLayerFormat` | `'usdc'` | `'usdc'` or `'usda'`. |
| `reencode` | `true` | Re-encode textures; `false` = passthrough. |
| `textureFormat` | `'keep'` | `'keep' \| 'png' \| 'jpeg'`. |
| `maxTextureSize` | `0` | Cap longest edge (px); 0 = no resize. |
| `targetTextureBytes` | `0` | Fit all textures into a total byte budget. |
| `fitStrategy` | `'size'` | `'size'` or `'quality'`. |
| `fitMinTextureSize` / `fitMinQuality` | `64` / `30` | Fit-search floors. |
| `jpegQuality` | `90` | JPEG quality (1–100). |
| `pngEncoder` | `'fpng'` | PNG backend (WASM uses fpng). |
| `maxUsdcMb` / `maxMemMb` | `0` | Raise the USDC write size / memory caps (MB). |
| `textureProcessor` | — | `async ({name,data,...}) => {data,ext,resized,reencoded} \| null`. Host-side texture work (the browser demo uses a canvas processor). **Providing it disables the low-heap path.** |
| `audioProcessor` | — | Host-side audio processing. Also disables the low-heap path. |
| `log` | no-op | `(message) => void` progress sink. |

---

## Low-heap paths and the 2 GB wasm32 heap

The wasm32 linear heap is capped at 2 GB and never shrinks. Building a whole
USDZ (decode/re-encode every texture + the full crate + the zip) in that heap
OOMs on large (100–300 MB) scenes. For a **single self-contained `.usdz`** with
**textures passed through** (`reencode:false`, `textureFormat:'keep'`, no
resize/target, no `textureProcessor`/`audioProcessor`), the converter takes a
**low-heap** path instead: it streams the rewritten root USDC straight into a JS
`Uint8Array` (not a wasm-side vector) and repacks the zip in JS (store, no
compression), copying texture entries through unchanged.

Which low-heap path runs depends on `arkitCompatible` and `opts.lowHeapStageMode`:

| Config | Path | What it writes |
|--------|------|----------------|
| not ARKit | **layer** | The composed Layer's PrimSpecs as-authored (faithful). |
| ARKit (default) | **flatten-layer** | C++ `Layer→Layer` flatten then write the Layer — no typed Stage, no layer copy; lightest and faithful. |
| ARKit + `lowHeapStageMode:'stage'` | **stage** | Typed-Prim Stage reconstruction (heaviest; matches the in-heap ARKit path). |

The `flatten-layer` and `layer` paths are both faithful (PrimSpecs written
as-authored — no typed-input loss) and markedly lighter than `stage` (which
copies the whole layer in `getStageFromLayer` and rebuilds a typed Stage). For
large scenes, `flatten-layer` cuts peak RSS by several hundred MB and avoids the
OOM. See [`../../usdzconvert.md`](../../usdzconvert.md) (local report, not
committed) for measurements.

### Tuning knobs

| Key | Default | Description |
|-----|---------|-------------|
| `lowHeapStageMode` | `'flatten-layer'` | `'flatten-layer'` or `'stage'` for the ARKit low-heap path. |
| `lowHeapRootSizeMultiplier` | `1.5` | Initial root-USDC buffer = `input × this` (grows on demand if too small). |
| `lowHeapFlattenUsdz` / `lowHeapStageUsdz` | `true` | Set `false` to opt out of the low-heap layer / stage paths. |
| `repackUsdz` / `passthroughUsdz` | `true` | Unpack+repack a `.usdz` input (passthrough textures). |

> If you pass a `textureProcessor` (the browser demo does, when textures need
> work), the low-heap path is **disabled** for that conversion — the host needs
> each decoded texture, so the converter uses the standard in-heap path.

---

## Differences from the native CLI

- **PNG encoder:** WASM uses portable fpng; native uses fpnge.
- **Memory:** WASM is bound by the 2 GB wasm32 heap, hence the low-heap paths
  above; native has no such cap.
- **Texture decode in the browser:** the demo decodes/encodes PNG/JPEG via the
  canvas API and routes other formats (e.g. EXR) to TinyUSDZ WASM.
