# TinyUSDZ API Documentation

## Introduction

TinyUSDZ is a lightweight, secure, portable, and dependency-free C++14 library for handling USDZ, USDC, and USDA files. It does not require the official OpenUSD (pxrUSD) library. It aims to provide core functionalities for parsing, inspecting, and (soon) writing USD files, with a focus on security and ease of integration, especially in resource-constrained environments like WebAssembly or mobile applications.

**Key Features (from README & Headers):**

*   Parsers for USDZ, USDC (Crate), and USDA.
*   Writer for USDA (USDC/USDZ writer is work-in-progress).
*   Support for basic Primitives (Xform, Mesh, BasisCurves, etc.).
*   Support for basic Lights and Shaders (UsdPreviewSurface, UsdUVTexture, UsdPrimvarReader).
*   Experimental support for composition features (subLayers, references, payload, inherits, variants).
*   A C API (`c-tinyusd.h`) for language bindings.
*   A Core C++ API (`tinyusdz.hh`) for direct C++ integration.
*   Tydra API (`tydra/`) for higher-level scene access and data conversion, useful for renderers and DCC tools.
*   Security features like memory budgeting.
*   WASM and WASI support.

**Version:** 0.8.0-rc5 (as of `tinyusdz.hh`)

## 1. Core C++ API (`tinyusdz.hh`)

This is the primary C++ interface for interacting with TinyUSDZ.

### 1.1. Main Loading Functions

The library provides functions to load USD data from files or memory, automatically detecting the format (USDA, USDC, USDZ).

*   `bool LoadUSDFromFile(const std::string &filename, Stage *stage, std::string *warn, std::string *err, const USDLoadOptions &options = USDLoadOptions())`: Loads a USD file (USDA, USDC, USDZ) into a `Stage` object.
*   `bool LoadUSDFromMemory(const uint8_t *addr, const size_t length, const std::string &filename, Stage *stage, std::string *warn, std::string *err, const USDLoadOptions &options = USDLoadOptions())`: Loads USD data from a memory buffer.
*   `bool LoadUSDZFromFile(const std::string &filename, Stage *stage, std::string *warn, std::string *err, const USDLoadOptions &options = USDLoadOptions())`: Specifically loads a USDZ archive.
*   `bool LoadUSDZFromMemory(const uint8_t *addr, const size_t length, const std::string &filename, Stage *stage, std::string *warn, std::string *err, const USDLoadOptions &options = USDLoadOptions())`: Loads a USDZ archive from memory.
*   `bool LoadUSDCFromFile(...)`, `bool LoadUSDCFromMemory(...)`: For USDC files/memory.
*   `bool LoadUSDAFromFile(...)`, `bool LoadUSDAFromMemory(...)`: For USDA files/memory.

**Layers (for composition):**

Similar loading functions are available for loading data as `Layer` objects, which are used in composition:
*   `LoadLayerFromFile(...)`
*   `LoadLayerFromMemory(...)`
*   `LoadUSDALayerFromMemory(...)`
*   `LoadUSDCLayerFromMemory(...)`
*   `LoadLayerFromAsset(...)` (uses `AssetResolutionResolver`)


### 1.2. Core Data Structures

*   **`Stage`**: Represents the entire USD scene graph. It holds the root prims and scene-level metadata. (Defined in `stage.hh`, included by `tinyusdz.hh`)
    *   `root_prims()`: Access root prims.
    *   `metas()`: Access stage metadata.
    *   `ExportToString()`: Serializes the stage to a string (usually USDA).
*   **`Prim`**: Represents an object in the scene graph (e.g., a mesh, a light, a transform). Prims have a type, a path, properties (attributes and relationships), and children. (Defined in `prim.hh`, included by `prim-types.hh`)
    *   `prim_type_name()`: Get the Prim type (e.g., "Xform", "Mesh").
    *   `element_name()`: Get the name of the Prim.
    *   `absolute_path()`: Get the absolute path of the Prim in the stage.
    *   `GetProperties()`: Get all properties.
    *   `GetProperty(name)`: Get a specific property by name.
    *   `GetAttribute(name)`: Get a specific attribute.
    *   `GetRelationship(name)`: Get a specific relationship.
    *   `children()`: Access child prims.
