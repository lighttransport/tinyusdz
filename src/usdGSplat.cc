// SPDX-License-Identifier: Apache 2.0
// Copyright 2024, Light Transport Entertainment Inc.

#include "usdGSplat.hh"

#include <cmath>
#include <algorithm>
#include <cstdio>

#include "usdGeom.hh"
#include "io-util.hh"
#include "str-util.hh"
#include "external/spz/load-spz.h"
#include "external/spz/splat-types.h"

namespace tinyusdz {
namespace usdGSplat {

namespace {

// Convert LoadOptions::CoordinateSystem to spz::CoordinateSystem
spz::CoordinateSystem ToSPZCoordinateSystem(LoadOptions::CoordinateSystem cs) {
  switch (cs) {
    case LoadOptions::CoordinateSystem::LDB: return spz::CoordinateSystem::LDB;
    case LoadOptions::CoordinateSystem::RDB: return spz::CoordinateSystem::RDB;
    case LoadOptions::CoordinateSystem::LUB: return spz::CoordinateSystem::LUB;
    case LoadOptions::CoordinateSystem::RUB: return spz::CoordinateSystem::RUB;
    case LoadOptions::CoordinateSystem::LDF: return spz::CoordinateSystem::LDF;
    case LoadOptions::CoordinateSystem::RDF: return spz::CoordinateSystem::RDF;
    case LoadOptions::CoordinateSystem::LUF: return spz::CoordinateSystem::LUF;
    case LoadOptions::CoordinateSystem::RUF: return spz::CoordinateSystem::RUF;
    default: return spz::CoordinateSystem::UNSPECIFIED;
  }
}

// Convert SPZ GaussianCloud to USD GeomPoints with gsplat primvars
bool ConvertSPZToGeomPoints(
    const spz::GaussianCloud &cloud,
    GeomPoints *prim,
    const LoadOptions &options,
    std::string *warn,
    std::string *err) {

  if (!prim) {
    if (err) *err = "prim is nullptr";
    return false;
  }

  if (cloud.numPoints <= 0) {
    if (err) *err = "GaussianCloud has no points";
    return false;
  }

  // Check memory limit
  size_t estimatedMemory =
      cloud.positions.size() * sizeof(float) +
      cloud.scales.size() * sizeof(float) +
      cloud.rotations.size() * sizeof(float) +
      cloud.alphas.size() * sizeof(float) +
      cloud.colors.size() * sizeof(float) +
      cloud.sh.size() * sizeof(float);

  if (options.maxMemoryBytes > 0 && estimatedMemory > options.maxMemoryBytes) {
    if (err) {
      *err = "Memory limit exceeded: estimated " + std::to_string(estimatedMemory) +
             " bytes, limit is " + std::to_string(options.maxMemoryBytes) + " bytes";
    }
    return false;
  }

  // Convert positions from SPZ to USD
  // SPZ stores as flat float array: [x0, y0, z0, x1, y1, z1, ...]
  // USD GeomPoints uses point3f array
  std::vector<value::point3f> positions(cloud.numPoints);
  for (int32_t i = 0; i < cloud.numPoints; i++) {
    positions[i].x = cloud.positions[i * 3 + 0];
    positions[i].y = cloud.positions[i * 3 + 1];
    positions[i].z = cloud.positions[i * 3 + 2];
  }

  // Set positions as the main points attribute
  prim->points.set_value(positions);

  // Convert scales (SPZ stores on log scale, we store actual scale values)
  // SPZ: log-scale float array [sx0, sy0, sz0, sx1, sy1, sz1, ...]
  // USD: primvars:gsplat:scales as float3[]
  std::vector<value::float3> scales(cloud.numPoints);
  for (int32_t i = 0; i < cloud.numPoints; i++) {
    // SPZ stores scales on log scale, convert to actual scale
    scales[i][0] = std::exp(cloud.scales[i * 3 + 0]);
    scales[i][1] = std::exp(cloud.scales[i * 3 + 1]);
    scales[i][2] = std::exp(cloud.scales[i * 3 + 2]);
  }

  // Create property with type name and set value (without Animatable wrapper)
  {
    Property prop = Property::MakeEmptyAttrib("float3[]", /* custom */ false);
    prop.attribute().set_var(scales);
    prim->props["primvars:gsplat:scales"] = std::move(prop);
  }

  // Convert rotations
  // SPZ: quaternion as [x, y, z, w] flat array
  // USD: primvars:gsplat:rotations as quatf[]
  std::vector<value::quatf> rotations(cloud.numPoints);
  for (int32_t i = 0; i < cloud.numPoints; i++) {
    // SPZ uses [x, y, z, w] convention
    rotations[i].imag[0] = cloud.rotations[i * 4 + 0];
    rotations[i].imag[1] = cloud.rotations[i * 4 + 1];
    rotations[i].imag[2] = cloud.rotations[i * 4 + 2];
    rotations[i].real = cloud.rotations[i * 4 + 3];
  }

  {
    Property prop = Property::MakeEmptyAttrib("quatf[]", /* custom */ false);
    prop.attribute().set_var(rotations);
    prim->props["primvars:gsplat:rotations"] = std::move(prop);
  }

  // Convert alphas
  // SPZ: inverse logistic (pre-activation)
  // USD: primvars:gsplat:alphas as float[] (in [0, 1] range)
  std::vector<float> alphas(cloud.numPoints);
  for (int32_t i = 0; i < cloud.numPoints; i++) {
    // SPZ stores alpha before sigmoid activation
    // Compute sigmoid(alpha) to get value in [0, 1]
    float a = cloud.alphas[i];
    alphas[i] = 1.0f / (1.0f + std::exp(-a));
  }

  {
    Property prop = Property::MakeEmptyAttrib("float[]", /* custom */ false);
    prop.attribute().set_var(alphas);
    prim->props["primvars:gsplat:alphas"] = std::move(prop);
  }

  // Convert colors (SH DC component to actual color)
  // SPZ: rgb as SH DC component
  // USD: primvars:gsplat:sh_l0 as float3[]
  std::vector<value::float3> sh_l0(cloud.numPoints);
  for (int32_t i = 0; i < cloud.numPoints; i++) {
    // SPZ stores colors as SH DC component
    // Compute 0.5 + 0.282095 * x to get color value between 0 and 1
    // But we store it as the SH coefficient directly
    sh_l0[i][0] = cloud.colors[i * 3 + 0];
    sh_l0[i][1] = cloud.colors[i * 3 + 1];
    sh_l0[i][2] = cloud.colors[i * 3 + 2];
  }

  {
    Property prop = Property::MakeEmptyAttrib("float3[]", /* custom */ false);
    prop.attribute().set_var(sh_l0);
    prim->props["primvars:gsplat:sh_l0"] = std::move(prop);
  }

  // Convert spherical harmonics coefficients (if present)
  if (cloud.shDegree > 0 && !cloud.sh.empty()) {
    // SPZ stores SH coefficients in a specific order:
    // For each point, all coefficients for R, then G, then B channels
    // Coefficient order depends on shDegree:
    //   degree 1: 9 values  (3 coeffs × 3 channels)
    //   degree 2: 24 values (8 coeffs × 3 channels)
    //   degree 3: 45 values (15 coeffs × 3 channels)

    int32_t numCoeffsPerChannel = 0;
    if (cloud.shDegree == 1) numCoeffsPerChannel = 3;
    else if (cloud.shDegree == 2) numCoeffsPerChannel = 8;
    else if (cloud.shDegree == 3) numCoeffsPerChannel = 15;

    if (numCoeffsPerChannel > 0) {
      // SPZ format: sh[i * numCoeffs + j * 3 + c] where j is coeff index, c is channel (RGB)
      // We need to extract each SH band separately

      // Extract sh_l1 (first 3 coefficients for degree >= 1)
      if (cloud.shDegree >= 1) {
        std::vector<value::float3> sh_l1(cloud.numPoints);
        // SPZ: sh1n1_r, sh1n1_g, sh1n1_b, sh10_r, sh10_g, sh10_b, sh1p1_r, sh1p1_g, sh1p1_b
        // We store as float3 array where each float3 has one coefficient for [r, g, b]
        // Actually, looking at the format more carefully, we should store all 3 coefficients
        // Let's check the exact layout... For now, skip higher order SH
        // TODO: Properly extract sh_l1, sh_l2, sh_l3
        if (warn) {
          *warn += "Higher order spherical harmonics (degree > 0) not yet fully implemented\n";
        }
      }
    }
  }

  // Store shDegree as uniform int
  {
    Property prop = Property::MakeEmptyAttrib("int", /* custom */ false);
    prop.attribute().set_var(cloud.shDegree);
    prim->props["primvars:gsplat:shDegree"] = std::move(prop);
  }

  return true;
}

} // anonymous namespace

bool IsSPZFile(const std::string &filepath) {
  return endsWith(filepath, ".spz") || endsWith(filepath, ".SPZ");
}

bool IsPLYFile(const std::string &filepath) {
  return endsWith(filepath, ".ply") || endsWith(filepath, ".PLY");
}

bool GetSPZInfo(
    const std::string &filepath,
    int32_t *numPoints,
    int32_t *shDegree,
    bool *antialiased,
    std::string *err) {

  // Load just the header information using the packed format
  spz::PackedGaussians packed = spz::loadSpzPacked(filepath);

  if (packed.numPoints == 0) {
    if (err) *err = "Failed to load SPZ file or file is empty";
    return false;
  }

  if (numPoints) *numPoints = packed.numPoints;
  if (shDegree) *shDegree = packed.shDegree;
  if (antialiased) *antialiased = packed.antialiased;

  return true;
}

bool ReadSPZFromFile(
    const std::string &filepath,
    GeomPoints *prim,
    const LoadOptions &options,
    std::string *warn,
    std::string *err) {

  if (!prim) {
    if (err) *err = "prim is nullptr";
    return false;
  }

  // Check file exists
  if (!io::FileExists(filepath)) {
    if (err) *err = "File does not exist: " + filepath;
    return false;
  }

  // Load SPZ file using the SPZ library
  spz::UnpackOptions unpackOpts;
  unpackOpts.to = ToSPZCoordinateSystem(options.targetCoordinateSystem);

  spz::GaussianCloud cloud = spz::loadSpz(filepath, unpackOpts);

  if (cloud.numPoints == 0) {
    if (err) *err = "Failed to load SPZ file or file is empty: " + filepath;
    return false;
  }

  // Convert to GeomPoints with gsplat primvars
  return ConvertSPZToGeomPoints(cloud, prim, options, warn, err);
}

bool ReadSPZFromMemory(
    const uint8_t *data,
    size_t size,
    GeomPoints *prim,
    const std::string &filename,
    const LoadOptions &options,
    std::string *warn,
    std::string *err) {

  if (!prim) {
    if (err) *err = "prim is nullptr";
    return false;
  }

  if (!data || size == 0) {
    if (err) *err = "data is nullptr or size is 0";
    return false;
  }

  // Load SPZ from memory using the SPZ library
  spz::UnpackOptions unpackOpts;
  unpackOpts.to = ToSPZCoordinateSystem(options.targetCoordinateSystem);

  spz::GaussianCloud cloud = spz::loadSpz(data, static_cast<int32_t>(size), unpackOpts);

  if (cloud.numPoints == 0) {
    if (err) {
      *err = "Failed to load SPZ from memory";
      if (!filename.empty()) {
        *err += " (filename: " + filename + ")";
      }
    }
    return false;
  }

  // Convert to GeomPoints with gsplat primvars
  return ConvertSPZToGeomPoints(cloud, prim, options, warn, err);
}

bool ReadPLYFromFile(
    const std::string &filepath,
    GeomPoints *prim,
    const LoadOptions &options,
    std::string *warn,
    std::string *err) {

  if (!prim) {
    if (err) *err = "prim is nullptr";
    return false;
  }

  // Check file exists
  if (!io::FileExists(filepath)) {
    if (err) *err = "File does not exist: " + filepath;
    return false;
  }

  // Load PLY file using the SPZ library
  spz::UnpackOptions unpackOpts;
  unpackOpts.to = ToSPZCoordinateSystem(options.targetCoordinateSystem);

  spz::GaussianCloud cloud = spz::loadSplatFromPly(filepath, unpackOpts);

  if (cloud.numPoints == 0) {
    if (err) *err = "Failed to load PLY file or file is empty: " + filepath;
    return false;
  }

  // Convert to GeomPoints with gsplat primvars
  return ConvertSPZToGeomPoints(cloud, prim, options, warn, err);
}

//
// FileFormat handler functions
//

bool CheckSPZFormat(
    const Asset &asset,
    std::string *warn,
    std::string *err,
    void *user_data) {

  (void)warn;
  (void)user_data;

  if (asset.size() < 16) {
    if (err) *err = "Asset too small to be a valid SPZ file (< 16 bytes)";
    return false;
  }

  // Check SPZ magic number: 0x5053474e ("PSGN" in little-endian)
  const uint8_t *data = asset.data();
  uint32_t magic = static_cast<uint32_t>(data[0]) |
                   (static_cast<uint32_t>(data[1]) << 8) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[3]) << 24);

