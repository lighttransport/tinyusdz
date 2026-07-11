# GPU Texture Compression (KTX2 / BC / ASTC / ETC2)

tinyusdz can load, decode, and produce GPU block-compressed textures to cut VRAM
and bandwidth in the viewers (tusdview / tusdrender) and on the web — while
staying **DCC-transparent** and **legacy-compatible**: a USD/USDZ asset that uses
compression still opens in stock USD tools and Apple Quick Look.

The compression codecs come from the vendored, pure-C11 **tinyexr texture tool
stack** at `src/external/textools/` (built as `tinyusdz_textools` when
`TINYUSDZ_WITH_TEXTOOLS=ON`, the default):

- **texcomp** — block encoders (BC1/3/5/6H/7, ETC2/EAC, ASTC LDR+HDR), the `uni`
  transcodable intermediate, and decoders (uni / BC7 / ASTC LDR).
- **texpipe** — mip-chain + DDS/KTX2 container writer **and reader**
  (`tp_ktx2_read` / `tp_ktx2_decode_level_rgba8` / `tp_ktx2_write_uni`).
- **tir** — content-aware image resize. **envmap** — IBL/prefiltered cubemaps.

See `src/external/textools/README.tinyusdz.md` for the vendor provenance and the
re-sync procedure (fix upstream in the tinyexr repo, then re-copy — keep the
vendored tree pristine).

## Background: why transcoding

No single GPU compressed format is available everywhere:

| Platform / API | BC1–7 (S3TC/RGTC/BPTC) | ETC2/EAC | ASTC |
|---|---|---|---|
| Desktop (Win/Linux, NVIDIA/AMD/Intel) | ✅ | rarely HW | generally none |
| macOS (pre-Apple9) | ✅ | ❌ | ❌ |
| iOS / Apple Silicon Apple9+ | ✅ (Apple9+) | ✅ | ✅ |
| Android (GLES3) | device-dependent | ✅ guaranteed | common |
| WebGL2 / WebGPU | s3tc/bptc common on desktop | common on mobile | common on mobile |

Only **BC6H** and **ASTC-HDR** carry HDR; everything else is LDR. Because formats
don't overlap, the portable strategy is: ship one *transcodable* asset and
convert it to the device's native format at load. tinyusdz uses its own
**Basis-free `uni`** intermediate (a UASTC-4x4 subset that is itself valid ASTC
4x4 and transcodes cheaply to BC7 / ASTC / ETC2, or decodes to RGBA8).

## USD authoring: the legacy-safe KTX2 hint

USD has **no standard** for compressed textures, and **USDZ forbids KTX2** (only
png/jpg/exr/avif are permitted). So tinyusdz keeps `inputs:file` pointing at a
conventional image and names the compressed companion in **attribute
`customData`**:

```usda
def Shader "diffuseTex" {
    uniform token info:id = "UsdUVTexture"
    asset inputs:file = @textures/diffuse.png@ (
        customData = { asset ktx2 = @textures/diffuse.ktx2@ }
    )
    float3 outputs:rgb
}
```

- tinyusdz-aware loaders **search the `.ktx2` first**; if it resolves, they load
  and transcode/decode it. Otherwise they fall back to `inputs:file`.
- Stock USD tools, Quick Look, and RealityKit ignore `customData` and see only
  the png — so the asset is fully valid and USDZ-legal. `usdz-convert` does not
  package `customData`-referenced assets, so a hinted stage is already
  spec-clean; the hint is inert to unaware consumers.
- Resolution happens in `RenderSceneConverter::ConvertUVTexture`
  (`src/tydra/render-data-material.cc`), gated by `TINYUSDZ_WITH_TEXTOOLS`.
  Non-UDIM textures only.

A **direct** reference (`inputs:file = @tex.ktx2@`) also works: the core image
loader decodes it, and `usdz-convert` transcodes it to png/jpg when a
legacy/ARKit target is requested (`IsAllowedTextureExt` allows `ktx2` as an
*input*; `IsAllowedARKitTextureExt` still excludes it from USDZ output).

## Core / tydra: loading a KTX2

`.ktx2` is a first-class loadable image. `tinyusdz::image::LoadImageFromMemory`
(`src/image-loader.cc`) detects the KTX2 identifier and decodes level 0 to
RGBA8 via the texpipe reader:

- Decodable formats: **uni**, **BC7**, **ASTC LDR**. Other block formats
  (BC1/3/5, ETC2/EAC) and HDR (BC6H / ASTC-HDR) are reported as an error — upload
  their blocks directly or keep them compressed instead of CPU-decoding.
- The reader validates the level index against the file size and each level's
  declared byte length against its block geometry (rejects crafted/truncated
  files).
- `supercompressionScheme = 0` (uncompressed) and `2` (Zstd) are supported —
  real-world KTX2 (esp. UASTC) is usually Zstd-supercompressed. Zstd support
  requires `TINYUSDZ_WITH_ZSTD_COMPRESSION` (on by default); the reader
  decompresses each level via the vendored ZSTD. BasisLZ (scheme 1) is not
  handled.

Because the loader decodes to a normal RGBA8 `Image`, every existing consumer
works unchanged: **tusdrender** (a software path tracer) consumes `.ktx2` for
free, and `usdz-convert` can re-encode it to png/jpg/exr for a legacy asset.

