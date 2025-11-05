# Layer/PrimSpec Serialization Design

**Date**: 2025-11-05
**Status**: 🚧 IN DEVELOPMENT
**Implementation**: Layer/PrimSpec→Crate conversion

## Overview

This document describes the design and implementation of Layer/PrimSpec-based serialization for the USD Crate writer. This approach provides a more direct mapping to USD's native spec model compared to the Stage-based approach.

## Architecture

### Data Models

#### TinyUSDZ Layer Model
```cpp
class Layer {
  std::unordered_map<std::string, PrimSpec> primspecs;  // Flat prim specs
  LayerMetas metas;                                      // Layer metadata
};

class PrimSpec {
  std::string name;                               // Prim name
  std::string typeName;                           // Type ("Xform", "Mesh", etc.)
  Specifier specifier;                            // def/over/class
  std::vector<PrimSpec> children;                 // Child prim specs
  std::map<std::string, Property> props;          // Properties (generic)
  PrimMeta metas;                                 // Metadata
  std::map<std::string, VariantSetSpec> variantSets;
  std::vector<Reference> references;
  std::vector<Payload> payloads;
};

class Property {
  enum Type { EmptyAttrib, Attrib, Relation, NoTargetsRelation, Connection };
  Attribute attrib;        // For attributes
  Relationship rel;        // For relationships
  std::string type_name;   // Value type name
  bool has_custom;         // Custom attribute flag
};
```

#### USD Crate Spec Model
```cpp
struct Spec {
  PathIndex path_index;        // Index into paths array
  FieldSetIndex fieldset_index; // Index into fieldsets array
  SpecType spec_type;          // Prim, Attribute, Relationship, etc.
};

struct Field {
  TokenIndex name;   // Field name (token)
  ValueRep value;    // Field value (inline or out-of-line)
};
```

### Conversion Strategy

```
Layer
  └─> For each PrimSpec in primspecs
       ├─> Create Prim Spec with SpecType::Prim
       │    ├─> Field: specifier = def/over/class
       │    ├─> Field: typeName = "Xform"/"Mesh"/etc.
       │    └─> Convert properties, metadata, composition
       │
       ├─> For each Property in props
       │    ├─> If Attribute:
       │    │    └─> Create Attribute Spec with SpecType::Attribute
       │    │         ├─> Field: typeName = "float3"/"int"/etc.
       │    │         ├─> Field: default = value
       │    │         ├─> Field: timeSamples = animated values
       │    │         └─> Field: variability/custom/etc.
       │    │
       │    ├─> If Relationship:
       │    │    └─> Create Relationship Spec with SpecType::Relationship
       │    │         ├─> Field: targetPaths = Path or PathVector
       │    │         └─> Field: noLoadHint/variability/etc.
       │    │
       │    └─> If Connection:
       │         └─> Create Connection Spec with SpecType::Connection
       │              ├─> Field: connectionPaths = Path or PathVector
       │              └─> Field: typeName
       │
       └─> Recurse into children
```

## Key Features

### 1. Generic Property Conversion

**Advantage**: No type-specific extraction code needed

```cpp
// PrimSpec already has properties as name→Property map
for (const auto& [prop_name, prop] : primspec.props()) {
  if (prop.is_attribute()) {
    // Generic attribute conversion
    ConvertAttributeToFields(prop_name, prop.get_attribute(), fields, err);
  } else if (prop.is_relationship()) {
    // Generic relationship conversion
    ConvertRelationshipToFields(prop_name, prop.get_relationship(), fields, err);
  } else if (prop.is_attribute_connection()) {
    // Attribute connection conversion
    ConvertConnectionToFields(prop_name, prop.get_attribute(), fields, err);
  }
}
```

### 2. Relationship Serialization

Relationships are first-class citizens in PrimSpec model:

```cpp
class Relationship {
  enum Type { DefineOnly, Path, PathVector, ValueBlock };
  Path targetPath;                    // Single target
  std::vector<Path> targetPathVector; // Multiple targets
  ListEditQual listOpQual;           // append/prepend/delete/etc.
};
```

