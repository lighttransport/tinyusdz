# tusdzconvert

Convert USD (`.usda` / `.usdc` / `.usdz`) into an ARKit-friendly USDZ package.

Capabilities:

- **Read USD, pack to USDZ** — loads any USD format and writes a spec-compliant
  USDZ (AOUSD Core Spec §17: uncompressed zip, 64-byte aligned, root layer first),
  then validates the result with `ValidateUSDZ`.
- **Composition flatten** — resolves sublayers / references / payloads / variants
  (LIVRPS) into a single flattened stage before writing.
- **Texture resize** — caps each texture's longest edge.
- **Texture re-encode** — re-encodes PNG textures with [fpnge](https://github.com/veluca93/fpnge)
  (fast SIMD PNG encoder) when the library is built with `-DTINYUSDZ_WITH_FPNGE=ON`,
  otherwise falls back to the portable `fpng` encoder.
- **Texture repack** — standalone channel merging (e.g. `R=gloss, G=roughness`)
  via a generic channel-map spec.

## Build

```bash
cmake -B build -DTINYUSDZ_BUILD_TOOLS=ON -DTINYUSDZ_WITH_TYDRA=ON \
      -DTINYUSDZ_WITH_FPNGE=ON -DTINYUSDZ_FPNGE_SIMD=avx2
cmake --build build -j16
# binary: build/tools/tusdzconvert/tusdzconvert
```

`TINYUSDZ_FPNGE_SIMD` selects the fpnge code path at compile time:
`avx2` (default), `sse41`, `sse2`, or `scalar`. fpnge upstream requires SSE4.1
minimum, so `sse2` and `scalar` do not compile fpnge and PNG encoding falls back
to `fpng`.

## Usage

```
tusdzconvert inputFile [outputFile] [options]
tusdzconvert -repack <outputImage> -packR <src> [-packG <src> ...] [options]
```

### Conversion options

| Flag | Meaning |
|------|---------|
| `-v`, `-verbose` | Verbose logging |
| `-noFlatten` | Do not compose/flatten (default: flatten) |
| `-arkitCompatible` | Apply ARKit-friendly stage metadata (Y-up, etc) |
| `-metersPerUnit <v>` | Override stage `metersPerUnit` |
| `-upAxis <X\|Y\|Z>` | Override stage up axis |
| `-url <s>` / `-copyright <s>` | Store in stage documentation |
| `-resizeTextures <N>` | Cap each texture's longest edge to N |
| `-textureFormat <keep\|png\|jpeg>` | Output texture format (default: keep) |
| `-pngEncoder <fpnge\|fpng>` | PNG encoder backend |
| `-jpegQuality <1-100>` | JPEG quality (default 90) |
| `-noReencode` | Copy unmodified textures through byte-for-byte |
| `-targetTextureSize <size>` | Shrink all textures so their **total** fits `<size>` (e.g. `100MB`, `50mb`, `1048576`) |
| `-fitStrategy <size\|quality>` | Lever to meet the budget: reduce dimensions (`size`) or transcode to JPEG + lower quality (`quality`) |
| `-fitMinTextureSize <N>` | Smallest longest-edge allowed by the size search (default 64) |
| `-fitMinQuality <1-100>` | Lowest JPEG quality allowed by the quality search (default 30) |

### Fit textures to a size budget

`-targetTextureSize` searches a single lever to make the **sum of all texture
bytes** fit the given budget:

```bash
# Make all textures fit ~100 MB by reducing their dimensions (keeps PNG)
tusdzconvert model.usd model.usdz -targetTextureSize 100MB -fitStrategy size

# Fit ~25 MB by transcoding to JPEG and lowering quality (references rewritten)
tusdzconvert model.usd model.usdz -targetTextureSize 25MB -fitStrategy quality

# Combine: cap dimensions at 2048 first, then search JPEG quality to fit
tusdzconvert model.usd model.usdz -targetTextureSize 25MB -fitStrategy quality -resizeTextures 2048
```

If the chosen lever cannot reach the budget at its floor (`-fitMinTextureSize` /
`-fitMinQuality`), the floor is used (best effort) and a warning is printed.

### Repack mode

`-repack <outputImage>` writes a single image whose channels are sourced from
other images. Each `-packR/-packG/-packB/-packA <src>` takes either
`file.png:CH` (CH = channel index 0..3, default 0) or `const:VALUE` (0..255).

```bash
# glTF-style ORM: R=occlusion, G=roughness, B=metallic
tusdzconvert -repack orm.png \
  -packR ao.png:0 -packG rough.png:0 -packB metal.png:0 -packChannels 3

# pack a gloss map into R and a roughness map into G
tusdzconvert -repack packed.png -packR gloss.png:0 -packG rough.png:0 -packChannels 2
```

## Examples

```bash
# Flatten + ARKit + downscale textures to 1024 px
tusdzconvert model.usd model.usdz -arkitCompatible -resizeTextures 1024 -v

# Repack a USDZ, transcoding every texture to fpnge-encoded PNG
tusdzconvert in.usdz out.usdz -textureFormat png -pngEncoder fpnge
```

## Notes / limitations

- Automatic re-wiring of a material's shader graph to consume a repacked texture
  (e.g. detecting separate metallic/roughness textures in an existing material and
  merging them) is **not** performed in this version — repack is exposed as a
  standalone texture operation. Resize / re-encode / path-normalization in the
  conversion pipeline preserve the existing shader graph.
- EXR/16-bit textures are passed through unchanged (no resize/transcode), and
  are counted as fixed overhead against a `-targetTextureSize` budget.
- `-targetTextureSize` budgets the **sum of texture bytes**; the final `.usdz`
  is slightly larger (USDC layer + zip overhead).
