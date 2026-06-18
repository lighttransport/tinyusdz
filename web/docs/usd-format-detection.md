# USD format detection (WASM)

The WASM module exposes magic-number based helpers to classify USD container
bytes **without** relying on the file extension. These are free functions on the
`Module` object (registered in `web/binding.cc`, `EMSCRIPTEN_BINDINGS(tinyusdz_module)`).

They are backed by `tinyusdz::IsUSD()` in `src/tinyusdz.cc`, so they stay in sync
with what the loader actually accepts.

## Why this exists

The top-level loader (`loadFromBinary` → `tinyusdz::LoadUSDFromMemory`) already
auto-detects a bare `.usd` file by magic bytes (zstd / `PXR-USDC` / `#usda` / ZIP),
so the extension is only used as the asset-resolution base dir.

The gap is **USDZ entry-layer selection**: `LoadUSDZFromMemory` picks the archive's
default layer by *extension* — only members named `.usdc` or `.usda` are recognized.
A USDZ whose root layer is named `.usd` (e.g. Unreal exports) fails with
*"Neither USDC nor USDA file found in USDZ."* Sniff the root layer's content with
these helpers and rename the archive entry to the correct `.usdc`/`.usda` before
packaging.

## Full detection (whole buffer)

```js
Module.detectUSDFormat(data)  // Uint8Array -> "usda" | "usdc" | "usdz" | ""
Module.isUSD(data)            // Uint8Array -> boolean
```

- Copies the entire buffer onto the wasm heap (bounded to 1 GiB).
- USDZ is validated with the full `ParseUSDZHeader` (reads the ZIP central
  directory at the end of the archive), so a `"usdz"` result means the archive
  is structurally a valid USDZ.

## Streaming / header-only detection (minimal bytes)

```js
Module.detectUSDFormatHeader(data)  // Uint8Array -> "usda" | "usdc" | "usdz" | ""
Module.isUSDHeader(data)            // Uint8Array -> boolean
Module.usdHeaderSniffBytes()        // -> 88  (max bytes inspected)
```

- Copies **at most 88 bytes** from the front of the input (via a `Uint8Array`
  subarray view) — never the whole buffer. Ideal for arbitrarily large files or
  the head of a stream / HTTP Range request.
- Minimal prefixes: USDA = 9 (`#usda 1.0`), USDC = 88 (`PXR-USDC` + crate
  bootstrap header), USDZ = 4 (ZIP local-file-header magic `PK\x03\x04`).
  Supplying fewer bytes is fine — a short buffer just limits which formats can match.
- **USDZ caveat:** matched by the ZIP magic only. A real USDZ always begins with
  this signature, but — unlike `detectUSDFormat()` — this does *not* validate that
  the ZIP contains a USD entry layer (that would require reading the end of the
  archive). Use the header variant for fast classification / root-layer extension
  sniffing; use `detectUSDFormat()` when you have the full bytes and want
  archive-level validation.

### Streaming example (HTTP Range)

```js
const head = await fetch(url, { headers: { Range: "bytes=0-87" } })
  .then((r) => r.arrayBuffer());
const fmt = Module.detectUSDFormatHeader(new Uint8Array(head)); // "usdc" | "usda" | ...
```

### Root-layer extension sniff (USDZ repack)

```js
const fmt = Module.detectUSDFormatHeader(rootBytes); // typically "usdc" or "usda"
const ext = fmt === "usdc" || fmt === "usda" ? `.${fmt}` : null;
// rename the archive entry from `Scene.usd` -> `Scene${ext}` so tinyusdz finds it
```

## Notes

- None of these helpers decompress zstd-wrapped layers. The full loader does, but
  the standalone detectors report `""` for a zstd-compressed root (matches a plain
  magic-byte check; not a regression).
- TypeScript declarations are intentionally not shipped; call via the `Module`
  object directly.
