// SPDX-License-Identifier: Apache-2.0
// tusdview - adaptive tessellation for USD parametric primitives.
//
// Stores the original parametric data for Sphere, Cube, Cylinder, Cone,
// Capsule, and Plane primitives so they can be re-tessellated at different
// quality levels based on camera distance (screen-space coverage).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gpu_scene.hh"
#include "light3d/math.h"

namespace tusdview {

// Parametric primitive types that support re-tessellation.
enum class ParamPrimType : uint8_t {
  Sphere,
  Cube,
  Cylinder,
  Cone,
  Capsule,
  Plane,
};

// Stored parametric data for a primitive that can be re-tessellated.
struct ParametricPrim {
  ParamPrimType type{ParamPrimType::Sphere};
  std::string absPath;   // USD absolute prim path (matches DrawMeshCPU::absPath)
  int drawMeshIndex{-1}; // index into DrawScene::meshes

  // Common parameters
  float world[16];       // column-major world transform

  // Type-specific parameters
  union {
    struct { float radius; } sphere;
    struct { float size; } cube;
    struct { float radius; float height; } cylinder;
    struct { float radius; float height; } cone;
    struct { float radius; float height; } capsule;
    struct { float width; float length; } plane;
  } params;

  // Current tessellation level (radial segments for round primitives)
  int currentSegments{16};
};

// Adaptive tessellation manager. Tracks parametric primitives and
// re-tessellates them based on camera distance.
class AdaptiveTessellator {
 public:
  // Register a parametric primitive from a converted DrawMeshCPU.
  // Returns the index in the parametrics array.
  int addPrimitive(const ParametricPrim& prim);

  // Compute the adaptive tessellation level for a primitive based on
  // camera position and the primitive's world-space size.
  // `qualityScale` is a user-controlled multiplier (1.0 = default).
  static int computeTessLevel(const ParametricPrim& prim,
                              const light3d::Vec3& cameraPos,
                              float qualityScale);

  // Re-tessellate all primitives that need a different tessellation level.
  // Returns true if any meshes were updated.
  bool retessellate(const light3d::Vec3& cameraPos,
                    float qualityScale,
                    DrawScene* draw);

  // Get the number of registered parametric primitives.
  size_t count() const { return parametrics_.size(); }

  // Clear all registered primitives.
  void clear();

  // Get a primitive by index.
  const ParametricPrim* getPrimitive(size_t index) const;

 private:
  std::vector<ParametricPrim> parametrics_;
  // absPath -> index for fast lookup
  std::unordered_map<std::string, size_t> pathMap_;
};

}  // namespace tusdview
