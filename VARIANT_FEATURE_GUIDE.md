# Tydra RenderScene Variant Support - Feature Guide

## Overview

This guide explains how to use the new variant support feature in TinyUSDZ's Tydra RenderScene. Variants allow you to define multiple options within a single USD/USDC/USDZ file (e.g., different colors, LOD levels, materials) and switch between them efficiently at runtime.

The implementation is inspired by glTF's `KHR_materials_variants` extension while supporting USD's more complex nested variant hierarchies.

## Quick Start

### 1. Define Variants in USD

Create a USDA file with variants:

```usda
#usda 1.0

def Xform "Car" (
    variantSets = ["color"]
    variants = {
        string color = "red"
    }
)
{
    variantSet "color" = {
        "red" {
            def Material "RedMaterial" { }
        }
        "blue" {
            def Material "BlueMaterial" { }
        }
        "green" {
            def Material "GreenMaterial" { }
        }
    }
}
```

### 2. Load and Convert

```cpp
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/variant-support.hh"

// Load USD file
tinyusdz::Stage stage;
std::string err, warn;
bool ok = tinyusdz::LoadUSDFromFile("car.usda", &stage, &warn, &err);

// Convert to RenderScene
tinyusdz::tydra::RenderSceneConverter converter;
tinyusdz::tydra::RenderScene render_scene;
// ... conversion code ...
```

### 3. Query Variants

```cpp
// Check available variants
for (const auto& group : render_scene.variant_groups) {
    std::cout << "Prim: " << group.prim_path << "\n";
    for (const auto& vset : group.variant_sets) {
        std::cout << "  VariantSet: " << vset.name << "\n";
        for (const auto& opt : vset.options) {
            std::cout << "    Option: " << opt.name << "\n";
        }
    }
}
```

### 4. Switch Variants

```cpp
// Select a specific variant
VariantSelection selection{
    variant_set_name: "color",
    variant_option_name: "red",
    prim_path: "/Car"
};

render_scene.active_selections.push_back(selection);

// Now render with selected variant
// (Apply material overrides, visibility changes, etc.)
```

## Architecture

### Core Data Structures

#### VariantOption
Represents a single option within a variant set.

```cpp
struct VariantOption {
    std::string name;                    // "red", "blue", etc.
    std::string description;             // Optional description
    std::vector<int32_t> mesh_ids;      // Meshes for this option
    std::vector<int32_t> material_ids;  // Materials for this option
    std::vector<int32_t> node_ids;      // Nodes for this option
    std::vector<int32_t> animation_ids; // Animations for this option
    std::map<std::string, std::string> property_overrides;
    std::vector<std::shared_ptr<VariantSet>> nested_variant_sets;
};
```

#### VariantSet
A set of mutually exclusive options.

```cpp
struct VariantSet {
    std::string name;                    // "color", "lod", etc.
    std::vector<VariantOption> options;  // Available options
    int32_t default_option_index{0};     // Which is default
    int32_t parent_prim_id{-1};          // For nested variants
    std::string parent_variant_option_name;
};
```

#### VariantGroup
All variants for a specific prim.

```cpp
struct VariantGroup {
    std::string prim_path;               // USD path like "/Car"
    std::vector<VariantSet> variant_sets;
    int32_t affected_node_id{-1};        // Primary affected node
    std::vector<int32_t> secondary_node_ids;
};
```

#### VariantSelection
Current active selection state.

```cpp
struct VariantSelection {
    std::string variant_set_name;
    std::string variant_option_name;
    std::string prim_path;
};
```

### Integration with RenderScene

The `RenderScene` class now includes variant support:

```cpp
class RenderScene {
    // ... existing fields ...

    // New variant support fields
    std::vector<VariantGroup> variant_groups;
    std::vector<VariantSelection> active_selections;
    std::map<std::string, int32_t> variant_group_map;
};
```

## Usage Examples

### Example 1: Material Variants (2-Level)

**USD Structure:**
```usda
def Xform "Vehicle" (
    variantSets = ["material"]
)
{
    variantSet "material" = {
        "pbr" { /* PBR material setup */ }
        "cartoon" { /* Cartoon material setup */ }
        "wireframe" { /* Wireframe material setup */ }
    }
}
```

**Code:**
```cpp
// Get variants
const VariantGroup* vg = FindVariantGroup("/Vehicle");
if (vg) {
    for (const auto& vset : vg->variant_sets) {
        if (vset.name == "material") {
            std::cout << "Available materials:\n";
            for (const auto& opt : vset.options) {
                std::cout << "  - " << opt.name << "\n";
            }
        }
    }
}

// Switch to cartoon material
SelectVariant("/Vehicle", "material", "cartoon");
```

### Example 2: Nested Variants (3-Level)

**USD Structure:**
```usda
def Xform "Building" (
    variantSets = ["style"]
)
{
    variantSet "style" = {
        "modern" {
            variantSet "complexity" = {
                "simple" { /* Simple modern geometry */ }
                "detailed" { /* Detailed modern geometry */ }
            }
        }
        "classic" {
            variantSet "complexity" = {
                "simple" { /* Simple classic geometry */ }
                "ornate" { /* Ornate classic geometry */ }
            }
        }
    }
}
```

