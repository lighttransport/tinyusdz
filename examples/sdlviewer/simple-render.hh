#pragma once

#include "tinyusdz.hh"

namespace example {

// Proxy class for tinyusdz::GeomMesh to use the primive for NanoSG/NanoRT.

struct DrawGeomMesh {

  DrawGeomMesh(const tinyusdz::GeomMesh *p) : mesh(p) {}

  // Pointer to GeomMesh.
  const tinyusdz::GeomMesh *mesh = nullptr;

  ///
  /// Required accessor API for NanoSG
  ///
  const float *GetVertices() const {
    // Assume vec3f
    return reinterpret_cast<const float *>(mesh->points.buffer.data.data());
  }

  size_t GetVertexStrideBytes() const {
    return sizeof(float) * 3;
  }

  std::vector<float> vertices;  // vec3f
  std::vector<uint32_t> facevarying_indices; // triangulated indices. 3 x num_faces
};

class DrawScene {
  std::vector<DrawGeomMesh> draw_meshes;
};


} // namespace example
