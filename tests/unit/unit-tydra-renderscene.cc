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

// ---------------------------------------------------------------------------
// Shared fixture for the streaming tests: two meshes (one with a bound
// material), a camera and a light under an Xform.
// ---------------------------------------------------------------------------
static const char *kStreamingUSDA = R"(#usda 1.0
(
  upAxis = "Y"
)
def Xform "Root" {
  def Mesh "MeshA" (
    prepend apiSchemas = ["MaterialBindingAPI"]
  ) {
    point3f[] points = [(0,0,0),(1,0,0),(0,1,0)]
    int[] faceVertexCounts = [3]
    int[] faceVertexIndices = [0,1,2]
    rel material:binding = </Root/Materials/MatA>
  }
  def Mesh "MeshB" {
    point3f[] points = [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]
    int[] faceVertexCounts = [4]
    int[] faceVertexIndices = [0,1,2,3]
  }
  def Scope "Materials" {
    def Material "MatA" {
      token outputs:surface.connect = </Root/Materials/MatA/PreviewSurface.outputs:surface>
      def Shader "PreviewSurface" {
        uniform token info:id = "UsdPreviewSurface"
        color3f inputs:diffuseColor = (0.8, 0.2, 0.1)
        token outputs:surface
      }
    }
  }
  def Camera "Cam" {
    float focalLength = 50
  }
  def SphereLight "Light" {
    float inputs:intensity = 100
  }
}
)";

