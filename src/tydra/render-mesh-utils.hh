// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Mesh processing utilities for render scene conversion
// Extracted from render-data.cc

#pragma once

#include <vector>
#include <array>
#include <string>
#include <map>
#include "../value-types.hh"
#include "../prim-types.hh"
#include "render-data.hh"

namespace tinyusdz {
namespace tydra {

// Forward declarations
class RenderMesh;
class VertexAttribute;
struct RenderSceneConverterEnv;

// Mesh triangulation utilities
namespace mesh_utils {

// Triangulation result
struct TriangulationResult {
  std::vector<uint32_t> indices;
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> uvs;
  bool success = false;
  std::string error;
};

// Triangulate a polygon mesh
TriangulationResult TriangulatePolygon(
    const std::vector<float> &positions,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices);

// Convert facevarying attributes to vertex attributes
bool TryConvertFacevaryingToVertexFloat(
    const std::vector<float> &facevarying_values,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<float> &vertex_values);

bool TryConvertFacevaryingToVertexInt(
    const std::vector<int> &facevarying_values,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<int> &vertex_values);

template<typename T>
bool TryConvertFacevaryingToVertex(
    const std::vector<T> &facevarying_values,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    std::vector<T> &vertex_values);

// Triangulate vertex attributes
bool TriangulateVertexAttribute(
    const VertexAttribute &src_attr,
    const std::vector<uint32_t> &faceVertexCounts,
    const std::vector<uint32_t> &faceVertexIndices,
    const std::vector<size_t> &faceVertexIndexRemapper,
    VertexAttribute &dst_attr);

// Compute normals
struct NormalComputationOptions {
  bool smooth_normals = true;
  float crease_angle_degrees = 60.0f;
  bool use_face_normals_for_flat = false;
};

std::vector<float> ComputeNormals(
    const std::vector<float> &positions,
    const std::vector<uint32_t> &indices,
    const NormalComputationOptions &options = {});

// Compute geometric (face) normals
std::vector<float> GeometricNormal(
    const std::vector<float> &positions,
    const std::vector<uint32_t> &indices);

// Tangent space computation
struct TangentSpaceResult {
  std::vector<float> tangents;
  std::vector<float> binormals;
  bool success = false;
  std::string error;
};

TangentSpaceResult ComputeTangentsAndBinormals(
    const std::vector<float> &positions,
    const std::vector<float> &normals,
    const std::vector<float> &uvs,
    const std::vector<uint32_t> &indices);

// Material binding utilities
struct MaterialSubset {
  std::string name;
  std::vector<uint32_t> indices;
  Path material_path;
};

std::vector<MaterialSubset> GetMaterialBindGeomSubsets(
    const GeomMesh &mesh,
    const RenderSceneConverterEnv &env);

// UV and texture coordinate utilities
struct TextureCoordinate {
  std::string name;
  std::vector<float> values;
  uint32_t components = 2;  // 2 for UV, 3 for UVW
};

std::vector<TextureCoordinate> GetTextureCoordinates(
    const GeomMesh &mesh,
    const std::string &primaryUV = "st");

// List all UV attribute names in a mesh
std::vector<std::string> ListUVNames(const GeomMesh &mesh);

// Vertex attribute conversion
VertexAttribute ToVertexAttribute(
    const value::Value &val,
    const std::string &name,
    const std::string &interpolation);

bool ScalarValueToVertexAttribute(
    const value::Value &val,
    const std::string &name,
    VertexAttribute &attr);

bool ArrayValueToVertexAttribute(
    const value::Value &val,
    const std::string &name,
    const std::string &interpolation,
    VertexAttribute &attr);

} // namespace mesh_utils

} // namespace tydra
} // namespace tinyusdz