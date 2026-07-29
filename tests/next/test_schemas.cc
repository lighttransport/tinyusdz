/// Schema tests for the next library.
/// Tests detection functions (IsMesh, IsCamera, etc.) and data getters.

#include "next/tinyusdz-next.hh"
#include "next/schema/geom-mesh.hh"
#include "next/schema/geom-point-instancer.hh"
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

  // PointInstancer
  layer.begin_prim("Inst", "PointInstancer");
  layer.add_relationship("prototypes", Path("/TestMesh"));
  layer.add_property("protoIndices", Value::MakeIntArray({0, 0}));
  layer.add_property("positions", Value::MakeFloat3Array({0, 0, 0, 2, 0, 0}));
  layer.add_property("orientations",
                     Value::MakeFloatCompArray({1, 0, 0, 0, 1, 0, 0, 0},
                                               TypeId::Quatf, 4));
  layer.add_property("scales", Value::MakeFloat3Array({1, 1, 1, 2, 2, 2}));
  layer.add_property("velocities", Value::MakeFloat3Array({0, 0, 1, 0, 0, 2}));
  layer.add_property("angularVelocities",
                     Value::MakeFloat3Array({0, 1, 0, 0, 2, 0}));
  layer.add_property("ids", Value::MakeInt64Array({100, 101}));
  layer.add_property("invisibleIds", Value::MakeInt64Array({101}));
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

void test_point_instancer_schema() {
  TEST("IsPointInstancer");
  auto stage = MakeTestStage();
  auto prim = stage.GetPrimAtPath("/Inst");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsPointInstancer(prim)) { FAIL("expected PointInstancer"); return; }
  UsdGeomPointInstancer pi(prim);
  assert(pi.IsValid());
  assert(pi.GetPrototypes().size() == 1);
  assert(pi.GetPrototypes()[0].str() == "/TestMesh");
  assert(pi.GetInstanceCount() == 2);
  assert(pi.GetProtoIndices() == std::vector<int32_t>({0, 0}));
  assert(pi.GetPositions() == std::vector<float>({0, 0, 0, 2, 0, 0}));
  assert(pi.GetScales() == std::vector<float>({1, 1, 1, 2, 2, 2}));
  assert(pi.GetVelocities() == std::vector<float>({0, 0, 1, 0, 0, 2}));
  assert(pi.GetAngularVelocities() ==
         std::vector<float>({0, 1, 0, 0, 2, 0}));
  assert(pi.GetIds() == std::vector<int64_t>({100, 101}));
  assert(pi.GetInvisibleIds() == std::vector<int64_t>({101}));
  std::string reason;
  assert(pi.HasValidInstanceArrays(0.0, &reason));
  assert(reason.empty());
  std::vector<PointInstancerTransform> transforms = pi.ComputeInstanceTransforms();
  assert(transforms.size() == 2);
  assert(transforms[0].matrix[12] == 0.0);
  assert(transforms[1].matrix[0] == 2.0);
  assert(transforms[1].matrix[12] == 2.0);

  // ComputeMaskAtTime: instance 0 (id 100) visible, instance 1 (id 101) is in
  // invisibleIds. Use explicit FAIL (assert is compiled out under NDEBUG).
  std::vector<bool> mask = pi.ComputeMaskAtTime();
  if (mask.size() != 2) { FAIL("mask size"); return; }
  if (!mask[0] || mask[1]) { FAIL("invisibleId not masked"); return; }

  auto all = GetAllPointInstancers(stage);
  assert(all.size() == 1);

  StageBuilder bad_sb;
  auto& bad_layer = bad_sb.GetLayerBuilder();
  bad_layer.begin_prim("Proto", "Mesh");
  bad_layer.end_prim();
  bad_layer.begin_prim("BadInst", "PointInstancer");
  bad_layer.add_relationship("prototypes", Path("/Proto"));
  bad_layer.add_property("protoIndices", Value::MakeIntArray({0, 0}));
  bad_layer.add_property("positions", Value::MakeFloat3Array({0, 0, 0, 1, 0, 0}));
  bad_layer.add_property("velocities", Value::MakeFloat3Array({0, 0, 1}));
  bad_layer.end_prim();
  bad_layer.finalize();
  Stage bad_stage = bad_sb.Build();
  UsdGeomPointInstancer bad_pi(bad_stage.GetPrimAtPath("/BadInst"));
  assert(!bad_pi.HasValidInstanceArrays(0.0, &reason));
  assert(reason == "velocities size does not match protoIndices");
  PASS();
}

