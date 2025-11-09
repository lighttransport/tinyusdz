// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
#pragma once

#include <string>
#include <vector>
#include "nonstd/expected.hpp"
#include "../value-types.hh"

namespace tinyusdz {
// Forward declarations
template<typename T> struct Animatable;
template<typename T> struct TypedTimeSamples;
}  // namespace tinyusdz

namespace tinyusdz {
namespace tydra {
namespace utils {

//
// Common error handling macro
//
#define TYDRA_PUSH_ERROR(err_ptr, msg) \
  if (err_ptr) {                       \
    (*(err_ptr)) += msg;               \
  }

//
// Data conversion utilities for vertex attributes
//

/// Convert vertex attribute with Uniform variability to facevarying variability
template <typename T>
nonstd::expected<std::vector<T>, std::string> UniformToFaceVarying(
    const std::vector<T> &inputs,
    const std::vector<uint32_t> &faceVertexCounts);

/// Convert vertex attribute with Uniform variability to vertex variability
template <typename T>
nonstd::expected<std::vector<T>, std::string> UniformToVertex(
    const std::vector<T> &inputs,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices);

/// Convert vertex attribute from vertex variability to facevarying variability
template <typename T>
nonstd::expected<std::vector<T>, std::string> VertexToFaceVarying(
    const std::vector<T> &inputs,
    const std::vector<uint32_t> &faceVertexIndices);

/// Convert constant attribute to facevarying variability
template <typename T>
nonstd::expected<std::vector<T>, std::string> ConstantToFaceVarying(
    const std::vector<T> &src, 
    const std::vector<uint32_t> &faceVertexCounts);

/// Convert constant attribute to vertex variability
nonstd::expected<std::vector<uint8_t>, std::string> ConstantToVertex(
    const std::vector<uint8_t> &src, 
    uint32_t elementSize, 
    size_t numVertices);

/// Convert constant attribute to facevarying for byte data
nonstd::expected<std::vector<uint8_t>, std::string> ConstantToFaceVarying(
    const std::vector<uint8_t> &src, 
    uint32_t elementSize,
    const std::vector<uint32_t> &faceVertexCounts);

//
// Vertex similarity and optimization utilities
//

/// Try to convert facevarying data to vertex data by checking vertex similarity
/// Returns true if conversion was successful
template <typename T>
bool TryConvertFacevaryingToVertex(
    const std::vector<T> &facevarying_data,
    const std::vector<uint32_t> &face_vertex_indices,
    std::vector<T> *vertex_data,
    std::vector<uint32_t> *vertex_indices,
    float epsilon = 0.0f);

//
// Memory management utilities
//

/// Move vector data and clear source to save memory
template<typename T>
void MoveAndClearVector(std::vector<T>& src, std::vector<T>& dst) {
  dst = std::move(src);
  src.clear();
  src.shrink_to_fit();
}

/// Extract data from Animatable and optionally clear source  
template<typename T>
void ExtractAnimatableData(const Animatable<T>& src, 
                          T* default_val, 
                          TypedTimeSamples<T>* ts,
                          bool clear_source = false);

//
// String and conversion utilities
//

/// Convert UVTexture channel enum to string
std::string ChannelToString(int channel_value);

/// Validate and sanitize asset paths (handle backslashes etc.)
std::string SanitizeAssetPath(const std::string& path, bool allow_backslashes = true);

}  // namespace utils
}  // namespace tydra  
}  // namespace tinyusdz

