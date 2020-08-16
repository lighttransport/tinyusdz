#pragma once

#include "tinyusdz.hh"

namespace example {

// Proxy class for tinyusdz::GeomMesh to use the primive for NanoSG/NanoRT.

struct DrawMeshGeom {

  // Pointer to GeomMesh.
  const tinyusdz::GeomMesh *mesh;

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

class Scene {


};


} // namespace example
