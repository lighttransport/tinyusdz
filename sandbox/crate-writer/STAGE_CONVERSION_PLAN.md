# Stage-to-Crate Conversion Implementation Plan

**Date**: 2025-11-04
**Status**: 📋 PLANNING PHASE
**Current State**: Core crate format working, minimal files validate with OpenUSD

## Current Status

### ✅ What Works Now

The crate-writer successfully creates **minimal valid USDC files**:
- Correct file format (headers, TOC, sections)
- Path tree encoding with proper token synchronization
- All sections written correctly
- Files validate with OpenUSD v0.25.8
- 99.35% success rate on batch testing (308/310 files)

**Limitation**: Files only contain:
- Root path ("/")
- PseudoRoot spec
- Empty field sets
- No actual scene content

### 🎯 Goal

Implement full Stage→Crate conversion to create USDC files with:
- Complete prim hierarchy
- Attribute values
- Properties and metadata
- Relationships
- Time samples (optional, future)

## Architecture Analysis

### TinyUSDZ Stage Structure

```cpp
class Stage {
  std::vector<Prim> _root_nodes;  // Top-level prims
  StageMetas stage_metas;         // Stage-level metadata
};
```

### Prim Structure

```cpp
class Prim {
  Path _elementPath;               // Prim name ("myXform")
  Path _abs_path;                  // Absolute path ("/World/myXform")
  std::string _prim_type_name;     // "Xform", "Mesh", "Material"
  Specifier _specifier;            // def, over, class

  value::Value _data;              // Typed prim data (Xform, Mesh, etc.)
  PrimMeta _metas;                 // Metadata
  std::vector<Prim> _children;     // Child prims
  std::map<std::string, VariantSet> _variantSets;

  // Properties are embedded in typed data (_data)
  // e.g., GeomMesh has points, normals, faceVertexCounts, etc.
};
```

### Crate Spec Structure

```cpp
struct Spec {
  PathIndex path_index;        // Index into paths array
  FieldSetIndex fieldset_index;  // Index into fieldsets array
  SpecType spec_type;          // Prim, Attribute, Relationship, etc.
};
```

### Key Differences

| TinyUSDZ | Crate Format |
|----------|--------------|
| Hierarchical Prims with children | Flat array of Specs with path indices |
| Properties embedded in typed data | Fields in fieldsets |
| Strong typing (Xform, Mesh, etc.) | Generic specs with fields |
| Properties stored as members | All data as Field name→value pairs |

## Implementation Strategy

### Phase 1: Basic Prim Conversion (PRIORITY)

**Goal**: Convert simple prims without properties

**Tasks**:
1. Implement `ConvertPrimToSpec()` function
   - Extract prim path
   - Determine SpecType (Prim, Attribute, Relationship)
   - Get specifier (def, over, class)
   - Create empty fieldset initially

2. Implement recursive traversal
   - Walk Stage.root_prims()
   - Recursively process Prim.children()
   - Build absolute paths as we traverse
   - Call AddSpec() for each prim

3. Test with simple hierarchy
   ```usda
   #usda 1.0
   def Xform "World" {
       def Cube "MyCube" {
       }
   }
   ```

**Expected Output**: USDC with prims but no attributes

### Phase 2: Basic Attribute Conversion

**Goal**: Convert simple scalar attributes

**Tasks**:
1. Extract properties from typed prim data
   - Access Xform.xformOp:translate
   - Access GeomMesh.points, normals, etc.
   - Handle Property struct

2. Convert Property→Field
   - Property.name → field name
   - Property.get_value() → CrateValue
   - Handle default value vs time samples

3. Implement value type conversion
   - Start with scalars: int, float, double, bool, token, string
   - Handle vectors: float3, double3, int3, etc.
   - Handle arrays: float[], int[], etc.

4. Test with simple attributes
   ```usda
   def Cube "MyCube" {
       double size = 2.0
       float3 xformOp:translate = (1, 2, 3)
   }
   ```

**Expected Output**: USDC with prims and basic attributes

### Phase 3: Metadata and Advanced Features

**Goal**: Complete feature parity

**Tasks**:
1. Convert PrimMeta→Fields
   - doc, comment, displayName, etc.
   - apiSchemas
   - customData

2. Handle relationships
   - Extract from Prim
   - Create Relationship specs
   - Handle targets

3. Handle variants
   - Extract VariantSet data
   - Create variant specs
   - Handle variant selection

4. Handle references and sublayers
   - Extract composition arcs
   - Add reference fields
   - Handle payload fields

5. Time samples (optional)
   - Extract time-varying data
   - Convert to time sample format
   - Handle interpolation

### Phase 4: Testing and Validation

