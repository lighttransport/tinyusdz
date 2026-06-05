# WebAssembly Heap Notes

This document tracks current heap behavior for large USDZ conversion paths and
possible reductions. It is written from the `large_sample.usdz` investigation on
2026-06-05.

## Current Situation

Test asset:

- Input: `/path/to/large_sample.usdz`
- Input size: `214034431` bytes, about 205 MB
- Archive contents: 93 entries, root layer `large_sample.usdc`

The plain no-transform USDZ-to-USDZ path now has a narrow passthrough fast path
in `web/js/src/usdzconvert.js`. When the input is a single `.usdz`, textures are
kept unchanged, no resize/target budget/ARKit rewrite is requested, and
`--no-reencode` is used, the converter writes the original archive bytes instead
of unpacking and loading the scene into the WASM heap.

Observed wasm32 passthrough result:

```console
node web/js/cli/usdzconvert.js \
  /path/to/large_sample.usdz \
  -o /tmp/large_sample.wasm.usdz \
  --no-reencode --max-usdc-mb 1024 --max-mem-mb 2048 -v
```

- Output: `/tmp/large_sample.wasm.usdz`
- Output size: `214034431` bytes
- Output SHA-256: identical to input
- Max RSS: `272656 KB`
- Result: succeeds without approaching the wasm32 2 GB heap limit

The full flattened JS/WASM path is different. It unpacks the USDZ, loads the
USDC layer, composes, flattens, writes a new USDC root layer, and packages a new
USDZ. For single-USDZ inputs with unchanged textures/audio/assets, the converter
now uses a low-heap root-rewrite path: only USD dependency layers are registered
with the WASM resolver, the flattened root layer is exported directly into a
JavaScript `Uint8Array`, and non-root archive entries are copied from the
original USDZ into a newly written USDZ.

Observed wasm32 low-heap flattened result:

```console
node web/js/cli/usdzconvert.js \
  /path/to/large_sample.usdz \
  -o /tmp/large_sample.wasm32_lowheap_direct.usdz \
  --no-reencode --max-usdc-mb 1024 --max-mem-mb 2048 -v
```

- Output: `/tmp/large_sample.wasm32_lowheap_direct.usdz`
- Output size: `535500713` bytes, about 511 MB
- Root layer: `root.usdc`
- Rewritten root USDC size: `402962737` bytes, about 384 MB
- Copied non-root entries: 92
- Final measured WASM heap after conversion: `408485888` bytes, about 390 MB
- Max process RSS: `1573400 KB`, about 1.57 GB
- Result: succeeds on wasm32 and stays below the 1 GB heap target
- Archive integrity: `unzip -t /tmp/large_sample.wasm32_lowheap_direct.usdz`
  passed

Observed wasm32 flattened result before passthrough was added:

- Result: failed during load/export
- Error included a failed heap growth request around `2357128952` bytes
- wasm32 limit: `2147483648` bytes
- Max process RSS near failure: about `1.74 GB`

Observed wasm32 low-heap flattened result before direct-to-JS USDC export:

- Output: `/tmp/large_sample.wasm32_lowheap_flat.usdz`
- Output size: `535500713` bytes, about 511 MB
- Final measured WASM heap after conversion: `1308622848` bytes, about 1.22 GB
- Max process RSS: `2428952 KB`, about 2.43 GB
- Result: succeeded, but missed the 1 GB heap target because the generated root
  USDC was still retained in a WASM-side export vector

Observed wasm64 flattened result:

```console
cmake --build web/build_64_ninja --target tinyusdz_64.js
```

Then force the shared converter through the flattened path by importing
`web/js/src/tinyusdz/tinyusdz_64.js` and passing:

```js
{
  rootPath: 'large_sample.usdz',
  passthroughUsdz: false,
  reencode: false,
  textureFormat: 'keep',
  rootLayerFormat: 'usdc',
  arkitCompatible: false,
  flatten: true,
  maxUsdcMb: 1024,
  maxMemMb: 4096
}
```

- Output: `/tmp/large_sample.wasm64_flat.usdz`
- Output size: `513131407` bytes, about 490 MB
- Convert time: `23843 ms`
- Max process RSS: `2765440 KB`, about 2.77 GB
- Largest logged WASM heap growth: `2373320704` bytes, about 2.37 GB
- Result: succeeds under wasm64 and stays below 4 GB process RSS for this asset
- Archive integrity: `unzip -t /tmp/large_sample.wasm64_flat.usdz` passed

A native `tusdcat /tmp/large_sample.wasm64_flat.usdz > /dev/null` parse/print
attempt was stopped after roughly 90 seconds with no stderr. Treat that as
inconclusive, not as a parse validation pass.

## Why Flattening Peaks High

The flattened path currently pays for several large live data sets at once:

- the original USDZ archive bytes in JS memory
- subarray views for unpacked archive entries
- registered asset bytes copied into the WASM asset cache through `setAsset`
- the parsed root layer and composed scene data
- flattened/export-side crate data and value tables
- the generated USDC root layer bytes
- the generated USDZ archive bytes copied back out to JS

