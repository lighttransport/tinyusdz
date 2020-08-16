#pragma once

#include "tinyusdz.hh"

namespace example {

// Proxy class for tinyusdz::GeomMesh to use the primive for NanoSG/NanoRT.

struct DrawMeshGeom {

  DrawMeshGeom(const tinyusdz::GeomMesh *p) : mesh(p) {}

  // Pointer to GeomMesh.
  const tinyusdz::GeomMesh *mesh = nullptr;

  ///
  /// Required accessor API
  ///
  const float *GetVertices() const {
    // Assume vec3f
    return reinterpret_cast<const float *>(mesh->points.buffer.data.data());
  }

  size_t GetVertexStrideBytes() const {
    return sizeof(float) * 3;
  }
};

class DrawScene {
  std::vector<DrawMeshGeom> draw_meshes;
};


} // namespace example
