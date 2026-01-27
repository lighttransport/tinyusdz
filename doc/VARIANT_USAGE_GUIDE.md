# USD Variant Support in TinyUSDZ

This guide covers how to work with USD variants using TinyUSDZ's comprehensive variant support system.

## Table of Contents

1. [Overview](#overview)
2. [Quick Start](#quick-start)
3. [Architecture](#architecture)
4. [API Reference](#api-reference)
5. [CLI Tools](#cli-tools)
6. [Python Bindings](#python-bindings)
7. [Best Practices](#best-practices)
8. [Examples](#examples)

## Overview

USD variants are a powerful feature that allow you to create multiple, mutually exclusive representations of a USD asset. TinyUSDZ provides comprehensive support for:

- **Variant Extraction**: Convert USD variant structures into RenderScene variant groups
- **Variant Analysis**: Query and analyze variant complexity metrics
- **Variant Selection**: Dynamically select different variants for rendering
- **Nested Variants**: Support for USD's nested variant hierarchy (up to 3 levels)
- **Change Tracking**: Track what content changes when variants are selected

### Common Use Cases

- **Level of Detail (LOD)**: Provide different geometry complexity levels for different viewing distances
- **Material Variants**: Offer different material or color options for assets
- **Geometry Variations**: Support different shapes, configurations, or assembly options
- **Asset Variants**: Multiple versions of characters, props, or environments
- **Locale Variants**: Different versions for different regions or languages

## Quick Start

### Using the Command-Line Tools

#### List variants in a USD file:

```bash
variant-lister model.usda
variant-lister model.usda --summary
variant-lister model.usda --json
```

#### Analyze variant complexity:

```bash
variant-analyzer model.usda
variant-analyzer model.usda --detailed
variant-analyzer model.usda --json
```

### Using C++ API

```cpp
#include "tinyusdz.hh"
#include "tydra/variant-support.hh"
#include "tydra/variant-converter.hh"

using namespace tinyusdz;
using namespace tinyusdz::tydra;

// Load USD file
Stage stage;
std::string err;
bool ret = LoadUSDFromFile("model.usda", &stage, nullptr, &err);

// Extract variants
RenderScene scene;
VariantConverter converter;
if (converter.ConvertVariants(stage, &scene, &err)) {
    // Work with variants
    DefaultVariantManager manager;
    manager.SetVariantGroups(scene.variant_groups);

    // Select a variant
    manager.SelectVariant(0, "color", "red");

    // Get statistics
    auto stats = manager.GetStatistics();
    std::cout << "Found " << stats.num_variant_groups << " variant groups\n";
}
```

### Using Python API

```python
import tinyusdz

# Load USD file
stage = tinyusdz.load_usd_from_file("model.usda")

# Extract variants
scene = tinyusdz.tydra.RenderScene()
converter = tinyusdz.tydra.VariantConverter()
converter.convert_variants(stage, scene)

# Create manager and select variants
manager = tinyusdz.tydra.DefaultVariantManager()
manager.set_variant_groups(scene.variant_groups)

# List available options
for group in scene.variant_groups:
    print(f"Prim: {group.prim_path}")
    for var_set in group.variant_sets:
        options = [opt.name for opt in var_set.options]
        print(f"  {var_set.name}: {options}")

# Select a variant
manager.select_variant(0, "color", "red")
```

## Architecture

### Core Data Structures

#### VariantOption
Represents a single variant option within a variant set.

```cpp
struct VariantOption {
    std::string name;                                    // Option name
    std::string description;                             // Optional description
    std::vector<int32_t> mesh_ids;                       // Associated mesh IDs
    std::vector<int32_t> material_ids;                   // Associated material IDs
    std::vector<int32_t> node_ids;                       // Affected node IDs
    std::vector<int32_t> animation_ids;                  // Animation IDs
    std::map<std::string, std::string> property_overrides;  // Property changes
    std::vector<std::shared_ptr<VariantSet>> nested_variant_sets;  // Nested variants
};
```

#### VariantSet
A set of mutually exclusive variant options.

```cpp
struct VariantSet {
    std::string name;                          // Set name (e.g., "color", "lod")
    std::vector<VariantOption> options;        // Available options
    int32_t default_option_index{0};          // Default option index
    int32_t parent_prim_id{-1};               // Parent for nested variants
    std::string parent_variant_option_name;    // Parent option name
};
```

#### VariantGroup
All variant sets for a specific prim/node.

```cpp
struct VariantGroup {
    std::string prim_path;                     // USD prim path
    std::vector<VariantSet> variant_sets;      // All variant sets for this prim
    int32_t affected_node_id{-1};              // Primary node affected
    std::vector<int32_t> secondary_node_ids;   // Secondary nodes affected
};
```

#### VariantSelection
Records which variant option is currently selected.

```cpp
struct VariantSelection {
    int32_t variant_group_id;                  // Which group
    int32_t variant_set_id;                    // Which set in the group
    int32_t selected_option_index;             // Which option is selected
};
```

### Variant Manager

The `DefaultVariantManager` provides high-level APIs for working with variants:

**Key Methods:**
- `HasVariants()` - Check if any variants exist
- `FindVariantGroup(prim_path)` - Get variant group by prim path
- `SelectVariant(group_id, set_name, option_name)` - Select a variant option
- `SelectVariantByIndex(group_id, set_id, option_index)` - Select by index
- `GetCurrentSelection(group_id)` - Get current selection
- `ResetToDefaults()` - Reset to default options
- `GetStatistics()` - Get variant metrics

### Variant Converter

The `VariantConverter` extracts USD variant information and maps it to RenderScene:

```cpp
class VariantConverter {
public:
    // Main entry point: convert all variants from USD Stage to RenderScene
    bool ConvertVariants(const Stage& stage, RenderScene* scene, std::string* err);

    // Configure maximum nesting depth to process
    void SetMaxNestingDepth(uint32_t depth);
};
```

## API Reference

### C++ API

#### VariantConverter

```cpp
// Extract variant information from a USD Stage
bool ConvertVariants(const Stage& stage, RenderScene* scene, std::string* err);

// Set maximum nesting depth (default: 3)
void SetMaxNestingDepth(uint32_t depth);
```

#### DefaultVariantManager

```cpp
// Initialize with variant groups
void SetVariantGroups(const std::vector<VariantGroup>& groups);

// Query variant information
bool HasVariants() const;
VariantGroup* FindVariantGroup(const std::string& prim_path);
VariantSet* FindVariantSet(int32_t group_id, const std::string& set_name);
VariantOption* FindVariantOption(int32_t group_id, int32_t set_id,
                                  const std::string& option_name);

// Select variants
bool SelectVariant(int32_t group_id, const std::string& set_name,
                   const std::string& option_name, std::string* err = nullptr);
bool SelectVariantByIndex(int32_t group_id, int32_t set_id,
                          int32_t option_index, std::string* err = nullptr);

// Query selections
const VariantSelection* GetCurrentSelection(int32_t group_id) const;
const std::vector<VariantSelection>& GetAllSelections() const;

// Reset state
bool ResetToDefaults(std::string* err = nullptr);

// Statistics
VariantStatistics GetStatistics() const;
```

### Python API

All C++ classes are exposed to Python with snake_case method names:

```python
manager.has_variants()
manager.find_variant_group(prim_path)
manager.select_variant(group_id, set_name, option_name)
manager.get_current_selection(group_id)
manager.get_statistics()
manager.reset_to_defaults()
```

## CLI Tools

### variant-lister

Lists all variants in a USD file with tree-format display.

```bash
# Basic usage
variant-lister model.usda

# Show summary statistics
variant-lister model.usda --summary

# JSON output
variant-lister model.usda --json

# Verbose output
variant-lister model.usda --verbose
```

**Output:**
```
=== Variants in model.usda ===

├─ Prim: /Root/Character
└─ VariantSet: "lod"
   ├─ Default: high
   ├─ "high" [+nested: 1]
   │  └─ VariantSet: "material" (2 options)
   └─ "low" [+nested: 1]
      └─ VariantSet: "material" (1 options)
```

### variant-analyzer

Analyzes variant complexity and provides recommendations.

```bash
# Basic analysis
variant-analyzer model.usda

# Detailed breakdown
variant-analyzer model.usda --detailed

# JSON output
variant-analyzer model.usda --json
```

**Output:**
```
=== Variant Complexity Analysis ===
File: model.usda

=== Summary ===
Prims with variants:     2
Total variant sets:      3
Total variant options:   8

=== Statistics ===
Max variants per prim:   2
Max options per set:     4
Avg options per set:     2.67
Max nesting depth:       2
Total combinations:      12

=== Complexity Assessment ===
✓  Low nesting depth (2)
✓  Manageable variant combinations (12)
⚡ Medium-sized variant set (4 options)
```

## Python Bindings

### Installation

Python bindings are included in the standard TinyUSDZ package:

```bash
pip install tinyusdz
```

### Available Classes

- `tinyusdz.tydra.VariantOption` - Variant option definition
- `tinyusdz.tydra.VariantSet` - Set of variant options
- `tinyusdz.tydra.VariantGroup` - All variants for a prim
- `tinyusdz.tydra.VariantSelection` - Current variant selection
- `tinyusdz.tydra.VariantConverter` - Extract variants from USD
- `tinyusdz.tydra.DefaultVariantManager` - Manage variant selections
- `tinyusdz.tydra.VariantStatistics` - Variant metrics

### Example Usage

```python
import tinyusdz

# Load USD
stage = tinyusdz.load_usd_from_file("model.usda")

# Extract variants
scene = tinyusdz.tydra.RenderScene()
converter = tinyusdz.tydra.VariantConverter()
converter.convert_variants(stage, scene)

# Manage variants
manager = tinyusdz.tydra.DefaultVariantManager()
manager.set_variant_groups(scene.variant_groups)

# Select variant
if manager.has_variants():
    groups = manager.get_variant_groups()
    for i, group in enumerate(groups):
        print(f"Group {i}: {group.prim_path}")
        for var_set in group.variant_sets:
            print(f"  {var_set.name}: {[o.name for o in var_set.options]}")

        # Select first option from first set
        if group.variant_sets:
            var_set = group.variant_sets[0]
            option_name = var_set.options[0].name if var_set.options else None
            if option_name:
                manager.select_variant(i, var_set.name, option_name)
```

## Best Practices

### 1. Organize Variants Hierarchically

Use nested variants for complex variation patterns:

```
Asset/
├── VariantSet "lod"
│   ├── "high" (nested: material variant)
│   ├── "medium" (nested: material variant)
│   └── "low" (nested: material variant)
└── VariantSet "color"
    ├── "red"
    ├── "blue"
    └── "green"
```

### 2. Use Meaningful Names

Choose clear, descriptive names:

```
✓ Good names:
  - "lod", "variant_lod", "lod_level"
  - "material_type", "material_variant"
  - "color_variant", "color"

✗ Avoid:
  - "v1", "v2", "var1"
  - "opt1", "option_a", "option_b"
```

### 3. Set Appropriate Defaults

Always specify meaningful default selections:

```cpp
variant_set.default_option_index = 0;  // First option is default
```

### 4. Validate Variant Selections

Always check return values:

```cpp
std::string err;
if (!manager.SelectVariant(0, "color", "red", &err)) {
    std::cerr << "Failed to select variant: " << err << "\n";
}
```

### 5. Limit Nesting Depth

Keep variant nesting to 3 levels maximum for performance:

```
Level 1: Prim variant sets
  Level 2: Option variant sets
    Level 3: Nested option variant sets
```

### 6. Document Variant Content

Add descriptions to important variants:

```cpp
option.description = "High-detail geometry (10M polygons)";
```

### 7. Cache Variant Selections

For frequently accessed variants, store selections:

```python
# Cache variant selection
current_lod = manager.get_current_selection(0)
```

## Examples

### Example 1: Load and List Variants

**C++:**
```cpp
#include "tinyusdz.hh"
#include "tydra/variant-converter.hh"

using namespace tinyusdz;

Stage stage;
LoadUSDFromFile("model.usda", &stage, nullptr, nullptr);

RenderScene scene;
VariantConverter converter;
converter.ConvertVariants(stage, &scene, nullptr);

std::cout << "Found " << scene.variant_groups.size() << " variant groups:\n";
for (const auto& group : scene.variant_groups) {
    std::cout << group.prim_path << ":\n";
    for (const auto& var_set : group.variant_sets) {
        std::cout << "  " << var_set.name << ": ";
        for (const auto& opt : var_set.options) {
            std::cout << opt.name << " ";
        }
        std::cout << "\n";
    }
}
```

**Python:**
```python
import tinyusdz

stage = tinyusdz.load_usd_from_file("model.usda")
scene = tinyusdz.tydra.RenderScene()
converter = tinyusdz.tydra.VariantConverter()
converter.convert_variants(stage, scene)

print(f"Found {len(scene.variant_groups)} variant groups:")
for group in scene.variant_groups:
    print(f"{group.prim_path}:")
    for var_set in group.variant_sets:
        options = [opt.name for opt in var_set.options]
        print(f"  {var_set.name}: {options}")
```

### Example 2: Select Variants

**C++:**
```cpp
DefaultVariantManager manager;
manager.SetVariantGroups(scene.variant_groups);

// Select "high" LOD
if (manager.SelectVariant(0, "lod", "high")) {
    std::cout << "Switched to high LOD\n";
}

// Get current selection
const auto* selection = manager.GetCurrentSelection(0);
if (selection) {
    std::cout << "Current selection index: " << selection->selected_option_index << "\n";
}
```

**Python:**
```python
manager = tinyusdz.tydra.DefaultVariantManager()
manager.set_variant_groups(scene.variant_groups)

# Select "high" LOD
if manager.select_variant(0, "lod", "high"):
    print("Switched to high LOD")

# Get current selection
selection = manager.get_current_selection(0)
if selection:
    print(f"Current selection: {selection.selected_option_index}")
```

### Example 3: Analyze Complexity

```bash
# Analyze variant complexity
variant-analyzer model.usda --detailed

# Output:
# Prim 1: /Characters/Hero
#   Variant sets: 2
#     - "lod": 3 options [with nested variants]
#       Possible combinations: 6
#     - "material": 2 options
#       Possible combinations: 2
```

## Troubleshooting

### No variants found in file

**Cause:** File doesn't contain variant definitions or they use unsupported structure

**Solution:**
1. Verify file contains variant sets in USD
2. Check with `variant-lister model.usda`
3. Ensure variants are defined in the prim spec, not overrides

### Variant selection fails

**Cause:** Invalid variant set or option name, or selection already active

**Solution:**
1. List available options: `variant-lister model.usda`
2. Check for typos in names (case-sensitive)
3. Verify variant group ID is valid (0-based index)

### Slow variant operations

**Cause:** High nesting depth or many variants

**Solution:**
1. Check complexity: `variant-analyzer model.usda`
2. Reduce nesting depth if possible
3. Consolidate related variants

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Load USD file | O(n) | n = file size |
| Convert variants | O(p * v) | p = prims, v = avg variants |
| Select variant | O(1) | Hash-based lookup |
| Query selection | O(1) | Direct array access |
| Get statistics | O(p * v) | One-time calculation |
| Reset defaults | O(s) | s = num selections |

## See Also

- [USD Variant Documentation](https://openusd.org/docs/api/class_usd_variant_set.html)
- [glTF Material Variants Extension](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_variants)
- TinyUSDZ Main Documentation