  if (magic != 0x5053474e) {
    if (err) {
      *err = "Invalid SPZ magic number. Expected 0x5053474e, got 0x" +
             std::to_string(magic);
    }
    return false;
  }

  return true;
}

bool ReadSPZAsset(
    const Asset &asset,
    PrimSpec &ps,
    std::string *warn,
    std::string *err,
    void *user_data) {

  // Get loading options from user_data if provided
  LoadOptions options;
  if (user_data) {
    options = *static_cast<const LoadOptions*>(user_data);
  }

  // Create a GeomPoints prim
  GeomPoints geomPoints;

  // Load SPZ from asset data
  if (!ReadSPZFromMemory(asset.data(), asset.size(), &geomPoints,
                         "", options, warn, err)) {
    return false;
  }

  // Convert GeomPoints to PrimSpec
  // Set the prim type
  ps.typeName() = "Points";

  // Set the name (can be overridden by the caller)
  if (ps.name().empty()) {
    ps.name() = "gsplat";
  }

  // Convert GeomPoints properties to PrimSpec properties
  for (const auto &prop : geomPoints.props) {
    ps.props()[prop.first] = prop.second;
  }

  // Note: The points attribute is already in GeomPoints.points and doesn't need
  // to be added to props. The primvars (scales, rotations, alphas, sh_l0, etc.)
  // are what go in props[].  The main points attribute is a core attribute of
  // GeomPoints that gets handled separately during USD composition.

  return true;
}

