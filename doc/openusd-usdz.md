# OpenUSD USDZ Creation & `usdchecker --arkit` Validation

Reference notes on **how the upstream OpenUSD (pxr) reference implementation creates USDZ
packages** and **what `usdchecker --arkit` validates**, gathered by reading the OpenUSD source
tree (local checkout at `/mnt/nvme02/work/tinyusdz-repo/OpenUSD`, paths below are relative to that
root). Written to inform tinyusdz's own USDZ creation / conversion pipeline (MJCF/URDF → USD,
UE-export large-scene conversion).

Two questions motivated this:

1. **Does OpenUSD convert / resize / repack textures when building a USDZ?** → **No.** See
   [Texture handling](#3-texture-handling--no-convertresizerepack).
2. **What does `usdchecker --arkit` require?** → See
   [usdchecker validation](#4-usdchecker--arkit-validation).

---

## 1. USDZ creation paths

### `usdzip` CLI — `pxr/bin/usdzip/usdzip.cpp`

| Flag | Meaning |
| --- | --- |
| `-r`, `--recurse` | Recursively add files from sub-directories |
| `-a`, `--asset` | Resolvable asset path to the root layer; triggers dependency-aware packaging via `UsdUtilsCreateNewUsdzPackage` |
| `--arkitAsset` | Like `--asset` but uses the ARKit packaging path (`UsdUtilsCreateNewARKitUsdzPackage`) |
| `-c`, `--checkCompliance` | Run compliance checking before packaging |
| `-l`, `--list` | List contents of an existing `.usdz` (`-` ⇒ stdout) |
| `-d`, `--dump` | Dump per-file metadata (offset, compressed/uncompressed size) |
| `-v`, `--verbose` | Verbose output |

Two operating modes:

- **Direct file inclusion** — files added verbatim through
  `SdfZipFileWriter::CreateNew()` / `AddFile()`. No transformation.
- **Asset-based packaging** — `UsdUtilsCreateNewUsdzPackage()` (or the ARKit variant) resolves and
  localizes dependencies, then writes the zip.

### Packaging API — `pxr/usd/usdUtils/usdzPackage.cpp`

**`UsdUtilsCreateNewUsdzPackage()`**

```cpp
bool UsdUtilsCreateNewUsdzPackage(
    const SdfAssetPath& assetPath,
    const std::string& usdzFilePath,
    const std::string& firstLayerName = std::string(),
    bool editLayersInPlace = false);
```

- Collects the asset and **all external dependencies** (recursive traversal).
- **Localizes** asset paths: remaps references to package-relative paths so the archive is
  self-contained. Anonymous layers discovered during traversal are serialized into the package.
- USD *layers* are rewritten (path remapping); **non-USD assets such as textures are not touched**.

**`UsdUtilsCreateNewARKitUsdzPackage()`** — stricter, for ARKit/RealityKit:

- Forces the root layer to **`.usdc`** (binary crate), required by ARKit.
- **Flattens** the USD layer when external composition arcs (references/sublayers/payloads) exist.
  It explicitly warns this loses features:
  *"This will result in loss of features such as variantSets and all asset references to be
  absolutized."*
- Disables UDIM path resolution: `params.SetResolveUdimPaths(false);`
  (`usdzPackage.cpp:126`).

Supporting helpers: `UsdUtilsComputeAllDependencies()` (buckets deps into layers vs. assets),
`UsdUtilsExtractExternalReferences()`, and the localization machinery in
`pxr/usd/usdUtils/assetLocalization*.cpp` / `assetLocalizationPackage.cpp`.

---

## 2. Zip container rules — `pxr/usd/sdf/zipFile.cpp`

USDZ is a plain ZIP with two hard constraints from the USDZ spec, both enforced by
`SdfZipFileWriter`:

- **No compression.** Every entry is stored, not deflated:
  `h.f.compressionMethod = 0; // No compression` (`zipFile.cpp:1044`). `compressedSize` equals
  `uncompressedSize`.
- **64-byte alignment.** Each file's data must start on a 64-byte boundary. Padding is inserted in
  the ZIP local-header "extra" field:

  ```cpp
  // Per usdz specifications, file data must be aligned to 64 byte boundaries.
  constexpr size_t _DataAlignment = 64;                       // zipFile.cpp:442
  uint16_t _ComputeExtraFieldPaddingSize(size_t offset) {
      uint16_t requiredPadding = _DataAlignment - (offset % _DataAlignment);
      if (requiredPadding == _DataAlignment) requiredPadding = 0;
      else if (requiredPadding < _HeaderSize)  // too small for the 4-byte extra header
          requiredPadding += _DataAlignment;   // bump up while keeping alignment
      return requiredPadding;
  }
  ```

Source files are memory-mapped read-only (`ArchMapFileReadOnly`) and their bytes copied straight
into the archive; CRC32 is computed over the original bytes.

---

## 3. Texture handling — NO convert/resize/repack

**OpenUSD does not convert, resize, downsample, recompress, change the colorspace of, or otherwise
transform texture/image assets when building a USDZ.** Image files are copied **byte-for-byte**.

Evidence:

- The packaging path (`assetLocalizationPackage.cpp` → `SdfZipFileWriter::AddFile`) memory-maps the
  source file (`ArchMapFileReadOnly`) and writes the mapping's raw bytes; no image decode/encode is
  involved.
- There is **no image-processing code** anywhere in `pxr/usd/usdUtils/` — no resize, no format
  conversion, no colorspace handling, no UDIM tiling. The ARKit path even *disables* UDIM resolution
  (`SetResolveUdimPaths(false)`).
- Only USD **layers** are ever modified (path localization, and ARKit flattening).

**Implication for tinyusdz:** if we want texture conversion (e.g. TGA/BMP → PNG), resizing/mipmap
budget control, or atlas repacking, we must implement it ourselves — there is no upstream behavior
to match or rely on. The reference tool's contract is "copy what you reference." This is consistent
with our existing decision to embed texture bytes for USDZ (see `[[mjcf-texture-asset-paths]]`).

---

## 4. `usdchecker --arkit` validation

### CLI — `pxr/usdValidation/bin/usdchecker/usdchecker.cpp`

| Flag | Meaning |
| --- | --- |
| `-s`, `--skipVariants` | Only check the default variant selections |
| `-p`, `--rootPackageOnly` | Check only the root package (skip nested packages) |
| `-o`, `--out FILE` | Output file (default stdout; `stderr` suppresses coloring) |
| `--noAssetChecks` | Skip asset-level checks (stage metadata, defaultPrim) |
| `--arkit` | Enable ARKit/RealityKit USDZ compatibility rules |
| `-d`, `--dumpRules` | Print the enabled rule list |
| `-t`, `--strict` | Treat warnings as failures (non-zero exit) |
| `-v`, `--verbose` | Verbose |
| `--useNewValidationFramework` | Use C++ `UsdValidationRegistry` validators instead of the Python checker; `--variantSets`, `--variants`, `--disableVariantValidationLimit` apply to this path |

Rules live in `pxr/usd/usdUtils/complianceChecker.py` (each rule is a `BaseRuleChecker` subclass).
`GetBaseRules()` always run; `GetARKitRules()` add when `--arkit` is passed.

### Base rules (always)

| Checker | Rule |
| --- | --- |
| `ByteAlignmentChecker` | Files in the usdz are 64-byte aligned (`offset % 64 == 0`) |
| `CompressionChecker` | Files are stored uncompressed (`compressionMethod == 0`) |
| `MissingReferenceChecker` | No unresolvable asset dependencies in any checked variant |
| `StageMetadataChecker` | Stage declares `upAxis` and `metersPerUnit`; asset-level layers need a `defaultPrim`; consumer-level must be **Y-up** |
| `TextureChecker` | For a USDZ (or consumer-level checks) texture extensions must be in `exr, jpg, jpeg, png`. Decodable-but-non-portable formats `bmp, tga, hdr, tif, tx, zfile` get a specific "non-portable" error |
| `PrimEncapsulationChecker` | Boundables not nested under Gprims; Connectable prims only under container-like Connectables |
| `NormalMapTextureChecker` | 8-bit normal maps need `scale = (2,2,2,1)`, `bias = (-1,-1,-1,0)`, `sourceColorSpace = "raw"` |
| `MaterialBindingAPIAppliedChecker` | Prims with material bindings have `MaterialBindingAPI` applied |
| `SkelBindingAPIAppliedChecker` | Prims with UsdSkel binding properties have `SkelBindingAPI` and live under a `SkelRoot` |
| `ShaderPropertyTypeConformanceChecker` | Shader input types match the SDR shader definition |

### ARKit-only rules (`--arkit`)

| Checker | Rule |
| --- | --- |
| `ARKitLayerChecker` | Layer files restricted to `usd, usda, usdc, usdz` |
| `ARKitPrimTypeChecker` | Prim type must be in a whitelist (below) or start with `RealityKit`; custom schemas forbidden |
| `ARKitShaderChecker` | Shaders use `id` implementation source; ids in `UsdPreviewSurface, UsdUVTexture, UsdTransform2d, UsdPrimvarReader*, ND_*`; one connection per input |
| `ARKitMaterialBindingChecker` | Material bindings (direct or collection-based) resolve to valid targets |
| `ARKitFileExtensionChecker` | Package may only contain layers (`usd/usda/usdc/usdz`) + textures (`exr/jpg/jpeg/png`) |
| `ARKitPackageEncapsulationChecker` | If the root is a package it must be self-contained — no references outside the package |

**ARKit allowed prim types** (`_allowedPrimTypeNames`, `complianceChecker.py:792`):
`'' (untyped), Scope, Xform, Camera, Shader, Material, Mesh, Sphere, Cube, Cylinder, Cone, Capsule,
GeomSubset, Points, SkelRoot, Skeleton, SkelAnimation, BlendShape, SpatialAudio, PhysicsScene,
Preliminary_ReferenceImage, Preliminary_Text, Preliminary_Trigger` — plus any `RealityKit*` type.

**Allowed texture formats** (`_basicUSDZImageFormats`, `complianceChecker.py:232`):
`("exr", "jpg", "jpeg", "png")`. Note the doc-string says "only .jpg/.jpeg/.png for consumer-level"
but the enforced list also permits `exr`.

### No file-count / asset-count limit

`usdchecker --arkit` imposes **no limit on the number of texture/asset files** in a package. All
checks are per-file and type/format-based (`ARKitFileExtensionChecker` validates each file's
extension; `TextureChecker` validates each texture's format) — nothing counts files or enforces a
maximum. The only numeric cap in the checker is an unrelated **1000-variant** validation limit in
the new C++ framework (`--disableVariantValidationLimit`).

Caveat: Apple's AR Quick Look *runtime* has its own practical memory/size constraints (e.g. texture
resolution and total package size) that are **not** part of the open-source validator. If the
concern is on-device ARKit behavior rather than passing `usdchecker`, that is a separate,
runtime-side limit.

---

## 5. Implications for tinyusdz

To produce output that passes `usdchecker --arkit`, our converter should:

- **Root layer = `.usdc`** crate; flatten composition (we already absolutize/flatten in conversions).
- Write **`upAxis = "Y"`** and **`metersPerUnit`** stage metadata; set a `defaultPrim`.
- ZIP writer must store **uncompressed** entries with **64-byte alignment** (matches our crate/usdz
  writer requirements).
- Restrict embedded textures to **png/jpeg** (exr is allowed but uncommon for our PBR output); we
  must **convert** non-portable inputs (tga/bmp/etc.) **ourselves** — OpenUSD will not.
- Materials: **`UsdPreviewSurface` + `UsdUVTexture`** (id-based), single connection per input;
  normal maps need scale `(2,2,2,1)` / bias `(-1,-1,-1,0)` / `sourceColorSpace = raw`.
- Apply **`MaterialBindingAPI`** on bound prims; keep prim types within the ARKit whitelist.
- Keep the package **self-contained** (no external references).

See also `[[mjcf-texture-asset-paths]]` for our texture-embedding decision.

---

## 6. Source reference

| Component | Path (relative to OpenUSD root) |
| --- | --- |
| `usdzip` CLI | `pxr/bin/usdzip/usdzip.cpp` |
| Packaging API | `pxr/usd/usdUtils/usdzPackage.cpp` (`.h`) |
| Asset localization | `pxr/usd/usdUtils/assetLocalization*.cpp`, `assetLocalizationPackage.cpp` |
| Dependency computation | `pxr/usd/usdUtils/dependencies.cpp` |
| ZIP reader/writer | `pxr/usd/sdf/zipFile.cpp` (`.h`) |
| Compliance / ARKit rules | `pxr/usd/usdUtils/complianceChecker.py` |
| `usdchecker` CLI | `pxr/usdValidation/bin/usdchecker/usdchecker.cpp` |
