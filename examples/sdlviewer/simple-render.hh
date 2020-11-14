#pragma once

#include "tinyusdz.hh"

#include "nanort.h"

namespace example {

using vec3 = std::array<float, 3>;

struct AOV {
  size_t width;
  size_t height;

  std::vector<float> rgb; // 3 x width x height
  std::vector<float> shading_normal; // 3 x width x height
  std::vector<float> geometric_normal; // 3 x width x height
  std::vector<float> texcoords; // 2 x width x height

  void Resize(size_t w, size_t h) {
    width = w;
    height = h;

    rgb.resize(width * height * 3);
    memset(rgb.data(), 0, sizeof(float) * rgb.size());

    shading_normal.resize(width * height * 3);
    memset(shading_normal.data(), 0, sizeof(float) * shading_normal.size());

    geometric_normal.resize(width * height * 3);
    memset(geometric_normal.data(), 0, sizeof(float) * geometric_normal.size());

    texcoords.resize(width * height * 2);
    memset(texcoords.data(), 0, sizeof(float) * texcoords.size());
  }

};

struct Camera
{
  float eye[3] = {0.0f, 0.0f, 25.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  float look_at[3] = {0.0f, 0.0f, 0.0f};
  float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float fov = 60.0f; // in degree
};

template<typename T>
struct Buffer
{
  size_t num_coords{1};  // e.g. 3 for vec3 type.  
  std::vector<T> data;
};

// Proxy class for tinyusdz::GeomMesh to use the primive for NanoSG/NanoRT.

struct DrawGeomMesh {

  DrawGeomMesh(const tinyusdz::GeomMesh *p) : ref_mesh(p) {}

  // Pointer to Reference GeomMesh.
  const tinyusdz::GeomMesh *ref_mesh = nullptr;

  ///
  /// Required accessor API for NanoSG
  ///
  const float *GetVertices() const {
    // Assume vec3f
    return reinterpret_cast<const float *>(ref_mesh->points.buffer.data.data());
  }

  size_t GetVertexStrideBytes() const {
    return sizeof(float) * 3;
  }

  std::vector<float> vertices;  // vec3f
  std::vector<uint32_t> facevarying_indices; // triangulated indices. 3 x num_faces
  std::vector<float> facevarying_normals; // 3 x 3 x num_faces
  std::vector<float> facevarying_texcoords; // 2 x 3 x num_faces

  // arbitrary primvars(including texcoords(float2))
  std::vector<Buffer<float>> float_primvars;
  std::map<std::string, size_t> float_primvars_map; // <name, index to `float_primvars`>

  // arbitrary primvars in int type(e.g. texcoord indices(int3))
  std::vector<Buffer<int32_t>> int_primvars;
  std::map<std::string, size_t> int_primvars_map; // <name, index to `int_primvars`>

  int material_id{-1}; // per-geom material. index to `RenderScene::materials`

  nanort::BVHAccel<float> accel;
};

struct UVReader {
  int32_t st_id{-1}; // index to DrawGeomMesh::float_primvars
  int32_t indices_id{-1}; // index to DrawGeomMesh::int_primvars

  // Fetch interpolated UV coordinate
  std::array<float, 2> fetch_uv(size_t face_id, float varyu, float varyv);
};

struct Texture {
  enum Channel {
    TEXTURE_CHANNEL_R,
    TEXTURE_CHANNEL_G,
    TEXTURE_CHANNEL_B,
    TEXTURE_CHANNEL_RGB,
    TEXTURE_CHANNEL_RGBA,
  };

  UVReader uv_reader;  
  int32_t image_id{-1};

  // NOTE: for single channel(e.g. R), [0] will be filled for the return value.
  std::array<float, 4> fetch(size_t face_id, float varyu, float varyv, Channel channel);

};

// base color(fallback color) or Texture
template<typename T>
struct ShaderParam {
  T value;
  int32_t texture_id{-1};
};

// UsdPreviewSurface 
struct Shader {

  ShaderParam<vec3> diffuseColor;
  ShaderParam<float> metallic;

  // TODO: Other PreviewSurface parameters
};

struct Material {
  Shader shader;
};

// Simple LDR texture
struct Image {
  std::vector<uint8_t> image;
  int32_t width{-1};
  int32_t height{-1};
  int32_t channels{-1}; // e.g. 3 for RGB.
};

class RenderScene {
 public:
  std::vector<DrawGeomMesh> draw_meshes;
  std::vector<Material> materials;
  std::vector<Texture> textures;
  std::vector<Image> images;

  // Convert meshes and build BVH
  bool Setup();
};


bool Render(
  const RenderScene &scene,
  const Camera &cam,
  AOV *output);

} // namespace example
