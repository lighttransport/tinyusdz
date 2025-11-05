# Layer/PrimSpec Conversion Implementation Status

**Date**: 2025-11-06
**Commit**: 8842b7da
**Status**: 🚧 Framework Complete, Implementation Pending

## Overview

This document tracks the implementation status of Layer/PrimSpec-based USD Crate serialization. The framework has been added to crate-writer, but full implementation is pending due to TinyUSDZ API mismatches discovered during development.

## Completed Work

### 1. API Framework ✅

**File**: `include/crate-writer.hh`

Added public method:
```cpp
bool ConvertLayerToSpecs(const Layer& layer, std::string* err = nullptr);
```

Added 5 private helper methods:
```cpp
bool ConvertPrimSpecRecursive(const PrimSpec& primspec, const Path& parent_path, std::string* err);
bool ConvertPropertyToFields(const std::string& prop_name, const Property& prop,
                             crate::FieldValuePairVector& fields, std::string* err);
bool ConvertAttributeToFields(const std::string& attr_name, const Attribute& attr,
                              crate::FieldValuePairVector& fields, std::string* err);
bool ConvertRelationshipToFields(const std::string& rel_name, const Relationship& rel,
                                 crate::FieldValuePairVector& fields, std::string* err);
bool ConvertConnectionToFields(const std::string& conn_name, const Attribute& attr,
                               crate::FieldValuePairVector& fields, std::string* err);
```

### 2. Design Documentation ✅

**File**: `LAYER_PRIMSPEC_DESIGN.md`

Comprehensive architecture document including:
- Data model comparison (Layer/PrimSpec vs Stage)
- Conversion strategy with code examples
- Benefits analysis table
- Testing strategy
- Phase-by-phase implementation plan

### 3. Stub Implementation ✅

**File**: `src/stage-converter.cc`

All methods stubbed out to:
- Compile cleanly without errors
- Document API mismatches in TODO comments
- Return proper error messages
- Use `(void)` to suppress unused parameter warnings

### 4. Build Integration ✅

- Added includes: `layer.hh`, `pprinter.hh`
- Clean compilation with no regressions
- Existing Stage-based conversion unaffected

## API Mismatches Discovered

During implementation, we discovered significant differences between the design expectations and TinyUSDZ's actual API:

### Attribute API Issues

| Expected | Actual | Status |
|----------|--------|--------|
| `attr.get_value()` | `attr.get_var()` returns `PrimVar` | ⚠️ Needs investigation |
| `attr.metas().custom` | `attr.metas().customData` | ⚠️ Different field name |
| `attr.is_custom()` | No such method | ⚠️ Use metas instead |
| `attr.has_value()` | Exists on PrimVar | ✅ Available |

### PrimVar API Issues

| Expected | Actual | Status |
|----------|--------|--------|
| `pvar.get_default()` | `pvar.value_raw()` | ⚠️ Different method |
| Direct value access | Need to understand Animatable<T> | ⚠️ Complex API |
| TimeSamples extraction | Embedded in PrimVar | ⚠️ Needs investigation |

### Relationship API Issues

| Expected | Actual | Status |
|----------|--------|--------|
| `rel.is_value_blocked()` | `rel.is_blocked()` | ✅ Found correct name |
| `rel.get_noload_hint()` | No such method | ❌ Not exposed |
| `rel.targetPath()` | ✅ Available | ✅ Works |
| `rel.targetPathVector()` | ✅ Available | ✅ Works |

### CrateValue API Issues

| Expected | Actual | Status |
|----------|--------|--------|
| `Set(const Path&)` | Requires rvalue or copy | ⚠️ Need std::move() or copy |
| `Set(const vector<Path>&)` | Requires rvalue or copy | ⚠️ Same issue |
| Inline path encoding | Works with copies | ✅ Workaround available |

### Property API

| Expected | Actual | Status |
|----------|--------|--------|
| `prop.is_empty_attrib()` | `prop.is_empty()` | ✅ Found correct name |
| `prop.is_attribute()` | ✅ Available | ✅ Works |
| `prop.is_relationship()` | ✅ Available | ✅ Works |
| `prop.is_attribute_connection()` | ✅ Available | ✅ Works |

## Implementation Blockers

### 1. PrimVar Value Extraction ⚠️

**Issue**: Need to understand how to extract typed values from PrimVar

**Investigation needed**:
```cpp
const primvar::PrimVar& pvar = attr.get_var();
// How to get the actual value?
// - pvar.value_raw() returns value::Value&
// - How to convert value::Value to CrateValue?
// - How to handle Animatable<T> for timeSamples?
```

**Current workaround**: Use existing `ConvertValue(value::Value, CrateValue)` helper

### 2. Custom Attribute Flag 🔍

**Issue**: AttrMetas structure doesn't have simple `custom` bool field

**Investigation needed**:
```cpp
const AttrMeta& metas = attr.metas();
// Expected: metas.custom
// Actual: metas.customData exists but unclear semantics
// Need to understand how TinyUSDZ represents custom attributes
```

### 3. Path Reference Semantics ⚠️

**Issue**: CrateValue::Set() requires rvalue references for Path types

**Current solution**:
```cpp
// Make explicit copies before calling Set()
Path target = rel.targetPath();  // Copy
crate::CrateValue value;
value.Set(target);  // Now works
```

**Optimization potential**: Use std::move() if we own the Path

### 4. Relationship as Separate Specs 🚧

**Design issue**: Relationships should create separate `SpecType::Relationship` specs, not just fields

**Current approach**: Adding relationship fields to parent prim spec (incorrect)

