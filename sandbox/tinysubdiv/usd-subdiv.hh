#pragma once

#include "tinysubdiv.hh"
#include <string>
#include <vector>
#include <memory>

namespace tinysubdiv {

// USD-compatible subdivision attributes
enum class SubdivisionScheme {
  CatmullClark,
  Loop,
  Bilinear,
  None
};

enum class InterpolateBoundary {
  None,           // "none" - no boundary interpolation
  EdgeAndCorner,  // "edgeAndCorner" - default, smooth boundaries
  EdgeOnly        // "edgeOnly" - smooth edges but sharp corners
};

enum class FaceVaryingLinearInterpolation {
  None,          // "none" - no face-varying interpolation
  CornersOnly,   // "cornersOnly" - linearly interpolate corners only
  CornersPlus1,  // "cornersPlus1" - corners + 1-ring (default)
  CornersPlus2,  // "cornersPlus2" - corners + 2-ring
  Boundaries,    // "boundaries" - linearly interpolate boundaries
  All            // "all" - linearly interpolate all face-varying data
};

// Triangle subdivision rule (for Loop scheme)
enum class TriangleSubdivisionRule {
  CatmullClark,  // "catmullClark" - Catmull-Clark weights (default)
  Smooth         // "smooth" - smooth weights
};

// USD subdivision surface descriptor
struct UsdSubdivisionDescriptor {
  // Primary subdivision attributes
  SubdivisionScheme scheme = SubdivisionScheme::CatmullClark;
  InterpolateBoundary interpolateBoundary = InterpolateBoundary::EdgeAndCorner;
  FaceVaryingLinearInterpolation faceVaryingInterpolation = FaceVaryingLinearInterpolation::CornersPlus1;
  TriangleSubdivisionRule triangleRule = TriangleSubdivisionRule::CatmullClark;

  // Crease data
  std::vector<int32_t> creaseIndices;     // Pairs of vertex indices defining creases
  std::vector<int32_t> creaseLengths;     // Length of each crease (usually 2 for edges)
  std::vector<float> creaseSharpnesses;   // Sharpness per crease (0=smooth, 10=sharp)

  // Corner data
  std::vector<int32_t> cornerIndices;     // Vertex indices for corners
  std::vector<float> cornerSharpnesses;   // Sharpness per corner (0=smooth, 10=sharp)

  // Hole data
  std::vector<int32_t> holeIndices;       // Face indices that should be holes

  // Helper methods
  bool hasCreases() const { return !creaseIndices.empty(); }
  bool hasCorners() const { return !cornerIndices.empty(); }
  bool hasHoles() const { return !holeIndices.empty(); }

  void clear() {
    creaseIndices.clear();
    creaseLengths.clear();
    creaseSharpnesses.clear();
    cornerIndices.clear();
    cornerSharpnesses.clear();
    holeIndices.clear();
  }
};

// Enhanced USD-compatible subdivision mesh
class UsdSubdivisionMesh : public SubdivisionMesh {
 public:
  UsdSubdivisionMesh();
  ~UsdSubdivisionMesh();

  // Set subdivision descriptor
  void set_subdivision_descriptor(const UsdSubdivisionDescriptor& desc);
  const UsdSubdivisionDescriptor& get_subdivision_descriptor() const { return descriptor_; }

  // Apply USD subdivision attributes
  void apply_usd_attributes();

  // Subdivide with USD scheme selection
  void subdivide_usd(int level);

  // Set hole faces (faces that should not be subdivided/rendered)
  void set_hole_faces(const std::vector<int32_t>& hole_indices);

  // Enhanced crease/corner setting with lengths support
  void set_creases_with_lengths(const int32_t* crease_indices,
                                const int32_t* crease_lengths,
                                const float* crease_sharpnesses,
                                size_t num_creases);

  // Boundary interpolation control
  void set_interpolate_boundary(InterpolateBoundary mode);

  // Face-varying data handling
  void set_facevarying_interpolation(FaceVaryingLinearInterpolation mode);

  // Triangle subdivision rule (for Loop scheme)
  void set_triangle_subdivision_rule(TriangleSubdivisionRule rule);

  // Check if a face is marked as a hole
  bool is_hole_face(uint32_t face_index) const;

  // Get subdivision statistics
  struct SubdivisionStats {
    size_t num_hole_faces;
    size_t num_creases;
    size_t num_corners;
    size_t num_boundary_edges;
    size_t num_boundary_vertices;
  };
  SubdivisionStats get_subdivision_stats() const;

 private:
  UsdSubdivisionDescriptor descriptor_;
  std::vector<bool> hole_face_flags_;  // Quick lookup for hole faces

  // Apply boundary interpolation rules
  void apply_boundary_interpolation();

  // Apply face-varying linear interpolation
  void apply_facevarying_interpolation();

  // Mark faces as holes
  void mark_hole_faces();

  // Convert crease lengths format to edge pairs
  void convert_crease_lengths_to_pairs();

  // Validate subdivision attributes
  bool validate_attributes() const;
};

// Utility functions for USD subdivision
namespace usd_utils {

// Convert USD subdivision scheme string to enum
SubdivisionScheme parse_subdivision_scheme(const std::string& scheme);
std::string to_string(SubdivisionScheme scheme);

// Convert USD interpolate boundary string to enum
InterpolateBoundary parse_interpolate_boundary(const std::string& mode);
std::string to_string(InterpolateBoundary mode);

// Convert USD face-varying interpolation string to enum
FaceVaryingLinearInterpolation parse_facevarying_interpolation(const std::string& mode);
std::string to_string(FaceVaryingLinearInterpolation mode);

// Convert triangle subdivision rule string to enum
TriangleSubdivisionRule parse_triangle_rule(const std::string& rule);
std::string to_string(TriangleSubdivisionRule rule);

// Validate crease data
bool validate_creases(const std::vector<int32_t>& indices,
                      const std::vector<int32_t>& lengths,
                      const std::vector<float>& sharpnesses);

// Validate corner data
bool validate_corners(const std::vector<int32_t>& indices,
                      const std::vector<float>& sharpnesses);

// Convert sharpness values between USD and internal representation
// USD uses 0 (smooth) to 10+ (infinitely sharp)
// Internal uses 0 (smooth) to 1 (sharp)
float usd_to_internal_sharpness(float usd_sharpness);
float internal_to_usd_sharpness(float internal_sharpness);

}  // namespace usd_utils

}  // namespace tinysubdiv