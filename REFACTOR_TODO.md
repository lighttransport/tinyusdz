# Refactoring Opportunities

This document outlines potential areas for refactoring in the TinyUSDZ codebase to improve maintainability, readability, and extensibility.

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

### 5. Centralize POD Type Metadata

*   **Files:** `src/timesamples.cc:203`, `src/timesamples.cc:260`
*   **Opportunity:** The same exhaustive POD type list is expanded in both `get_samples_converted()` and `get_element_size()`. Moving the type id → traits metadata into a shared table (e.g., `constexpr std::array` or traits map) would eliminate duplicate macros and make it harder to miss new role types.

### 6. Split PODTimeSamples Sorting Paths

*   **File:** `src/timesamples.hh:120`
*   **Opportunity:** `PODTimeSamples::update()` interleaves three sorting strategies (offset-backed, legacy AoS, and minimal) in a single function with nested loops. Extracting helpers for each path or adopting a strategy object would clarify the branching and make it easier to reason about blocked-value handling.

### 7. Factor Type/Offset Guards for POD Samples

*   **File:** `src/timesamples.hh:198`, `src/timesamples.hh:244`, `src/timesamples.hh:300`
*   **Opportunity:** The three `add_*` methods repeat the same underlying type checks, offset bootstrap, and error string construction. Introducing a small guard utility (e.g., `ensure_type_initialized<T>()`) and a reusable offset-initializer would reduce code size and ensure blocked/array transitions stay consistent.

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
