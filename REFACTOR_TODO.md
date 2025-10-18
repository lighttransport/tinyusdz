# Refactoring Opportunities

This document outlines potential areas for refactoring in the TinyUSDZ codebase to improve maintainability, readability, and extensibility.

## Timesamples Module (`src/timesamples.*` and `src/timesamples-pprint.*`)

### Summary of Refactoring Opportunities

The timesamples module contains several areas where code duplication and complexity could be reduced through refactoring:

#### 1. ✅ COMPLETED: Consolidate POD Type Metadata and Handling

*   **Files:** `src/timesamples.cc:203-429` (get_samples_converted), `src/timesamples.cc:432-498` (get_element_size)
*   **Status:** Completed (2025-01-18)
*   **Solution Implemented:**
    - Created centralized `TINYUSDZ_POD_TYPE_LIST` macro that lists all ~40 POD types (lines 17-71)
    - Refactored `get_element_size()` to use the type registry, reducing from ~65 lines to ~17 lines (lines 488-506)
    - Refactored `get_samples_converted()` to use the type registry, reducing ~45 lines of type enumeration to 7 lines (lines 432-438)
    - All unit tests pass - functionality preserved
*   **Impact:**
    - Eliminated ~100+ lines of duplicate type enumeration
    - Adding new POD types now requires single entry in centralized macro
    - Both functions now automatically stay in sync when types are added/removed
    - Improved maintainability and reduced potential for inconsistencies

#### 2. ✅ COMPLETED: Simplify PODTimeSamples::update() Sorting Logic

*   **File:** `src/timesamples.cc:73-226`
*   **Status:** Completed (2025-01-18)
*   **Solution Implemented:**
    - Extracted three sorting strategies into separate helper functions (lines 77-180):
      - `create_sort_indices()` - Creates sorted index array
      - `sort_with_offsets()` - Strategy 1: Offset-backed sorting
      - `sort_with_compact_values()` - Strategy 2: Legacy compact value storage
      - `sort_minimal()` - Strategy 3: Minimal sorting (times + blocked flags only)
    - Simplified `update()` method to clean dispatch logic (lines 182-226)
    - Each strategy is now testable in isolation
*   **Impact:**
    - Improved code clarity - each sorting strategy is self-contained
    - Reduced cognitive complexity of main update() method
    - Easier to maintain and debug individual sorting paths
    - Better separation of concerns

#### 3. Refactor Repetitive add_* Methods

*   **Files:** `src/timesamples.hh:198-300`
*   **Opportunity:** The `add_sample`, `add_array_sample`, and `add_typed_array_sample` methods repeat the same underlying type checks, offset initialization, and error handling. A common template or base implementation could reduce duplication.
*   **Pattern Found:** Each method performs:
    - Type ID validation
    - Offset table initialization on first non-blocked sample
    - Buffer resizing
    - Similar error message construction

#### 4. ✅ PARTIALLY COMPLETED: Reduce Template Specialization Redundancy

*   **File:** `src/timesamples.cc`
*   **Status:** Partially Completed (2025-01-18)
*   **Solution Implemented:**
    - Refactored `PODTimeSamples::add_sample` instantiations (lines 732-794):
      - **Before**: 48 manual template instantiations (~68 lines)
      - **After**: Macro-based generator with explicit list (~63 lines, but more maintainable)
      - Uses `INSTANTIATE_ADD_SAMPLE` macro to reduce boilerplate
    - Refactored `PODTimeSamples::add_typed_array_sample` instantiations (lines 833-852):
      - **Before**: 21 manual instantiations (~21 lines)
      - **After**: Macro-based generator using `TINYUSDZ_POD_TYPE_LIST` + 6 matrix types (~14 lines)
      - Reduction: ~33% fewer lines
*   **Impact:**
    - Reduced boilerplate for template instantiations
    - Consistent pattern using centralized type registry where possible
    - Easier to add new types that support TypedArray
*   **Remaining Work:**
    - `TypedTimeSamples::get()` instantiations (140+ lines) could benefit from similar treatment
    - However, these include many non-POD types (vectors, strings, etc.) making macro generation complex

#### 5. ✅ COMPLETED: Consolidate Pretty Print Functions

*   **File:** `src/timesamples-pprint.cc`
*   **Status:** Completed (2025-01-18)
*   **Solution Implemented:**
    - Created `OutputAdapter` abstraction to unify string and StreamWriter output (lines 26-62)
    - Implemented unified `print_type` and `print_vector` templates using SFINAE (lines 116-226)
    - Added type traits system with `is_value_type` template for compile-time type detection (lines 64-113)
    - Reduced both `pprint_pod_value_by_type` functions from ~150 lines each to 4 lines each (lines 1382-1393)
    - Disabled 600+ lines of legacy print functions (wrapped in `#if 0` block for future cleanup)
*   **Impact:**
    - Eliminated ~370 lines of duplicate switch statements
    - All unit tests pass - functionality preserved
    - Adding new types now requires single entry in dispatch table

#### 6. ✅ COMPLETED: Unify Type Dispatch Mechanisms

