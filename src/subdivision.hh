//
// Dependency-free subdivision surface library for USD
//
// Copyright (c) 2025, Lightweight USD contributors
// Licensed under MIT license
//

#ifndef TINYUSDZ_SUBDIVISION_HH_
#define TINYUSDZ_SUBDIVISION_HH_

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <limits>

namespace tinyusdz {
namespace subdiv {

///
/// USD Primvar interpolation types
///
enum class InterpolationType {
  Constant,      // Per-object, not subdivided
  Uniform,       // Per-face
  Vertex,        // Per-vertex (interpolated from original vertices)
  Varying,       // Per-vertex (smooth interpolation, like positions)
  FaceVarying    // Per-face-vertex (like UVs, may have discontinuities)
};

///
/// Subdivision scheme types
///
enum class SubdivisionScheme {
  CatmullClark,  // For quad-dominant meshes
  Loop,          // For triangle meshes only
  Bilinear       // Simple linear subdivision (optional)
};

///
/// Boundary interpolation modes (USD compatible)
///
enum class BoundaryInterpolation {
  None,                    // Boundaries behave like interior vertices
  EdgeOnly,               // Sharpen edges, smooth corners
  EdgeAndCorner           // Sharpen both edges and corners
};

///
/// Result status for subdivision operations
///
struct SubdivResult {
  bool success{false};
  std::string error;

  SubdivResult() = default;
  explicit SubdivResult(bool s) : success(s) {}
  SubdivResult(bool s, const std::string& e) : success(s), error(e) {}
};

///
/// Generic primvar data container
/// Supports various data types commonly used in USD
///
template<typename T>
struct PrimvarData {
  std::vector<T> values;
  InterpolationType interpolation{InterpolationType::Vertex};

  PrimvarData() = default;
  PrimvarData(const std::vector<T>& v, InterpolationType interp)
    : values(v), interpolation(interp) {}
};

///
/// Half-edge mesh data structure for efficient subdivision
///
struct HalfEdgeMesh {
  // Topology
  std::vector<uint32_t> face_vertex_counts;  // Number of vertices per face
  std::vector<uint32_t> face_vertex_indices; // Flattened vertex indices

  // Original vertex positions
  std::vector<float> points;  // xyz xyz xyz...

  // Boundary information
  std::vector<bool> vertex_on_boundary;
  std::vector<bool> edge_on_boundary;

  // Validation
  bool IsValid() const;
  uint32_t GetNumVertices() const { return static_cast<uint32_t>(points.size() / 3); }
  uint32_t GetNumFaces() const { return static_cast<uint32_t>(face_vertex_counts.size()); }

  // Security: Check for reasonable limits
  static constexpr uint32_t kMaxVertices = 10000000;    // 10M vertices
  static constexpr uint32_t kMaxFaces = 10000000;       // 10M faces
  static constexpr uint32_t kMaxFaceVertices = 256;     // Max vertices per face
};

///
/// Catmull-Clark Subdivision
///
/// Suitable for quad-dominant meshes. Produces smooth surfaces.
/// Implements the classic algorithm:
///   1. Compute face points (average of face vertices)
///   2. Compute edge points (average of edge endpoints and adjacent face points)
///   3. Compute new vertex points (weighted average)
///   4. Connect new points to form refined mesh
///
class CatmullClarkSubdivider {
public:
  CatmullClarkSubdivider() = default;

  /// Set boundary interpolation mode
  void SetBoundaryInterpolation(BoundaryInterpolation mode) {
    boundary_mode_ = mode;
  }

  /// Subdivide mesh one level
  /// @param[in] input Input mesh
  /// @param[out] output Subdivided mesh
  /// @param[in] levels Number of subdivision levels (default: 1)
  /// @return Result status
  SubdivResult Subdivide(const HalfEdgeMesh& input,
                         HalfEdgeMesh& output,
                         int levels = 1);

  /// Subdivide generic primvar data
  /// @param[in] input_primvar Input primvar data
  /// @param[in] input_mesh Original mesh (for topology)
  /// @param[in] output_mesh Subdivided mesh (for new topology)
  /// @param[out] output_primvar Subdivided primvar data
  template<typename T>
  SubdivResult SubdividePrimvar(const PrimvarData<T>& input_primvar,
                                const HalfEdgeMesh& input_mesh,
                                const HalfEdgeMesh& output_mesh,
                                PrimvarData<T>& output_primvar);

private:
  BoundaryInterpolation boundary_mode_{BoundaryInterpolation::EdgeAndCorner};

  // Internal topology structures for efficient subdivision
  struct EdgeKey {
    uint32_t v0, v1;
    EdgeKey(uint32_t a, uint32_t b) : v0(a < b ? a : b), v1(a < b ? b : a) {}
    bool operator<(const EdgeKey& other) const {
      return (v0 < other.v0) || (v0 == other.v0 && v1 < other.v1);
    }
    bool operator==(const EdgeKey& other) const {
      return v0 == other.v0 && v1 == other.v1;
    }
  };

  struct VertexInfo {
    std::vector<uint32_t> adjacent_faces;
    std::vector<uint32_t> adjacent_vertices;
    std::vector<EdgeKey> adjacent_edges;
    bool is_boundary{false};
    uint32_t valence{0};
  };

  // Build topology information
  SubdivResult BuildTopology(const HalfEdgeMesh& mesh,
                             std::vector<VertexInfo>& vertex_info);

