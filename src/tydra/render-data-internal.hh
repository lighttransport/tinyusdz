// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file render-data-internal.hh
/// @brief Internal shared declarations for render-data modules
///

#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "asset-resolution.hh"
#include "common.hh"
#include "image-loader.hh"
#include "io-util.hh"
#include "math-util.inc"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usdSkel.hh"
#include "value-types.hh"

#include "common-macros.inc"

#include "render-data.hh"
#include "scene-access.hh"
#include "shader-network.hh"

namespace tinyusdz {
namespace tydra {

//
// Internal error handling macros
//

#define PUSH_ERROR_AND_RETURN(msg) \
  do {                             \
    _err += msg;                   \
    _err += "\n";                  \
    return false;                  \
  } while (0)

#define PUSH_WARN(msg) \
  do {                 \
    _warn += msg;      \
    _warn += "\n";     \
  } while (0)

//
// Internal helper function declarations
//

// Forward declarations for internal types
struct RenderSceneConverterEnv;

// Variability conversion helpers (render-attribute-converter.cc)
template <typename T>
bool UniformToVertex(const std::vector<uint32_t> &faceVertexCounts,
                     const std::vector<T> &uniform_data,
                     std::vector<T> &vertex_data);

template <typename T>
bool UniformToFaceVarying(const std::vector<uint32_t> &faceVertexCounts,
                          const std::vector<T> &uniform_data,
                          std::vector<T> &facevarying_data);

template <typename T>
bool VertexToFaceVarying(const std::vector<uint32_t> &faceVertexCounts,
                         const std::vector<uint32_t> &faceVertexIndices,
                         const std::vector<T> &vertex_data,
                         std::vector<T> &facevarying_data);

template <typename T>
bool ConstantToVertex(const std::vector<uint32_t> &faceVertexCounts,
                      const std::vector<uint32_t> &faceVertexIndices,
                      const T &constant_data,
                      std::vector<T> &vertex_data);

// Helper to check if two floating point values are approximately equal
template <typename T>
inline bool IsNearlyEqual(T a, T b, T epsilon = T(1e-6)) {
  return std::abs(a - b) <= epsilon;
}

// Check if a matrix4d is identity
bool IsIdentityMatrix(const value::matrix4d &m);

// Quaternion multiplication helper
value::quatf quat_mul(const value::quatf &a, const value::quatf &b);

// Quaternion from axis-angle
value::quatf to_quaternion(const value::float3 &axis, float angle_rad);

// Transform point by matrix4d
vec3 TransformPoint(const value::matrix4d &m, const vec3 &p);

// Transform direction (normal) by matrix4d
vec3 TransformNormal(const value::matrix4d &m, const vec3 &n);

// Internal texture loading helper
bool RawAssetRead(const value::AssetPath &assetPath,
                  const AssetInfo &assetInfo,
                  const AssetResolutionResolver &assetResolver,
                  Asset *asset,
                  std::string &resolvedPath,
                  std::string *warn,
                  std::string *err,
                  std::string *readErr);

}  // namespace tydra
}  // namespace tinyusdz
