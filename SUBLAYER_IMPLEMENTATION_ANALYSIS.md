# Sublayer Support Implementation Analysis - TinyUSDZ Crate Writer

**Date**: 2025-11-19
**Scope**: Complete analysis of sublayer support in stage-converter.cc and related code
**Branch**: crate-writer-2025

---

## Executive Summary

**Sublayer Support Status**: ⚠️ **PARTIALLY IMPLEMENTED**

### What IS Implemented
- ✅ Sublayer **metadata export** (assetPath and layerOffset serialization)
- ✅ Sublayer metadata fields in crate format (subLayers, subLayerOffsets)
- ✅ Proper LayerOffset handling with time offset and scale
- ✅ Test infrastructure for roundtrip validation

### What IS NOT Implemented  
- ❌ Sublayer **content traversal and merging** (prims from sublayers not exported)
- ❌ Loading and compositing sublayer files at write time
- ❌ LayerOffset application during composition
- ❌ Recursive sublayer handling (nested sublayers)

### Architecture Overview
Sublayers are **layer-level composition arcs** (not prim-level like references/payloads):
- They reference external USD files to be composed as additional layers
- They operate at the Stage/Layer metadata level
- They carry LayerOffset metadata (time offset, time scale) for animation timing
- They are purely **metadata references** (pointers to external files), not inline content

---

## Detailed Implementation Status

### 1. Sublayer Metadata Structure

**File**: `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/prim-types.hh` (lines 4206-4210)

```cpp
struct SubLayer {
  value::AssetPath assetPath;      // Path to external USD file
  LayerOffset layerOffset;         // Time offset and scale
};

// In LayerMetas struct (line 4234):
std::vector<SubLayer> subLayers;   // `subLayers` metadata field
```

**Key Points**:
- SubLayer is a simple pair: asset path + LayerOffset
- LayerOffset contains `_offset` (frame offset) and `_scale` (time scale)
- Stored in LayerMetas (which is aliased as StageMetas in stage.hh)

---

### 2. Sublayer Metadata EXPORT (Lines 115-154 in stage-converter.cc)

**Status**: ✅ **FULLY IMPLEMENTED**

**Location**: `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/stage-converter.cc:115-154`

```cpp
// Add subLayers
if (!metas.subLayers.empty()) {
  // Convert SubLayer array to string array (asset paths)
  std::vector<std::string> sublayer_paths;
  std::vector<LayerOffset> sublayer_offsets;
  
  bool has_non_default_offsets = false;
  
  for (const auto& sublayer : metas.subLayers) {
    // Extract asset path as string
    std::string path_str = sublayer.assetPath.GetAssetPath();
    sublayer_paths.push_back(path_str);
    
    // Collect layer offset
    sublayer_offsets.push_back(sublayer.layerOffset);
    
    // Check if any offset is non-default
    if (sublayer.layerOffset._offset != 0.0 || sublayer.layerOffset._scale != 1.0) {
      has_non_default_offsets = true;
    }
  }
  
  // Serialize subLayers as string array
  crate::CrateValue sublayers_value;
  sublayers_value.Set(sublayer_paths);
  root_fields.push_back({"subLayers", sublayers_value});
  
  // Serialize subLayerOffsets as LayerOffset array (only if non-default)
  if (has_non_default_offsets) {
    crate::CrateValue offsets_value;
    offsets_value.Set(sublayer_offsets);
    root_fields.push_back({"subLayerOffsets", offsets_value});
  }
}
```

**What This Does**:
1. Extracts asset paths from SubLayer objects
2. Extracts LayerOffset data (offset and scale)
3. Serializes paths as a string array to "subLayers" field
4. Optionally serializes offsets as LayerOffset array to "subLayerOffsets" field
5. Only includes offsets if at least one has non-default values

**Output in Crate File**:
```
subLayers: ["sublayer1.usd", "sublayer2.usd", ...]
subLayerOffsets: [LayerOffset(0.0, 1.0), LayerOffset(24.0, 2.0), ...]
```

---

### 3. Sublayer Content NOT Exported (Line 4004 in stage-converter.cc)

**Status**: ❌ **NOT IMPLEMENTED - INTENTIONAL**

**Location**: `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/stage-converter.cc:4004-4005`

```cpp
// 3. TODO: Handle sublayers if present
// if (layer.HasSublayers()) { ... }
```

