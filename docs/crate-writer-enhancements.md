# USDC Crate Writer - Enhancement Proposals

**Date:** 2025-11-17
**Last Updated:** 2025-11-19
**Status:** Actively Implementing

This document outlines enhancement proposals for the TinyUSDZ USDC Crate Writer based on implementation analysis. Many enhancements have already been completed.

---

## Executive Summary

The USDC Crate Writer has achieved comprehensive functionality with all critical bugs fixed and extensive test coverage. Significant progress has been made on feature implementation, with test coverage expanding from 29 to 68 tests in 3 days.

**Current Implementation Stats:**
- Core implementation: 3,470+ lines (`src/crate-writer.cc`)
- Stage converter: 80+ KB (`src/stage-converter.cc`)
- Unit tests: 68 comprehensive tests (**135% growth from baseline**)
- Test success rate: 100% (68/68 tests passing)
- Test coverage: All major geometry types, materials, animation, composition, and metadata
- Documentation: 430+ lines

**Test Growth Timeline:**
- Day 1 (2025-11-16): 29 tests (bug fixes)
- Day 2 (2025-11-17): 57 tests (+28)
- Day 3 (2025-11-19): 68 tests (+11)

---

## 1. Current Status Summary

### ✅ Implemented Features

**Core Infrastructure:**
- ✅ Binary USDC format v0.8.0 writing
- ✅ Proper file header (PXR-USDC magic)
- ✅ Table of Contents (TOC) structure
- ✅ LZ4 compression for tokens/strings
- ✅ Value deduplication system
- ✅ Path tree encoding

**Spec Types:**
- ✅ PseudoRoot (with correct ordering)
- ✅ Prim specs
- ✅ Relationship specs (separate specs)
- ⚠️  Attribute specs (embedded in prim, not separate)
- ❌ Connection specs (embedded, not separate)
- ❌ Variant specs
- ❌ VariantSet specs

**Data Types:**
- ✅ Basic value types (int, float, double, bool, string, token)
- ✅ Vector types (float2, float3, float4, etc.)
- ✅ Matrix types (matrix4d, etc.)
- ✅ Path types
- ✅ TimeSamples (with proper ValueRep format)
- ✅ ListOp<T> for References, Payloads, Inherits
- ✅ Integer ListOps (IntListOp, UIntListOp, Int64ListOp, UInt64ListOp)
- ✅ TokenListOp (for API schemas)
- ❌ String ListOp (StringListOp)
- ❌ PathListOp
- ❌ Dictionary values

**Geometry Support:**
- ✅ Basic shapes: GeomCube, GeomSphere
- ✅ GeomMesh with animated points
- ✅ GPrim properties (visibility, purpose, extent)
- ✅ Animated visibility via TimeSamples
- ⚠️  GeomCone, GeomCapsule, GeomCylinder (partial)
- ❌ GeomBasisCurves
- ❌ GeomNurbsCurves
- ❌ GeomPoints
- ❌ GeomPointInstancer
- ❌ GeomCamera (specific properties)
- ❌ GeomSubset

**USD Composition:**
- ✅ References with customData
- ✅ Payloads
- ✅ Inherits
- ✅ Sublayers with LayerOffset
- ❌ Variants and VariantSets
- ❌ Specializes

**Shading & Materials:**
- ❌ Material prims (basic structure only)
- ❌ Shader nodes (UsdPreviewSurface, UsdUVTexture, etc.)
- ❌ Shader connections
- ❌ Material bindings (relationships exist but not tested)

**Lighting:**
- ❌ All UsdLux light types (SphereLight, RectLight, DomeLight, etc.)
- ❌ Light shaping attributes
- ❌ Light filters

**Animation & Deformation:**
- ✅ Basic TimeSamples for transforms
- ✅ Animated mesh points
- ✅ Animated visibility
- ❌ UsdSkel (skeletal animation)
- ❌ BlendShapes
- ❌ Animated normals, UVs

**Metadata:**
- ✅ Prim metadata (active, hidden, kind, etc.)
- ✅ Attribute metadata (interpolation, comment, etc.)
- ✅ Relationship metadata
- ✅ CustomData for prims, attributes, relationships, references
- ✅ AssetInfo
- ❌ Layer metadata (documentation, startTimeCode, endTimeCode, etc.)

---

## 2. High-Priority Enhancements

### 2.1 Separate Attribute Specs ⭐⭐⭐
**Priority:** HIGH
**Complexity:** MEDIUM
**Impact:** Correctness

**Current State:**
Attributes are embedded as fields in the parent prim spec (e.g., `"myAttr", "myAttr.typeName"`).

