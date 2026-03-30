# USD Variant Best Practices Guide

This guide provides best practices for designing, implementing, and using USD variants in TinyUSDZ applications.

## Table of Contents

1. [Design Principles](#design-principles)
2. [Structure Guidelines](#structure-guidelines)
3. [Naming Conventions](#naming-conventions)
4. [Performance Optimization](#performance-optimization)
5. [Integration Patterns](#integration-patterns)
6. [Common Pitfalls](#common-pitfalls)
7. [Testing and Validation](#testing-and-validation)

## Design Principles

### 1. Clarity Over Brevity

Variants should be self-documenting. Use clear, descriptive names that indicate purpose:

```
✓ GOOD:
  geometry_lod
  material_finish
  assembly_type
  locale_variant

✗ BAD:
  var1, var2, v3
  opt_a, opt_b
  geom, mat
```

### 2. Single Responsibility

Each variant set should represent a single dimension of variation:

```
✓ GOOD (separate concerns):
  VariantSet "level_of_detail"  → [high, medium, low]
  VariantSet "material_type"    → [plastic, metal, rubber]
  VariantSet "damage"           → [pristine, scratched, broken]

✗ BAD (mixed concerns):
  VariantSet "variations"       → [high_plastic, low_metal, broken_rubber]
```

### 3. Explicit Defaults

Always explicitly set and document default variant selections:

```cpp
struct VariantSet {
    std::string name;
    std::vector<VariantOption> options;
    int32_t default_option_index = 0;  // Always explicit
    // ...
};
```

### 4. Consistent Ordering

Maintain consistent ordering of options within a variant set:

```
✓ GOOD (logical ordering):
  lod: [high, medium, low]        // Descending detail
  quality: [maximum, high, medium, low]  // Descending quality

✗ BAD (random ordering):
  lod: [low, high, medium]        // Confusing order
```

### 5. Avoid Over-nesting

While USD supports unlimited nesting, limit to 3 levels for maintainability:

```
✓ GOOD (3 levels):
  Level 1: Asset variant sets
    Level 2: LOD option variants
      Level 3: Material option variants

✗ BAD (excessive nesting):
  Level 1 → Level 2 → Level 3 → Level 4 → Level 5
```

## Structure Guidelines

### Variant Hierarchy Design

Design variant hierarchies bottom-up, starting with dependencies:

```
Character Asset
├── VariantSet "character_type"    [primary dimension]
│   ├── Option "hero"
│   │   └── VariantSet "skin_tone"
│   │       ├── "light"
│   │       ├── "medium"
│   │       └── "dark"
│   ├── Option "villain"
│   │   └── VariantSet "damage_level"
│   │       ├── "pristine"
│   │       └── "damaged"
│   └── Option "extra"
│       └── VariantSet "costume"
│           ├── "red"
│           └── "blue"
└── VariantSet "cloth_material"    [independent dimension]
    ├── "cotton"
    ├── "silk"
    └── "leather"
```

### Content Organization

Organize content (geometry, materials) to align with variant structure:

```
/Characters/Hero
├── Geometry (LOD variants)
│   ├── high_poly (for "high" LOD)
│   ├── medium_poly (for "medium" LOD)
│   └── low_poly (for "low" LOD)
├── Materials (material variants)
│   ├── Material_Metal
│   ├── Material_Plastic
│   └── Material_Rubber
└── Animations (animation variants)
    ├── Anim_Run_Fast
    ├── Anim_Run_Slow
    └── Anim_Walk
```

### Variant Composition Strategy

**Strategy 1: Content Swapping (Recommended)**

Different geometry/materials per option:

```
✓ Efficient for: Material variations, LOD transitions
✗ Inefficient for: Many similar options with minor differences
```

**Strategy 2: Property Overrides**

Same content with different properties:

```
✓ Efficient for: Color variants, parameter adjustments
✗ Inefficient for: Major structural changes
```

**Strategy 3: Hybrid Approach**

Combine content swapping and property overrides:

```
✓ Use content swapping for major differences (LOD, materials)
✓ Use property overrides for minor adjustments (colors, scales)
```

## Naming Conventions

### Variant Set Names

Use snake_case for variant set names:

```
✓ GOOD:
  level_of_detail
  material_finish
  assembly_configuration
  regional_variant

✗ BAD:
  LevelOfDetail
  materialFinish
  assembly-configuration
```

### Option Names

Use descriptive, lowercase names:

```
✓ GOOD:
  high, medium, low              (clarity)
  plastic, metal, rubber         (material types)
  damaged, scratched, pristine   (conditions)

✗ BAD:
  h, m, l                        (abbreviations)
  mtl1, mtl2, mtl3               (cryptic)
  var_a, var_b, var_c            (meaningless)
```

### Description Fields

Always provide descriptions for complex or non-obvious options:

```cpp
VariantOption option;
option.name = "ultra_high_poly";
option.description = "Ultra-high detail geometry (50M polygons, 4K textures)";
option.description += " - Recommended for close-up shots and hero renders";
```

## Performance Optimization

### 1. Minimize Variant Combinations

Excessive combinations hurt performance:

```
✗ BAD (96 combinations):
  lod:      [high, medium, low]        (3 options)
  material: [gold, silver, bronze]     (3 options)
  color:    [red, green, blue, black]  (4 options)
  size:     [small, medium, large]     (4 options)
  finish:   [matte, glossy, metallic]  (3 options)
  Total:    3 × 3 × 4 × 4 × 3 = 432 combinations

✓ GOOD (12 combinations):
  lod:      [high, medium, low]        (3 options)
  variant:  [gold, silver, bronze]     (3 options)
  Total:    3 × 3 = 9 combinations

(Other variations via materials/properties)
```

### 2. Lazy Load Variant Content

Load variant content only when selected:

```cpp
class SmartVariantManager {
    std::unordered_map<std::string, ContentCache> cache;

    void SelectVariant(const std::string& variant) {
        if (cache.find(variant) == cache.end()) {
            // Load on-demand
            cache[variant] = LoadVariantContent(variant);
        }
        // Apply cached content
        ApplyContent(cache[variant]);
    }
};
```

### 3. Use Indices Instead of Names

In performance-critical code, use indices:

```cpp
// ✓ GOOD (O(1) lookup):
manager.SelectVariantByIndex(group_id, set_id, option_index);

// ✗ SLOWER (string comparison):
manager.SelectVariant(group_id, set_name, option_name);
```

### 4. Batch Variant Selections

When changing multiple variants, batch the selections:

```cpp
// ✓ GOOD (single state update):
std::vector<VariantSelection> selections = {
    {group_0, set_0, index_a},
    {group_1, set_1, index_b},
    {group_2, set_2, index_c}
};
manager.ApplySelections(selections);

// ✗ INEFFICIENT (multiple updates):
manager.SelectVariant(group_0, set_0_name, option_a);
manager.SelectVariant(group_1, set_1_name, option_b);
manager.SelectVariant(group_2, set_2_name, option_c);
```

### 5. Profile Variant Operations

Measure variant switching performance:

```cpp
auto start = std::chrono::high_resolution_clock::now();
manager.SelectVariant(group_id, set_name, option_name);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
std::cout << "Selection took: " << duration.count() << "ms\n";
```

## Integration Patterns

### Pattern 1: Variant UI Panel

Create a UI for variant selection:

```python
class VariantPanel:
    def __init__(self, manager: VariantManager):
        self.manager = manager
        self.groups = manager.get_variant_groups()

    def render_variants(self):
        for i, group in enumerate(self.groups):
            print(f"\n{group.prim_path}")
            for j, var_set in enumerate(group.variant_sets):
                print(f"  {var_set.name}:")
                for k, option in enumerate(var_set.options):
                    selected = "✓" if self.is_selected(i, j, k) else " "
                    print(f"    [{selected}] {option.name}")

    def on_variant_clicked(self, group_id, set_id, option_id):
        self.manager.select_variant_by_index(group_id, set_id, option_id)
```

### Pattern 2: Variant History/Undo

Maintain variant selection history:

```cpp
class VariantHistory {
    std::vector<std::vector<VariantSelection>> history;
    size_t current_index = 0;

    void Push(const std::vector<VariantSelection>& selections) {
        // Remove forward history if we're not at the end
        history.erase(history.begin() + current_index + 1, history.end());
        history.push_back(selections);
        current_index++;
    }

    bool Undo() {
        if (current_index > 0) {
            current_index--;
            ApplySelections(history[current_index]);
            return true;
        }
        return false;
    }

    bool Redo() {
        if (current_index < history.size() - 1) {
            current_index++;
            ApplySelections(history[current_index]);
            return true;
        }
        return false;
    }
};
```

### Pattern 3: Variant Presets

Save and restore variant configurations:

```python
class VariantPreset:
    def __init__(self, name: str):
        self.name = name
        self.selections = []

    def save(self, manager: DefaultVariantManager):
        self.selections = list(manager.get_all_selections())

    def apply(self, manager: DefaultVariantManager):
        for selection in self.selections:
            manager.apply_selection(selection)

    def to_json(self):
        return json.dumps([{
            'group_id': s.variant_group_id,
            'set_id': s.variant_set_id,
            'option': s.selected_option_index
        } for s in self.selections])
```

### Pattern 4: Variant Export

Export to different formats based on variant selection:

```cpp
void ExportWithVariant(const std::string& variant,
                       const std::string& output_format) {
    // Select variant
    manager.SelectVariant(0, "material", variant);

    // Export based on format
    if (output_format == "glb") {
        ExportGLB(scene, "model_" + variant + ".glb");
    } else if (output_format == "usda") {
        ExportUSDA(stage, "model_" + variant + ".usda");
    }
}
```

## Common Pitfalls

### Pitfall 1: Case-Sensitive Names

Variant names are case-sensitive:

```cpp
// These are DIFFERENT:
manager.SelectVariant(0, "color", "Red");      // May fail
manager.SelectVariant(0, "color", "red");      // Correct
```

**Solution:** Use lowercase consistently.

### Pitfall 2: Forgetting Default Index

Not setting default_option_index:

```cpp
// ✗ WRONG (no default):
variant_set.options = [{name: "high"}, {name: "low"}];

// ✓ CORRECT:
variant_set.options = [{name: "high"}, {name: "low"}];
variant_set.default_option_index = 0;  // "high" is default
```

### Pitfall 3: Invalid Index Access

Accessing variant options with wrong indices:

```cpp
// ✗ WRONG (may crash):
auto option = variant_set.options[999];

// ✓ CORRECT (bounds checking):
if (option_index >= 0 && option_index < variant_set.options.size()) {
    auto option = variant_set.options[option_index];
}
```

### Pitfall 4: Modifying Shared References

Modifying variant data that's referenced elsewhere:

```cpp
// ✗ WRONG (modifying shared data):
auto* group = manager.FindVariantGroup(path);
group->prim_path = "new_path";  // Affects other references

// ✓ CORRECT (work with copies):
auto groups = manager.GetVariantGroups();
for (auto& group : groups) {
    // Read-only operations, or
    // Copy data before modifying
}
```

### Pitfall 5: Excessive Nesting Depth

Using too many nesting levels:

```
✗ BAD (5 levels):
  Level 1: Character type
    Level 2: Outfit type
      Level 3: Color
        Level 4: Texture quality
          Level 5: Special effects

✓ GOOD (2-3 levels):
  Level 1: Character type → Outfit type
  Level 2: Color variant

  (Texture quality as property override, not nested variant)
```

## Testing and Validation

### Test 1: Variant Coverage

Ensure all variant paths are tested:

```cpp
TEST(VariantTest, AllVariantCombinations) {
    for (const auto& group : scene.variant_groups) {
        for (const auto& var_set : group.variant_sets) {
            for (const auto& option : var_set.options) {
                EXPECT_TRUE(manager.SelectVariant(
                    group.id, var_set.name, option.name));
                ValidateVariantContent(scene);
            }
        }
    }
}
```

### Test 2: Selection Validation

Verify selections are applied correctly:

```cpp
TEST(VariantTest, SelectionValidation) {
    manager.SelectVariant(0, "color", "red");
    const auto* sel = manager.GetCurrentSelection(0);
    EXPECT_NE(sel, nullptr);
    EXPECT_EQ(sel->selected_option_index, 0);  // "red" is index 0
}
```

### Test 3: Performance Benchmarking

Measure variant switching performance:

```cpp
TEST(VariantTest, PerformanceBenchmark) {
    const int ITERATIONS = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; i++) {
        manager.SelectVariant(0, "lod", "high");
        manager.SelectVariant(0, "lod", "low");
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    auto avg = duration.count() / (double)(ITERATIONS * 2);
    EXPECT_LT(avg, 1.0);  // Should be < 1ms per selection
}
```

### Test 4: Variant Data Integrity

Ensure variant data is correctly extracted:

```cpp
TEST(VariantConverterTest, DataIntegrity) {
    const auto& group = scene.variant_groups[0];
    EXPECT_FALSE(group.prim_path.empty());
    EXPECT_FALSE(group.variant_sets.empty());

    for (const auto& var_set : group.variant_sets) {
        EXPECT_FALSE(var_set.name.empty());
        EXPECT_FALSE(var_set.options.empty());
        EXPECT_GE(var_set.default_option_index, 0);
        EXPECT_LT(var_set.default_option_index, var_set.options.size());
    }
}
```

### Test 5: Nested Variant Validation

Verify nested variants are correctly handled:

```cpp
TEST(VariantTest, NestedVariantValidation) {
    for (const auto& group : scene.variant_groups) {
        for (const auto& var_set : group.variant_sets) {
            for (const auto& option : var_set.options) {
                // Check nesting depth
                uint32_t depth = 1;
                for (const auto& nested : option.nested_variant_sets) {
                    EXPECT_LE(++depth, MAX_NESTING_DEPTH);
                }
            }
        }
    }
}
```

## Summary

Key takeaways for variant best practices:

1. **Design for clarity** - Use self-documenting structures
2. **Limit complexity** - Keep nesting and combinations reasonable
3. **Follow conventions** - Use consistent naming and ordering
4. **Optimize performance** - Minimize variant combinations and use indices
5. **Integrate thoughtfully** - Use patterns for UI, undo, presets, export
6. **Avoid pitfalls** - Watch for case sensitivity, bounds checking, defaults
7. **Test thoroughly** - Validate all variant combinations and paths

## See Also

- [VARIANT_USAGE_GUIDE.md](VARIANT_USAGE_GUIDE.md) - Comprehensive API reference
- USD Variant Specification: https://openusd.org/docs/api/class_usd_variant_set.html
- TinyUSDZ Variant Examples: `python/examples/variant_example.py`