`tydra::TextureImage` (`src/tydra/render-data.hh`) also carries a
`TextureBlockFormat blockFormat` (+ `blockWidth`/`blockHeight`) for a future
keep-compressed GPU passthrough; today the loader always decodes to RGBA8.

## tusdview: compress-on-load

tusdview converts decoded textures to a GPU block format at load to save VRAM,
selectable on the CLI:

```
tusdview <scene.usd[z]> --texture-compress off|bc|bc7|astc|etc2|auto
tusdview <scene.usd[z]> --texture-mips on|off
```

- `bc` = BC1/BC3 by opacity; `bc7` = BC7; `astc` = ASTC 4x4; `etc2` = ETC2_RGBA;
  `auto` = the best format for the device.
- **Cap-gated fallback:** the requested format is remapped to one the GPU
  actually supports before encoding, so `--texture-compress astc` on a desktop
  BC-only GPU transparently falls back to BC7 (and to uncompressed if nothing is
  available). Device capabilities are queried from the renderer
  (`RendererCaps`: GL extension strings / `VkPhysicalDeviceFeatures`) and passed
  into the texture build (`ChooseCompressedFormat` in `mesh_build.cc`).
- Both the OpenGL (`glCompressedTexImage2D`, S3TC/RGTC/BPTC/ETC2/ASTC enums) and
  Vulkan (`VK_FORMAT_*_BLOCK`) backends upload the compressed blocks and their
  precomputed content-aware mip chains. The Vulkan backend enables the
  `textureCompression{BC,ETC2,ASTC_LDR}` device features at creation.
- `maxTextureSize` / `textureBudgetMB` cap decoded texture memory before
  compression.

BC6H (HDR) is reserved in the format enum but not yet wired into the RGBA8
compress-on-load path (it needs the float/EXR texture route).

### Kept-compressed KTX2 passthrough

When the texture asset is already a `.ktx2`, tusdview can upload its GPU blocks
directly instead of decoding to RGBA8 and re-encoding:

```
tusdview <scene> --texture-keep-compressed on
# runnable example (a 64x64 uni KTX2 with a full mip chain):
tusdview models/ktx2-uni-plane.usda --texture-keep-compressed on
```

- Core/tydra loads the `.ktx2` block payload without decoding
  (`RenderSceneConverterConfig::keep_compressed_textures`), tagging the tydra
  `TextureImage` with its `TextureBlockFormat`.
- tusdview adapts the blocks to the device (`TexToolsAdaptCompressed`): the `uni`
  intermediate is a byte-copy to ASTC where supported, a cheap transcode to BC7
  or ETC2 otherwise, or a decode to RGBA8 as a last resort; a stored BC7/ASTC/BCn
  payload is uploaded as-is when the device supports it. This skips the expensive
  re-encode (and its quality loss).
- The KTX2's full precomputed mip chain is carried through (each level copied or
  transcoded to the chosen format), so minification stays correct without
  re-generating mips. Disabled by default; size-cap / budget resize falls back to
  the normal decode path.

## Web demo

`web/js/texcomp.{html,js}` demonstrates the same strategy in the browser,
Basis-free (no `basis_universal`, no `KTX2Loader`):

1. A sample RGBA8 texture is compressed once to `uni` in a small WebAssembly
   module (`web/js/texcomp/texcomp_web.c`, built by `build.sh` with emscripten —
   pure C11 over the textools `texcomp` sources, not linked against tinyusdz).
2. JS detects the browser's compressed-texture support (WebGL2
   `WEBGL_compressed_texture_*` / `EXT_texture_compression_bptc`).
3. It transcodes `uni` to the best available format (BC7 / ASTC / ETC2) and
   uploads a `THREE.CompressedTexture`; where no compressed format exists it
   decodes `uni` to RGBA8 and uploads a `THREE.DataTexture`.

Build and run:

```sh
source /path/to/emsdk/emsdk_env.sh
bash web/js/texcomp/build.sh          # -> texcomp_web.{mjs,wasm} (git-ignored)
cd web/js && npm run dev:texcomp      # or any static server, open /texcomp.html
```

The page reports the detected caps, the chosen GPU format, and the VRAM saving
(e.g. a 256×256 texture: 256 KiB RGBA8 → 64 KiB BC7 = 4×).

## Building / gating

- `TINYUSDZ_WITH_TEXTOOLS` (default ON) builds `tinyusdz_textools` and enables
  the KTX2 image path in the core library and the compression paths in tusdview
  (`TUSDVIEW_WITH_TEXTOOLS`). When OFF, `.ktx2` is not decoded and tusdview falls
  back to its built-in BC1/BC3 encoder.
- textools tests: `ctest -R 'textools-' --output-on-failure` (KTX2
  write→read→decode round-trips live in `textools-texpipe`).

## Not yet supported / follow-ups

- **HDR compressed** decode-to-EXR (BC6H / ASTC-HDR); BC6H is the reliable HDR
  GPU-upload target.
- **KTX2 Zstd** supercompression on the *writer* side (reading is supported).
- **Basis Universal / `KHR_texture_basisu`** interop (ETC1S/UASTC) for glTF and
  three.js `KTX2Loader` — the "Basis later" seam; the current pipeline is
  intentionally Basis-free.
- Routing real USD **scene** textures through the web compressed path (the demo
  uses a generated texture; wiring the tinyusdz WASM to expose `uni` bytes is a
  follow-up).