**Correct approach**:
```cpp
// Create separate spec for relationship
Path rel_path = prim_path.AppendProperty(rel_name);
AddSpec(rel_path, SpecType::Relationship, rel_fields, err);
```

### 5. Attribute Connections as Separate Specs 🚧

**Design issue**: Attribute connections should create `SpecType::Connection` specs

**Current approach**: Adding connection fields to parent prim spec (incorrect)

**Correct approach**:
```cpp
// Create separate spec for connection
Path conn_path = prim_path.AppendProperty(attr_name).AppendProperty("connect");
AddSpec(conn_path, SpecType::Connection, conn_fields, err);
```

## Next Steps for Implementation

### Phase 1: Fix Core API Issues

1. **Study PrimVar API** (priority: high)
   - Read `src/primvar.hh` for PrimVar structure
   - Understand `value_raw()` vs default value
   - Learn how Animatable<T> works for timeSamples
   - Test value extraction with sample data

2. **Clarify AttrMeta Structure** (priority: high)
   - Read `src/prim-types.hh` for AttrMetas definition
   - Understand `customData` semantics
   - Determine how to check if attribute is custom

3. **Test Path Handling** (priority: medium)
   - Create test case with Path copying
   - Measure performance impact
   - Consider using std::move() where possible

### Phase 2: Fix Spec Architecture

4. **Implement Proper Relationship Specs** (priority: high)
   - Create separate spec for each relationship
   - Use `SpecType::Relationship`
   - Add targetPaths field to relationship spec

5. **Implement Proper Connection Specs** (priority: high)
   - Create separate spec for each connection
   - Use `SpecType::Connection`
   - Add connectionPaths field to connection spec

### Phase 3: Complete Property Conversion

6. **Implement ConvertAttributeToFields()** (priority: high)
   - Extract type name ✅ (already working)
   - Extract default value (fix PrimVar API)
   - Extract timeSamples (understand Animatable<T>)
   - Extract variability ✅ (already working)
   - Extract custom flag (fix AttrMeta)

7. **Implement ConvertRelationshipToFields()** (priority: medium)
   - Extract targetPaths ✅ (concept working, needs spec fix)
   - Extract list op qualifier (TODO)
   - Handle value blocks (use is_blocked())

8. **Implement ConvertConnectionToFields()** (priority: medium)
   - Extract connectionPaths ✅ (concept working, needs spec fix)
   - Extract type name ✅ (already working)

### Phase 4: Metadata and Composition

9. **Extract PrimSpec Metadata** (priority: low)
   - Study PrimMeta structure
   - Convert metadata to fields
   - Add to prim spec

10. **Support Composition Arcs** (priority: low)
    - References
    - Payloads
    - Inherits
    - Variants

### Phase 5: Testing

11. **Unit Tests**
    - Basic PrimSpec with properties
    - Relationship serialization
    - Connection serialization

12. **Round-Trip Tests**
    - USDA→Layer→USDC→Layer
    - Compare original vs loaded

13. **Integration Tests**
    - Test with existing 310 file corpus
    - Verify no regressions

## Benefits of Layer/PrimSpec Approach

Once implemented, this approach will provide:

| Feature | Stage Model | Layer/PrimSpec Model |
|---------|-------------|---------------------|
| **Property Access** | Type-specific extraction (GeomMesh, Xform, etc.) | Generic name→value map |
| **Relationships** | Difficult to extract from Prim | First-class Property type |
| **Connections** | Not easily accessible | Direct Attribute field |
| **Composition** | Requires Stage traversal | Direct PrimSpec fields |
| **Variants** | Complex extraction | Structured variantSets map |
| **Metadata** | Type-dependent | Uniform PrimMeta access |
| **Code Complexity** | High (many type handlers) | Low (generic handlers) |
| **Maintainability** | Difficult | Easy |

## Code Size Comparison

### Stage-Based Approach (Current)
- Type-specific handlers for ~20 prim types
- ~500 lines of extraction code
- Difficult to add new types
- Relationships not supported

### Layer/PrimSpec Approach (Target)
- Generic property handler
- ~200 lines of conversion code (estimated)
- Easy to add new types (automatic)
- Full relationship support

## References

- **Design Document**: `LAYER_PRIMSPEC_DESIGN.md`
- **TinyUSDZ Headers**:
  - `src/layer.hh` - Layer class
  - `src/prim-types.hh` - PrimSpec, Property, Attribute, Relationship
  - `src/primvar.hh` - PrimVar for value storage
  - `src/pprinter.hh` - to_string() utilities
- **OpenUSD Reference**: `pxr/usd/sdf/spec.h`

## Timeline Estimate

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| Phase 1: API Research | 2-3 hours | None |
| Phase 2: Spec Architecture | 4-6 hours | Phase 1 |
| Phase 3: Property Conversion | 8-10 hours | Phase 1, 2 |
| Phase 4: Metadata/Composition | 6-8 hours | Phase 3 |
| Phase 5: Testing | 4-6 hours | Phase 4 |
| **Total** | **24-33 hours** | Sequential |

## Conclusion

The Layer/PrimSpec conversion framework is architecturally sound and provides a cleaner, more maintainable approach than Stage-based conversion. However, full implementation requires careful study of TinyUSDZ's internal APIs, particularly around PrimVar value extraction and metadata handling.

The stub implementation allows the crate-writer to compile cleanly while we investigate these API details. Once the API mismatches are resolved, the actual implementation should be straightforward following the patterns in `LAYER_PRIMSPEC_DESIGN.md`.
