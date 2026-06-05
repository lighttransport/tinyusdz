# `tusdzconvert` — native USD → USDZ converter

`tusdzconvert` packages a USD scene (a `.usd`/`.usda`/`.usdc` layer plus its
textures, or an existing `.usdz`) into a `.usdz` archive, with optional texture
resize / re-encode / format conversion, scene flattening, and an ARKit
compatibility policy. It also has a standalone channel-repack mode for building
ORM-style textures.

It is the native counterpart of the WebAssembly converter
([`web/docs/usdzconvert.md`](../web/docs/usdzconvert.md)). The native build uses
the **fpnge** PNG encoder (x86 SIMD); the WASM build falls back to portable
**fpng**.

## Build

`tusdzconvert` builds with the rest of the tools:

```bash
# from the repo root, with the Ninja build dir (build/)
ninja -C build tusdzconvert
# binary: build/tools/tusdzconvert/tusdzconvert
```

## Usage

```text
tusdzconvert <inputFile> [outputFile] [options]
tusdzconvert -repack <outputImage> -packR <src> [-packG <src> -packB <src> -packA <src>] [options]
```

`inputFile` may be a `.usd`/`.usda`/`.usdc` layer (its sibling textures are
resolved relative to it) or a self-contained `.usdz`. If `outputFile` is
omitted the output is `<input>.usdz`.

## Conversion options

| Option | Description |
|--------|-------------|
| `-h`, `-help` | Show help. |
| `-v`, `-verbose` | Verbose logging. |
| `-noFlatten` | Do **not** compose/flatten before writing (default: flatten). `-noFlatten` writes a USDA root to preserve composition arcs. |
| `--outputFormat <usdz\|usdc\|usda>` | Output format (default `usdz`). `usdc`/`usda` produce flat files; textures stay as external references. |
| `--rootLayerFormat <usdc\|usda>` | USDZ root layer format (default `usdc`). |
| `-arkitCompatible` | Apply the ARKit USDZ policy: flatten, USDC root, Y-up metadata, and stricter texture checks. |
| `-metersPerUnit <value>` | Override stage `metersPerUnit`. |
| `-upAxis <X\|Y\|Z>` | Override stage up axis. |
| `-url <string>` | Store a URL in the stage `documentation`. |
| `-copyright <string>` | Store a copyright string in the stage `documentation`. |
| `-copytextures` | Accepted for compatibility (textures are always packed). |
| `--pxr-usdcat <path>` | Also run pxrUSD `usdcat --flatten` to produce a reference file (for diffing). |

## Texture options

| Option | Description |
|--------|-------------|
| `-resizeTextures <N>` | Cap each texture's longest edge to N pixels. |
| `-textureFormat <keep\|png\|jpeg>` | Output texture format (default `keep`). `keep` preserves the source format (incl. EXR/HDR). |
| `-pngEncoder <fpnge\|fpng>` | PNG encoder backend (default `fpnge` when available). |
| `-jpegQuality <1-100>` | JPEG quality when (re-)encoding (default `90`). |
| `-noReencode` | Copy unmodified textures through byte-for-byte. |

### Fit textures to a total size budget

| Option | Description |
|--------|-------------|
| `-targetTextureSize <size>` | Shrink all textures so their **total** fits `<size>` (e.g. `100MB`, `50mb`, `1048576`). Runs a fit search. |
| `-fitStrategy <size\|quality>` | Lever to meet the budget: reduce dimensions (`size`) or transcode to JPEG + lower quality (`quality`). |
| `-fitMinTextureSize <N>` | Smallest longest-edge allowed by the size search (default `64`). |
| `-fitMinQuality <1-100>` | Lowest JPEG quality allowed by the quality search (default `30`). |

## Repack mode

Merge image channels into a single texture (e.g. pack ambient-occlusion,
roughness, metallic into one ORM map):

| Option | Description |
|--------|-------------|
| `-repack <outputImage>` | Enable repack mode; write the packed image here. |
| `-packR`/`-packG`/`-packB`/`-packA <src>` | Channel source: `file.png:CH` (CH = 0..3, default 0) or `const:VALUE` (VALUE = 0..255). |
| `-packChannels <1-4>` | Number of output channels (default: inferred from the `-pack*` flags). |
| `-packSize <W>x<H>` | Output size (default: max of the referenced inputs). |

## Examples

```bash
# Convert a scene to an ARKit-compatible USDZ, capping textures at 1024 px
tusdzconvert model.usd model.usdz -arkitCompatible -resizeTextures 1024 -v

# Repack a .usdz, re-encoding PNGs with fpnge
tusdzconvert in.usdz out.usdz -textureFormat png -pngEncoder fpnge

# Pass textures through unchanged (fast, byte-identical)
tusdzconvert in.usdz out.usdz -noReencode

# Flatten to a single USDA (textures stay as external refs)
tusdzconvert scene.usda out.usda --outputFormat usda

# Build an ORM map: R = AO, G = roughness, B = metallic
tusdzconvert -repack orm.png -packR ao.png:0 -packG rough.png:0 -packB metal.png:0 -packChannels 3
```

## Notes

- **Flatten** (default) composes subLayers, references, payload, inherits, and
  variantSets into a single root layer before writing. Use `-noFlatten` to keep
  composition arcs (written as a USDA root).
- **`-arkitCompatible`** forces a flattened USDC root with Y-up metadata — the
  layout Apple's Quick Look expects.
- A few scenes **inflate** on flatten (instanced/composed content is expanded);
  this is expected, not an error.
- The native writer's timesample/array dedup keeps flattened animation from
  blowing up on disk (per-attribute value dedup).
