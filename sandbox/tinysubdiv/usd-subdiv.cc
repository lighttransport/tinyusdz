#include "usd-subdiv.hh"
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <iostream>

namespace tinysubdiv {

UsdSubdivisionMesh::UsdSubdivisionMesh() = default;
UsdSubdivisionMesh::~UsdSubdivisionMesh() = default;

void UsdSubdivisionMesh::set_subdivision_descriptor(const UsdSubdivisionDescriptor& desc) {
  descriptor_ = desc;
  apply_usd_attributes();
}

void UsdSubdivisionMesh::apply_usd_attributes() {
  // Apply creases
  if (descriptor_.hasCreases()) {
    if (!descriptor_.creaseLengths.empty()) {
      set_creases_with_lengths(
        descriptor_.creaseIndices.data(),
        descriptor_.creaseLengths.data(),
        descriptor_.creaseSharpnesses.data(),
        descriptor_.creaseLengths.size());
    } else {
      // Assume pairs if no lengths specified
      set_creases(
        reinterpret_cast<const uint32_t*>(descriptor_.creaseIndices.data()),
        descriptor_.creaseSharpnesses.data(),
        descriptor_.creaseSharpnesses.size());
    }
  }

  // Apply corners
  if (descriptor_.hasCorners()) {
    set_corners(
      reinterpret_cast<const uint32_t*>(descriptor_.cornerIndices.data()),
      descriptor_.cornerSharpnesses.data(),
      descriptor_.cornerIndices.size());
  }

  // Apply holes
  if (descriptor_.hasHoles()) {
    set_hole_faces(descriptor_.holeIndices);
  }

  // Apply interpolation modes
  set_interpolate_boundary(descriptor_.interpolateBoundary);
  set_facevarying_interpolation(descriptor_.faceVaryingInterpolation);
  set_triangle_subdivision_rule(descriptor_.triangleRule);
}

void UsdSubdivisionMesh::subdivide_usd(int level) {
  // Select subdivision scheme based on descriptor
  switch (descriptor_.scheme) {
    case SubdivisionScheme::CatmullClark:
      subdivide_catmull_clark(level);
      break;
    case SubdivisionScheme::Loop:
      subdivide_loop(level);
      break;
    case SubdivisionScheme::Bilinear:
      // Bilinear is simple linear interpolation
      // TODO: Implement bilinear subdivision
      std::cerr << "Bilinear subdivision not yet implemented\n";
      break;
    case SubdivisionScheme::None:
      // No subdivision
      break;
  }
}

void UsdSubdivisionMesh::set_hole_faces(const std::vector<int32_t>& hole_indices) {
  descriptor_.holeIndices = hole_indices;

  // Build quick lookup
  hole_face_flags_.clear();
  if (faces_.size() > 0) {
    hole_face_flags_.resize(faces_.size(), false);
    for (int32_t idx : hole_indices) {
      if (idx >= 0 && static_cast<size_t>(idx) < hole_face_flags_.size()) {
        hole_face_flags_[idx] = true;
      }
    }
  }
}

void UsdSubdivisionMesh::set_creases_with_lengths(const int32_t* crease_indices,
                                                  const int32_t* crease_lengths,
                                                  const float* crease_sharpnesses,
                                                  size_t num_creases) {
  descriptor_.creaseIndices.clear();
  descriptor_.creaseLengths.clear();
  descriptor_.creaseSharpnesses.clear();

  size_t idx = 0;
  for (size_t i = 0; i < num_creases; ++i) {
    int32_t length = crease_lengths[i];
    descriptor_.creaseLengths.push_back(length);
    descriptor_.creaseSharpnesses.push_back(crease_sharpnesses[i]);

    // Convert to edge pairs
    for (int32_t j = 0; j < length - 1; ++j) {
      if (idx + 1 < static_cast<size_t>(length * num_creases)) {
        uint32_t v0 = static_cast<uint32_t>(crease_indices[idx]);
        uint32_t v1 = static_cast<uint32_t>(crease_indices[idx + 1]);

        // Apply crease to the edge
        uint64_t key = edge_key(v0, v1);
        auto it = edge_map_.find(key);
        if (it != edge_map_.end()) {
          edges_[it->second].is_crease = true;
          edges_[it->second].sharpness = usd_utils::usd_to_internal_sharpness(crease_sharpnesses[i]);
        }

        descriptor_.creaseIndices.push_back(crease_indices[idx]);
        descriptor_.creaseIndices.push_back(crease_indices[idx + 1]);
        idx++;
      }
    }
    idx++; // Move to next crease
  }
}

void UsdSubdivisionMesh::set_interpolate_boundary(InterpolateBoundary mode) {
  descriptor_.interpolateBoundary = mode;

  // Apply to existing boundary edges and vertices
  for (size_t i = 0; i < edges_.size(); ++i) {
    auto& edge = edges_[i];
    if (edge.is_boundary()) {
      switch (mode) {
        case InterpolateBoundary::None:
          // Keep boundaries sharp
          edge.is_crease = true;
          edge.sharpness = 1.0f;
          break;
        case InterpolateBoundary::EdgeAndCorner:
          // Smooth boundaries (default)
          if (!edge.is_crease) {
            edge.sharpness = 0.0f;
          }
          break;
        case InterpolateBoundary::EdgeOnly:
          // Smooth edges but keep corners sharp
          // This requires checking vertex valence
          break;
      }
    }
  }

  // Apply to boundary vertices
  if (mode == InterpolateBoundary::EdgeOnly) {
    for (size_t vi = 0; vi < vertex_data_.size(); ++vi) {
      if (vertex_data_[vi].is_boundary) {
        // Count boundary edges
        int boundary_edge_count = 0;
        for (uint32_t edge_idx : vertex_edges_[vi]) {
          if (edges_[edge_idx].is_boundary()) {
            boundary_edge_count++;
          }
        }

        // Corner vertices have valence != 2 on boundaries
        if (boundary_edge_count != 2) {
          vertex_data_[vi].is_corner = true;
          vertex_data_[vi].sharpness = 1.0f;
        }
      }
    }
  }
}

void UsdSubdivisionMesh::set_facevarying_interpolation(FaceVaryingLinearInterpolation mode) {
  descriptor_.faceVaryingInterpolation = mode;
  // This affects how face-varying data (like UVs) is interpolated
  // The actual implementation would be in the subdivision algorithms
}

void UsdSubdivisionMesh::set_triangle_subdivision_rule(TriangleSubdivisionRule rule) {
  descriptor_.triangleRule = rule;
  // This affects Loop subdivision weights
}

bool UsdSubdivisionMesh::is_hole_face(uint32_t face_index) const {
  if (face_index < hole_face_flags_.size()) {
    return hole_face_flags_[face_index];
  }
  return false;
}

UsdSubdivisionMesh::SubdivisionStats UsdSubdivisionMesh::get_subdivision_stats() const {
  SubdivisionStats stats = {};

  stats.num_hole_faces = descriptor_.holeIndices.size();
  stats.num_creases = descriptor_.creaseLengths.size();
  stats.num_corners = descriptor_.cornerIndices.size();

  // Count boundary edges and vertices
  for (size_t i = 0; i < edges_.size(); ++i) {
    const auto& edge = edges_[i];
    if (edge.is_boundary()) {
      stats.num_boundary_edges++;
    }
  }

  for (size_t i = 0; i < vertex_data_.size(); ++i) {
    const auto& vertex = vertex_data_[i];
    if (vertex.is_boundary) {
      stats.num_boundary_vertices++;
    }
  }

  return stats;
}

void UsdSubdivisionMesh::apply_boundary_interpolation() {
  // Already handled in set_interpolate_boundary
}

void UsdSubdivisionMesh::apply_facevarying_interpolation() {
  // This would affect UV and other per-face-vertex data interpolation
  // Implementation depends on the subdivision algorithm
}

void UsdSubdivisionMesh::mark_hole_faces() {
  // Already handled in set_hole_faces
}

void UsdSubdivisionMesh::convert_crease_lengths_to_pairs() {
  // Already handled in set_creases_with_lengths
}

bool UsdSubdivisionMesh::validate_attributes() const {
  // Validate creases
  if (!descriptor_.creaseIndices.empty()) {
    if (descriptor_.creaseLengths.empty()) {
      // Must be pairs
      if (descriptor_.creaseIndices.size() % 2 != 0) {
        return false;
      }
    } else {
      // Check lengths sum
      size_t total_length = 0;
      for (int32_t len : descriptor_.creaseLengths) {
        if (len < 2) return false;  // Crease must have at least 2 vertices
        total_length += len;
      }
      if (total_length != descriptor_.creaseIndices.size()) {
        return false;
      }
    }
  }

  // Validate corners
  if (descriptor_.cornerIndices.size() != descriptor_.cornerSharpnesses.size()) {
    return false;
  }

  // Validate holes
  for (int32_t hole_idx : descriptor_.holeIndices) {
    if (hole_idx < 0 || static_cast<size_t>(hole_idx) >= faces_.size()) {
      return false;
    }
  }

  return true;
}

// Utility functions implementation
namespace usd_utils {

SubdivisionScheme parse_subdivision_scheme(const std::string& scheme) {
  if (scheme == "catmullClark") return SubdivisionScheme::CatmullClark;
  if (scheme == "loop") return SubdivisionScheme::Loop;
  if (scheme == "bilinear") return SubdivisionScheme::Bilinear;
  if (scheme == "none") return SubdivisionScheme::None;
  return SubdivisionScheme::CatmullClark;  // default
}

std::string to_string(SubdivisionScheme scheme) {
  switch (scheme) {
    case SubdivisionScheme::CatmullClark: return "catmullClark";
    case SubdivisionScheme::Loop: return "loop";
    case SubdivisionScheme::Bilinear: return "bilinear";
    case SubdivisionScheme::None: return "none";
  }
  return "catmullClark";
}

InterpolateBoundary parse_interpolate_boundary(const std::string& mode) {
  if (mode == "none") return InterpolateBoundary::None;
  if (mode == "edgeAndCorner") return InterpolateBoundary::EdgeAndCorner;
  if (mode == "edgeOnly") return InterpolateBoundary::EdgeOnly;
  return InterpolateBoundary::EdgeAndCorner;  // default
}

std::string to_string(InterpolateBoundary mode) {
  switch (mode) {
    case InterpolateBoundary::None: return "none";
    case InterpolateBoundary::EdgeAndCorner: return "edgeAndCorner";
    case InterpolateBoundary::EdgeOnly: return "edgeOnly";
  }
  return "edgeAndCorner";
}

FaceVaryingLinearInterpolation parse_facevarying_interpolation(const std::string& mode) {
  if (mode == "none") return FaceVaryingLinearInterpolation::None;
  if (mode == "cornersOnly") return FaceVaryingLinearInterpolation::CornersOnly;
  if (mode == "cornersPlus1") return FaceVaryingLinearInterpolation::CornersPlus1;
  if (mode == "cornersPlus2") return FaceVaryingLinearInterpolation::CornersPlus2;
  if (mode == "boundaries") return FaceVaryingLinearInterpolation::Boundaries;
  if (mode == "all") return FaceVaryingLinearInterpolation::All;
  return FaceVaryingLinearInterpolation::CornersPlus1;  // default
}

std::string to_string(FaceVaryingLinearInterpolation mode) {
  switch (mode) {
    case FaceVaryingLinearInterpolation::None: return "none";
    case FaceVaryingLinearInterpolation::CornersOnly: return "cornersOnly";
    case FaceVaryingLinearInterpolation::CornersPlus1: return "cornersPlus1";
    case FaceVaryingLinearInterpolation::CornersPlus2: return "cornersPlus2";
    case FaceVaryingLinearInterpolation::Boundaries: return "boundaries";
    case FaceVaryingLinearInterpolation::All: return "all";
  }
  return "cornersPlus1";
}

TriangleSubdivisionRule parse_triangle_rule(const std::string& rule) {
  if (rule == "catmullClark") return TriangleSubdivisionRule::CatmullClark;
  if (rule == "smooth") return TriangleSubdivisionRule::Smooth;
  return TriangleSubdivisionRule::CatmullClark;  // default
}

std::string to_string(TriangleSubdivisionRule rule) {
  switch (rule) {
    case TriangleSubdivisionRule::CatmullClark: return "catmullClark";
    case TriangleSubdivisionRule::Smooth: return "smooth";
  }
  return "catmullClark";
}

bool validate_creases(const std::vector<int32_t>& indices,
                     const std::vector<int32_t>& lengths,
                     const std::vector<float>& sharpnesses) {
  if (lengths.size() != sharpnesses.size()) {
    return false;
  }

  size_t total_indices = 0;
  for (int32_t len : lengths) {
    if (len < 2) return false;
    total_indices += len;
  }

  return total_indices == indices.size();
}

bool validate_corners(const std::vector<int32_t>& indices,
                     const std::vector<float>& sharpnesses) {
  return indices.size() == sharpnesses.size();
}

float usd_to_internal_sharpness(float usd_sharpness) {
  // USD uses 0 (smooth) to 10+ (infinitely sharp)
  // We map to 0 (smooth) to 1 (sharp)
  if (usd_sharpness <= 0.0f) return 0.0f;
  if (usd_sharpness >= 10.0f) return 1.0f;
  return usd_sharpness / 10.0f;
}

float internal_to_usd_sharpness(float internal_sharpness) {
  // Convert back from internal [0,1] to USD [0,10+]
  if (internal_sharpness <= 0.0f) return 0.0f;
  if (internal_sharpness >= 1.0f) return 10.0f;
  return internal_sharpness * 10.0f;
}

}  // namespace usd_utils

}  // namespace tinysubdiv