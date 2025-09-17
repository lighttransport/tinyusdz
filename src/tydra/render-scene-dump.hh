// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Debugging and dumping utilities for render scene
// Extracted from render-data.cc

#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <functional>
#include "render-data.hh"

namespace tinyusdz {
namespace tydra {

// Forward declarations
class RenderScene;
class RenderMesh;
class RenderMaterial;
class Node;
class Animation;
class Camera;
class Skeleton;
class BufferData;
class TextureImage;
class UVTexture;

namespace dump_utils {

// Dump configuration
struct DumpConfig {
  // Output settings
  bool use_colors = false;
  bool show_arrays = true;
  bool show_binary_data = false;
  bool compact_mode = false;
  int indent_width = 2;
  int max_array_elements = 10;
  int max_string_length = 80;
  
  // Content filters
  bool show_nodes = true;
  bool show_meshes = true;
  bool show_materials = true;
  bool show_animations = true;
  bool show_cameras = true;
  bool show_lights = true;
  bool show_skeletons = true;
  bool show_buffers = true;
  bool show_images = true;
  bool show_textures = true;
  
  // Detail levels
  enum DetailLevel {
    SUMMARY,   // Just counts and names
    NORMAL,    // Standard information
    DETAILED,  // All properties
    DEBUG      // Include internal state
  };
  DetailLevel detail_level = NORMAL;
};

// Dump functions for individual components
std::string DumpVertexAttribute(
    const VertexAttribute &attr,
    const DumpConfig &config = {});

std::string DumpVertexAttributeData(
    const VertexAttribute &attr,
    size_t max_elements = 10);

std::string DumpNode(
    const Node &node,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpMesh(
    const RenderMesh &mesh,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpMaterial(
    const RenderMaterial &material,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpMaterialSubset(
    const MaterialSubset &subset,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpAnimation(
    const Animation &animation,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpAnimChannel(
    const AnimationChannel &channel,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpCamera(
    const Camera &camera,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpLight(
    const Light &light,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpSkeleton(
    const Skeleton &skeleton,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpSkelNode(
    const SkelNode &node,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpBuffer(
    const BufferData &buffer,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpImage(
    const TextureImage &image,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpUVTexture(
    const UVTexture &texture,
    const DumpConfig &config = {},
    int indent = 0);

std::string DumpPreviewSurface(
    const PreviewSurface &surface,
    const DumpConfig &config = {},
    int indent = 0);

// Main dump function for entire scene
std::string DumpRenderScene(
    const RenderScene &scene,
    const DumpConfig &config = {});

// Stream output operators
std::ostream& operator<<(std::ostream &os, const RenderScene &scene);
std::ostream& operator<<(std::ostream &os, const RenderMesh &mesh);
std::ostream& operator<<(std::ostream &os, const RenderMaterial &material);
std::ostream& operator<<(std::ostream &os, const Node &node);

// Statistics gathering
struct SceneStatistics {
  size_t num_nodes = 0;
  size_t num_meshes = 0;
  size_t num_materials = 0;
  size_t num_textures = 0;
  size_t num_images = 0;
  size_t num_animations = 0;
  size_t num_cameras = 0;
  size_t num_lights = 0;
  size_t num_skeletons = 0;
  
  size_t total_vertices = 0;
  size_t total_triangles = 0;
  size_t total_memory_bytes = 0;
  
  std::string ToString() const;
};

SceneStatistics GatherStatistics(const RenderScene &scene);

// Validation and error checking
struct ValidationResult {
  bool valid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  
  std::string ToString() const;
};

ValidationResult ValidateRenderScene(const RenderScene &scene);
ValidationResult ValidateMesh(const RenderMesh &mesh);
ValidationResult ValidateMaterial(const RenderMaterial &material);
ValidationResult ValidateNode(const Node &node);

// Pretty printing utilities
namespace detail {

std::string Indent(int level, const DumpConfig &config = {});
std::string FormatArray(const std::vector<float> &arr, size_t max_elements = 10);
std::string FormatArray(const std::vector<int> &arr, size_t max_elements = 10);
std::string FormatArray(const std::vector<uint32_t> &arr, size_t max_elements = 10);
std::string FormatMatrix(const value::matrix4d &mat);
std::string FormatColor(const value::float3 &color);
std::string FormatBounds(const value::float3 &min, const value::float3 &max);

template<typename T>
std::string PrintAnimationSamples(
    const std::vector<AnimationSample<T>> &samples,
    size_t max_samples = 5) {
  std::stringstream ss;
  size_t count = std::min(samples.size(), max_samples);
  
  for (size_t i = 0; i < count; ++i) {
    ss << "  t=" << samples[i].t << ": " << samples[i].value;
    if (i < count - 1) ss << "\n";
  }
  
  if (samples.size() > max_samples) {
    ss << "\n  ... (" << (samples.size() - max_samples) << " more samples)";
  }
  
  return ss.str();
}

} // namespace detail

// JSON export (for debugging/visualization)
std::string ExportToJSON(const RenderScene &scene, bool pretty = true);
std::string ExportToJSON(const RenderMesh &mesh, bool pretty = true);
std::string ExportToJSON(const RenderMaterial &material, bool pretty = true);

// GLTF export utilities (for debugging/validation)
struct GLTFExportOptions {
  bool embed_images = false;
  bool embed_buffers = false;
  bool pretty_print = true;
  std::string base_path = "./";
};

std::string ExportToGLTF(
    const RenderScene &scene,
    const GLTFExportOptions &options = {});

} // namespace dump_utils

} // namespace tydra
} // namespace tinyusdz