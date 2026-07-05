// SPDX-License-Identifier: Apache-2.0
#include "mesh_build.hh"

#include <array>
#include <cmath>
#include <cstdio>

#include "tydra/render-data.hh"

namespace {

namespace tydra = tinyusdz::tydra;

bool Near(float a, float b, float eps = 1.0e-6f) {
  return std::fabs(a - b) <= eps;
}

template <typename T, size_t N>
void SetAttr(tydra::VertexAttributeFormat fmt, tydra::VertexVariability var,
             const std::array<T, N>& values, tydra::VertexAttribute* out) {
  out->format = fmt;
  out->set_buffer(reinterpret_cast<const uint8_t*>(values.data()),
                  values.size() * sizeof(T));
  out->variability = var;
}

}  // namespace

int main() {
  tydra::RenderScene scene;

  tydra::RenderMesh mesh;
  mesh.prim_name = "FaceVaryingPrimvars";
  mesh.abs_path = "/FaceVaryingPrimvars";
  mesh.is_single_indexable = false;
  mesh.points = {{-1.0f, -1.0f, 0.0f},
                 {1.0f, -1.0f, 0.0f},
                 {1.0f, 1.0f, 0.0f},
                 {-1.0f, 1.0f, 0.0f}};
  mesh.usdFaceVertexCounts = {3, 3};
  mesh.usdFaceVertexIndices = {0, 1, 2, 0, 2, 3};

  const std::array<tydra::vec3, 6> tangents = {
      tydra::vec3{1.0f, 0.0f, 0.0f}, tydra::vec3{1.0f, 0.0f, 0.0f},
      tydra::vec3{1.0f, 0.0f, 0.0f}, tydra::vec3{0.0f, 1.0f, 0.0f},
      tydra::vec3{0.0f, 1.0f, 0.0f}, tydra::vec3{0.0f, 1.0f, 0.0f}};
  const std::array<tydra::vec3, 6> binormals = {
      tydra::vec3{0.0f, 1.0f, 0.0f}, tydra::vec3{0.0f, 1.0f, 0.0f},
      tydra::vec3{0.0f, 1.0f, 0.0f}, tydra::vec3{-1.0f, 0.0f, 0.0f},
      tydra::vec3{-1.0f, 0.0f, 0.0f}, tydra::vec3{-1.0f, 0.0f, 0.0f}};
  const std::array<tinyusdz::value::texcoord2f, 6> uv1 = {
      tinyusdz::value::texcoord2f{0.1f, 0.2f},
      tinyusdz::value::texcoord2f{0.8f, 0.2f},
      tinyusdz::value::texcoord2f{0.8f, 0.9f},
      tinyusdz::value::texcoord2f{0.1f, 0.2f},
      tinyusdz::value::texcoord2f{0.8f, 0.9f},
      tinyusdz::value::texcoord2f{0.1f, 0.9f}};
  const std::array<tydra::vec3, 6> display = {
      tydra::vec3{1.0f, 0.0f, 0.0f}, tydra::vec3{0.0f, 1.0f, 0.0f},
      tydra::vec3{0.0f, 0.0f, 1.0f}, tydra::vec3{1.0f, 1.0f, 0.0f},
      tydra::vec3{0.0f, 1.0f, 1.0f}, tydra::vec3{1.0f, 0.0f, 1.0f}};

  SetAttr(tydra::VertexAttributeFormat::Vec3,
          tydra::VertexVariability::FaceVarying, tangents, &mesh.tangents);
  SetAttr(tydra::VertexAttributeFormat::Vec3,
          tydra::VertexVariability::FaceVarying, binormals, &mesh.binormals);
  SetAttr(tydra::VertexAttributeFormat::Vec2,
          tydra::VertexVariability::FaceVarying, uv1, &mesh.texcoords[1]);
  SetAttr(tydra::VertexAttributeFormat::Vec3,
          tydra::VertexVariability::FaceVarying, display, &mesh.vertex_colors);

  scene.meshes.push_back(std::move(mesh));

  tusdview::DrawScene draw;
  tusdview::BuildDrawScene(scene, &draw);
  if (draw.meshes.size() != 1) {
    std::fprintf(stderr, "expected one draw mesh, got %zu\n", draw.meshes.size());
    return 1;
  }

  const tusdview::DrawMeshCPU& dm = draw.meshes[0];
  if (dm.vertices.size() != 6 || dm.indices.size() != 6) {
    std::fprintf(stderr, "facevarying mesh was not expanded to six draw vertices\n");
    return 1;
  }
  if (dm.tangents.size() != dm.vertices.size() * 3 ||
      dm.binormals.size() != dm.vertices.size() * 3) {
    std::fprintf(stderr, "tangent frame primvars were not packed\n");
    return 1;
  }
  if (dm.uv1.size() != dm.vertices.size() * 2) {
    std::fprintf(stderr, "secondary texcoord primvar was not packed\n");
    return 1;
  }
  if (dm.vertexColors.size() != dm.vertices.size() * 3) {
    std::fprintf(stderr, "displayColor primvar was not packed\n");
    return 1;
  }

  if (!Near(dm.tangents[0], 1.0f) || !Near(dm.tangents[9], 0.0f) ||
      !Near(dm.tangents[10], 1.0f) || !Near(dm.binormals[9], -1.0f) ||
      !Near(dm.binormals[10], 0.0f)) {
    std::fprintf(stderr, "facevarying tangent/binormal values changed\n");
    return 1;
  }
  if (!Near(dm.uv1[0], 0.1f) || !Near(dm.uv1[1], 0.8f) ||
      !Near(dm.uv1[10], 0.1f) || !Near(dm.uv1[11], 0.1f)) {
    std::fprintf(stderr, "secondary texcoord values or USD V flip changed\n");
    return 1;
  }
  if (!Near(dm.vertexColors[0], 1.0f) || !Near(dm.vertexColors[4], 1.0f) ||
      !Near(dm.vertexColors[17], 1.0f)) {
    std::fprintf(stderr, "facevarying displayColor values changed\n");
    return 1;
  }

  return 0;
}