  // Subdivision step
  SubdivResult SubdivideOnce(const HalfEdgeMesh& input, HalfEdgeMesh& output);
};

///
/// Loop Subdivision
///
/// Suitable for triangle meshes only. Produces smooth surfaces.
/// Implements Charles Loop's 1987 algorithm:
///   1. Split each triangle into 4 smaller triangles
///   2. Compute new vertex positions using weighted averages
///   3. Update existing vertex positions
///
class LoopSubdivider {
public:
  LoopSubdivider() = default;

  /// Set boundary interpolation mode
  void SetBoundaryInterpolation(BoundaryInterpolation mode) {
    boundary_mode_ = mode;
  }

  /// Subdivide mesh one level
  /// @param[in] input Input mesh (must be all triangles)
  /// @param[out] output Subdivided mesh
  /// @param[in] levels Number of subdivision levels (default: 1)
  /// @return Result status
  SubdivResult Subdivide(const HalfEdgeMesh& input,
                         HalfEdgeMesh& output,
                         int levels = 1);

  /// Subdivide generic primvar data
  template<typename T>
  SubdivResult SubdividePrimvar(const PrimvarData<T>& input_primvar,
                                const HalfEdgeMesh& input_mesh,
                                const HalfEdgeMesh& output_mesh,
                                PrimvarData<T>& output_primvar);

private:
  BoundaryInterpolation boundary_mode_{BoundaryInterpolation::EdgeAndCorner};

  struct EdgeKey {
    uint32_t v0, v1;
    EdgeKey(uint32_t a, uint32_t b) : v0(a < b ? a : b), v1(a < b ? b : a) {}
    bool operator<(const EdgeKey& other) const {
      return (v0 < other.v0) || (v0 == other.v0 && v1 < other.v1);
    }
    bool operator==(const EdgeKey& other) const {
      return v0 == other.v0 && v1 == other.v1;
    }
  };

  struct VertexInfo {
    std::vector<uint32_t> adjacent_vertices;
    bool is_boundary{false};
    uint32_t valence{0};
  };

  // Validate that mesh is all triangles
  SubdivResult ValidateTriangleMesh(const HalfEdgeMesh& mesh);

  // Build topology information
  SubdivResult BuildTopology(const HalfEdgeMesh& mesh,
                             std::vector<VertexInfo>& vertex_info);

  // Subdivision step
  SubdivResult SubdivideOnce(const HalfEdgeMesh& input, HalfEdgeMesh& output);

  // Compute Loop beta weight for vertex smoothing
  float ComputeBeta(uint32_t valence) const;
};

///
/// Bilinear Subdivision
///
/// Simplest subdivision scheme using linear interpolation only.
/// No smoothing is applied - original vertices remain unchanged.
/// Suitable for maintaining sharp features or creating simple tessellations.
///
class BilinearSubdivider {
public:
  BilinearSubdivider() = default;

  /// Subdivide mesh one level
  /// @param[in] input Input mesh (quads or triangles)
  /// @param[out] output Subdivided mesh
  /// @param[in] levels Number of subdivision levels (default: 1)
  /// @return Result status
  SubdivResult Subdivide(const HalfEdgeMesh& input,
                         HalfEdgeMesh& output,
                         int levels = 1);

  /// Subdivide generic primvar data
  template<typename T>
  SubdivResult SubdividePrimvar(const PrimvarData<T>& input_primvar,
                                const HalfEdgeMesh& input_mesh,
                                const HalfEdgeMesh& output_mesh,
                                PrimvarData<T>& output_primvar);

private:
  struct EdgeKey {
    uint32_t v0, v1;
    EdgeKey(uint32_t a, uint32_t b) : v0(a < b ? a : b), v1(a < b ? b : a) {}
    bool operator<(const EdgeKey& other) const {
      return (v0 < other.v0) || (v0 == other.v0 && v1 < other.v1);
    }
    bool operator==(const EdgeKey& other) const {
      return v0 == other.v0 && v1 == other.v1;
    }
  };

  // Subdivision step
  SubdivResult SubdivideOnce(const HalfEdgeMesh& input, HalfEdgeMesh& output);
};

///
/// Utility functions
///

/// Convert from USD mesh format to HalfEdgeMesh
/// @param[in] face_vertex_counts USD faceVertexCounts
/// @param[in] face_vertex_indices USD faceVertexIndices
/// @param[in] points USD points (vec3f array)
/// @param[out] mesh Output half-edge mesh
SubdivResult ConvertToHalfEdgeMesh(
    const std::vector<uint32_t>& face_vertex_counts,
    const std::vector<uint32_t>& face_vertex_indices,
    const std::vector<float>& points,
    HalfEdgeMesh& mesh);

/// Convert from HalfEdgeMesh back to USD mesh format
/// @param[in] mesh Input half-edge mesh
/// @param[out] face_vertex_counts USD faceVertexCounts
/// @param[out] face_vertex_indices USD faceVertexIndices
/// @param[out] points USD points (vec3f array)
SubdivResult ConvertFromHalfEdgeMesh(
    const HalfEdgeMesh& mesh,
    std::vector<uint32_t>& face_vertex_counts,
    std::vector<uint32_t>& face_vertex_indices,
    std::vector<float>& points);

}  // namespace subdiv
}  // namespace tinyusdz

#endif  // TINYUSDZ_SUBDIVISION_HH_
