// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#pragma once

// Internal implementation header for render-data module
// Contains helper functions and utilities used across the split modules

#include <vector>
#include <string>

#include "render-data.hh"
#include "value-types.hh"
#include "usdGeom.hh"
#include "common-utils.hh"

namespace tinyusdz {
namespace tydra {
namespace detail {

///
/// Convert scalar value to VertexAttribute
///
template <typename T>
bool ScalarValueToVertexAttribute(
    const T& value,
    const std::string& name,
    VertexAttribute& attr);

///
/// Convert array value to VertexAttribute
///
template <typename T>
bool ArrayValueToVertexAttribute(
    const std::vector<T>& values,
    const std::string& name,
    VertexVariability variability,
    VertexAttribute& attr);

///
/// General conversion to VertexAttribute
///
bool ToVertexAttribute(
    const Animatable<Attribute>& attr,
    const std::string& name,
    VertexAttribute& vertex_attr,
    std::string* err);

///
/// Triangulate vertex attribute data
///
inline bool TriangulateVertexAttribute(
    VertexAttribute& vattr,
    const std::vector<uint32_t>& triangulatedFaceVertexCounts,
    const std::vector<size_t>& triangulatedToOrigFaceVertexIndexMap) {
  // TODO: Implement proper triangulation of vertex attributes
  // For now, just return true as placeholder
  return true;
}

///
/// Get material binding GeomSubsets
///
inline bool GetMaterialBindGeomSubsets(
    const GeomMesh& mesh,
    std::vector<MaterialSubset>& subsets,
    std::string* err) {
  // TODO: Implement extraction of material binding subsets from mesh
  // For now, just return true with empty subsets
  (void)mesh;
  (void)err;
  subsets.clear();
  return true;
}

///
/// List UV names from materials
///
inline bool ListUVNames(const RenderMaterial& material,
                const std::vector<UVTexture>& textures,
                StringAndIdMap& si_map) {
  // TODO: Implement UV name extraction from materials
  // For now, just return true
  (void)material;
  (void)textures;
  (void)si_map;
  return true;
}

///
/// Raw asset read for textures
///
bool RawAssetRead(const AssetResolutionResolver& resolver,
                  const std::string& asset_path,
                  std::vector<uint8_t>& data,
                  std::string* warn,
                  std::string* err);

///
/// Default texture image loader function
///
bool DefaultTextureImageLoaderFunction(
    const std::string& filename,
    TextureImage* texture,
    std::string* warn,
    std::string* err);

///
/// Infer color space from input name
///
ColorSpace InferColorSpace(const std::string& input_name);

} // namespace detail
} // namespace tydra
} // namespace tinyusdz