*   **`Attribute`**: A property that holds data (e.g., mesh points, material color). Attributes have a type and can store a single value or time-sampled values. (Defined in `property.hh`, included by `prim-types.hh`)
    *   `get_type_name()`: Returns the value type as a string (e.g., "float3[]", "token").
    *   `get_value()`: Access the attribute's value (if not time-sampled or connection).
    *   `get_connection_path()` / `get_connection_paths()`: If it's a connection.
    *   `get_samples()`: If it's time-sampled.
*   **`Relationship`**: A property that defines a connection or link to other prims or properties. (Defined in `property.hh`, included by `prim-types.hh`)
    *   `target_paths()`: A list of paths to the target(s) of the relationship.
*   **`USDLoadOptions`**: A struct to control various aspects of the loading process:
    *   `num_threads`: Number of threads for parsing.
    *   `max_memory_limit_in_mb`: Memory budget.
    *   `load_assets`: Whether to load associated assets (textures, etc.). (Deprecated)
    *   `do_composition`, `load_sublayers`, `load_references`, `load_payloads`: Flags for controlling composition (Deprecated in favor of more explicit layer manipulation).
    *   `max_image_width`, `max_image_height`, `max_image_channels`: Constraints for image loading.
    *   `fileformats`: Map for user-defined file format handlers.
    *   `upAxis`: Scene's up-axis.

### 1.3. USDZ Asset Handling

*   **`USDZAsset`**: Structure to hold information about assets within a USDZ archive.
    *   `asset_map`: Maps asset names to their byte ranges within the USDZ data.
    *   `data` / `addr`, `size`: Holds or points to the USDZ archive data.
*   `ReadUSDZAssetInfoFromFile(...)` and `ReadUSDZAssetInfoFromMemory(...)`: Read the asset map from a USDZ file or memory.
*   `SetupUSDZAssetResolution(...)`: Utility to configure an `AssetResolutionResolver` to load assets from a `USDZAsset`.
*   `USDZResolveAsset`, `USDZSizeAsset`, `USDZReadAsset`: Default callback functions for asset resolution from USDZ.

### 1.4. Format Checking

*   `IsUSD(filename/memory, &detected_format_string)`
*   `IsUSDA(filename/memory)`
*   `IsUSDC(filename/memory)`
*   `IsUSDZ(filename/memory)`

### 1.5. Example Usage (Conceptual, based on README)

```cpp
#include "tinyusdz.hh"
#include <iostream>
#include "pprinter.hh" // For printing stage/prim details

int main(int argc, char **argv) {
  std::string filename = "input.usd";
  if (argc > 1) {
    filename = argv[1];
  }

  tinyusdz::Stage stage;
  std::string warn;
  std::string err;
  tinyusdz::USDLoadOptions options;
  // Configure options if needed

  bool ret = tinyusdz::LoadUSDFromFile(filename, &stage, &warn, &err, options);

  if (!warn.empty()) {
    std::cout << "WARN : " << warn << std::endl;
  }

  if (!ret) {
    if (!err.empty()) {
      std::cerr << "ERR : " << err << std::endl;
    }
    return EXIT_FAILURE;
  }

  // Print Stage (Scene graph)
  // std::cout << tinyusdz::to_string(stage) << std::endl; // Using pprinter.hh
  // Or, manually traverse:
  for (const tinyusdz::Prim &root_prim : stage.root_prims()) {
    std::cout << root_prim.absolute_path().full_path_name() << std::endl;
    // Traverse children, inspect properties, etc.
  }

  return EXIT_SUCCESS;
}
```
