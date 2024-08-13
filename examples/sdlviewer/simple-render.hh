#pragma once

#include <unordered_map>

#include "nanort.h"
#include "nanosg.h"
//
#include "tinyusdz.hh"
#include "tydra/render-data.hh"

namespace example {

// GLES-like naming

using vec3 = tinyusdz::value::float3;
using vec2 = tinyusdz::value::float2;
using mat2 = tinyusdz::value::matrix2f;

template<typename T>
inline void lerp(T dst[3], const T v0[3], const T v1[3], const T v2[3], float u, float v) {
  dst[0] = (static_cast<T>(1.0) - u - v) * v0[0] + u * v1[0] + v * v2[0];
  dst[1] = (static_cast<T>(1.0) - u - v) * v0[1] + u * v1[1] + v * v2[1];
  dst[2] = (static_cast<T>(1.0) - u - v) * v0[2] + u * v1[2] + v * v2[2];
}

template <typename T>
inline T vlength(const T v[3]) {
  const T d = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (std::fabs(d) > std::numeric_limits<T>::epsilon()) {
    return std::sqrt(d);
  } else {
    return static_cast<T>(0.0);
  }
}

template <typename T>
inline void vnormalize(T dst[3], const T v[3]) {
  dst[0] = v[0];
  dst[1] = v[1];
  dst[2] = v[2];
  const T len = vlength(v);
  if (std::fabs(len) > std::numeric_limits<T>::epsilon()) {
    const T inv_len = static_cast<T>(1.0) / len;
    dst[0] *= inv_len;
    dst[1] *= inv_len;
    dst[2] *= inv_len;
  }
}

template <typename T>
inline void vcross(T dst[3], const T a[3], const T b[3]) {
  dst[0] = a[1] * b[2] - a[2] * b[1];
  dst[1] = a[2] * b[0] - a[0] * b[2];
  dst[2] = a[0] * b[1] - a[1] * b[0];
}

template <typename T>
inline void vsub(T dst[3], const T a[3], const T b[3]) {
  dst[0] = a[0] - b[0];
  dst[1] = a[1] - b[1];
  dst[2] = a[2] - b[2];
}

template<typename T>
inline void calculate_normal(T Nn[3], const T v0[3], const T v1[3], const T v2[3]) {
  T v10[3];
  T v20[3];

  vsub(v10, v1, v0);
  vsub(v20, v2, v0);

  T N[3];
  vcross(N, v20, v10);
  vnormalize(Nn, N);
}

struct AOV {
  size_t width;
  size_t height;

  std::vector<float> rgb;               // 3 x width x height
  std::vector<float> shading_normal;    // 3 x width x height
  std::vector<float> geometric_normal;  // 3 x width x height
  std::vector<float> texcoords;         // 2 x width x height

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

struct Camera {
  float eye[3] = {0.0f, 0.0f, 25.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  float look_at[3] = {0.0f, 0.0f, 0.0f};
  float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float fov = 60.0f;  // in degree
};

template <typename T>
struct Buffer {
  size_t num_coords{1};  // e.g. 3 for vec3 type.
  std::vector<T> data;
};

//
// Renderable Node class for NanoSG. Includes xform
//
struct DrawNode {
  std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
  std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};  // euler rotation
  std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

//
// Mesh class used in NanoRT/NanoSG.
// Currently mesh data must be triangulated and all attributes are facevarying.
//
template<typename T>
struct DrawGeomMesh {
  DrawGeomMesh() {}

  ///
  /// Required accessor API for NanoSG
  ///
  const float *GetVertices() const {
     return nullptr; // TODO
    //return reinterpret_cast<const float *>(ref_mesh->points.data());
  }

  const unsigned int *GetFaces() const {
    return facevertex_indices.data();
  }

  size_t GetVertexStrideBytes() const { return sizeof(float) * 3; }

