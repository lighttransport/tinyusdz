# UDIM Texture Support in TinyUSDZ

TinyUSDZ supports UDIM textures: loading tiles through the asset resolver, a sparse internal representation for editing, combining tiles into a single atlas texture for the Tydra `RenderScene` (WebGL) and USDZ conversion, and `<UDIM>` round-tripping through the USDA/USDC readers and writers.

## What is UDIM?

A UDIM asset path embeds a `<UDIM>` token (e.g. `diffuse.<UDIM>.png`) that expands to per-tile files numbered `1001..1100`. Tile `1000 + (u+1) + 10*v` covers UV region `[u, u+1] x [v, v+1]`, where `u` is in `[0, 10)` and `v >= 0` (0-based column/row). For example:

| UDIM id | column `u` | row `v` | UV region |
|---------|-----------|---------|-----------|
| 1001 | 0 | 0 | `[0,1] x [0,1]` |
| 1002 | 1 | 0 | `[1,2] x [0,1]` |
| 1011 | 0 | 1 | `[0,1] x [1,2]` |

This matches OpenUSD (`pxr/usd/usdShade/udimUtils.cpp`, `pxr/imaging/hdSt/udimTextureObject.cpp`). The `<UDIM>` token is preserved verbatim in the USD data model (`value::AssetPath`) and serialized as-is by both the USDA and USDC writers — no special encoding is needed.

## Source Files

| File | Description |
|------|-------------|
| `src/io-util.{hh,cc}` | `IsUDIMPath()`, `SplitUDIMPath()` (split into prefix/suffix around the `<UDIM>` token), `UDIMAssetTiles` helper |
| `src/tydra/render-data.hh` | `UVTexture` UDIM fields, sparse `UDIMTexture` struct, `RenderScene::udim_textures` |
| `src/tydra/render-data-converter.hh` | `MaterialConverterConfig` UDIM knobs; `UDIMTile` / `UDIMAtlas`; `ExpandUDIMTiles()` / `BuildUDIMAtlas()` declarations |
| `src/tydra/render-data.cc` | `ExpandUDIMTiles()`, `BuildUDIMAtlas()` implementations |
| `src/tydra/render-data-material.cc` | UDIM branch in `ConvertUVTexture()` (combine + keep-as-is) |
| `src/tydra/render-data-mesh.cc` | Mesh UV rebake for combined-UDIM textures (extended `ListUVNames`) |
| `src/tydra/usd-export.cc` | Writes the original `<UDIM>` pattern for sparse UDIM textures |
| `src/usdz-convert.cc` | `ExpandAndPackUDIM()` — packs per-tile files into the `.usdz` |
| `web/binding.cc` | `numUDIMTextures()`, `getUDIMTexture()`, `setCombineUDIMTiles()` JS bindings |

## Modes

A UDIM texture is handled by the Tydra `RenderSceneConverter` in one of two modes, selected by `MaterialConverterConfig::combine_udim_tiles`.

### Combine mode (default)

Tiles are loaded through the asset resolver and packed into a single grid-atlas `TextureImage`. The referenced mesh UV set is **rebaked** so each tile lands in its atlas cell, producing an ordinary single-texture material suitable for WebGL/USDZ viewers.

For tiles spanning a `cols x rows` grid (origin at the lowest present tile `(min_u, min_v)`), the atlas is `cols x rows` cells and the UV remap applied to the mesh texcoord set is:

```
uv' = uv * scale + offset
scale  = (1/cols, 1/rows)
offset = (-min_u/cols, -min_v/rows)
```

The per-tile cell size is the largest power-of-two `<= udim_max_atlas_size / max(cols, rows)`, so the atlas longest edge never exceeds `udim_max_atlas_size`. Empty cells are left transparent. The atlas image's `asset_identifier` keeps the original `<UDIM>` path.

> Note: the rebake scales the specific texcoord primvar referenced by the UDIM texture's UV reader (matched by primvar name). If a material binds both a UDIM texture and a non-UDIM texture to the *same* primvar, only UDIM workflows (where the UV set spans multiple tiles) are well-defined; the non-UDIM texture on that shared set would be affected by the scaling.

### Keep-as-is mode (opt-out)

Set `combine_udim_tiles = false`. Each resolved tile is loaded as its own `TextureImage`, and a sparse `tydra::UDIMTexture` is recorded in `RenderScene::udim_textures`, keyed by UDIM id:

```cpp
struct UDIMTexture {
  std::string asset_identifier;                 // original `<UDIM>` path
  std::unordered_map<uint32_t, int32_t> imageTileIds;  // udim id -> RenderScene::images index
  // prim_name, abs_path, display_name, fetch(...)
};
```

The `UVTexture` links to it via `udim_texture_id` (an index into `RenderScene::udim_textures`); its `texture_image_id` points at a representative tile (lowest id) for renderers that do not understand UDIM. No mesh-UV rebake is performed in this mode. This mode is intended for editing UDIM tiles in the web `RenderScene`/binding.

## Configuration

`MaterialConverterConfig` (`src/tydra/render-data-converter.hh`):

| Field | Default | Description |
|-------|---------|-------------|
| `combine_udim_tiles` | `true` | Combine tiles into an atlas (`true`) or keep them sparse (`false`). |
| `udim_max_atlas_size` | `4096` | Max longest edge (px) of the combined atlas. Per-tile cell size = largest pow2 `<= udim_max_atlas_size / max(cols, rows)`. |
| `udim_max_tiles` | `100` | Safety cap on the number of tiles to load (UDIM is 1001..1100). |

## API

```cpp
// src/tydra/render-data-converter.hh

// Discover tiles that actually resolve for a `<UDIM>` asset path (ids 1001..1100).
bool ExpandUDIMTiles(const std::string &udimAssetPath,
                     const AssetResolutionResolver &assetResolver,
                     int max_tiles, std::vector<UDIMTile> *tilesOut,
                     std::string *warn, std::string *err);

// Combine resolved tiles into a single grid-atlas image (+ UV remap).
bool BuildUDIMAtlas(const std::vector<UDIMTile> &tiles,
                    const AssetResolutionResolver &assetResolver,
                    int max_atlas_size, bool srgb, UDIMAtlas *atlasOut,
                    std::string *warn, std::string *err);
```

Tile discovery is resolver-driven (no globbing): each `prefix + id + suffix` is passed through `AssetResolutionResolver::resolve()`, so it works for on-disk, search-path, and in-USDZ assets. Atlas tiles are currently limited to 8-bit images; non-8-bit tiles are skipped with a warning.

## USDZ packaging

When converting to `.usdz`, `ExpandAndPackUDIM()` (`src/usdz-convert.cc`) expands a `<UDIM>` reference to its tiles, runs each tile through the normal texture pipeline (resize/re-encode), packs them under per-tile archive names, and rewrites the authored `inputs:file` to the archive-side `<UDIM>` pattern. ARKit's USDZ viewer supports the `<UDIM>` token.

Example:

```
$ tusdzconvert scene.usda out.usdz
Wrote: out.usdz
  textures: 3, resized: 0, reencoded: 3, passthrough: 0

$ unzip -l out.usdz
  root.usdc
  tile.1001.png
  tile.1002.png
  tile.1011.png
```

The packed `root.usdc` keeps `asset inputs:file = @tile.<UDIM>.png@`.

## Web / JavaScript bindings

`web/binding.cc` exposes UDIM to the WebAssembly loader:

| Method | Description |
|--------|-------------|
| `setCombineUDIMTiles(bool)` / `getCombineUDIMTiles()` | Toggle combine vs keep-as-is before conversion. |
| `numUDIMTextures()` | Number of sparse `UDIMTexture`s in the scene. |
| `getUDIMTexture(id)` | `{ primName, absPath, displayName, assetIdentifier, tiles: [{ udim, u, v, imageId }] }`. Fetch each tile image with `getImage(imageId)`. |
| `getTexture(id)` | For UDIM textures also reports `isUDIM`, `udimTextureId`, and the `udimUvScale*` / `udimUvOffset*` atlas remap. |

## Verification

A unit test, `tydra_udim_texture_test` (`tests/unit/unit-tydra.cc`), writes tiles `1001`, `1002`, `1011`, then asserts:

- `ExpandUDIMTiles()` discovers the 3 tiles with correct `(u, v)`.
- `BuildUDIMAtlas()` produces a `2x2` grid with `scale = (0.5, 0.5)`, `offset = (0, 0)`, atlas edge `<= udim_max_atlas_size`.
- Combine mode: one combined atlas image, `UVTexture::is_udim` set, `udim_uv_scale = (0.5, 0.5)`.
- Keep-as-is mode: one `UDIMTexture` with `imageTileIds = {1001, 1002, 1011}`, three images.

End-to-end, `tydra_to_renderscene --texload` on a UDIM scene builds the atlas and rebakes `primvars:st = [(0,0),(2,0),(0,2)]` to `[(0,0),(1,0),(0,1)]`.