// Regression: PointInstancer getters previously read time samples HELD
// (GetValueAtTime only), so animated positions/scales snapped to the
// earlier key. They must linearly interpolate between samples like the
// tydra render-extract path.
void test_point_instancer_interpolation() {
  TEST("PointInstancerInterpolation");
  StageBuilder sb;
  auto& layer = sb.GetLayerBuilder();
  layer.begin_prim("Proto", "Mesh");
  layer.end_prim();
  layer.begin_prim("Anim", "PointInstancer");
  layer.add_relationship("prototypes", Path("/Proto"));
  layer.add_property("protoIndices", Value::MakeIntArray({0}));
  layer.add_time_sample("positions", 0.0,
                        Value::MakeFloat3Array({0, 0, 0}));
  layer.add_time_sample("positions", 10.0,
                        Value::MakeFloat3Array({4, 0, 0}));
  layer.add_time_sample("scales", 0.0, Value::MakeFloat3Array({1, 1, 1}));
  layer.add_time_sample("scales", 10.0, Value::MakeFloat3Array({3, 3, 3}));
  layer.end_prim();
  layer.finalize();
  Stage stage = sb.Build();
  UsdGeomPointInstancer pi(stage.GetPrimAtPath("/Anim"));
  if (!pi.IsValid()) { FAIL("prim not found"); return; }

  // Exact sample times.
  if (pi.GetPositions(0.0) != std::vector<float>({0, 0, 0})) {
    FAIL("positions at t=0");
    return;
  }
  if (pi.GetPositions(10.0) != std::vector<float>({4, 0, 0})) {
    FAIL("positions at t=10");
    return;
  }
  // Midpoint must interpolate, not snap to the earlier key.
  if (pi.GetPositions(5.0) != std::vector<float>({2, 0, 0})) {
    FAIL("positions at t=5 not interpolated");
    return;
  }
  if (pi.GetScales(5.0) != std::vector<float>({2, 2, 2})) {
    FAIL("scales at t=5 not interpolated");
    return;
  }
  // Interpolated transforms flow through ComputeInstanceTransforms too.
  std::vector<PointInstancerTransform> xf = pi.ComputeInstanceTransforms(5.0);
  if (xf.size() != 1 || xf[0].matrix[12] != 2.0 || xf[0].matrix[0] != 2.0) {
    FAIL("interpolated instance transform");
    return;
  }
  PASS();
}

