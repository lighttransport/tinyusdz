# Nested VariantSet Test Files

This directory contains synthetic USDA test files for validating nested variantSet parsing and construction in TinyUSDZ.

## Test Files

### 1. basic-2level-001.usda
**Description:** Basic 2-level nested variantSet test
**Structure:**
- Outer variantSet: `shape` (sphere, cube)
- Inner variantSet: `size` (small, large)
- Tests: Simple two-level nesting with different geometry primitives

### 2. triple-nesting-001.usda
**Description:** Triple nested variantSet test - 3 levels deep
**Structure:**
- Level 1: `lod` (high, low)
- Level 2: `material` (metal, plastic)
- Level 3: `finish` (polished, brushed, glossy, matte)
- Tests: Deep nesting with mesh geometry and material properties

### 3. property-override-001.usda
**Description:** Nested variantSet with property overrides
**Structure:**
- Outer variantSet: `type` (car, truck)
- Inner variantSet: `color` (red, blue, green, yellow)
- Tests: Property value changes across variants (color, name, scale)

### 4. mixed-geometry-001.usda
**Description:** Nested variantSet with mixed geometry types
**Structure:**
- Outer variantSet: `style` (modern, classic)
- Inner variantSet: `detail` (simple, detailed, ornate)
- Tests: Multiple geometry types (Cube, Sphere, Cylinder, Cone, Capsule)

### 5. with-metadata-001.usda
**Description:** Nested variantSet with metadata annotations
**Structure:**
- Outer variantSet: `species` (human, robot) with metadata
- Inner variantSet: `gender`/`model` with metadata
- Tests: Metadata preservation in nested variants, explicit variant selection

### 6. with-selection-001.usda
**Description:** Nested variantSet with explicit variant selections
**Structure:**
- Level 1: `category` (furniture, appliance) with selection
- Level 2: `type` with variant selections
- Level 3: `style`/`size` variants
- Tests: Explicit variant selection at different nesting levels

### 7. asymmetric-nesting-001.usda
**Description:** Asymmetric nested variantSet - different depths in different branches
**Structure:**
- Outer variantSet: `season` (spring, summer, autumn, winter)
- Asymmetric depths:
  - spring: 3 levels (season → bloom → health)
  - summer: 1 level (no nesting)
  - autumn: 2-3 levels (season → leafColor [→ intensity for red])
  - winter: 1 level (no nesting)
- Tests: Non-uniform nesting depths, tree structure validation

## Validation Results

All test files have been validated with:
- ✅ `test_tinyusdz` - All files load successfully
- ✅ `tusdcat` - All files parse and reconstruct correctly

## Testing Commands

```bash
# Test a single file
./build/test_tinyusdz tests/feat/nestedVariantSet/basic-2level-001.usda

# View parsed structure
./build/tusdcat tests/feat/nestedVariantSet/basic-2level-001.usda

# Test all files
for f in tests/feat/nestedVariantSet/*.usda; do
    echo "Testing: $f"
    ./build/test_tinyusdz "$f" && echo "✓ PASSED"
done
```

## Coverage

These tests cover:
- ✅ 2-level nesting
- ✅ 3-level nesting (triple)
- ✅ Asymmetric nesting depths
- ✅ Multiple variants per variantSet
- ✅ Property overrides across variants
- ✅ Mixed geometry types
- ✅ Metadata in nested variants
- ✅ Explicit variant selections
- ✅ Different primitive types (Sphere, Cube, Cylinder, Cone, Capsule, Mesh)

## Implementation Status

Successfully validated nested variantSet support in TinyUSDZ USDA parser on the `nested-variantset` branch.