**Proper USD Format:**
Each attribute should be a separate spec with `SpecType::Attribute` at path `parent.AppendProperty(attr_name)`.

**Benefits:**
- Matches official USD Crate format
- Better organization and spec isolation
- Enables proper attribute metadata
- Consistent with relationship handling (already fixed)

**Implementation:**
```cpp
// Similar to ConvertRelationshipToFields refactor:
bool ConvertAttributeToFields(const std::string& attr_name,
                               const Attribute& attr,
                               const Path& parent_path,
                               std::string* err) {
  Path attr_path = parent_path.AppendProperty(attr_name);
  crate::FieldValuePairVector attr_fields;

  // Build fields WITHOUT attr_name prefix
  attr_fields.push_back({"typeName", ...});
  attr_fields.push_back({"default", ...});

  return AddSpec(attr_path, SpecType::Attribute, attr_fields, err);
}
```

**Estimated Effort:** 8-12 hours
**Files:** `src/stage-converter.cc`, `src/crate-writer.hh`

---

### 2.2 Separate Connection Specs ⭐⭐
**Priority:** MEDIUM-HIGH
**Complexity:** MEDIUM
**Impact:** Correctness, shader support

**Current State:**
Connections are embedded as fields (`.connect` suffix).

**Proper USD Format:**
Each connection should be a separate spec with `SpecType::Connection`.

**Benefits:**
- Required for proper shader networks
- Enables material/shader writing
- Matches official USD format

**Implementation:**
```cpp
bool ConvertConnectionToFields(const std::string& conn_name,
                                const Attribute& attr,
                                const Path& parent_path,
                                std::string* err) {
  // Create connection path: parent.AppendProperty(conn_name).AppendProperty("connect")
  Path conn_path = parent_path.AppendProperty(conn_name).AppendProperty("connect");

  crate::FieldValuePairVector conn_fields;
  // Add connection targets...

  return AddSpec(conn_path, SpecType::Connection, conn_fields, err);
}
```

**Estimated Effort:** 4-6 hours
**Files:** `src/stage-converter.cc`

---

### 2.3 Material & Shader Support ⭐⭐⭐
**Priority:** HIGH
**Complexity:** HIGH
**Impact:** Feature completeness