// Regression: GetPointsAtTimecode / GetTranslationAtTimecode (formerly
// *AtTime) sampled held-only; they must interpolate between samples.
void test_mesh_xform_timecode_interpolation() {
  TEST("MeshXformTimecodeInterpolation");
  StageBuilder sb;
  auto& layer = sb.GetLayerBuilder();
  layer.begin_prim("AnimMesh", "Mesh");
  layer.add_time_sample("points", 0.0, Value::MakeFloat3Array({0, 0, 0}));
  layer.add_time_sample("points", 10.0, Value::MakeFloat3Array({4, 2, 0}));
  layer.end_prim();
  layer.begin_prim("AnimXf", "Xform");
  layer.add_property("xformOpOrder",
                     Value::MakeTokenArray({"xformOp:translate"}));
  layer.add_time_sample("xformOp:translate", 0.0,
                        Value::MakeFloat3(0, 0, 0));
  layer.add_time_sample("xformOp:translate", 10.0,
                        Value::MakeFloat3(4, 0, 8));
  layer.end_prim();
  layer.finalize();
  Stage stage = sb.Build();

  UsdGeomMesh mesh(stage.GetPrimAtPath("/AnimMesh"));
  if (!mesh.IsValid()) { FAIL("mesh prim not found"); return; }
  if (mesh.GetPointsAtTimecode(0.0) != std::vector<float>({0, 0, 0})) {
    FAIL("points at t=0");
    return;
  }
  if (mesh.GetPointsAtTimecode(5.0) != std::vector<float>({2, 1, 0})) {
    FAIL("points at t=5 not interpolated");
    return;
  }
  if (mesh.GetPointsAtTimecode(20.0) != std::vector<float>({4, 2, 0})) {
    FAIL("points past last sample not held");
    return;
  }

  UsdGeomXform xf(stage.GetPrimAtPath("/AnimXf"));
  if (!xf.IsValid()) { FAIL("xform prim not found"); return; }
  float x = -1, y = -1, z = -1;
  if (!xf.GetTranslationAtTimecode(5.0, &x, &y, &z)) {
    FAIL("translation read failed");
    return;
  }
  if (x != 2.0f || y != 0.0f || z != 4.0f) {
    FAIL("translation at t=5 not interpolated");
    return;
  }
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

  StageBuilder distant_builder;
  LayerBuilder& layer = distant_builder.GetLayerBuilder();
  layer.begin_prim("Sun", "DistantLight");
  layer.end_prim();
  layer.begin_prim("AuthoredSun", "DistantLight");
  layer.add_property("inputs:intensity", Value(12.0f));
  layer.end_prim();
  Stage distant_stage = distant_builder.Build();
  const UsdPrim sun = distant_stage.GetPrimAtPath("/Sun");
  DistantLightData sun_data;
  if (!GetDistantLightData(distant_stage, sun, &sun_data) ||
      std::abs(sun_data.intensity - 50000.0f) > 0.01f ||
      std::abs(GetLightIntensity(distant_stage, sun) - 50000.0f) > 0.01f) {
    FAIL("DistantLight schema intensity fallback"); return;
  }
  const UsdPrim authored = distant_stage.GetPrimAtPath("/AuthoredSun");
  if (std::abs(GetLightIntensity(distant_stage, authored) - 12.0f) > 0.01f) {
    FAIL("authored DistantLight intensity override"); return;
  }
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

void test_material_binding_resolution() {
  TEST("material binding fallback/inheritance");
  StageBuilder sb;
  LayerBuilder& layer = sb.GetLayerBuilder();

  layer.begin_prim("Root", "Xform");
  layer.add_relationship("material:binding", Path("/ParentMat"));
  layer.add_relationship("material:binding:back", Path("/ParentBackMat"));
  {
    PropMeta& pm =
        layer.current()->ensure_property_meta("material:binding");
    pm.authored |= PropMeta::kBindMaterialAs;
    pm.bindMaterialAs = "strongerThanDescendants";
  }
  {
    PropMeta& pm =
        layer.current()->ensure_property_meta("material:binding:back");
    pm.authored |= PropMeta::kBindMaterialAs;
    pm.bindMaterialAs = "strongerThanDescendants";
  }
  layer.begin_prim("Mesh", "Mesh");
  layer.add_relationship("material:binding:preview", Path("/MissingMat"));
  layer.add_relationship("material:binding", Path("/LeafMat"));
  layer.add_relationship("material:binding:back", Path("/LeafBackMat"));
  layer.end_prim();
  layer.end_prim();

  layer.begin_prim("LooseMesh", "Mesh");
  layer.add_relationship("material:binding:preview", Path("/MissingMat"));
  layer.add_relationship("material:binding", Path("/LeafMat"));
  layer.add_relationship("material:binding:back", Path("/MissingBackMat"));
  layer.end_prim();
  layer.begin_prim("ParentMat", "Material");
  layer.end_prim();
  layer.begin_prim("LeafMat", "Material");
  layer.end_prim();
  layer.begin_prim("ParentBackMat", "Material");
  layer.end_prim();
  layer.begin_prim("LeafBackMat", "Material");
  layer.end_prim();
  layer.finalize();
  Stage stage = sb.Build();

  const UsdPrim mesh = stage.GetPrimAtPath("/Root/Mesh");
  const UsdPrim direct = GetBoundMaterial(stage, mesh);
  if (!direct.IsValid() || direct.GetPath().str() != "/LeafMat") {
    FAIL("dangling preview binding did not fall through");
    return;
  }
  if (GetInheritedBoundMaterialPath(stage, "/Root/Mesh") != "/ParentMat") {
    FAIL("strong ancestor binding did not override leaf binding");
    return;
  }
  if (GetInheritedBoundMaterialPath(stage, "/LooseMesh") != "/LeafMat") {
    FAIL("valid same-prim fallback binding was not selected");
    return;
  }
  if (GetInheritedBoundMaterialPathForPurpose(stage, "/Root/Mesh", "back") !=
      "/ParentBackMat") {
    FAIL("strong ancestor back-purpose binding did not override leaf binding");
    return;
  }
  if (!GetInheritedBoundMaterialPathForPurpose(stage, "/LooseMesh", "back")
           .empty()) {
    FAIL("missing back-purpose binding did not remain empty");
    return;
  }
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
  test_point_instancer_schema();
  test_point_instancer_interpolation();
  test_mesh_xform_timecode_interpolation();
  test_xform_schema();
  test_camera_schema();
  test_light_schema();
  test_shade_schema();
  test_material_binding_resolution();
  test_prim_children();

  printf("\n%d/%d tests passed\n", pass_count, test_count);
  return pass_count == test_count ? 0 : 1;
}