1. Unit tests for each conversion function
2. Round-trip testing (USDA → USDC → USDA)
3. Validation with complex scenes
4. Performance profiling
5. Memory usage optimization

## Detailed Implementation: Phase 1

### Step 1: Add ConvertStageToSpecs() Method

```cpp
// In crate-writer.hh
class CrateWriter {
public:
  // New method
  bool ConvertStageToSpecs(const Stage& stage, std::string* err = nullptr);

private:
  // Helper to recursively convert prims
  bool ConvertPrimRecursive(
    const Prim& prim,
    const Path& parent_path,
    std::string* err
  );
};
```

### Step 2: Implement Basic Conversion

```cpp
// In crate-writer.cc

bool CrateWriter::ConvertStageToSpecs(const Stage& stage, std::string* err) {
  // Add root spec
  Path root_path("/", "");
  tcrate::FieldValuePairVector root_fields;

  if (!AddSpec(root_path, SpecType::PseudoRoot, root_fields, err)) {
    return false;
  }

  // Convert all root prims
  for (const auto& prim : stage.root_prims()) {
    Path parent_path("/", "");
    if (!ConvertPrimRecursive(prim, parent_path, err)) {
      return false;
    }
  }

  return true;
}

bool CrateWriter::ConvertPrimRecursive(
  const Prim& prim,
  const Path& parent_path,
  std::string* err
) {
  // Build absolute path
  std::string prim_name = prim.element_name();
  std::string parent_str = parent_path.prim_part();
  std::string abs_path_str;

  if (parent_str == "/") {
    abs_path_str = "/" + prim_name;
  } else {
    abs_path_str = parent_str + "/" + prim_name;
  }

  Path prim_path(abs_path_str, "");

  // Create empty fieldset for now
  tcrate::FieldValuePairVector fields;

  // TODO: Extract properties and add as fields

  // Add spec for this prim
  SpecType spec_type = SpecType::Prim;

  if (!AddSpec(prim_path, spec_type, fields, err)) {
    if (err) *err = "Failed to add spec for: " + abs_path_str;
    return false;
  }

  // Recursively process children
  for (const auto& child : prim.children()) {
    if (!ConvertPrimRecursive(child, prim_path, err)) {
      return false;
    }
  }

  return true;
}
```

### Step 3: Update batch_convert_usda.cc

```cpp
// Replace the TODO section with actual conversion

// Convert stage to specs
if (!writer.ConvertStageToSpecs(stage, &err)) {
  if (err_out) {
    *err_out = "Failed to convert stage: " + err;
  }
  if (verbose) std::cout << "FAILED (convert)\n";
  writer.Close();
  return false;
}
```

## Property Extraction Strategy

### Understanding TinyUSDZ Property Storage

Properties in TinyUSDZ are stored in typed prim data. For example:

**GeomMesh** (in usdGeom.hh):
```cpp
struct GeomMesh {
  Animatable<std::vector<value::point3f>> points;
  Animatable<std::vector<value::normal3f>> normals;
  Animatable<std::vector<int>> faceVertexCounts;
  Animatable<std::vector<int>> faceVertexIndices;
  // ... etc
};
```

**Xform** (in usdGeom.hh):
```cpp
struct Xform {
  std::vector<XformOp> xformOps;
  // Properties are in xformOps
};
```

### Extraction Approach

Two options:

**Option A: Reflection/Introspection**
- Use TinyUSDZ's value::Value system
- Iterate through all properties
- Convert each to Field

**Option B: Manual Extraction**
- Check prim type (prim.is<GeomMesh>())
- Cast to specific type (prim.as<GeomMesh>())
- Manually extract each property
- More work but more control

**Recommended**: Start with Option B for common types (Xform, Mesh, Sphere, etc.), then expand

### Example Property Extraction

```cpp
bool ExtractPropertiesFromPrim(
  const Prim& prim,
  tcrate::FieldValuePairVector& fields,
  std::string* err
) {
  // Check for Xform
  if (prim.is<Xform>()) {
    const Xform* xform = prim.as<Xform>();
    if (!xform) return true; // Skip if cast fails

    // Extract xformOps
    for (const auto& xformOp : xform->xformOps) {
      tcrate::CrateValue value;

      if (xformOp.op_type == XformOp::OpType::Translate) {
        // Extract translate value
        // value.Set(xformOp.value);  // Needs value type conversion

        std::string field_name = "xformOp:translate";
        fields.push_back({field_name, value});
      }
      // ... handle other op types
    }
  }

  // Check for GeomMesh
  else if (prim.is<GeomMesh>()) {
    const GeomMesh* mesh = prim.as<GeomMesh>();
    if (!mesh) return true;

    // Extract points
    if (mesh->points.has_value()) {
      tcrate::CrateValue value;
      // value.Set(mesh->points.get_value());
      fields.push_back({"points", value});
    }

    // ... extract other mesh properties
  }

  // ... handle other prim types

  return true;
}
```

