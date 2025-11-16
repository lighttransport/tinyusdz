// SPDX-License-Identifier: Apache 2.0
// Copyright 2024, Light Transport Entertainment Inc.

///
/// @file usdGSplat.hh
/// @brief Gaussian Splatting file format support
///
/// Built-in Gaussian splat format import plugin.
/// Supports .spz (Niantic SPZ format) and potentially other formats in the future.
/// Import only. Writing gsplat data to external formats is not yet supported.
///
/// Example usage:
///
/// def Points "splat" (
///   prepend references = @model.spz@
/// )
/// {
///    ...
/// }
///
/// The imported data will be converted to GeomPoints with primvars:gsplat:* attributes
/// compatible with the Gaussian splat rendering pipeline.
///

#pragma once

#include <string>
#include <vector>

#include "prim-types.hh"
#include "value-types.hh"
#include "asset-resolution.hh"

namespace tinyusdz {

// Forward declaration
class GeomPoints;
struct Asset;
struct PrimSpec;

namespace usdGSplat {

/// Options for loading Gaussian splat files
struct LoadOptions {
  /// Target coordinate system for the loaded data
  /// Default is RUB (OpenGL/Three.js convention)
  enum class CoordinateSystem {
    UNSPECIFIED = 0,
    LDB = 1,  // Left Down Back
    RDB = 2,  // Right Down Back
    LUB = 3,  // Left Up Back
    RUB = 4,  // Right Up Back, Three.js coordinate system (default)
    LDF = 5,  // Left Down Front
    RDF = 6,  // Right Down Front, PLY coordinate system
    LUF = 7,  // Left Up Front, GLB coordinate system
    RUF = 8,  // Right Up Front, Unity coordinate system
  };

  CoordinateSystem targetCoordinateSystem = CoordinateSystem::RUB;

  /// Whether to apply antialiasing (mip splatting)
  bool enableAntialiasing = false;

  /// Memory limit for loading (0 = no limit)
  size_t maxMemoryBytes = 0;
};

/// Load Gaussian splat from .spz file and convert to GeomPoints with gsplat primvars
/// @param filepath Path to the .spz file
/// @param prim Output GeomPoints primitive (will be populated with gsplat data)
/// @param options Loading options
/// @param warn Output warning messages
/// @param err Output error messages
/// @return true on success, false on failure
bool ReadSPZFromFile(
    const std::string &filepath,
    GeomPoints *prim,
    const LoadOptions &options = LoadOptions(),
    std::string *warn = nullptr,
    std::string *err = nullptr);

/// Load Gaussian splat from .ply file and convert to GeomPoints with gsplat primvars
/// @param filepath Path to the .ply file (binary PLY format with gaussian splat data)
/// @param prim Output GeomPoints primitive (will be populated with gsplat data)
/// @param options Loading options
/// @param warn Output warning messages
/// @param err Output error messages
/// @return true on success, false on failure
bool ReadPLYFromFile(
    const std::string &filepath,
    GeomPoints *prim,
    const LoadOptions &options = LoadOptions(),
    std::string *warn = nullptr,
    std::string *err = nullptr);

/// Load Gaussian splat from .spz data in memory
/// @param data Pointer to .spz file data
/// @param size Size of data in bytes
/// @param prim Output GeomPoints primitive
/// @param filename Optional filename for error messages
/// @param options Loading options
/// @param warn Output warning messages
/// @param err Output error messages
/// @return true on success, false on failure
bool ReadSPZFromMemory(
    const uint8_t *data,
    size_t size,
    GeomPoints *prim,
    const std::string &filename = "",
    const LoadOptions &options = LoadOptions(),
    std::string *warn = nullptr,
    std::string *err = nullptr);

/// Check if a file has .spz extension
/// @param filepath Path to check
/// @return true if file has .spz extension
bool IsSPZFile(const std::string &filepath);

/// Check if a file has .ply extension
/// @param filepath Path to check
/// @return true if file has .ply extension
bool IsPLYFile(const std::string &filepath);

/// Get information about an .spz file without fully loading it
/// @param filepath Path to the .spz file
/// @param numPoints Output number of gaussian splats
/// @param shDegree Output spherical harmonics degree
/// @param antialiased Output whether antialiasing is enabled
/// @param err Output error messages
/// @return true on success, false on failure
bool GetSPZInfo(
    const std::string &filepath,
    int32_t *numPoints,
    int32_t *shDegree,
    bool *antialiased,
    std::string *err = nullptr);

//
// FileFormat handler functions for asset resolution
//

/// Check if an asset is a valid SPZ file
/// @param asset Asset data to check
/// @param warn Output warning messages
/// @param err Output error messages
/// @param user_data User data (can be LoadOptions*)
/// @return true if valid SPZ file
bool CheckSPZFormat(
    const Asset &asset,
    std::string *warn,
    std::string *err,
    void *user_data);

/// Read SPZ asset and convert to PrimSpec
/// @param asset Asset data to read
/// @param ps Output PrimSpec (will contain GeomPoints with gsplat data)
/// @param warn Output warning messages
/// @param err Output error messages
/// @param user_data User data (can be LoadOptions*)
/// @return true on success
bool ReadSPZAsset(
    const Asset &asset,
    PrimSpec &ps,
    std::string *warn,
    std::string *err,
    void *user_data);

/// Create a FileFormatHandler for SPZ files
/// @param options Optional loading options
/// @return FileFormatHandler configured for .spz files
FileFormatHandler CreateSPZFileFormatHandler(const LoadOptions &options = LoadOptions());

/// Check if an asset is a valid PLY file with gaussian splat data
/// @param asset Asset data to check
/// @param warn Output warning messages
/// @param err Output error messages
/// @param user_data User data (can be LoadOptions*)
/// @return true if valid PLY file
bool CheckPLYFormat(
    const Asset &asset,
    std::string *warn,
    std::string *err,
    void *user_data);

/// Read PLY asset and convert to PrimSpec
/// @param asset Asset data to read
/// @param ps Output PrimSpec (will contain GeomPoints with gsplat data)
/// @param warn Output warning messages
/// @param err Output error messages
/// @param user_data User data (can be LoadOptions*)
/// @return true on success
bool ReadPLYAsset(
    const Asset &asset,
    PrimSpec &ps,
    std::string *warn,
    std::string *err,
    void *user_data);

/// Create a FileFormatHandler for PLY files
/// @param options Optional loading options
/// @return FileFormatHandler configured for .ply files
FileFormatHandler CreatePLYFileFormatHandler(const LoadOptions &options = LoadOptions());

} // namespace usdGSplat
} // namespace tinyusdz