**Serialization**:
```cpp
bool ConvertRelationshipToFields(const string& rel_name, const Relationship& rel, ...) {
  // 1. Create relationship spec for path "/Prim.relationshipName"
  Path rel_path = parent_path.AppendProperty(rel_name);
  AddSpec(rel_path, SpecType::Relationship, fields, err);

  // 2. Add target field
  if (rel.is_path()) {
    // Field: targetPaths = single Path
    CrateValue value;
    value.Set(rel.targetPath);
    fields.push_back({"targetPaths", value});
  } else if (rel.is_pathvector()) {
    // Field: targetPaths = PathVector
    CrateValue value;
    value.Set(rel.targetPathVector);
    fields.push_back({"targetPaths", value});
  }

  // 3. Add list op qualifier if needed
  if (rel.listOpQual != ListEditQual::ResetToExplicit) {
    // Field: listOpQual = append/prepend/delete/etc.
  }
}
```

### 3. Attribute Connection Serialization

Attribute connections represent `.connect` relationships:

```cpp
bool ConvertConnectionToFields(const string& conn_name, const Attribute& attr, ...) {
  // 1. Create connection spec for path "/Prim.attrName.connect"
  Path conn_path = parent_path.AppendProperty(conn_name);
  AddSpec(conn_path, SpecType::Connection, fields, err);

  // 2. Add connection paths
  if (attr.has_connection()) {
    // Field: connectionPaths = single Path
    CrateValue value;
    value.Set(attr.get_connection());
    fields.push_back({"connectionPaths", value});
  } else if (attr.has_connections()) {
    // Field: connectionPaths = PathVector
    CrateValue value;
    value.Set(attr.get_connections());
    fields.push_back({"connectionPaths", value});
  }

  // 3. Add type name
  CrateValue type_value;
  type_value.Set(GetOrCreateToken(attr.type_name()));
  fields.push_back({"typeName", type_value});
}
```

### 4. Composition Arcs Support

PrimSpec has direct access to composition structures:

```cpp
// References
for (const Reference& ref : primspec.get_references()) {
  // Create Reference value
  CrateValue ref_value;
  ref_value.Set(ref);
  fields.push_back({"references", ref_value});
}

// Payloads
for (const Payload& payload : primspec.get_payloads()) {
  // Create Payload value
  CrateValue payload_value;
  payload_value.Set(payload);
  fields.push_back({"payload", payload_value});
}

// Variants
for (const auto& [variant_name, variant_set] : primspec.variantSets()) {
  // Create VariantSet value
  CrateValue variant_value;
  variant_value.Set(variant_set);
  fields.push_back({"variants", variant_value});
}
```

## Implementation Plan

### Phase 1: Core Layer/PrimSpec Conversion (Current)

**Files**: `include/crate-writer.hh`, `src/stage-converter.cc`

1. ✅ Add `ConvertLayerToSpecs()` API declaration
2. ✅ Add private helper method declarations:
   - `ConvertPrimSpecRecursive()`
   - `ConvertPropertyToFields()`
   - `ConvertAttributeToFields()`
   - `ConvertRelationshipToFields()`
   - `ConvertConnectionToFields()`

3. 🚧 Implement `ConvertLayerToSpecs()`:
   ```cpp
   bool CrateWriter::ConvertLayerToSpecs(const Layer& layer, std::string* err) {
     // 1. Add PseudoRoot spec
     // 2. Convert layer metadata to fields
     // 3. For each primspec in layer.primspecs():
     //    - ConvertPrimSpecRecursive(primspec, Path("/"), err)
     return true;
   }
   ```