## Value Type Conversion Matrix

| TinyUSDZ Type | Crate Value Type | Notes |
|---------------|------------------|-------|
| int32_t | ValueTypeInt | Direct |
| int64_t | ValueTypeInt64 | Direct |
| float | ValueTypeFloat | Direct |
| double | ValueTypeDouble | Direct |
| bool | ValueTypeBool | Direct |
| std::string | ValueTypeString | Via string index |
| value::token | ValueTypeToken | Via token index |
| value::float3 | ValueTypeFloat3 | 3-element array |
| value::double3 | ValueTypeDouble3 | 3-element array |
| value::matrix4d | ValueTypeMatrix4d | 16-element array |
| std::vector<T> | ValueTypeXArray | Array of T |
| Animatable<T> | Special handling | Has timeSamples |

## Testing Strategy

### Unit Tests

1. **test_stage_conversion_empty.cc**
   - Empty stage
   - Verify only root spec created

2. **test_stage_conversion_single_prim.cc**
   - Single Xform prim
   - Verify prim spec created with correct path

3. **test_stage_conversion_hierarchy.cc**
   - Parent-child hierarchy
   - Verify all prims created with correct paths

4. **test_stage_conversion_simple_attr.cc**
   - Prim with one attribute
   - Verify attribute field added

5. **test_stage_conversion_complex.cc**
   - Multiple prims with attributes
   - Verify complete conversion

### Integration Tests

1. Convert all test USDA files
2. Validate with OpenUSD
3. Compare with reference USDC files
4. Round-trip test (USDA → USDC → USDA comparison)

## Estimated Effort

| Phase | Complexity | Estimated Time |
|-------|------------|----------------|
| Phase 1: Basic Prim Conversion | Medium | 4-6 hours |
| Phase 2: Basic Attributes | High | 8-12 hours |
| Phase 3: Advanced Features | Very High | 20-30 hours |
| Phase 4: Testing & Validation | Medium | 6-10 hours |
| **Total** | | **38-58 hours** |

## Challenges and Risks

### Technical Challenges

1. **Property Extraction Complexity**
   - TinyUSDZ has many prim types (50+)
   - Each type has different properties
   - Need comprehensive type handling

2. **Value Type Conversion**
   - Many USD value types
   - Animatable<T> adds complexity
   - Time samples are complex

3. **Metadata Handling**
   - Many metadata types
   - Some are optional
   - Need to handle all correctly

4. **Composition Arcs**
   - References, payloads, inherits, specializes
   - Complex USD feature
   - May need composition system integration

### Mitigations

1. **Incremental Development**
   - Start with common types only
   - Expand gradually
   - Test at each step

2. **Reference Implementation**
   - Study OpenUSD source code
   - Use existing USDC files as reference
   - Validate against OpenUSD continuously

3. **Documentation**
   - Document each conversion rule
   - Create conversion matrix
   - Add inline comments

## Next Steps (Immediate)

1. **Implement Phase 1 (Basic Prim Conversion)**
   - Add ConvertStageToSpecs() method
   - Implement ConvertPrimRecursive()
   - Test with simple hierarchies

2. **Create Test Infrastructure**
   - Add unit tests
   - Create test USDA files
   - Set up validation pipeline

3. **Validate Progress**
   - Test with cube-with-xform.usda
   - Verify prim hierarchy is correct
   - Check that OpenUSD can read it

4. **Document Findings**
   - Update this plan based on discoveries
   - Document issues encountered
   - Track progress

## References

- TinyUSDZ source: `src/prim-types.hh`, `src/stage.hh`, `src/usdGeom.hh`
- OpenUSD source: `pxr/usd/sdf/spec.h`, `pxr/usd/sdf/crateFile.cpp`
- Crate Format: Already documented in previous reports
- USD Spec: https://openusd.org/release/spec.html

## Conclusion

Implementing full Stage-to-Crate conversion is a **substantial engineering effort** requiring:
- Deep understanding of both TinyUSDZ and USD Crate formats
- Comprehensive type conversion system
- Extensive testing and validation

**Recommended Approach**:
- Implement incrementally (Phase 1 → 2 → 3)
- Test thoroughly at each phase
- Focus on common use cases first
- Expand to advanced features later

**Current Achievement**:
- ✅ Core crate format is solid
- ✅ 99.35% validation success on minimal files
- ✅ Foundation is ready for content conversion

The crate-writer is production-ready for its current scope (minimal valid files). The next phase requires dedicated development time to build the complete conversion system.
