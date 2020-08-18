#pragma once

#include "tinyusdz.hh"

namespace example {

struct AOV {
  size_t width;
  size_t height;

  std::vector<float> rgb; // 3 x width x height
  //std::vector<float> shading_normal; // 3 x width x height
  //std::vector<float> geometric_normal; // 3 x width x height

  bool Resize(size_t w, size_t h) {
    width = w;
    height = h;

    rgb.resize(width * height * 3);

  }

};

struct Camera
{
  float eye[3] = {0.0f, 0.0f, 5.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  float look_at[3] = {0.0f, 0.0f, 0.0f};
  float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float fov = 60.0f; // in degree
};

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

class RenderScene {
 public:
  std::vector<DrawGeomMesh> draw_meshes;
};


bool Render(
  const RenderScene &scene,
  const Camera &cam,
  AOV *output);

} // namespace example