*   **File:** `src/timesamples-pprint.cc` (completed for this file)
*   **Status:** Partially completed - `timesamples-pprint.cc` done (2025-01-18), `timesamples.cc` still pending
*   **Solution Implemented:**
    - Created centralized `print_pod_value_dispatch` function using macro-based dispatch (lines 250-330)
    - Implemented `DISPATCH_POD_TYPE`, `DISPATCH_VALUE_TYPE`, and `DISPATCH_VECTOR_TYPE` macros
    - Handles 60+ type cases uniformly through single switch statement
    - Uses adapter pattern to route both string and StreamWriter output through same dispatch logic
*   **Impact:**
    - Reduced code duplication significantly
    - Improved maintainability and extensibility
    - Type dispatch now centralized and consistent
*   **Remaining Work:**
    - `src/timesamples.cc` still uses multiple large switch statements for type dispatch
    - Could apply similar pattern to other type dispatch locations in the codebase

#### 7. Extract Common Buffer Management Logic

*   **Files:** `src/timesamples.hh`, `src/timesamples.cc`
*   **Opportunity:** The PODTimeSamples class manages several parallel buffers (_times, _values, _blocked, _offsets) with complex synchronization requirements. Extract a BufferManager class to handle:
    - Coordinated resizing
    - Offset management
    - Dirty tracking
    - Memory estimation

#### 8. Simplify TimeSamples/PODTimeSamples Interaction

*   **Files:** `src/timesamples.hh`
*   **Opportunity:** The TimeSamples class wraps PODTimeSamples for POD types but maintains its own storage for non-POD types. This dual-storage approach leads to:
    - Complex conditional logic throughout the API
    - Duplication of methods between the two classes
    - Potential for inconsistencies
*   **Solution:** Consider a unified storage approach or clearer separation of responsibilities

## C++ Core (`src` directory)

### 1. Consolidate File Type Detection

*   **File:** `src/tinyusdz.cc`
*   **Opportunity:** The `LoadUSDFromMemory` function contains repetitive code for detecting USDA, USDC, and USDZ file types. This logic can be centralized to reduce duplication. Similarly, `LoadUSDZFromMemory` and `LoadUSDZFromFile` have duplicated logic that could be shared.

### 2. Refactor Large `if-else` Chains

*   **Files:** `src/usda-reader.cc`, `src/usdc-reader.cc`
*   **Opportunity:** The `ReconstructPrimFromTypeName` functions in both the USDA and USDC readers are implemented as large `if-else` chains. This makes them difficult to maintain and extend. Refactoring this to use a map of function pointers or a similar factory pattern would be beneficial.

### 3. Decompose Large Functions

*   **Files:** `src/usda-reader.cc`, `src/usdc-reader.cc`, `src/tydra/render-data.cc`
*   **Opportunity:** Several functions are overly long and complex.
    *   In `usda-reader.cc` and `usdc-reader.cc`, functions like `ParseProperty`, `ParsePrimSpec`, and `ReconstructPrimMeta` could be broken down into smaller, more focused functions.
    *   In `tydra/render-data.cc`, the `TriangulatePolygon` function is a candidate for simplification and decomposition.

### 4. Generalize Template Specializations

*   **File:** `src/tydra/scene-access.cc`
*   **Opportunity:** The `GetPrimProperty` and `ToProperty` template specializations contain a lot of repeated code. A more generic, template-based approach could reduce this duplication.

### 5. [Moved to Timesamples Module Section]

*   See "Timesamples Module" section above for comprehensive refactoring opportunities for POD type metadata centralization.

### 6. [Moved to Timesamples Module Section]

*   See "Timesamples Module" section above for comprehensive refactoring opportunities for PODTimeSamples sorting paths.

### 7. [Moved to Timesamples Module Section]

*   See "Timesamples Module" section above for comprehensive refactoring opportunities for type/offset guards in POD samples.

### 8. Generic Index Accessors

*   **File:** `src/crate-reader.cc:141`
*   **Opportunity:** `GetField`, `GetToken`, `GetPath`, `GetElementPath`, and `GetPathString` all share the same bounds-check pattern. A templated `lookup_optional(vector, Index)` (with optional logging hook) would remove boilerplate and centralize future diagnostics.

## JavaScript/WASM Bindings (`web` directory)

### 1. Modularize Emscripten Bindings

*   **File:** `web/binding.cc`
*   **Opportunity:** This is a very large file containing all Emscripten bindings. It should be split into multiple files based on functionality (e.g., `stage_bindings.cc`, `scene_bindings.cc`, `asset_bindings.cc`) to improve organization and build times.

### 2. Refactor `TinyUSDZLoaderNative`

*   **File:** `web/binding.cc`
*   **Opportunity:** The `TinyUSDZLoaderNative` class has too many responsibilities. The asset caching and streaming logic, for example, could be extracted into a separate `AssetManager` class.

### 3. Consolidate JavaScript Loading Logic

*   **File:** `web/js/src/tinyusdz/TinyUSDZLoader.js`
*   **Opportunity:** The `load` and `loadAsLayer` methods share a lot of similar logic. This could be consolidated into a common internal loading function.

### 4. Data-Driven Material Conversion

*   **File:** `web/js/src/tinyusdz/TinyUSDZLoaderUtils.js`
*   **Opportunity:** The `convertUsdMaterialToMeshPhysicalMaterial` function, which maps USD material properties to Three.js material properties, could be refactored to be more data-driven. Using a mapping table or a similar approach would make it easier to add or modify material property mappings.