FileFormatHandler CreateSPZFileFormatHandler(const LoadOptions &options) {
  FileFormatHandler handler;
  handler.extension = "spz";
  handler.description = "Niantic SPZ Gaussian Splat format";
  handler.checker = CheckSPZFormat;
  handler.reader = ReadSPZAsset;
  handler.writer = nullptr; // Writing not supported yet

  // Copy options to heap so they persist
  LoadOptions *options_copy = new LoadOptions(options);
  handler.userdata = options_copy;

  return handler;
}

bool CheckPLYFormat(
    const Asset &asset,
    std::string *warn,
    std::string *err,
    void *user_data) {

  (void)warn;
  (void)user_data;

  if (asset.size() < 4) {
    if (err) *err = "Asset too small to be a valid PLY file (< 4 bytes)";
    return false;
  }

  // Check PLY magic: "ply\n" or "ply\r"
  const uint8_t *data = asset.data();
  if (data[0] == 'p' && data[1] == 'l' && data[2] == 'y' &&
      (data[3] == '\n' || data[3] == '\r')) {
    return true;
  }

  if (err) {
    *err = "Invalid PLY magic. Expected 'ply' header.";
  }
  return false;
}

bool ReadPLYAsset(
    const Asset &asset,
    PrimSpec &ps,
    std::string *warn,
    std::string *err,
    void *user_data) {

  // Get loading options from user_data if provided
  LoadOptions options;
  if (user_data) {
    options = *static_cast<const LoadOptions*>(user_data);
  }

  // PLY loading from memory requires writing to temp file
  // since spz::loadSplatFromPly expects a file path
  // TODO: Enhance SPZ library to support memory-based PLY loading
  // For now, write to temporary file
  std::string tempPath = "/tmp/tinyusdz_ply_temp_" +
                         std::to_string(reinterpret_cast<uintptr_t>(asset.data())) + ".ply";

  if (!io::WriteWholeFile(tempPath, asset.data(), asset.size(), err)) {
    if (err) *err = "Failed to write PLY asset to temporary file: " + *err;
    return false;
  }

  // Create a GeomPoints prim
  GeomPoints geomPoints;

  // Load PLY from temporary file
  bool success = ReadPLYFromFile(tempPath, &geomPoints, options, warn, err);

  // Clean up temporary file
  std::remove(tempPath.c_str());

  if (!success) {
    return false;
  }

  // Convert GeomPoints to PrimSpec
  ps.typeName() = "Points";

  if (ps.name().empty()) {
    ps.name() = "gsplat";
  }

  // Convert GeomPoints properties to PrimSpec properties
  for (const auto &prop : geomPoints.props) {
    ps.props()[prop.first] = prop.second;
  }

  // Note: The points attribute is already in GeomPoints.points and doesn't need
  // to be added to props. The primvars (scales, rotations, alphas, sh_l0, etc.)
  // are what go in props[].  The main points attribute is a core attribute of
  // GeomPoints that gets handled separately during USD composition.

  return true;
}

FileFormatHandler CreatePLYFileFormatHandler(const LoadOptions &options) {
  FileFormatHandler handler;
  handler.extension = "ply";
  handler.description = "PLY Gaussian Splat format";
  handler.checker = CheckPLYFormat;
  handler.reader = ReadPLYAsset;
  handler.writer = nullptr; // Writing not supported yet

  // Copy options to heap so they persist
  LoadOptions *options_copy = new LoadOptions(options);
  handler.userdata = options_copy;

  return handler;
}

} // namespace usdGSplat
} // namespace tinyusdz
