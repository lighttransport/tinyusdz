#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-tydra-subdivision.h"

#include <cmath>
#include <cstdint>
#include <string>

#include "tinyusdz.hh"
#include "tydra/render-data.hh"

namespace {

tinyusdz::Stage LoadStageFromString(const std::string &usda) {
  tinyusdz::Stage stage;
  std::string warn;
  std::string err;
  bool ret = tinyusdz::LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda.data()), usda.size(), ".", &stage,
      &warn, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("Failed to load USDA: %s", err.c_str());
  }
  return stage;
}

bool ConvertSceneWithSubdivision(const std::string &usda, int32_t subdivision_level,
                                 tinyusdz::tydra::RenderScene *scene,
                                 std::string *err,
                                 std::string *warn = nullptr) {
  tinyusdz::Stage stage = LoadStageFromString(usda);

  tinyusdz::tydra::RenderSceneConverterEnv env(stage);
  env.mesh_config.subdivision_level = subdivision_level;

  tinyusdz::tydra::RenderSceneConverter converter;
  bool ret = converter.ConvertToRenderScene(env, scene);
  if (err) {
    *err = converter.GetError();
  }
  if (warn) {
    *warn = converter.GetWarning();
  }
  return ret;
}

}  // namespace

void tydra_subdivision_catmullclark_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [
            0, 1, 2, 3,
            7, 6, 5, 4,
            0, 4, 5, 1,
            1, 5, 6, 2,
            2, 6, 7, 3,
            3, 7, 4, 0
        ]
        point3f[] points = [
            (-1, -1, -1),
            ( 1, -1, -1),
            ( 1,  1, -1),
            (-1,  1, -1),
            (-1, -1,  1),
            ( 1, -1,  1),
            ( 1,  1,  1),
            (-1,  1,  1)
        ]
        uniform token subdivisionScheme = "catmullClark"
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  TEST_CHECK(scene.meshes.size() == 1);
  if (scene.meshes.empty()) {
    return;
  }

  const auto &mesh = scene.meshes[0];
  TEST_CHECK(mesh.points.size() > 8);
  TEST_CHECK(mesh.faceVertexIndices().size() > 24);
  TEST_CHECK(mesh.normals.vertex_count() == mesh.points.size());
  TEST_CHECK(mesh.faceVertexCounts().size() == mesh.faceVertexIndices().size() / 3);
}

void tydra_subdivision_loop_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [3, 3, 3, 3]
        int[] faceVertexIndices = [
            0, 1, 2,
            0, 3, 1,
            1, 3, 2,
            2, 3, 0
        ]
        point3f[] points = [
            (0, 0, 1),
            (1, 0, 0),
            (0, 1, 0),
            (-1, -1, -1)
        ]
        uniform token subdivisionScheme = "loop"
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  TEST_CHECK(scene.meshes.size() == 1);
  if (scene.meshes.empty()) {
    return;
  }

  const auto &mesh = scene.meshes[0];
  TEST_CHECK(mesh.points.size() > 4);
  TEST_CHECK(mesh.faceVertexCounts().size() > 4);
  TEST_CHECK(mesh.faceVertexIndices().size() == mesh.faceVertexCounts().size() * 3);
  TEST_CHECK(mesh.normals.vertex_count() == mesh.points.size());
}

void tydra_subdivision_bilinear_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "bilinear"
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  TEST_CHECK(scene.meshes.size() == 1);
  if (scene.meshes.empty()) {
    return;
  }

  const auto &mesh = scene.meshes[0];
  TEST_CHECK(mesh.points.size() > 4);
  TEST_CHECK(mesh.faceVertexCounts().size() == mesh.faceVertexIndices().size() / 3);
  TEST_CHECK(mesh.normals.vertex_count() == mesh.points.size());
}