// ---------------------------------------------------------------------------
// h. Streaming conversion equivalence: ConvertToRenderSceneStreaming must
//    deliver every element via the sink (counts == final array sizes), in the
//    documented order (material before the mesh that binds it; no node before
//    the last mesh), fire on_complete exactly once, and produce a RenderScene
//    equal to the monolithic ConvertToRenderScene.
// ---------------------------------------------------------------------------
void tydra_renderscene_streaming_equivalence_test(void) {
  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(kStreamingUSDA),
      std::strlen(kStreamingUSDA), "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  // Monolithic reference.
  tydra::RenderScene sceneA;
  {
    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderSceneConverter converter;
    ok = converter.ConvertToRenderScene(env, &sceneA);
    TEST_CHECK(ok);
    TEST_MSG("ConvertToRenderScene failed: %s", converter.GetError().c_str());
  }

  // Streaming.
  struct StreamLog {
    size_t images = 0, buffers = 0, textures = 0, materials = 0, meshes = 0;
    size_t lights = 0, cameras = 0, nodes = 0;
    size_t skeletons = 0, animations = 0, instances = 0;
    size_t completes = 0;
    bool material_before_mesh_ok = true;  // each mesh.material_id < materials seen
    bool node_seen = false;
    bool mesh_after_node = false;  // a mesh arrived after a node (ordering bug)
  } log;

  tydra::RenderSceneSink sink;
  sink.on_image = [&](const tydra::TextureImage &, size_t, void *) {
    log.images++; return true; };
  sink.on_buffer = [&](const tydra::BufferData &, size_t, void *) {
    log.buffers++; return true; };
  sink.on_texture = [&](const tydra::UVTexture &, size_t, const std::string &, void *) {
    log.textures++; return true; };
  sink.on_material = [&](const tydra::RenderMaterial &, size_t, const std::string &, void *) {
    log.materials++; return true; };
  sink.on_mesh = [&](const tydra::RenderMesh &m, size_t, const std::string &, void *) {
    if (m.material_id >= 0 && size_t(m.material_id) >= log.materials) {
      log.material_before_mesh_ok = false;
    }
    if (log.node_seen) log.mesh_after_node = true;
    log.meshes++;
    return true;
  };
  sink.on_light = [&](const tydra::RenderLight &, size_t, const std::string &, void *) {
    log.lights++; return true; };
  sink.on_camera = [&](const tydra::RenderCamera &, size_t, const std::string &, void *) {
    log.cameras++; return true; };
  sink.on_root_node = [&](const tydra::Node &, size_t, void *) {
    log.node_seen = true; log.nodes++; return true; };
  sink.on_skeleton = [&](const tydra::SkelHierarchy &, size_t, const std::string &, void *) {
    log.skeletons++; return true; };
  sink.on_animation = [&](const tydra::AnimationClip &, size_t, const std::string &, void *) {
    log.animations++; return true; };
  sink.on_instance = [&](const tydra::RenderInstance &, size_t, const std::string &, void *) {
    log.instances++; return true; };
  sink.on_complete = [&](const tydra::RenderScene &, void *) {
    log.completes++; return true; };

  tydra::RenderScene sceneB;
  {
    tydra::RenderSceneConverterEnv env(stage);
    tydra::RenderSceneConverter converter;
    ok = converter.ConvertToRenderSceneStreaming(env, sink, &sceneB);
    TEST_CHECK(ok);
    TEST_MSG("ConvertToRenderSceneStreaming failed: %s",
             converter.GetError().c_str());
  }

  // Streamed counts match the streamed scene's arrays.
  TEST_CHECK(log.meshes == sceneB.meshes.size());
  TEST_MSG("on_mesh count %zu != meshes %zu", log.meshes, sceneB.meshes.size());
  TEST_CHECK(log.materials == sceneB.materials.size());
  TEST_MSG("on_material count %zu != materials %zu", log.materials,
           sceneB.materials.size());
  TEST_CHECK(log.textures == sceneB.textures.size());
  TEST_CHECK(log.images == sceneB.images.size());
  TEST_CHECK(log.buffers == sceneB.buffers.size());
  TEST_CHECK(log.lights == sceneB.lights.size());
  TEST_CHECK(log.cameras == sceneB.cameras.size());
  TEST_CHECK(log.nodes == sceneB.nodes.size());
  TEST_CHECK(log.skeletons == sceneB.skeletons.size());
  TEST_CHECK(log.animations == sceneB.animations.size());
  TEST_CHECK(log.instances == sceneB.instances.size());

  // Ordering guarantees.
  TEST_CHECK(log.material_before_mesh_ok);
  TEST_MSG("a mesh referenced a material that was not emitted before it");
  TEST_CHECK(!log.mesh_after_node);
  TEST_MSG("a mesh was streamed after a node (meshes must precede the hierarchy)");

  // on_complete fires exactly once.
  TEST_CHECK(log.completes == 1);
  TEST_MSG("on_complete fired %zu times (expected 1)", log.completes);

  // Streaming result equals the monolithic result.
  TEST_CHECK(sceneA.meshes.size() == sceneB.meshes.size());
  TEST_CHECK(sceneA.materials.size() == sceneB.materials.size());
  TEST_CHECK(sceneA.nodes.size() == sceneB.nodes.size());
  TEST_CHECK(sceneA.lights.size() == sceneB.lights.size());
  TEST_CHECK(sceneA.cameras.size() == sceneB.cameras.size());
  bool per_mesh_ok = true;
  for (size_t i = 0; i < sceneA.meshes.size() && i < sceneB.meshes.size(); i++) {
    if (sceneA.meshes[i].points.size() != sceneB.meshes[i].points.size() ||
        sceneA.meshes[i].material_id != sceneB.meshes[i].material_id) {
      per_mesh_ok = false;
    }
  }
  TEST_CHECK(per_mesh_ok);
  TEST_MSG("streamed meshes differ from monolithic meshes");

  // Sanity: the fixture really exercises meshes + a material.
  TEST_CHECK(sceneB.meshes.size() == 2);
  TEST_CHECK(sceneB.materials.size() >= 1);
}

// ---------------------------------------------------------------------------
// i. Streaming cancellation: a sink that returns false from on_mesh aborts the
//    conversion; the consumer keeps exactly the meshes delivered so far.
// ---------------------------------------------------------------------------
void tydra_renderscene_streaming_cancel_test(void) {
  Stage stage;
  std::string warn, err;
  bool ok = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(kStreamingUSDA),
      std::strlen(kStreamingUSDA), "test.usda", &stage, &warn, &err);
  TEST_CHECK(ok);
  TEST_MSG("LoadUSDAFromMemory failed: %s", err.c_str());

  size_t meshes_seen = 0;
  tydra::RenderSceneSink sink;
  sink.on_mesh = [&](const tydra::RenderMesh &, size_t, const std::string &, void *) {
    meshes_seen++;
    return false;  // cancel on the very first mesh
  };

  tydra::RenderSceneConverterEnv env(stage);
  tydra::RenderScene scene;
  tydra::RenderSceneConverter converter;
  ok = converter.ConvertToRenderSceneStreaming(env, sink, &scene);

  TEST_CHECK(!ok);
  TEST_MSG("streaming conversion should report failure when cancelled");
  TEST_CHECK(meshes_seen == 1);
  TEST_MSG("expected exactly 1 mesh delivered before cancel, got %zu",
           meshes_seen);
}
