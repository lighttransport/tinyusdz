# LightUSD Status

## Overview

LightUSD is a lightweight, dependency-free USD library implemented in C++17. It provides parsing, writing, and validation capabilities for USD files.

## Recent Changes

### C++17 Upgrade
- Upgraded from C++14 to C++17
- Enables use of `std::optional`, `std::string_view`, `std::variant`, and `if constexpr`

### Primvar Support
New files: `include/lightusd/primvar.hh`, `src/primvar.cc`

- **Interpolation modes**: Constant, Uniform, Vertex, FaceVarying, Varying
- **Primvar class**: Encapsulates primvar attributes with:
  - Name and underlying attribute
  - Interpolation mode
  - Indexed primvar support (with indices array)
  - Element size for grouped data
  - Validation against geometry counts
- **PrimvarSet class**: Collection manager for primvars
- **Utility functions**: `extract_primvar()`, `extract_primvars()`, `flatten_indexed_primvar()`

### JSON Schema Validation System
New files: `include/lightusd/schema.hh`, `src/schema.cc`

- **JsonValue class**: Minimal, dependency-free JSON parser using `std::variant`
- **PropertySchema**: Defines property constraints (type, required, allowed values, min/max)
- **PrimSchema**: Defines prim type schemas with inheritance support
- **ValidationResult**: Structured validation output with severity levels (Error, Warning, Info)

### Schema Registry
New files: `include/lightusd/schema_registry.hh`, `src/schema_registry.cc`

- **SchemaRegistry singleton**: Central registry for prim schemas
- **User schema support**: `register_schema()` for custom prim types
- **Validation API**: `validate()`, `validate_stage()`, `validate_prim_tree()`
- **Schema inheritance resolution** with cycle detection

## Built-in Schemas (24 total)

### Base Schemas (Abstract)
- Imageable - Base for renderable primitives
- Boundable - Base for primitives with bounds
- Gprim - Base for geometric primitives
- Light - Base for lights

### Geometry Primitives
- Mesh - Polygonal mesh with subdivision support
- Points - Point cloud primitive
- BasisCurves - Curves primitive (hair, fur, etc.)
- Cube - Cube primitive
- Sphere - Sphere primitive
- Cylinder - Cylinder primitive
- Cone - Cone primitive
- Capsule - Capsule primitive

### Scene Organization
- Xform - Transformable group
- Scope - Grouping without transformation
- Camera - Camera with projection settings

### Materials
- Material - Material definition
- Shader - Shader node

### Lights
- SphereLight - Point/sphere light
- DistantLight - Directional light
- DomeLight - Environment light

### Skeletal Animation
- SkelRoot - Root for skeletal animation hierarchy
- Skeleton - Skeleton definition with joints and bind poses
- SkelAnimation - Animation data (translations, rotations, scales, blend shape weights)
- BlendShape - Blend shape deformation target

## Schema Property Details

### SkelAnimation Properties
| Property | Type | Required | Description |
|----------|------|----------|-------------|
| joints | token[] | Yes | Joint paths this animation targets |
| translations | float3[] | No | Joint local translation values |
| rotations | quatf[] | No | Joint local rotation values as quaternions |
| scales | float3[] | No | Joint local scale values |
| blendShapes | token[] | No | Blend shape targets to animate |
| blendShapeWeights | float[] | No | Blend shape weight values |

### BlendShape Properties
| Property | Type | Required | Description |
|----------|------|----------|-------------|
| offsets | vector3f[] | Yes | Per-point position offsets |
| normalOffsets | vector3f[] | No | Per-point normal offsets |
| pointIndices | int[] | No | Indices of affected points (sparse) |

## Usage Example

```cpp
#include "lightusd/lightusd.hh"

// Get schema registry
auto& registry = lightusd::SchemaRegistry::instance();

// Check if schema exists
if (registry.has_schema("SkelAnimation")) {
    // Get schema
    const auto* schema = registry.get_schema("SkelAnimation");

    // Validate a prim
    lightusd::Prim anim_prim("MyAnimation", "SkelAnimation");
    auto result = registry.validate(anim_prim);

    if (!result.valid) {
        for (const auto& issue : result.format_issues()) {
            std::cerr << issue << "\n";
        }
    }
}

// Register custom schema
const char* custom_schema = R"({
    "typeName": "MyCustomPrim",
    "properties": [
        {"name": "myProperty", "type": "float", "required": "required"}
    ]
})";
registry.register_schema(custom_schema, "user");
```

## Test Status

- All 111 tests passing
- 677 assertions verified