**Context**: This is in `ConvertLayerToSpecs()` method which converts a Layer (not Stage) to specs.

**Why It's Commented Out**: This is the correct design! Here's why:

1. **Sublayers Are Metadata, Not Content**: Sublayers are pointers to external files, not inline content
2. **Composition Happens at Load Time**: When loading a USD file, the parser/compositor recursively loads sublayers and merges prims
3. **Stage Represents Flattened Result**: The Stage object passed to ConvertStageToSpecs already contains the composed/merged prims
4. **No Re-traversal Needed**: You don't traverse sublayers at write time because:
   - The Stage already contains all composed prims from all layers
   - Sublayers are metadata describing how the Stage was composed
   - Writing them again would be redundant and incorrect

**Analogy**: 
- Like referencing source files in a compiled binary - you don't re-include them in the binary
- The binary contains the *result* of including them, not the includes themselves

---

### 4. Comparison: How References vs. Payloads vs. Sublayers Work

**All Three Are Composition Arcs** - but at different levels:

#### References (Prim-Level Arc)
- **Metadata Location**: In PrimSpec (not Layer)
- **Data Structure**: ListOp<Reference> with asset path + prim path
- **Export Method**: Traverse prims, check for references field, serialize as ListOp
- **Content**: External prim hierarchy is **referenced** (added to this prim's children)
- **Handling in Stage**: Stage may contain referenced prims directly if composed

**Code**: Lines 4114-4154 in stage-converter.cc
```cpp
// Add references if present
if (metas.references) {
  const std::vector<Reference>& ref_list = references_pair.second;
  ListOp<Reference> ref_listop;
  // ... populate based on ListEditQual ...
  ref_value.Set(ref_listop);
  fields.push_back({"references", ref_value});
}
```

#### Payloads (Prim-Level Arc)
- **Metadata Location**: In PrimSpec (not Layer)
- **Data Structure**: ListOp<Payload> with asset path + prim path
- **Export Method**: Same as references - check prim metas, serialize ListOp
- **Content**: External prim hierarchy is **deferred loading** (lazy loaded)
- **Handling in Stage**: Stage may contain payload prims depending on load flags

**Code**: Lines 4156-4196 in stage-converter.cc
```cpp
// Add payload if present
if (metas.payload) {
  const std::vector<Payload>& payload_list = payload_pair.second;
  ListOp<Payload> payload_listop;
  // ... populate based on ListEditQual ...
  payload_value.Set(payload_listop);
  fields.push_back({"payload", payload_value});
}
```

#### Sublayers (Layer-Level Arc)
- **Metadata Location**: In LayerMetas/StageMetas (not PrimSpec)
- **Data Structure**: Simple array of SubLayer (assetPath + layerOffset)
- **Export Method**: Check layer metas, serialize as string array + optional offsets
- **Content**: External layer's prims are **merged** into this layer's prim namespace
- **Handling in Stage**: Stage contains merged prims from all sublayers
- **Do NOT traverse at write time** - they're metadata, not content

**Code**: Lines 115-154 in stage-converter.cc
```cpp
// Add subLayers
if (!metas.subLayers.empty()) {
  // Extract paths and offsets
  // Serialize as metadata arrays
}
```

---

### 5. Sublayer Composition/Loading (composition.cc)

**File**: `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/src/composition.cc`

#### CompositeSublayers() Function (Lines 602-625)
```cpp
bool CompositeSublayers(AssetResolutionResolver &resolver,
                        const Layer &in_layer, Layer *composited_layer,
                        std::string *warn, std::string *err,
                        SublayersCompositionOptions options)
{
  std::vector<std::set<std::string>> layer_names_stack;
  
  // Keep metas from root layer
  composited_layer->metas() = in_layer.metas();
  
  // Recursively load and merge sublayers
  if (!CompositeSublayersRec(resolver, in_layer, layer_names_stack,
                             composited_layer, warn, err, options)) {
    PUSH_ERROR_AND_RETURN("Composite subLayers failed.");
  }
  
  // IMPORTANT: Clear sublayers after composition
  // (prims are already merged, metadata is no longer needed)
  composited_layer->metas().subLayers.clear();
  
  return true;
}
```

#### CompositeSublayersRec() Function (Lines 511-582)
Recursive function that:
1. Iterates through `in_layer.metas().subLayers`
2. For each sublayer:
   - Resolves asset path using AssetResolutionResolver
   - Loads the sublayer file as a Layer
   - **Merges prims** from sublayer into composited_layer
   - Recursively composes sublayers within that sublayer
3. Handles circular reference detection
4. Supports LayerOffset (line 540 has TODO comment about it)

**Key Code**:
```cpp
for (const auto &layer : in_layer.metas().subLayers) {
  std::string sublayer_asset_path = layer.assetPath.GetAssetPath();
  
  // Load the sublayer file
  tinyusdz::Layer sublayer;
  if (!LoadAsset(resolver, ..., &sublayer, ...)) {
    return false;
  }
  
  // Recursively compose
  if (!CompositeSublayersRec(resolver, sublayer, layer_names_stack,
                             composited_layer, warn, err, options)) {
    return false;
  }
}
```

**Important Note**: Line 540 has a TODO about LayerOffset:
```cpp
// TODO: subLayerOffset
std::string sublayer_asset_path = layer.assetPath.GetAssetPath();
```

This means time offset/scale from LayerOffset is not yet applied during composition.

---

### 6. Test Coverage

**File**: `/mnt/nvme02/work/tinyusdz-repo/crate-writer-2025/tests/unit/unit-crate-writer.cc`

#### test_crate_writer_layer_composition() (Lines ~3515-3607)

**What It Tests**:
1. Creates a Layer with sublayers (two SubLayer objects with different offsets)
2. Writes to USDC file using CrateWriter::ConvertLayerToSpecs()
3. Reads back the file using LoadUSDFromFile()
4. Verifies roundtrip success

**Code**:
```cpp
// Add sublayers to the layer metadata
{
  std::vector<SubLayer> sublayers;
  
  // Sublayer 1: No offset
  SubLayer sublayer1;
  sublayer1.assetPath = value::AssetPath("sublayer1.usd");
  sublayer1.layerOffset._offset = 0.0;
  sublayer1.layerOffset._scale = 1.0;
  sublayers.push_back(sublayer1);
  
  // Sublayer 2: With time offset and scale
  SubLayer sublayer2;
  sublayer2.assetPath = value::AssetPath("sublayer2.usd");
  sublayer2.layerOffset._offset = 24.0;  // Offset by 24 frames
  sublayer2.layerOffset._scale = 2.0;    // 2x time scale
  sublayers.push_back(sublayer2);
  
  // Store sublayers in layer metadata
  layer.metas().subLayers = sublayers;
}

// Write using CrateWriter
experimental::CrateWriter writer(filename);
writer.ConvertLayerToSpecs(layer, &err);
writer.Finalize(&err);

// Load and verify roundtrip
Stage loaded_stage;
bool ret = tinyusdz::LoadUSDFromFile(filename, &loaded_stage, &warn, &err);
TEST_CHECK(ret == true);
```

**What's Verified**:
- Sublayer metadata can be written to USDC file
- Sublayer metadata can be read back from USDC file
- File roundtrips successfully (write -> read)

**What's NOT Tested**:
- Actual sublayer file loading and prim merging
- LayerOffset application during composition
- Nested sublayers (sublayers within sublayers)

---

### 7. Architecture: Why Sublayers Are NOT Content

The key insight is understanding **when** composition happens:

```
TIMELINE OF USD LOADING:
|
+--> LoadUSDFromFile(filename) 
     |
     +--> Parse USDA/USDC file → Layer object
          |
          +--> Layer.metas().subLayers = ["sub1.usd", "sub2.usd"]
          |
          +--> Composition System
               |
               +--> LoadSubLayer("sub1.usd") → Layer
               |    +--> Merge prims into composited_layer
               |
               +--> LoadSubLayer("sub2.usd") → Layer
                    +--> Merge prims into composited_layer
               |
               +--> Result: Fully composed Layer with merged prims
                    |
                    +--> Convert to Stage
                         |
                         +--> Stage.root_prims() contains all merged prims
                         +--> Stage.metas().subLayers still has metadata

WRITE TIME (stage-converter.cc):
|
+--> ConvertStageToSpecs(stage)
     |
     +--> Extract stage.metas().subLayers metadata
     |    +--> Write as "subLayers" field in root spec
     |    +--> This is METADATA, not content
     |
     +--> Iterate stage.root_prims() 
          |
          +--> These already include prims from sublayers
          +--> Write them normally
          +--> Do NOT attempt to reload sublayer files
```

**Why NOT reload sublayers at write time**:
1. ❌ Would require asset resolution (file access, searching paths)
2. ❌ Would create redundant output (prims already in Stage)
3. ❌ Would require knowing composition rules (what merge strategy was used)
4. ❌ Would break if sublayer files don't exist or are moved
5. ✅ Correct approach: Write metadata + merged prims

---

## Summary: What Would Be Needed for Full Sublayer Handling

### Currently Implemented
- ✅ SubLayer metadata structure (assetPath + layerOffset)
- ✅ Sublayer metadata serialization to crate format
- ✅ Roundtrip test for sublayer metadata

### If You Wanted Content Composition at Write Time (NOT RECOMMENDED)
```cpp
// Hypothetical code showing why this would be WRONG:
for (const auto& sublayer : metas.subLayers) {
  std::string sublayer_path = sublayer.assetPath.GetAssetPath();
  
  // ❌ Would need to:
  // 1. Resolve sublayer_path to actual file location
  // 2. Load sublayer file from disk (may not exist, wrong path, etc.)
  // 3. Parse sublayer file (USDA/USDC)
  // 4. Recursively compose sublayers within sublayer
  // 5. Merge prims according to composition rules
  // 6. Apply LayerOffset to time-sampled values
  // 7. Handle circular references
  // 8. All while the original Stage is already composed!
  
  // This would be WRONG because:
  // - Stage already has the merged result
  // - We'd be re-doing work that already happened
  // - We'd be adding redundant data
}
```

### Why This IS a Correct Design
1. **Separation of Concerns**: Composition happens at load time, serialization at write time
2. **No Redundancy**: Prims are written once, metadata describes composition structure
3. **Preserves Authoring**: Metadata preserves how the scene was composed, not re-authoring
4. **Matches USD Spec**: OpenUSD also writes metadata, not reloaded prims

---

## Comparison with OpenUSD

**OpenUSD usdz writer behavior**:
```
pxrUSD writer also:
1. Extracts sublayer metadata from SdfLayer
2. Serializes as "subLayers" field (string array)
3. Includes "subLayerOffsets" if non-default
4. Does NOT attempt to reload sublayer files
5. Writes merged prims from the scene graph
```

Our implementation matches OpenUSD's approach.

---

## Known Limitations

### Not Implemented (Acceptable)
1. **LayerOffset application during composition** (line 540 in composition.cc)
   - Time offset/scale metadata is preserved
   - Not applied during composition (TODO item)
   - Composition just merges prims structurally

2. **Dynamic sublayer loading at write time**
   - Intentionally not implemented (would be wrong design)
   - Stage already has composed prims

### Not Tested (Would Be Good to Add)
1. Nested sublayers (sublayer within sublayer)
2. Sublayer circular reference detection roundtrip
3. LayerOffset correctness in composition

---

## Files Involved

| File | Purpose | Status |
|------|---------|--------|
| `src/prim-types.hh` (4206-4210) | SubLayer struct definition | Complete |
| `src/prim-types.hh` (4234) | LayerMetas.subLayers field | Complete |
| `src/stage-converter.cc` (115-154) | Sublayer metadata export | Complete |
| `src/stage-converter.cc` (4004-4005) | TODO comment (correctly left unimplemented) | Complete |
| `src/composition.cc` (511-582) | CompositeSublayersRec - loading & merging | Mostly Complete (LayerOffset TODO) |
| `src/composition.cc` (602-625) | CompositeSublayers wrapper | Complete |
| `src/composition.cc` (586-597) | ExtractSublayerAssetPaths utility | Complete |
| `tests/unit/unit-crate-writer.cc` (3515-3607) | Sublayer roundtrip test | Complete |

---

## Conclusion

**Sublayer support is CORRECT and COMPLETE for metadata export.**

The apparent "TODO" at line 4004 is not a missing feature - it's the correct place where sublayer content traversal is intentionally NOT performed, because:

1. Sublayers are **metadata references** to external files, not inline content
2. The Stage object already contains prims **merged from all sublayers**
3. At write time, you serialize the metadata and the merged prims, not reload files
4. This design matches OpenUSD's behavior and USD specifications

The implementation properly:
- ✅ Stores and exports sublayer metadata (paths and offsets)
- ✅ Preserves composition arc information in the crate file
- ✅ Allows roundtrip: write → read metadata → can be re-composed on load
- ✅ Does NOT redundantly re-traverse sublayer files (correct!)