void tydra_subdivision_remaps_material_subset_faces_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4, 4]
        int[] faceVertexIndices = [
            0, 1, 4, 3,
            1, 2, 5, 4
        ]
        point3f[] points = [
            (0, 0, 0),
            (1, 0, 0),
            (2, 0, 0),
            (0, 1, 0),
            (1, 1, 0),
            (2, 1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        def GeomSubset "front"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [1]
        }
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  TEST_CHECK(scene.meshes.size() == 1);
  if (scene.meshes.empty()) {
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto it = mesh.material_subsetMap.find("front");
  TEST_CHECK(it != mesh.material_subsetMap.end());
  if (it == mesh.material_subsetMap.end()) {
    return;
  }

  TEST_CHECK(it->second.usdIndices.size() == 1);
  TEST_CHECK(it->second.indices().size() == 8);
  for (int face_index : it->second.indices()) {
    TEST_CHECK(face_index >= 0);
    TEST_CHECK(size_t(face_index) < mesh.faceVertexCounts().size());
  }
}

void tydra_subdivision_refines_facevarying_uv_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        texCoord2f[] primvars:st = [
            (0, 0),
            (1, 0),
            (1, 1),
            (0, 1)
        ] (
            interpolation = "faceVarying"
        )
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto tc = mesh.texcoords.find(0);
  TEST_CHECK(tc != mesh.texcoords.end());
  if (tc == mesh.texcoords.end()) {
    return;
  }

  const auto &uv = tc->second;
  const size_t n = uv.vertex_count();
  TEST_CHECK(n > 4);  // refined beyond the 4 authored corners
  const float *vals = reinterpret_cast<const float *>(uv.buffer());

  // All refined UVs stay inside the [0,1]^2 chart hull (affine combinations
  // of the corner values).
  bool in_hull = true;
  for (size_t i = 0; i < n * 2; i++) {
    in_hull &= (vals[i] >= -1e-5f) && (vals[i] <= 1.0f + 1e-5f);
  }
  TEST_CHECK(in_hull);

  // cornersPlus1 (USD default) pins the 4 chart corners exactly.
  const float corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (const auto &c : corners) {
    bool found = false;
    for (size_t i = 0; i < n && !found; i++) {
      found = (vals[2 * i] == c[0]) && (vals[2 * i + 1] == c[1]);
    }
    TEST_CHECK(found);
  }

  // The chart center refines to (0.5, 0.5) for a single quad.
  bool found_center = false;
  for (size_t i = 0; i < n && !found_center; i++) {
    found_center = (std::fabs(vals[2 * i] - 0.5f) < 1e-6f) &&
                   (std::fabs(vals[2 * i + 1] - 0.5f) < 1e-6f);
  }
  TEST_CHECK(found_center);
}

void tydra_subdivision_refines_uv_seam_test(void) {
  // Two quads with a UV seam between them (separate islands): the refined
  // mesh must keep the seam (UVs from both islands present, none blended
  // across the 0.6 gap between island ranges [0,0.4] and [0.6,1.0]).
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4, 4]
        int[] faceVertexIndices = [
            0, 1, 4, 3,
            1, 2, 5, 4
        ]
        point3f[] points = [
            (0, 0, 0),
            (1, 0, 0),
            (2, 0, 0),
            (0, 1, 0),
            (1, 1, 0),
            (2, 1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        texCoord2f[] primvars:st = [
            (0, 0), (0.4, 0), (0.4, 0.4), (0, 0.4),
            (0.6, 0.6), (1, 0.6), (1, 1), (0.6, 1)
        ] (
            interpolation = "faceVarying"
        )
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 2, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto tc = mesh.texcoords.find(0);
  TEST_CHECK(tc != mesh.texcoords.end());
  if (tc == mesh.texcoords.end()) {
    return;
  }

  const auto &uv = tc->second;
  const size_t n = uv.vertex_count();
  TEST_CHECK(n > 8);
  const float *vals = reinterpret_cast<const float *>(uv.buffer());

  // No refined U value may land strictly inside the inter-island gap
  // (0.4, 0.6): that would mean values were blended across the seam.
  bool island0 = false;
  bool island1 = false;
  bool blended = false;
  for (size_t i = 0; i < n; i++) {
    const float u = vals[2 * i];
    island0 |= (u <= 0.4f + 1e-6f);
    island1 |= (u >= 0.6f - 1e-6f);
    blended |= (u > 0.4f + 1e-4f) && (u < 0.6f - 1e-4f);
  }
  TEST_CHECK(island0);
  TEST_CHECK(island1);
  TEST_CHECK(!blended);
}

void tydra_subdivision_multiple_material_subsets_disjoint_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4, 4]
        int[] faceVertexIndices = [
            0, 1, 4, 3,
            1, 2, 5, 4
        ]
        point3f[] points = [
            (0, 0, 0),
            (1, 0, 0),
            (2, 0, 0),
            (0, 1, 0),
            (1, 1, 0),
            (2, 1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        def GeomSubset "left"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [0]
        }

        def GeomSubset "right"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [1]
        }
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto left = mesh.material_subsetMap.find("left");
  auto right = mesh.material_subsetMap.find("right");
  TEST_CHECK(left != mesh.material_subsetMap.end());
  TEST_CHECK(right != mesh.material_subsetMap.end());
  if (left == mesh.material_subsetMap.end() ||
      right == mesh.material_subsetMap.end()) {
    return;
  }

  TEST_CHECK(left->second.indices().size() == 8);
  TEST_CHECK(right->second.indices().size() == 8);
  for (int idx : left->second.indices()) {
    TEST_CHECK(idx >= 0 && idx < 8);
  }
  for (int idx : right->second.indices()) {
    TEST_CHECK(idx >= 8 && idx < 16);
  }
}