The recent `subarray` change avoids copying every USDZ member during JS unpack,
and the no-transform passthrough avoids the whole flattened pipeline. It does
not reduce the memory needed by a true flattened rewrite where a new root layer
must be generated.

## Practical Guidance

Use wasm32 for normal files, no-transform USDZ passthrough, and single-USDZ
flattened root rewrites where textures/audio/assets are unchanged. Use wasm64
for large flattened rewrites that need texture processing, ARKit rewrites,
multi-root packaging, or other paths that still route large payloads through the
WASM asset cache/export buffers.

For CLI users:

- Use `--no-reencode` for unchanged USDZ packaging. This enables the passthrough
  path when no other transform is requested.
- Use `--no-reencode` with default flattening for the low-heap root-rewrite
  path. This creates a new flattened USDZ while copying unchanged non-root
  entries from the original archive.
- Use the wasm64 build for large flattened exports. The build target is
  `tinyusdz_64.js`; the web build uses `-sMEMORY64 -sMAXIMUM_MEMORY=8GB`.
- Keep `--max-usdc-mb` and `--max-mem-mb` explicit for large test scenes so the
  writer caps are not the first failure mode.

## Possible Memory Reduction Enhancements

### Stream USDZ Output

Current export returns a complete USDZ byte array to JS. A streaming writer would
write the output archive incrementally to a sink. For Node this could be a file
stream; for browsers this could be a `WritableStream`.

Expected benefit: removes the final full-output JS copy, and can remove or
shorten the lifetime of the full output buffer in WASM.

### Stream USDC Root Into USDZ

The low-heap root-rewrite path materializes the generated USDC root layer in a
JavaScript buffer before it is inserted into the USDZ archive. A ZIP writer that
accepts a streaming first entry could write `root.usdc` as the crate writer
produces it.

Expected benefit: avoids holding both generated `root.usdc` and generated
`.usdz` bytes at the same time.

Caveat: USDZ requires STORE entries and 64-byte alignment. The writer must know
or patch sizes/CRC in the central directory. ZIP data descriptors or a two-pass
size computation may be needed.

### Avoid Asset Cache Copies

`setAsset(name, bytes)` moves JS `Uint8Array`/`Buffer` data into the WASM-side
asset resolver cache. For large USDZ inputs this duplicates texture bytes that
already exist in the original archive buffer.

Possible approaches:

- let the exporter read passthrough asset bytes from JS by callback at package
  time
- store asset references as `(archive_buffer_id, offset, size)` instead of
  copying bytes into WASM
- add a zero-copy or borrowed-asset API with clear lifetime rules

Expected benefit: reduces peak memory for texture-heavy USDZ rewrites.

### Separate Texture Repack From Root Flatten

If textures are unchanged, the converter can preserve original asset entries and
only rewrite the root layer. This still creates a new USDZ, but the non-root
entries can be copied from original archive offsets without entering WASM.

Expected benefit: keeps image payloads out of the WASM heap for flattened root
rewrites with unchanged textures.

### Earlier Lifetime Release

After each phase, release data that is no longer needed:

- clear expanded USDZ maps after assets have been registered or referenced
- release raw input archive bytes after root load if no output-copy path needs
  original entries
- delete or clear resolver asset cache after output packaging
- copy output to JS only after deleting large loader/export temporaries where
  API shape permits

Expected benefit: may not reduce absolute writer requirements, but should lower
RSS and make failure modes less sensitive to allocator growth history.

### Reduce Stage/Layer Duplication During Flatten

The full path loads a layer, composes, converts/reconstructs stage data, then
writes crate data. There may be duplicated representations of large arrays and
timesamples during this pipeline.

Possible approaches:

- keep flattened export closer to `LayerSpec`/crate data when no typed stage
  rewrite is required
- avoid reconstructing large repeated timesample arrays
- preserve read-side shared offsets where possible and write repeated `ValueRep`
  entries directly

Expected benefit: reduces the root-layer side of peak memory, not just texture
payload duplication.

### Incremental Crate Value Writing

The dedup fix now reconstructs one timesample at a time and deduplicates per
attribute. Further reductions may be possible by avoiding temporary canonical
byte buffers for very large arrays when the array already has stable binary
storage.

Possible approaches:

- hash directly from existing binary sample refs
- use borrowed spans for dedup keys where lifetime is bounded by the attribute
  write
- cap dedup key retention for very large unique arrays

Expected benefit: smaller per-attribute transient memory for animated dense
arrays.

## Open Questions

- Should the Node CLI expose an explicit `--wasm64` option for `usdzconvert.js`,
  matching `load-test-node.js`?
- Should the browser converter automatically retry large flattened exports with
  wasm64 when supported?
- Should no-transform passthrough be the default only with `--no-reencode`, or
  also when texture processing options are omitted?
- What validation should be used for very large outputs when pretty-printing the
  entire layer is too slow for routine checks?
