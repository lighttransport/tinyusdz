# Refactoring Opportunities

This document outlines potential areas for refactoring in the TinyUSDZ codebase to improve maintainability, readability, and extensibility.

## Timesamples Module (`src/timesamples.*` and `src/timesamples-pprint.*`)

### Summary of Refactoring Opportunities

The timesamples module contains several areas where code duplication and complexity could be reduced through refactoring:

#### 1. Consolidate POD Type Metadata and Handling

*   **Files:** `src/timesamples.cc:203-429` (get_samples_converted), `src/timesamples.cc:432-498` (get_element_size)
*   **Opportunity:** Both functions expand the same exhaustive POD type list using macros. This could be refactored to use a centralized type traits table or constexpr map that associates type IDs with their properties (size, converter functions).
*   **Impact:** Would eliminate ~100+ lines of duplicate macro expansions and make adding new types easier.

#### 2. Simplify PODTimeSamples::update() Sorting Logic

*   **File:** `src/timesamples.cc:17-115`
*   **Opportunity:** The function interleaves three distinct sorting strategies (offset-backed, legacy AoS, minimal) with deeply nested conditionals. Extract each strategy into separate helper methods or adopt a strategy pattern.
*   **Current Issues:**
    - Complex branching makes it hard to reason about the flow
    - Difficult to test individual sorting paths
    - The offset calculation loop for legacy path is particularly convoluted

#### 3. Refactor Repetitive add_* Methods

*   **Files:** `src/timesamples.hh:198-300`
*   **Opportunity:** The `add_sample`, `add_array_sample`, and `add_typed_array_sample` methods repeat the same underlying type checks, offset initialization, and error handling. A common template or base implementation could reduce duplication.
*   **Pattern Found:** Each method performs:
    - Type ID validation
    - Offset table initialization on first non-blocked sample
    - Buffer resizing
    - Similar error message construction

#### 4. Reduce Template Specialization Redundancy

*   **File:** `src/timesamples.cc` (48 PODTimeSamples::add_sample specializations, 21 add_typed_array_sample specializations, 100+ TypedTimeSamples::get specializations)
*   **Opportunity:** These explicit template specializations could potentially be generated through template metaprogramming or replaced with a single variadic template implementation that handles all POD types uniformly.

#### 5. Consolidate Pretty Print Functions

*   **File:** `src/timesamples-pprint.cc`
*   **Opportunity:** Contains numerous repetitive print functions (print_float, print_double, print_float2, print_float3, etc.). These could be:
    - Unified using templates with proper SFINAE/concepts
    - Replaced with a visitor pattern
    - Generated from a type traits table
*   **Current Pattern:** Each type has its own print function with nearly identical implementation

#### 6. Unify Type Dispatch Mechanisms

*   **Files:** `src/timesamples.cc`, `src/timesamples-pprint.cc`
*   **Opportunity:** Multiple locations use large if-else chains or switch statements to dispatch based on type_id. This could be centralized using:
    - A type registry with function pointers
    - std::variant with std::visit (C++17)
    - A compile-time map of type_id to handler functions

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
