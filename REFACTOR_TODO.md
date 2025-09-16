# TinyUSDZ Refactoring TODO

## Overview
This document outlines refactoring opportunities identified in the TinyUSDZ src/ directory to improve maintainability, readability, and modularity.

## High Priority Refactoring

### 1. Split Large Parser Files

#### crate-reader.cc (6,800 lines)
**Current Issues:**
- Extremely large single file handling multiple responsibilities
- Complex binary format parsing mixed with data structure building
- Memory management scattered throughout
- Threading logic intertwined with parsing logic

**Refactor into:**
```
crate-reader.cc → Split into:
├── crate-reader-core.cc (main reader interface)
├── crate-parser.cc (low-level binary parsing)
├── crate-data-builder.cc (data structure construction)
├── crate-memory-manager.cc (memory budget management)
├── crate-threading.cc (concurrent processing)
└── crate-validation.cc (data validation)
```

#### ascii-parser.cc (5,434 lines)
**Current Issues:**
- Hand-written parser with complex state management
- Multiple parsing responsibilities mixed together
- Large functions with deep nesting
- Memory checking scattered throughout

**Refactor into:**
```
ascii-parser.cc → Split into:
├── ascii-parser-core.cc (main parser interface)
├── ascii-lexer.cc (tokenization and lexical analysis)
├── ascii-expression-parser.cc (expression parsing)
├── ascii-type-parser.cc (type-specific parsing)
├── ascii-property-parser.cc (property and attribute parsing)
└── ascii-error-handling.cc (error reporting and recovery)
```

#### prim-reconstruct.cc (5,025 lines)
**Current Issues:**
- Massive single file with schema reconstruction logic
- Template specialization mixed with business logic
- No clear separation between different primitive types

**Refactor into:**
```
prim-reconstruct.cc → Split into:
├── prim-reconstruct-core.cc (base reconstruction framework)
├── geometry-reconstructor.cc (GeomMesh, GeomSphere, etc.)
├── light-reconstructor.cc (DomeLight, SphereLight, etc.)
├── shader-reconstructor.cc (UsdPreviewSurface, UsdUVTexture, etc.)
├── skeletal-reconstructor.cc (SkelRoot, Skeleton, etc.)
└── material-reconstructor.cc (Material, Shader binding)
```

## Medium Priority Refactoring

### 2. Decompose Mega-Headers

#### prim-types.hh (955 lines, 33+ includes)
**Current Issues:**
- Too many includes creating tight coupling (lines 40-73)
- Mixed responsibilities (utilities, types, containers)
- Includes scattered throughout file:
  - Line 425: `#include "property.hh"` in middle of header
  - Line 897: `#include "primspec.hh"` in middle of header
  - Line 952: `#include "define-type-trait.hh"` at end

**Refactor into:**
```
prim-types.hh → Split into:
├── prim-core.hh (Prim class and basic types)
├── prim-variant.hh (Variant, VariantSet classes)
├── prim-metadata.hh (PrimMeta and related)
├── prim-container.hh (Path operators, utility functions)
└── prim-forward-decl.hh (forward declarations)
```

#### value-types.hh (3,052 lines)
**Current Issues:**
- Extremely large header defining all value types
- Mathematical operations mixed with type definitions
- Template specializations spread throughout

**Refactor into:**
```
value-types.hh → Split into:
├── value-core-types.hh (basic types: token, string, TimeCode)
├── value-math-types.hh (vectors, matrices, mathematical types)
├── value-array-types.hh (array and compound types)
├── value-container-types.hh (Dictionary, TimeSamples)
└── value-type-traits.hh (TypeTraits and type system)
```

### 3. Extract Format-Specific Logic

#### pprinter.cc (4,850 lines)
**Current Issues:**
- Mixed formatting responsibilities
- Different output formats handled in same file
- Complex string building logic

**Refactor into:**
```
pprinter.cc → Split into:
├── pprinter-core.cc (base printing interface)
├── usda-formatter.cc (USDA format output)
├── json-formatter.cc (JSON format output)
├── value-formatter.cc (value type formatting)
└── pretty-print-utils.cc (common formatting utilities)
```

## Code Duplication Issues

### 4. Eliminate Macro-Based Code Generation

**Location:** prim-types.cc:189-275

**Current Pattern:**
```cpp
#define GET_PRIM_META(__ty)       \
  if (v.as<__ty>()) {             \
    return &(v.as<__ty>()->meta); \
  }

GET_PRIM_META(Model)
GET_PRIM_META(Scope)
GET_PRIM_META(Xform)
// ... 20+ more repetitions
```

**Solution:** Replace with template-based approach or visitor pattern

### 5. Consolidate Type Registration
**Issue:** Repetitive type registration code scattered across parsers
**Solution:** Create centralized type registry system

## Architectural Improvements

### 6. Separate Concerns

**Current Issues:**
- Parsers handling both syntax and semantics
- Data structures mixed with algorithms
- I/O mixed with business logic

**Recommendations:**
- Separate syntax analysis from semantic analysis
- Extract data transformation logic into separate modules
- Implement clear layered architecture (Parsing → Validation → Construction)

### 7. Extract Cross-Cutting Concerns

**Issues:**
- Error handling scattered throughout files
- Memory management mixed with core logic
- Logging and debugging code embedded everywhere

**Recommendations:**
- Implement consistent error handling strategy
- Create dedicated memory management utilities (RAII-based)
- Use aspect-oriented patterns for logging

### 8. Improve Header Organization

**Current Issues:**
- Headers with 30+ includes creating compilation dependencies
- Forward declarations scattered
- Template definitions mixed with declarations

**Recommendations:**
- Create forward declaration headers
- Separate template implementations
- Use PIMPL pattern for complex classes

## Implementation Phases

### Phase 1 - Core Infrastructure (Q1)
- [ ] Split crate-reader.cc into modular components
- [ ] Separate ascii-parser.cc lexical analysis from parsing
- [ ] Extract type-specific logic from prim-reconstruct.cc

### Phase 2 - Organization (Q2)
- [ ] Break down prim-types.hh mega-header
- [ ] Separate value-types.hh mathematical types from core types
- [ ] Extract format-specific printers from pprinter.cc

### Phase 3 - Polish (Q3)
- [ ] Replace macro-heavy code with templates/visitors
- [ ] Implement centralized type registry
- [ ] Consolidate error handling patterns
- [ ] Extract common utility patterns

## Expected Benefits

1. **Maintainability:** Smaller, focused files are easier to understand and modify
2. **Compilation Speed:** Reduced header dependencies will improve build times (estimated 30-40% improvement)
3. **Testing:** Smaller units enable more granular unit testing
4. **Parallel Development:** Teams can work on different modules independently
5. **Code Reuse:** Extracted utilities can be reused across the codebase

## Migration Strategy

1. **Incremental Refactoring:** Start with extracting utility functions
2. **Preserve Interfaces:** Maintain existing public APIs during refactoring
3. **Test Coverage:** Ensure comprehensive tests before refactoring
4. **Header-First:** Start with header organization to establish clear boundaries
5. **Validate Performance:** Ensure refactoring doesn't impact performance

## Success Metrics

- Reduce average file size from 2,000+ lines to <500 lines
- Decrease compilation time by 30-40%
- Improve test coverage from current to 80%+
- Reduce cyclomatic complexity of functions to <10
- Eliminate 90% of macro-based code generation

## Notes

- Priority levels based on impact to maintainability and development velocity
- All refactoring should maintain backward compatibility with existing API
- Performance benchmarks should be run before and after each major refactoring