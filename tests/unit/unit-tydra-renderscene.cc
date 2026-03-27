#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-tydra-renderscene.h"

#include <cstring>

#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-converter.hh"
#include "tydra/scene-access.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "stage.hh"

using namespace tinyusdz;

// ---------------------------------------------------------------------------
// a. Empty stage -> RenderScene should succeed with zero content
// ---------------------------------------------------------------------------
void tydra_renderscene_empty_stage_test(void) {
  Stage stage;
  stage.commit();

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  bool ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene should succeed on empty stage");

  TEST_CHECK(scene.meshes.size() == 0);
  TEST_MSG("Empty stage should produce 0 meshes");

  TEST_CHECK(scene.materials.size() == 0);
  TEST_MSG("Empty stage should produce 0 materials");

  TEST_CHECK(scene.lights.size() == 0);
  TEST_MSG("Empty stage should produce 0 lights");

  TEST_CHECK(scene.cameras.size() == 0);
  TEST_MSG("Empty stage should produce 0 cameras");
}

// ---------------------------------------------------------------------------
// b. Single triangle mesh -> parse, convert, verify mesh exists with vertices
// ---------------------------------------------------------------------------
void tydra_renderscene_single_mesh_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "Tri" {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.meshes.size() >= 1);
  TEST_MSG("Expected at least 1 mesh, got %zu", scene.meshes.size());

  TEST_CHECK(scene.meshes[0].points.size() > 0);
  TEST_MSG("Mesh should have vertices");
}

// ---------------------------------------------------------------------------
// c. Xform containing Mesh child -> verify nodes and meshes
// ---------------------------------------------------------------------------
void tydra_renderscene_xform_hierarchy_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Xform "Group" {
    def Mesh "ChildMesh" {
      point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
      int[] faceVertexCounts = [3]
      int[] faceVertexIndices = [0,1,2]
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.nodes.size() >= 1);
  TEST_MSG("Expected at least 1 node, got %zu", scene.nodes.size());

  TEST_CHECK(scene.meshes.size() >= 1);
  TEST_MSG("Expected at least 1 mesh, got %zu", scene.meshes.size());
}

// ---------------------------------------------------------------------------
// d. Mesh bound to Material with UsdPreviewSurface -> verify material
// ---------------------------------------------------------------------------
void tydra_renderscene_material_binding_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "MyMesh" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MyMat>
  }
  def Scope "Materials" {
    def Material "MyMat" {
      token outputs:surface.connect = </Root/Materials/MyMat/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.8, 0.2, 0.1)
        float inputs:roughness = 0.4
        float inputs:metallic = 0.0
        token outputs:surface
      }
    }
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.materials.size() >= 1);
  TEST_MSG("Expected at least 1 material, got %zu", scene.materials.size());
}

// ---------------------------------------------------------------------------
// e. SphereLight -> verify light in converted scene
// ---------------------------------------------------------------------------
void tydra_renderscene_sphere_light_test(void) {
  const char *usda = R"(#usda 1.0
def SphereLight "MyLight" {
  float inputs:intensity = 100
  color3f inputs:color = (1, 1, 1)
  float inputs:radius = 0.5
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.lights.size() >= 1);
  TEST_MSG("Expected at least 1 light, got %zu", scene.lights.size());
}

// ---------------------------------------------------------------------------
// f. Camera -> verify camera in converted scene
// ---------------------------------------------------------------------------
void tydra_renderscene_camera_test(void) {
  const char *usda = R"(#usda 1.0
def Camera "MainCam" {
  float focalLength = 50
  float horizontalAperture = 36
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  TEST_CHECK(scene.cameras.size() >= 1);
  TEST_MSG("Expected at least 1 camera, got %zu", scene.cameras.size());
}

// ---------------------------------------------------------------------------
// g. Memory estimation -> convert scene, check estimate_memory_usage() > 0
// ---------------------------------------------------------------------------
void tydra_renderscene_memory_estimation_test(void) {
  const char *usda = R"(#usda 1.0
def Xform "Root" {
  def Mesh "Quad" {
    point3f[] points = [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
}
)";

  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda),
      "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;

  ok = converter.ConvertToRenderScene(env, &scene);
  TEST_CHECK(ok);
  TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());

  size_t mem = scene.estimate_memory_usage();
  TEST_CHECK(mem > 0);
  TEST_MSG("estimate_memory_usage() should be > 0, got %zu", mem);

  // Also verify that at least the arrays are non-empty as a sanity check
  bool has_content = !scene.nodes.empty() || !scene.meshes.empty();
  TEST_CHECK(has_content);
  TEST_MSG("Converted scene should have non-empty arrays");
}