void tydra_subdivision_level2_subset_remap_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        def GeomSubset "patch"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [0]
        }
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 2, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto patch = mesh.material_subsetMap.find("patch");
  TEST_CHECK(patch != mesh.material_subsetMap.end());
  if (patch == mesh.material_subsetMap.end()) {
    return;
  }

  TEST_CHECK(mesh.faceVertexCounts().size() == 32);
  TEST_CHECK(patch->second.indices().size() == 32);
  TEST_CHECK(patch->second.indices().front() == 0);
  TEST_CHECK(patch->second.indices().back() == 31);
}

void tydra_subdivision_loop_subset_remap_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [3, 3, 3, 3]
        int[] faceVertexIndices = [
            0, 1, 2,
            0, 3, 1,
            1, 3, 2,
            2, 3, 0
        ]
        point3f[] points = [
            (0, 0, 1),
            (1, 0, 0),
            (0, 1, 0),
            (-1, -1, -1)
        ]
        uniform token subdivisionScheme = "loop"

        def GeomSubset "face2"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [2]
        }
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto subset = mesh.material_subsetMap.find("face2");
  TEST_CHECK(subset != mesh.material_subsetMap.end());
  if (subset == mesh.material_subsetMap.end()) {
    return;
  }

  TEST_CHECK(subset->second.indices().size() == 4);
  TEST_CHECK(subset->second.indices().front() == 8);
  TEST_CHECK(subset->second.indices().back() == 11);
}

void tydra_subdivision_bilinear_mixed_topology_subset_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [3, 4]
        int[] faceVertexIndices = [
            0, 1, 2,
            1, 3, 4, 2
        ]
        point3f[] points = [
            (0, 0, 0),
            (1, 0, 0),
            (0, 1, 0),
            (2, 0, 0),
            (2, 1, 0)
        ]
        uniform token subdivisionScheme = "bilinear"

        def GeomSubset "triFace"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [0]
        }

        def GeomSubset "quadFace"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [1]
        }
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto tri = mesh.material_subsetMap.find("triFace");
  auto quad = mesh.material_subsetMap.find("quadFace");
  TEST_CHECK(tri != mesh.material_subsetMap.end());
  TEST_CHECK(quad != mesh.material_subsetMap.end());
  if (tri == mesh.material_subsetMap.end() ||
      quad == mesh.material_subsetMap.end()) {
    return;
  }

  // Bilinear splits an n-gon into n quads (OpenSubdiv semantics): the
  // triangle becomes 3 quads (6 triangles after triangulation) and the quad
  // becomes 4 quads (8 triangles).
  TEST_CHECK(tri->second.indices().size() == 6);
  TEST_CHECK(quad->second.indices().size() == 8);
  TEST_CHECK(tri->second.indices().front() == 0);
  TEST_CHECK(tri->second.indices().back() == 5);
  TEST_CHECK(quad->second.indices().front() == 6);
  TEST_CHECK(quad->second.indices().back() == 13);
}

void tydra_subdivision_level_zero_keeps_base_mesh_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 0, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  TEST_CHECK(mesh.points.size() == 4);
  TEST_CHECK(mesh.faceVertexCounts().size() == 2);
  TEST_CHECK(mesh.faceVertexIndices().size() == 6);
}

void tydra_subdivision_scheme_none_ignores_requested_level_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "none"
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 2, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  TEST_CHECK(mesh.points.size() == 4);
  TEST_CHECK(mesh.faceVertexCounts().size() == 2);
  TEST_CHECK(mesh.faceVertexIndices().size() == 6);
}