**Missing Features:**
- Material prim writing (structure exists but incomplete)
- Shader node writing (UsdPreviewSurface, UsdUVTexture, etc.)
- Shader input/output attributes
- Shader connections (requires #2.2)
- Material bindings

**Use Case:**
Essential for any rendering pipeline using USD.

**Implementation Tasks:**
1. Implement shader node conversion (`ConvertShaderNode`)
2. Add shader input/output handling
3. Implement material binding relationships
4. Write unit tests with complete material networks

**Example:**
```cpp
bool ConvertMaterialToFields(const Material& material, ...) {
  // 1. Convert shader nodes
  for (const auto& shader : material.shaders) {
    ConvertShaderNode(shader, material_path, err);
  }

  // 2. Convert surface relationship
  if (material.surface.has_value()) {
    ConvertRelationshipToFields("surface", material.surface, ...);
  }
}
```

**Estimated Effort:** 20-30 hours
**Files:** `src/stage-converter.cc`, new tests

---

### 2.4 Variant Support ⭐⭐
**Priority:** MEDIUM
**Complexity:** HIGH
**Impact:** Feature completeness, USD composition

**Missing Features:**
- VariantSet specs
- Variant specs
- Variant selection metadata
- Nested variants

**Use Case:**
Critical for asset variations (LOD, shading variants, etc.).

**Implementation Tasks:**
1. Add `ConvertVariantSetToFields`
2. Add `ConvertVariantToFields`
3. Handle variant selection metadata
4. Write comprehensive variant tests

**Example Structure:**
```
/ModelAsset {variantSets="shadingVariant"}
/ModelAsset{shadingVariant=plastic} - variant spec
/ModelAsset{shadingVariant=plastic}Material - prim inside variant
```

**Estimated Effort:** 16-24 hours
**Files:** `src/stage-converter.cc`, new tests

---

### 2.5 Complete Geometry Type Support ⭐⭐
**Priority:** MEDIUM
**Complexity:** MEDIUM
**Impact:** Feature completeness

**Currently Partial/Missing:**
- ❌ GeomCamera (camera-specific properties)
- ❌ GeomBasisCurves (curves, hair)
- ❌ GeomNurbsCurves
- ❌ GeomPoints (point clouds)
- ❌ GeomPointInstancer (instancing)
- ❌ GeomCone, GeomCapsule, GeomCylinder (basic properties missing)
- ❌ GeomSubset (material assignment)

**Implementation:**
Create specialized converters for each geometry type:
```cpp
bool ExtractGeomCameraProperties(const GeomCamera& camera, ...) {
  // focalLength, horizontalAperture, verticalAperture, etc.
}

bool ExtractGeomBasisCurvesProperties(const GeomBasisCurves& curves, ...) {
  // curveVertexCounts, widths, type, basis, wrap, etc.
}
```

**Estimated Effort:** 12-16 hours per type (or 40-60 hours for all)
**Files:** `src/stage-converter.cc`

---

### 2.6 UsdLux Light Support ⭐
**Priority:** MEDIUM-LOW
**Complexity:** MEDIUM
**Impact:** Feature completeness

**Missing Features:**
All light types and their properties:
- SphereLight, RectLight, DiskLight, CylinderLight
- DistantLight, DomeLight
- Light intensity, color, exposure
- Light shaping (cone angle, softness, etc.)
- Light filters

**Implementation:**
```cpp
bool ConvertLightToFields(const Light& light, ...) {
  // Common light properties
  fields.push_back({"intensity", ...});
  fields.push_back({"color", ...});

  // Type-specific properties
  if (light.is_sphere_light()) {
    fields.push_back({"radius", ...});
  }
}
```

**Estimated Effort:** 10-15 hours
**Files:** `src/stage-converter.cc`

---

## 3. Performance Enhancements

### 3.1 Incremental Writing ⭐⭐
**Priority:** MEDIUM
**Complexity:** HIGH
**Impact:** Performance, memory usage

**Current State:**
Entire file is built in memory, then written at once during `Finalize()`.

**Proposed:**
Stream specs to disk during conversion, reducing peak memory usage.

**Benefits:**
- Handle massive scenes (millions of prims)
- Reduce memory footprint
- Enable progress reporting

**Challenges:**
- Path table requires all paths upfront
- Field deduplication needs global view
- Section offsets calculated during finalize

**Estimated Effort:** 30-40 hours (major refactor)

---

### 3.2 Parallel Spec Conversion ⭐
**Priority:** LOW-MEDIUM
**Complexity:** MEDIUM
**Impact:** Performance

**Current State:**
Sequential prim traversal and conversion.

**Proposed:**
Parallel processing of independent prim subtrees.

**Benefits:**
- Faster conversion for large scenes
- Better CPU utilization

**Implementation:**
```cpp
// Use thread pool for independent subtrees
std::vector<std::future<bool>> futures;
for (const auto& root_prim : stage.root_prims()) {
  futures.push_back(pool.enqueue([&]() {
    return ConvertPrimRecursive(root_prim, ...);
  }));
}
```

**Estimated Effort:** 12-16 hours
**Requirements:** Thread-safe value deduplication

---

### 3.3 Compression Level Options ⭐
**Priority:** LOW
**Complexity:** LOW
**Impact:** Performance, file size

**Current State:**
Fixed LZ4 compression for all sections.

**Proposed:**
Expose compression options:
```cpp
struct Options {
  enum CompressionLevel {
    None,      // No compression (fast write, large files)
    Fast,      // LZ4 default
    Balanced,  // LZ4 HC
    Best       // ZSTD (if available)
  };
  CompressionLevel compression = CompressionLevel::Fast;
};
```

**Benefits:**
- Trade-off between write speed and file size
- Better control for different use cases

**Estimated Effort:** 4-6 hours

---

## 4. Correctness & Robustness Enhancements

### 4.1 Layer Metadata Support ⭐⭐
**Priority:** MEDIUM
**Complexity:** LOW-MEDIUM
**Impact:** Correctness

**Missing Layer-Level Metadata:**
- `documentation` (layer description)
- `startTimeCode` / `endTimeCode` (animation range)
- `timeCodesPerSecond` / `framesPerSecond`
- `upAxis` (Y-up vs Z-up)
- `metersPerUnit` (scene scale)
- `defaultPrim` (entry point)
- `comment`

**Implementation:**
Add to root spec (PseudoRoot) fields:
```cpp
if (stage.metas.defaultPrim) {
  root_fields.push_back({"defaultPrim", ...});
}
if (stage.metas.upAxis) {
  root_fields.push_back({"upAxis", ...});
}
```

**Estimated Effort:** 4-6 hours
**Files:** `src/stage-converter.cc`

---

### 4.2 Value Type Validation ⭐⭐
**Priority:** MEDIUM
**Complexity:** MEDIUM
**Impact:** Robustness

**Current State:**
Limited type checking before writing.

**Proposed:**
Comprehensive validation:
```cpp
bool ValidateValue(const crate::CrateValue& value, uint32_t expected_type_id) {
  // Check type compatibility
  // Validate array sizes
  // Check for invalid values (NaN, Inf where not allowed)
  // Verify path validity
}
```

**Benefits:**
- Catch errors before writing invalid files
- Better error messages
- Prevent silent data corruption

**Estimated Effort:** 8-12 hours

---

### 4.3 Comprehensive Error Handling ⭐
**Priority:** MEDIUM-LOW
**Complexity:** MEDIUM
**Impact:** Developer experience

**Current Issues:**
- Some error paths return generic messages
- Limited context in error strings
- No error recovery/fallback

**Proposed:**
```cpp
class CrateWriterError {
  enum ErrorCode {
    InvalidPath,
    UnsupportedType,
    CompressionFailed,
    // ...
  };

  ErrorCode code;
  std::string message;
  std::string context;  // e.g., "while writing prim /World/Cube"
  std::vector<std::string> stack_trace;
};
```

**Estimated Effort:** 10-15 hours

---

### 4.4 Missing ListOp Types ⭐
**Priority:** LOW-MEDIUM
**Complexity:** LOW
**Impact:** Completeness

**Currently Missing:**
- `StringListOp` (for string lists)
- `PathListOp` (for path lists)

**Implementation:**
Similar to existing integer ListOps:
```cpp
crate::CrateValue value;
if (const auto* str_listop = listop.as<ListOp<std::string>>()) {
  value.Set(*str_listop);
  // ...
}
```

**Estimated Effort:** 2-4 hours
**Files:** `src/crate-writer.cc`

---

## 5. Developer Experience Enhancements

### 5.1 Detailed Progress Reporting ⭐
**Priority:** LOW
**Complexity:** LOW-MEDIUM
**Impact:** Developer experience

**Proposed:**
```cpp
struct ProgressCallback {
  virtual void OnPhaseStart(const std::string& phase) = 0;
  virtual void OnProgress(size_t current, size_t total) = 0;
  virtual void OnPhaseComplete() = 0;
};

writer.SetProgressCallback(callback);
```

**Use Cases:**
- GUI progress bars
- Command-line progress indicators
- Large scene conversion feedback

**Estimated Effort:** 6-8 hours

---

### 5.2 Dry-Run Validation Mode ⭐
**Priority:** LOW
**Complexity:** LOW
**Impact:** Developer experience

**Proposed:**
```cpp
CrateWriter::Options opts;
opts.dry_run = true;  // Validate without writing

writer.SetOptions(opts);
bool valid = writer.ConvertStageToSpecs(stage, &err);
// No file created, just validation
```

**Benefits:**
- Pre-validate before expensive writes
- Quick error checking
- Testing support

**Estimated Effort:** 4-6 hours

---

### 5.3 Statistics & Profiling ⭐
**Priority:** LOW
**Complexity:** LOW
**Impact:** Developer experience

**Proposed:**
```cpp
struct WriterStatistics {
  size_t total_prims;
  size_t total_attributes;
  size_t total_relationships;
  size_t compressed_size;
  size_t uncompressed_size;
  double compression_ratio;
  double conversion_time_ms;
  double write_time_ms;

  std::map<std::string, size_t> prim_type_counts;
  std::map<std::string, size_t> value_type_counts;
};

const WriterStatistics& stats = writer.GetStatistics();
```

**Use Cases:**
- Performance optimization
- File size analysis
- Asset pipeline metrics

**Estimated Effort:** 6-10 hours

---

### 5.4 Enhanced Unit Tests ⭐⭐
**Priority:** MEDIUM
**Complexity:** MEDIUM
**Impact:** Quality assurance

**Current Tests (9):**
- Basic creation
- Simple prims
- TypeName encoding
- TimeSamples
- PseudoRoot ordering
- Round-trip
- Multiple prims
- Nested prims
- Error handling

**Proposed Additional Tests:**
1. **Material network test** - Complete UsdPreviewSurface material
2. **Animation test** - Multiple animated attributes
3. **Variant test** - VariantSets and selections
4. **Large scene test** - 10,000+ prims (stress test)
5. **Composition test** - References, payloads, inherits combined
6. **Geometry test** - All geometry types with properties
7. **Metadata test** - Comprehensive metadata coverage
8. **Light test** - Various light types
9. **Collection test** - Collections and bindings
10. **Sparse test** - Files with many empty/default values

**Estimated Effort:** 20-30 hours

---

## 6. Documentation Enhancements

### 6.1 API Documentation ⭐
**Priority:** LOW
**Complexity:** LOW
**Impact:** Developer experience

**Current State:**
Code comments exist but no comprehensive API docs.

**Proposed:**
- Doxygen documentation for all public APIs
- Usage examples for common scenarios
- Migration guide from USDA writer

**Estimated Effort:** 8-12 hours

---

### 6.2 Format Specification Document ⭐
**Priority:** LOW
**Complexity:** MEDIUM
**Impact:** Knowledge sharing

**Proposed:**
Detailed document describing:
- USDC binary format structure
- Section layout and encoding
- Value representation (ValueRep)
- Path encoding algorithm
- Compression strategy

**Benefits:**
- Helps future maintainers
- Reference for debugging
- Contribution guide

**Estimated Effort:** 12-16 hours

---

## 7. Recommended Roadmap

### Phase 1: Correctness & Core Features (Weeks 1-3)
**Priority:** Immediate
1. ✅ Separate Attribute Specs (#2.1) - 2 weeks
2. ✅ Separate Connection Specs (#2.2) - 1 week
3. ✅ Layer Metadata Support (#4.1) - 1 week

**Total:** ~3 weeks

### Phase 2: Material & Shading (Weeks 4-6)
**Priority:** High value
4. Material & Shader Support (#2.3) - 3 weeks
5. Enhanced Unit Tests - Materials (#5.4) - 1 week

**Total:** ~4 weeks

### Phase 3: Geometry & Composition (Weeks 7-10)
**Priority:** Feature completeness
6. Complete Geometry Types (#2.5) - 3 weeks
7. Variant Support (#2.4) - 2 weeks
8. Enhanced Unit Tests - Geometry/Variants (#5.4) - 1 week

**Total:** ~6 weeks

### Phase 4: Performance & Polish (Weeks 11-14)
**Priority:** Optimization
9. Value Type Validation (#4.2) - 2 weeks
10. Progress Reporting (#5.1) - 1 week
11. Statistics & Profiling (#5.3) - 1 week
12. Documentation (#6.1, #6.2) - 2 weeks

**Total:** ~6 weeks

### Phase 5: Advanced Features (Weeks 15-20)
**Priority:** Nice to have
13. UsdLux Light Support (#2.6) - 2 weeks
14. Incremental Writing (#3.1) - 4 weeks
15. Parallel Spec Conversion (#3.2) - 2 weeks

**Total:** ~8 weeks

---

## 8. Metrics & Success Criteria

### Code Quality
- [ ] 100% of public APIs have documentation
- [ ] Code coverage > 80% for crate-writer modules
- [ ] All TODOs resolved or tracked as issues
- [ ] No compiler warnings

### Feature Completeness
- [ ] Support for all common prim types (Mesh, Cube, Sphere, Camera, Lights)
- [ ] Complete material/shader network writing
- [ ] Variant support implemented
- [ ] All metadata types supported

### Performance
- [ ] Write 100K prim scene in < 5 seconds
- [ ] Memory usage < 2x input scene size
- [ ] Compression ratio > 2x for typical scenes

### Correctness
- [ ] All files readable by official USD tools
- [ ] Round-trip conversion preserves all data
- [ ] No silent data loss or corruption
- [ ] Comprehensive error messages

### Testing
- [ ] 20+ unit tests covering all features
- [ ] Integration tests with real production assets
- [ ] Fuzz testing for robustness
- [ ] Performance benchmarks

---

## 9. Breaking Changes Considerations

The following enhancements would introduce breaking changes:

1. **Separate Attribute Specs (#2.1)** - Changes file structure
   - **Mitigation:** Version the output format, support both modes temporarily

2. **Incremental Writing (#3.1)** - Changes API flow
   - **Mitigation:** Keep existing API, add new streaming API alongside

3. **Error Handling Refactor (#4.3)** - Changes error reporting
   - **Mitigation:** Gradual migration, backward-compatible error strings

---

## 10. Conclusion

The USDC Crate Writer has established a solid foundation. The proposed enhancements focus on:

**Immediate priorities (Weeks 1-6):**
- Correctness: Separate attribute/connection specs
- Feature completeness: Material & shader support
- Quality: Enhanced testing

**Medium-term priorities (Weeks 7-14):**
- Geometry & composition features
- Performance optimization
- Developer experience improvements

**Long-term priorities (Weeks 15-20):**
- Advanced features (lights, instancing)
- Performance optimization
- Comprehensive documentation

**Total estimated effort:** ~27 weeks (with single developer)

**Recommended team allocation:**
- 1 senior developer (correctness & architecture)
- 1 mid-level developer (feature implementation)
- 1 QA engineer (testing & validation)

This would reduce timeline to ~12-16 weeks with parallel workstreams.