  ///
  /// Get the geometric normal and the shading normal at `face_idx' th face.
  ///
  void GetNormal(float Ng[3], float Ns[3], const unsigned int face_idx, const float u, const float v) const {
    // Compute geometric normal.
    unsigned int f0, f1, f2;
    float v0[3], v1[3], v2[3];

    f0 = facevertex_indices[3 * face_idx + 0];
    f1 = facevertex_indices[3 * face_idx + 1];
    f2 = facevertex_indices[3 * face_idx + 2];

    v0[0] = vertices[3 * f0 + 0];
    v0[1] = vertices[3 * f0 + 1];
    v0[2] = vertices[3 * f0 + 2];

    v1[0] = vertices[3 * f1 + 0];
    v1[1] = vertices[3 * f1 + 1];
    v1[2] = vertices[3 * f1 + 2];

    v2[0] = vertices[3 * f2 + 0];
    v2[1] = vertices[3 * f2 + 1];
    v2[2] = vertices[3 * f2 + 2];

    calculate_normal(Ng, v0, v1, v2);

    if (vertex_normals.size() > 0) {
      uint32_t v0 = facevertex_indices[3 * face_idx]; 
      uint32_t v1 = facevertex_indices[3 * face_idx]; 
      uint32_t v2 = facevertex_indices[3 * face_idx]; 

      float n0[3], n1[3], n2[3];

      n0[0] = vertex_normals[3 * v0 + 0];
      n0[1] = vertex_normals[3 * v0 + 1];
      n0[2] = vertex_normals[3 * v0 + 2];

      n1[0] = vertex_normals[3 * v1 + 0];
      n1[1] = vertex_normals[3 * v1 + 1];
      n1[2] = vertex_normals[3 * v1 + 2];

      n2[0] = vertex_normals[3 * v2 + 0];
      n2[1] = vertex_normals[3 * v2 + 1];
      n2[2] = vertex_normals[3 * v2 + 2];

      lerp(Ns, n0, n1, n2, u, v);
    
    } else if (facevarying_normals.size() > 0) {

      float n0[3], n1[3], n2[3];

      n0[0] = facevarying_normals[9 * face_idx + 0];
      n0[1] = facevarying_normals[9 * face_idx + 1];
      n0[2] = facevarying_normals[9 * face_idx + 2];

      n1[0] = facevarying_normals[9 * face_idx + 3];
      n1[1] = facevarying_normals[9 * face_idx + 4];
      n1[2] = facevarying_normals[9 * face_idx + 5];

      n2[0] = facevarying_normals[9 * face_idx + 6];
      n2[1] = facevarying_normals[9 * face_idx + 7];
      n2[2] = facevarying_normals[9 * face_idx + 8];

      lerp(Ns, n0, n1, n2, u, v);

    } else {

      // Use geometric normal.
      Ns[0] = Ng[0];
      Ns[1] = Ng[1];
      Ns[2] = Ng[2];

    }

  }

  ///
  /// Get texture coordinate at `face_idx' th face.
  ///
  void GetTexCoord(float tcoord[3], const unsigned int face_idx, const float u, const float v) {

    if (vertex_uvs.size() > 0) {
      uint32_t v0 = facevertex_indices[3 * face_idx]; 
      uint32_t v1 = facevertex_indices[3 * face_idx]; 
      uint32_t v2 = facevertex_indices[3 * face_idx]; 

      float t0[3], t1[3], t2[3];

      t0[0] = facevarying_uvs[6 * face_idx + 0];
      t0[1] = facevarying_uvs[6 * face_idx + 1];
      t0[2] = static_cast<T>(0.0);

      t1[0] = facevarying_uvs[6 * face_idx + 2];
      t1[1] = facevarying_uvs[6 * face_idx + 3];
      t1[2] = static_cast<T>(0.0);

      t2[0] = facevarying_uvs[6 * face_idx + 4];
      t2[1] = facevarying_uvs[6 * face_idx + 5];
      t2[2] = static_cast<T>(0.0);

      lerp(tcoord, t0, t1, t2, u, v);

    } else if (facevarying_uvs.size() > 0) {

      float t0[3], t1[3], t2[3];

      t0[0] = facevarying_uvs[6 * face_idx + 0];
      t0[1] = facevarying_uvs[6 * face_idx + 1];
      t0[2] = static_cast<T>(0.0);

      t1[0] = facevarying_uvs[6 * face_idx + 2];
      t1[1] = facevarying_uvs[6 * face_idx + 3];
      t1[2] = static_cast<T>(0.0);

      t2[0] = facevarying_uvs[6 * face_idx + 4];
      t2[1] = facevarying_uvs[6 * face_idx + 5];
      t2[2] = static_cast<T>(0.0);

      lerp(tcoord, t0, t1, t2, u, v);

    } else {

      tcoord[0] = static_cast<T>(0.0);
      tcoord[1] = static_cast<T>(0.0);
      tcoord[2] = static_cast<T>(0.0);

    }

  }


  ///
  /// ---
  ///

  std::vector<float> vertices;  // vec3f
  std::vector<uint32_t>
      facevertex_indices;  // triangulated indices. 3 x num_faces

  std::vector<float> vertex_normals;    // 'vertex'-varying normals. 3 x 3 x num_verts
  std::vector<float> facevarying_normals;    // 3 x 3 x num_faces

  std::vector<float> vertex_uvs;  // 2 x 3 x num_verts
  std::vector<float> facevarying_uvs;  // 2 x 3 x num_faces

