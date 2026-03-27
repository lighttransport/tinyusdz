# USD Variant Support in TinyUSDZ

## Overview

USD variants allow multiple, mutually exclusive representations of an asset. TinyUSDZ parses and writes variant structures in USDA and USDC formats, including nested variants (up to 3 levels).

### Common Use Cases

- **Level of Detail (LOD)**: Different geometry complexity levels
- **Material Variants**: Different material or color options
- **Geometry Variations**: Different shapes, configurations, or assembly options

## Core Data Structures

### VariantSet and Variant

In TinyUSDZ's internal representation (`src/prim-types.hh`):

```cpp
struct Variant {
  PrimMeta metas;                                    // Variant metadata
  std::map<std::string, Property> props;            // Properties in this variant
  std::vector<value::token> primChildren;            // Child prim names
  std::map<std::string, VariantSet> variantSets;    // Nested variant sets
};

struct VariantSet {
  std::string name;
  std::map<std::string, Variant> variantSet;  // name -> Variant
};
```

### Variant Selections

Variant selections are stored in prim metadata:

```cpp
// In PrimMeta
std::map<std::string, std::string> variants;  // variantSet name -> selected variant
std::vector<value::token> variantSetNames;     // ordered variantSet names
```

## USDA Syntax

```usda
def Xform "Asset" (
    prepend variantSets = "lod"
    variants = { string lod = "high" }
)
{
    variantSet "lod" = {
        "high" {
            def Mesh "Mesh" {
                # high-detail geometry
            }
        }
        "low" {
            def Mesh "Mesh" {
                # low-detail geometry
            }
        }
    }
}
```

### Nested Variants

```usda
variantSet "lod" = {
    "high" (
        prepend variantSets = "material"
    ) {
        def Mesh "Mesh" { }
        variantSet "material" = {
            "plastic" { }
            "metal" { }
        }
    }
}
```

## Design Principles

### Naming Conventions

Use snake_case for variant set names and descriptive lowercase for option names:

```
Good: level_of_detail, material_finish, assembly_type
Bad:  var1, v2, opt_a
```

### Single Responsibility

Each variant set should represent one dimension of variation:

```
Good:
  VariantSet "level_of_detail"  -> [high, medium, low]
  VariantSet "material_type"    -> [plastic, metal, rubber]

Bad:
  VariantSet "variations"       -> [high_plastic, low_metal]
```

### Limit Nesting Depth

Keep variant nesting to 3 levels maximum:

```
Level 1: Prim variant sets
  Level 2: Option variant sets
    Level 3: Nested option variant sets
```

### Minimize Combinations

Excessive variant combinations increase complexity:

```
Manageable: 3 LODs x 3 materials = 9 combinations
Excessive:  3 x 3 x 4 x 4 x 3 = 432 combinations
```

## Testing

Variant roundtrip tests are in `tests/usda/`:
- `variantSet-*.usda` - Basic variant parsing
- `variantSet-nested-*.usda` - Nested variant structures

Run with:
```bash
node tests/compare-usda.js --detailed-diff \
  --tusdcat ./build/tusdcat --usdcat usdcat \
  tests/usda/variantSet-nested-001.usda
```

## See Also

- [USD Variant Documentation](https://openusd.org/docs/api/class_usd_variant_set.html)
- `src/prim-types.hh` - VariantSet/Variant definitions
- `src/pprinter.cc` - `print_variantSetStmt()` for USDA output
- `src/ascii-parser.cc` - Variant parsing
