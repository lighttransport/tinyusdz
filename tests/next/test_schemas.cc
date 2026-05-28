/// Schema tests for the next library.
/// Tests detection functions (IsMesh, IsCamera, etc.) and data getters.

#include "next/tinyusdz-next.hh"
#include "next/schema/geom-mesh.hh"
#include "next/schema/geom-xform.hh"
#include "next/schema/usd-geom-camera.hh"
#include "next/schema/usd-lux.hh"
#include "next/schema/usd-shade.hh"
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>

using namespace tinyusdz::next;

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { printf("  %s ... ", name); test_count++; } while(0)
#define PASS() do { pass_count++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

static Stage MakeTestStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("Test");
  sb.SetUpAxis("Y");

  auto& layer = sb.GetLayerBuilder();

  // Xform
  layer.begin_prim("Test", "Xform");
  layer.add_property("visibility", Value::MakeToken("inherited"));
  layer.end_prim();

  // Mesh
  layer.begin_prim("TestMesh", "Mesh");
  std::vector<float> pts = {-1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1,
                            -1,-1, 1, 1,-1, 1, 1,1, 1, -1,1, 1};
  layer.add_property("points", Value::MakeFloat3Array(pts));
  layer.add_property("faceVertexCounts", Value::MakeIntArray({4,4,4,4,4,4}));
  layer.end_prim();

  // Camera
  layer.begin_prim("TestCam", "Camera");
  layer.add_property("focalLength", Value(50.0));
  layer.end_prim();

  // SphereLight
  layer.begin_prim("TestLight", "SphereLight");
  layer.add_property("intensity", Value(100.0f));
  layer.end_prim();

  // Material
  layer.begin_prim("TestMat", "Material");
  layer.end_prim();

  // Shader
  layer.begin_prim("TestShader", "Shader");
  layer.add_property("info:id", Value::MakeToken("UsdPreviewSurface"));
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

void test_mesh_schema() {
  TEST("IsMesh");
  auto stage = MakeTestStage();
  auto prim = stage.GetPrimAtPath("/TestMesh");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsMesh(prim)) { FAIL("expected Mesh"); return; }
  UsdGeomMesh mesh(prim);
  assert(mesh.IsValid());
  assert(mesh.GetFaceCount() == 6);
  assert(mesh.GetPointCount() == 8);
  PASS();
}

void test_xform_schema() {
  TEST("IsXform");
  auto stage = MakeTestStage();
  auto prim = stage.GetPrimAtPath("/Test");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsXform(prim)) { FAIL("expected Xform"); return; }
  PASS();
}

void test_camera_schema() {
  TEST("IsCamera");
  auto stage = MakeTestStage();
  auto prim = stage.GetPrimAtPath("/TestCam");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsCamera(prim)) { FAIL("expected Camera"); return; }
  CameraData data;
  if (!GetCameraData(stage, prim, &data)) { FAIL("GetCameraData"); return; }
  PASS();
}

void test_light_schema() {
  TEST("IsLight");
  auto stage = MakeTestStage();
  auto prim = stage.GetPrimAtPath("/TestLight");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  auto lt = GetLightType(prim);
  if (lt != LightType::SphereLight) { FAIL("expected SphereLight"); return; }
  LightData data;
  if (!GetLightData(stage, prim, &data)) { FAIL("GetLightData"); return; }
  PASS();
}

void test_shade_schema() {
  TEST("IsMaterial");
  auto stage = MakeTestStage();
  auto mat = stage.GetPrimAtPath("/TestMat");
  if (!mat.IsValid()) { FAIL("mat not found"); return; }
  if (!IsMaterial(mat)) { FAIL("expected Material"); return; }

  auto sh = stage.GetPrimAtPath("/TestShader");
  if (!sh.IsValid()) { FAIL("shader not found"); return; }
  if (!IsShader(sh)) { FAIL("expected Shader"); return; }
  auto id = GetShaderId(sh);
  if (id != "UsdPreviewSurface") { FAIL("expected UsdPreviewSurface"); return; }
  PASS();
}

void test_prim_children() {
  TEST("GetChildren");
  auto stage = MakeTestStage();
  auto root_prims = stage.GetRootPrims();
  // Should have at least 6 root prims
  if (root_prims.size() < 6) {
    FAIL("expected >= 6 root prims");
    return;
  }
  PASS();
}

int main() {
  printf("Schema Tests\n");
  printf("============\n\n");

  test_mesh_schema();
  test_xform_schema();
  test_camera_schema();
  test_light_schema();
  test_shade_schema();
  test_prim_children();

  printf("\n%d/%d tests passed\n", pass_count, test_count);
  return pass_count == test_count ? 0 : 1;
}