  // arbitrary primvars(including texcoords(float2))
  std::vector<Buffer<float>> float_primvars;
  std::map<std::string, size_t>
      float_primvars_map;  // <name, index to `float_primvars`>

  // arbitrary primvars in int type(e.g. texcoord indices(int3))
  std::vector<Buffer<int32_t>> int_primvars;
  std::map<std::string, size_t>
      int_primvars_map;  // <name, index to `int_primvars`>

  int material_id{-1};  // per-geom material. index to `RenderScene::materials`

  float world_matrix[4][4];

  //nanort::BVHAccel<float> accel;
};

template<typename T>
struct UVReader {

  static_assert(std::is_same<T, float>::value || std::is_same<T, vec2>::value || std::is_same<T, vec3>::value,
                "Unsupported type for UVReader");

  int32_t st_id{-1};       // index to DrawGeomMesh::float_primvars
  int32_t indices_id{-1};  // index to DrawGeomMesh::int_primvars

  mat2 uv_transform;

  // Fetch interpolated UV coordinate
  T fetch_uv(size_t face_id, float varyu, float varyv);
};

struct Texture {
  enum Channel {
    TEXTURE_CHANNEL_R,
    TEXTURE_CHANNEL_G,
    TEXTURE_CHANNEL_B,
    TEXTURE_CHANNEL_RGB,
    TEXTURE_CHANNEL_RGBA,
  };

  UVReader<vec2> uv_reader;
  int32_t image_id{-1};

  // NOTE: for single channel(e.g. R), [0] will be filled for the return value.
  std::array<float, 4> fetch(size_t face_id, float varyu, float varyv,
                             Channel channel);
};

// https://graphics.pixar.com/usd/release/spec_usdpreviewsurface.html#texture-reader
// https://learn.foundry.com/modo/901/content/help/pages/uving/udim_workflow.html
// Up to 10 tiles for U direction.
// Not sure there is an limitation for V direction. Anyway maximum tile id is
// 9999.

#if 0
static uint32_t GetUDIMTileId(uint32_t u, uint32_t v)
{
  uint32_t uu = std::max(1u, std::min(10u, u+1)); // clamp U dir.

  return 1000 + v * 10 + uu;
}
#endif

struct UDIMTexture {
  UVReader<vec2> uv_reader;
  std::unordered_map<uint32_t, int32_t>
      images;  // key: udim tile_id, value: image_id

  // NOTE: for single channel(e.g. R), [0] will be filled for the return value.
  std::array<float, 4> fetch(size_t face_id, float varyu, float varyv,
                             Texture::Channel channel);
};

// base color(fallback color) or Texture
template <typename T>
struct ShaderParam {
  ShaderParam(const T &t) { value = t; }

  T value;
  int32_t texture_id{-1};
};

// UsdPreviewSurface
struct PreviewSurfaceShader {
  bool useSpecularWorkFlow{false};

  ShaderParam<vec3> diffuseColor{{0.18f, 0.18f, 0.18f}};
  ShaderParam<float> metallic{0.0f};
  ShaderParam<float> roughness{0.5f};
  ShaderParam<float> clearcoat{0.0f};
  ShaderParam<float> clearcoatRoughness{0.01f};
  ShaderParam<float> opacity{1.0f};
  ShaderParam<float> opacityThreshold{0.0f};
  ShaderParam<float> ior{1.5f};
  ShaderParam<vec3> normal{{0.0f, 0.0f, 1.0f}};
  ShaderParam<float> displacement{0.0f};
  ShaderParam<float> occlusion{0.0f};
};

struct Material {
  PreviewSurfaceShader shader;
};


// Simple LDR texture
struct Image {
  std::vector<uint8_t> image;
  int32_t width{-1};
  int32_t height{-1};
  int32_t channels{-1};  // e.g. 3 for RGB.
};

class RTRenderScene {
 public:
  std::vector<DrawGeomMesh<float>> draw_meshes;
  std::vector<Material> materials;
  std::vector<Texture> textures;
  std::vector<Image> images;

  // <precision, ptr to mesh class>
  //std::vector<nanosg::Node<float, DrawGeomMesh>> nodes;
  nanosg::Scene<float, DrawGeomMesh<float>> scene;


  bool SetupFromUSDFile(const std::string &usd_filename, std::string &warn, std::string &err);
};

bool Render(const RTRenderScene &scene, const Camera &cam, AOV *output);

// Render images for lines [start_y, end_y]
// single-threaded. for webassembly.
bool RenderLines(int start_y, int end_y, const RTRenderScene &scene,
                 const Camera &cam, AOV *output);

}  // namespace example