4. 🚧 Implement `ConvertPrimSpecRecursive()`:
   ```cpp
   bool CrateWriter::ConvertPrimSpecRecursive(
       const PrimSpec& primspec,
       const Path& parent_path,
       std::string* err) {

     // 1. Build prim path
     Path prim_path = parent_path.AppendChild(primspec.name());

     // 2. Create fields for this prim
     crate::FieldValuePairVector fields;

     // Add specifier
     CrateValue spec_value;
     spec_value.Set(primspec.specifier());
     fields.push_back({"specifier", spec_value});

     // Add typeName if present
     if (!primspec.typeName().empty()) {
       CrateValue type_value;
       type_value.Set(GetOrCreateToken(primspec.typeName()));
       fields.push_back({"typeName", type_value});
     }

     // 3. Convert properties
     for (const auto& [prop_name, prop] : primspec.props()) {
       ConvertPropertyToFields(prop_name, prop, fields, err);
     }

     // 4. Convert metadata
     // TODO: Extract from primspec.metas()

     // 5. Convert composition arcs
     // TODO: references, payloads, variants

     // 6. Add spec to file
     AddSpec(prim_path, SpecType::Prim, fields, err);

     // 7. Recurse into children
     for (const PrimSpec& child : primspec.children()) {
       ConvertPrimSpecRecursive(child, prim_path, err);
     }

     return true;
   }
   ```

### Phase 2: Property Conversion

5. 🚧 Implement `ConvertPropertyToFields()`:
   - Dispatch based on Property::Type
   - Call appropriate converter

6. 🚧 Implement `ConvertAttributeToFields()`:
   - Extract type name
   - Extract default value
   - Extract timeSamples if present
   - Extract variability, custom flag

7. 🚧 Implement `ConvertRelationshipToFields()`:
   - Create Relationship spec
   - Add targetPaths (Path or PathVector)
   - Add list op qualifier

8. 🚧 Implement `ConvertConnectionToFields()`:
   - Create Connection spec
   - Add connectionPaths
   - Add type information

### Phase 3: Advanced Features

9. ⏳ Implement metadata extraction from PrimSpec.metas
10. ⏳ Implement composition arcs (references, payloads, inherits)
11. ⏳ Implement variant support
12. ⏳ Add comprehensive testing

## Testing Strategy

### Unit Tests

```cpp
// Test 1: Basic PrimSpec with properties
Layer layer;
PrimSpec prim_spec(Specifier::Def, "Xform", "World");
prim_spec.props()["xformOp:translate"] = Property(Attribute(...));
layer.add_primspec("World", prim_spec);

CrateWriter writer("test.usdc");
writer.ConvertLayerToSpecs(layer);
writer.Finalize();

// Validate with TinyUSDZ reader
```

### Integration Tests

```cpp
// Test 2: Relationship serialization
PrimSpec prim(Specifier::Def, "Material", "mat");
Relationship rel;
rel.set(Path("/Material/shader"));
prim.props()["surface"] = Property(rel);
// ... convert and validate
```

### Round-trip Tests

```cpp
// Test 3: Layer→USDC→Layer round-trip
Layer original = LoadLayer("input.usda");
CrateWriter writer("output.usdc");
writer.ConvertLayerToSpecs(original);
writer.Finalize();

Layer loaded = LoadLayer("output.usdc");
// Compare original vs loaded
```

## Benefits Over Stage-based Approach

| Feature | Stage Model | Layer/PrimSpec Model |
|---------|-------------|---------------------|
| Property access | Type-specific extraction | Generic name→value map |
| Relationships | Difficult to extract | First-class support |
| Connections | Not easily accessible | Direct attribute field |
| Composition | Requires traversal | Direct access |
| Variants | Complex extraction | Structured data |
| Metadata | Type-dependent | Uniform access |
| Code complexity | High (type-specific) | Low (generic) |
| Maintainability | Difficult | Easy |

## Current Status

**Implemented**:
- ✅ API declarations in header
- ✅ Helper method declarations

**In Progress**:
- 🚧 ConvertLayerToSpecs() implementation
- 🚧 ConvertPrimSpecRecursive() implementation
- 🚧 Property conversion methods

**Planned**:
- ⏳ Relationship serialization
- ⏳ Connection serialization
- ⏳ Composition arcs
- ⏳ Variant support
- ⏳ Metadata extraction

## References

- `src/layer.hh` - Layer class definition
- `src/prim-types.hh` - PrimSpec, Property, Relationship definitions
- `src/crate-format.hh` - USD Crate format structures
- OpenUSD `pxr/usd/sdf/spec.h` - Reference SdfSpec implementation