**Code:**
```cpp
// Select modern style
SelectVariant("/Building", "style", "modern");

// Then select complexity level
SelectVariant("/Building", "complexity", "detailed");
```

### Example 3: LOD (Level of Detail) Variants

**USD Structure:**
```usda
def Mesh "Character" (
    variantSets = ["lod"]
    variants = { string lod = "medium" }
)
{
    variantSet "lod" = {
        "high" {
            # High-poly mesh with many vertices
        }
        "medium" {
            # Medium-poly mesh (default)
        }
        "low" {
            # Low-poly mesh for distance/mobile
        }
    }
}
```

**Code:**
```cpp
// Get statistics
VariantStatistics stats = GetVariantStatistics(render_scene);
std::cout << "Total variant groups: " << stats.num_variant_groups << "\n";
std::cout << "Total variant sets: " << stats.num_variant_sets << "\n";
std::cout << "Total options: " << stats.num_variant_options << "\n";

// Select LOD based on distance or platform
if (is_mobile) {
    SelectVariant("/Character", "lod", "low");
} else if (is_close) {
    SelectVariant("/Character", "lod", "high");
} else {
    SelectVariant("/Character", "lod", "medium");
}
```

### Example 4: Property Overrides

**USD Structure:**
```usda
def Xform "Sphere" (
    variantSets = ["appearance"]
)
{
    variantSet "appearance" = {
        "shiny" {
            float metallic = 1.0
            float roughness = 0.1
        }
        "matte" {
            float metallic = 0.0
            float roughness = 0.9
        }
    }
}
```

## Performance Considerations

- **Lazy Loading**: Variants reference existing meshes/materials rather than duplicating
- **Fast Switching**: O(1) variant selection via hash map lookup
- **Memory Efficient**: Only metadata is stored; geometry/texture data is shared
- **No Runtime Overhead**: When variant support isn't used, there's zero overhead

## API Reference

### VariantManager (DefaultVariantManager)

```cpp
// Query APIs
const VariantGroup* FindVariantGroup(const std::string& prim_path);
const VariantSet* FindVariantSet(const std::string& prim_path,
                                 const std::string& variant_set_name);
const VariantOption* FindVariantOption(const std::string& prim_path,
                                      const std::string& variant_set_name,
                                      const std::string& variant_option_name);

// Selection APIs
bool SelectVariant(const std::string& prim_path,
                  const std::string& variant_set_name,
                  const std::string& variant_option_name);
bool SelectVariantByIndex(const std::string& prim_path,
                         const std::string& variant_set_name,
                         uint32_t option_index);

// State APIs
const std::vector<VariantSelection>& GetAllSelections();
nonstd::optional<VariantSelection> GetCurrentSelection(
    const std::string& prim_path,
    const std::string& variant_set_name);
void ResetToDefaults();

// Utility APIs
bool HasVariants(const std::string& prim_path);
bool VariantSetExists(const std::string& prim_path,
                     const std::string& variant_set_name);
VariantStatistics GetStatistics();
```

## Testing

Tests are located in `tests/feat/nestedVariantSet/`:

- `test_variant_api.cpp` - Basic API functionality tests
- `variant_feature_test.cc` - Comprehensive feature tests
- Various USDA test files with different variant configurations

Run tests:
```bash
cd tests/feat/nestedVariantSet
g++ -std=c++17 -Wall test_variant_api.cpp -o test_variant_api
./test_variant_api
```

## Implementation Status

| Feature | Status | Notes |
|---------|--------|-------|
| Basic variant structures | ✅ Complete | VariantOption, VariantSet, VariantGroup |
| VariantSelection API | ✅ Complete | Query and set variants |
| Nested variants | ✅ Supported | Up to arbitrary depth |
| Property overrides | ✅ Supported | Key-value string maps |
| Material variants | ✅ Supported | Via material_ids |
| Geometry variants | ✅ Supported | Via mesh_ids |
| Animation variants | ✅ Supported | Via animation_ids |
| Variant conversion | 🔄 In Progress | RenderSceneConverter integration |
| Variant flattening | 🔄 In Progress | Scene flattening for rendering |
| Dynamic variant switching | ✅ Ready | Via SelectVariant API |

## Future Enhancements

1. **Variant Blending**: Smooth interpolation between variants
2. **Variant Scripts**: Custom callbacks on variant selection
3. **Variant Animations**: Animated transitions between variants
4. **Variant Baking**: Pre-bake variants into separate files
5. **glTF Export**: Export variants as KHR_materials_variants
6. **Variant Queries**: Advanced filtering and search

## Files

- `src/tydra/variant-support.hh` - Core data structures and API
- `src/tydra/variant-support.cc` - DefaultVariantManager implementation
- `src/tydra/variant-converter.hh` - USD to RenderScene conversion
- `src/tydra/variant-converter.cc` - Variant conversion implementation
- `src/tydra/render-data.hh` - Updated with variant fields
- `tests/feat/nestedVariantSet/` - Test files and USDA examples

## Related Documentation

- `src/tydra/variant-support-design.md` - Detailed design document
- `CLAUDE.md` - Project overview
- USD Official Documentation: https://openusd.org/

## Support

For issues, questions, or contributions related to variant support:
1. Check the design document: `variant-support-design.md`
2. Review test files for usage examples
3. Examine USDA test files for USD variant structures