void tydra_subdivision_subset_with_multiple_source_faces_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4, 4, 4]
        int[] faceVertexIndices = [
            0, 1, 5, 4,
            1, 2, 6, 5,
            2, 3, 7, 6
        ]
        point3f[] points = [
            (0, 0, 0),
            (1, 0, 0),
            (2, 0, 0),
            (3, 0, 0),
            (0, 1, 0),
            (1, 1, 0),
            (2, 1, 0),
            (3, 1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        def GeomSubset "outer"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [0, 2]
        }
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  const auto &mesh = scene.meshes[0];
  auto subset = mesh.material_subsetMap.find("outer");
  TEST_CHECK(subset != mesh.material_subsetMap.end());
  if (subset == mesh.material_subsetMap.end()) {
    return;
  }

  TEST_CHECK(subset->second.indices().size() == 16);
  for (int idx : subset->second.indices()) {
    TEST_CHECK((idx >= 0 && idx < 8) || (idx >= 16 && idx < 24));
  }
}

void tydra_subdivision_recomputes_authored_normals_with_warning_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        normal3f[] normals = [
            (0, 0, 1),
            (0, 0, 1),
            (0, 0, 1),
            (0, 0, 1)
        ] (
            interpolation = "vertex"
        )
        uniform token subdivisionScheme = "catmullClark"
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  std::string warn;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err, &warn);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  TEST_CHECK(warn.find("ignores authored normals") != std::string::npos);
  TEST_CHECK(scene.meshes[0].normals.vertex_count() == scene.meshes[0].points.size());
}

void tydra_subdivision_incomplete_partition_warns_but_converts_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        uniform token subsetFamily:materialBind:familyType = "partition"
        int[] faceVertexCounts = [4, 4]
        int[] faceVertexIndices = [
            0, 1, 4, 3,
            1, 2, 5, 4
        ]
        point3f[] points = [
            (0, 0, 0),
            (1, 0, 0),
            (2, 0, 0),
            (0, 1, 0),
            (1, 1, 0),
            (2, 1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        def GeomSubset "onlyHalf"
        {
            uniform token elementType = "face"
            uniform token familyName = "materialBind"
            int[] indices = [0]
        }
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  std::string warn;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err, &warn);
  TEST_CHECK(ret);
  if (!ret || scene.meshes.empty()) {
    TEST_MSG("ConvertToRenderScene failed: %s", err.c_str());
    return;
  }

  TEST_CHECK(warn.find("GeomSubset validation:") != std::string::npos);
  TEST_CHECK(scene.meshes[0].material_subsetMap.size() == 1);
}

void tydra_subdivision_rejects_display_color_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"
        color3f[] primvars:displayColor = [(1, 0, 0)] (
            interpolation = "constant"
        )
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("displayColor") != std::string::npos);
}

void tydra_subdivision_rejects_display_opacity_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"
        float[] primvars:displayOpacity = [0.5] (
            interpolation = "constant"
        )
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("displayOpacity") != std::string::npos);
}

void tydra_subdivision_rejects_tangents_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"
        float3[] primvars:tangents = [
            (1, 0, 0),
            (1, 0, 0),
            (1, 0, 0),
            (1, 0, 0)
        ] (
            interpolation = "vertex"
        )
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("tangent") != std::string::npos);
}

void tydra_subdivision_rejects_binormals_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"
        float3[] primvars:binormals = [
            (0, 1, 0),
            (0, 1, 0),
            (0, 1, 0),
            (0, 1, 0)
        ] (
            interpolation = "vertex"
        )
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("binormal") != std::string::npos);
}

void tydra_subdivision_rejects_skinning_test(void) {
  const std::string usda = R"usda(#usda 1.0
(
    defaultPrim = "Root"
)

def Xform "Root"
{
    def Mesh "Mesh"
    {
        int[] faceVertexCounts = [4]
        int[] faceVertexIndices = [0, 1, 2, 3]
        point3f[] points = [
            (-1, -1, 0),
            ( 1, -1, 0),
            ( 1,  1, 0),
            (-1,  1, 0)
        ]
        uniform token subdivisionScheme = "catmullClark"

        int[] primvars:skel:jointIndices = [
            0, 1,
            0, 1,
            0, 1,
            0, 1
        ] (
            interpolation = "vertex"
            elementSize = 2
        )

        float[] primvars:skel:jointWeights = [
            0.75, 0.25,
            0.75, 0.25,
            0.75, 0.25,
            0.75, 0.25
        ] (
            interpolation = "vertex"
            elementSize = 2
        )
    }
}
)usda";

  tinyusdz::tydra::RenderScene scene;
  std::string err;
  bool ret = ConvertSceneWithSubdivision(usda, 1, &scene, &err);
  TEST_CHECK(!ret);
  TEST_CHECK(err.find("skinned meshes") != std::string::npos);
}
