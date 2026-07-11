/// Extended schema tests for the next library.
/// Tests all 11 committed schema modules: Skel, AR, Mtlx, Media, Physics.

#include "next/tinyusdz-next.hh"
#include "next/schema/usd-skel.hh"
#include "next/schema/usd-ar.hh"
#include "next/schema/usd-mtlx.hh"
#include "next/schema/usd-media.hh"
#include "next/schema/physics-scene.hh"
#include "next/schema/physics-api.hh"
#include "next/schema/physics-joint.hh"
#include "next/schema/physics-collision.hh"
#include "next/prim/path.hh"
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

// ============================================================
// Test stage builders
// ============================================================

static Stage MakeSkelStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("SkelTest");
  auto& layer = sb.GetLayerBuilder();

  layer.begin_prim("Root", "SkelRoot");
  layer.add_property("skel:skeleton", Value::MakeAssetPath("/Root/Skel"));
  layer.add_property("skel:animationSource", Value::MakeAssetPath("/Root/Anim"));
  layer.end_prim();

  layer.begin_prim("Skel", "Skeleton");
  std::vector<std::string> joints = {"Hip", "Hip/Spine", "Hip/Spine/Neck"};
  layer.add_property("primvars:skel:joints", Value::MakeTokenArray(joints));
  layer.add_property("primvars:skel:bindTransforms", Value::MakeFloatArray({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}));
  layer.add_property("primvars:skel:restTransforms", Value::MakeFloatArray({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}));
  layer.end_prim();

  layer.begin_prim("Anim", "SkelAnimation");
  layer.add_property("translations", Value::MakeFloat3Array({0,0,0, 0,10,0, 0,20,0}));
  layer.add_property("rotations", Value::MakeFloatArray({1,0,0,0, 1,0,0,0, 1,0,0,0}));
  layer.end_prim();

  layer.begin_prim("Blend", "BlendShape");
  layer.add_property("offsets", Value::MakeFloat3Array({0,0,0, 0.5,0,0}));
  layer.add_property("pointIndices", Value::MakeIntArray({0, 1}));
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

static Stage MakeARStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("ARTest");
  auto& layer = sb.GetLayerBuilder();

  layer.begin_prim("Anchor", "ARAnchor");
  layer.add_property("ar:location", Value::MakeFloat3Array({1,2,3}));
  layer.end_prim();

  layer.begin_prim("Image", "ARImage");
  layer.add_property("ar:imageAsset", Value::MakeAssetPath("image.png"));
  layer.add_property("ar:physicalWidth", Value(0.5f));
  layer.add_property("ar:physicalHeight", Value(0.3f));
  layer.end_prim();

  layer.begin_prim("Face", "ARFaceGeometry");
  layer.add_property("ar:blendShapeCoefficients", Value::MakeFloatArray({0, 0.5}));
  layer.end_prim();

  layer.begin_prim("Plane", "ARPlane");
  layer.add_property("ar:semanticType", Value::MakeToken("Horizontal"));
  layer.add_property("ar:extent", Value::MakeFloatArray({1, 2}));
  layer.end_prim();

  layer.begin_prim("Cloud", "ARPointCloud");
  layer.add_property("ar:points", Value::MakeFloat3Array({0,0,0, 1,1,1}));
  layer.end_prim();

  layer.begin_prim("Device", "ARDevice");
  layer.add_property("ar:fieldOfView", Value(60.0f));
  layer.add_property("ar:aspectRatio", Value(1.5f));
  layer.end_prim();

  layer.begin_prim("Scene", "ARScene");
  layer.end_prim();

  // AR API schemas on a regular prim
  layer.begin_prim("WithAnchorAPI", "Xform");
  auto* spec = layer.current();
  spec->meta().apiSchemas().push_back("ARAnchorAPI");
  spec->meta().apiSchemas().push_back("ARImagingAPI");
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

static Stage MakeMtlxStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("MtlxTest");
  auto& layer = sb.GetLayerBuilder();

  layer.begin_prim("Shader", "MaterialXShader");
  layer.add_property("inputs:nodeUri", Value("uri:test"));
  layer.add_property("inputs:nodeDefId", Value("ND_StandardSurface"));
  layer.add_property("outputs:mtlx:output", Value("out"));
  layer.end_prim();

  layer.begin_prim("NodeGraph", "MaterialXNodeGraph");
  layer.add_property("strings:mtlx:docUri", Value("doc:test"));
  layer.end_prim();

  layer.begin_prim("LightNode", "MaterialXLightNode");
  layer.add_property("strings:mtlx:nodeUri", Value("uri:light"));
  layer.add_property("inputs:mtlx:color", Value::MakeColor3f(1,1,1));
  layer.end_prim();

  layer.begin_prim("NodeIO", "MaterialXNodeIO");
  layer.add_property("strings:mtlx:nodeUri", Value("uri:io"));
  layer.add_property("outputs:mtlx:output", Value("out"));
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

static Stage MakeMediaStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("MediaTest");
  auto& layer = sb.GetLayerBuilder();

  layer.begin_prim("Audio", "MediaAudio");
  layer.add_property("media:filePath", Value::MakeAssetPath("sound.wav"));
  layer.add_property("media:gain", Value(0.8f));
  layer.add_property("media:loop", Value(true));
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

static Stage MakePhysicsStage() {
  StageBuilder sb;
  sb.SetDefaultPrim("PhysicsTest");
  auto& layer = sb.GetLayerBuilder();

  // PhysicsScene
  layer.begin_prim("Scene", "PhysicsScene");
  layer.add_property("physics:gravityDirection", Value::MakeFloat3(0, -1, 0));
  layer.add_property("physics:gravityMagnitude", Value(-9.81f));
  layer.end_prim();

  // Applied API schemas on Xform
  layer.begin_prim("RigidBody", "Xform");
  { auto* s = layer.current(); s->meta().apiSchemas().push_back("PhysicsRigidBodyAPI"); }
  layer.add_property("physics:rigidBodyEnabled", Value(true));
  layer.end_prim();

  layer.begin_prim("Collider", "Xform");
  { auto* s = layer.current(); s->meta().apiSchemas().push_back("PhysicsCollisionAPI"); }
  layer.add_property("physics:collisionEnabled", Value(true));
  layer.end_prim();

  layer.begin_prim("Mat", "Xform");
  { auto* s = layer.current(); s->meta().apiSchemas().push_back("PhysicsMaterialAPI"); }
  layer.add_property("physics:staticFriction", Value(0.5f));
  layer.end_prim();

  layer.begin_prim("Mass", "Xform");
  { auto* s = layer.current(); s->meta().apiSchemas().push_back("PhysicsMassAPI"); }
  layer.add_property("physics:mass", Value(10.0f));
  layer.end_prim();

  // Joints
  layer.begin_prim("Joint", "PhysicsJoint");
  layer.add_property("physics:breakForce", Value(100.0f));
  layer.end_prim();

  layer.begin_prim("Prismatic", "PhysicsPrismaticJoint");
  layer.add_property("physics:lowerLimit", Value(-10.0f));
  layer.end_prim();

  layer.begin_prim("Revolute", "PhysicsRevoluteJoint");
  layer.add_property("physics:lowerLimit", Value(-90.0f));
  layer.end_prim();

  layer.begin_prim("Spherical", "PhysicsSphericalJoint");
  layer.add_property("physics:coneAngleLimit", Value(45.0f));
  layer.end_prim();

  layer.begin_prim("Fixed", "PhysicsFixedJoint");
  layer.end_prim();

  layer.begin_prim("Distance", "PhysicsDistanceJoint");
  layer.add_property("physics:minDistance", Value(0.5f));
  layer.end_prim();

  // CollisionGroup
  layer.begin_prim("ColGroup", "PhysicsCollisionGroup");
  layer.add_property("physics:collisionEnabled", Value(true));
  layer.end_prim();

  layer.finalize();
  return sb.Build();
}

// ============================================================
// Skel tests
// ============================================================

void test_skel_types() {
  TEST("IsSkelRoot");
  auto stage = MakeSkelStage();
  auto prim = stage.GetPrimAtPath("/Root");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsSkelRoot(prim)) { FAIL("expected SkelRoot"); return; }
  PASS();
}

void test_skeleton() {
  TEST("IsSkeleton");
  auto stage = MakeSkelStage();
  auto prim = stage.GetPrimAtPath("/Skel");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsSkeleton(prim)) { FAIL("expected Skeleton"); return; }
  SkeletonData data;
  if (!GetSkeletonData(stage, prim, &data)) { FAIL("GetSkeletonData"); return; }
  if (data.bindTransforms.size() != 16) { FAIL("expected 16 bindTransforms"); return; }
  PASS();
}

void test_skel_animation() {
  TEST("IsSkelAnimation");
  auto stage = MakeSkelStage();
  auto prim = stage.GetPrimAtPath("/Anim");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsSkelAnimation(prim)) { FAIL("expected SkelAnimation"); return; }
  SkelAnimationData data;
  if (!GetSkelAnimationData(stage, prim, &data)) { FAIL("GetSkelAnimationData"); return; }
  if (!data.hasTranslations) { FAIL("expected translations"); return; }
  PASS();
}

void test_blend_shape() {
  TEST("IsBlendShape");
  auto stage = MakeSkelStage();
  auto prim = stage.GetPrimAtPath("/Blend");
  if (!prim.IsValid()) { FAIL("prim not found"); return; }
  if (!IsBlendShape(prim)) { FAIL("expected BlendShape"); return; }
  BlendShapeData data;
  if (!GetBlendShapeData(stage, prim, &data)) { FAIL("GetBlendShapeData"); return; }
  if (data.offsets.size() != 6) { FAIL("expected 6 offset values"); return; }
  PASS();
}

// ============================================================
// AR tests
// ============================================================

void test_ar_types() {
  auto stage = MakeARStage();

  {
    TEST("IsARAnchor");
    auto p = stage.GetPrimAtPath("/Anchor");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsARAnchor(p)) { FAIL("expected ARAnchor"); return; }
    ARAnchorData d;
    if (!GetARAnchorData(stage, p, &d)) { FAIL("GetARAnchorData"); return; }
    PASS();
  }
  {
    TEST("IsARImage");
    auto p = stage.GetPrimAtPath("/Image");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsARImage(p)) { FAIL("expected ARImage"); return; }
    ARImageData d;
    if (!GetARImageData(stage, p, &d)) { FAIL("GetARImageData"); return; }
    if (d.physicalWidth < 0.4f) { FAIL("expected physicalWidth"); return; }
    PASS();
  }
  {
    TEST("IsARFaceGeometry");
    auto p = stage.GetPrimAtPath("/Face");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsARFaceGeometry(p)) { FAIL("expected ARFaceGeometry"); return; }
    ARFaceGeometryData d;
    if (!GetARFaceGeometryData(stage, p, &d)) { FAIL("GetARFaceGeometryData"); return; }
    PASS();
  }
  {
    TEST("IsARPlane");
    auto p = stage.GetPrimAtPath("/Plane");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsARPlane(p)) { FAIL("expected ARPlane"); return; }
    ARPlaneData d;
    if (!GetARPlaneData(stage, p, &d)) { FAIL("GetARPlaneData"); return; }
    PASS();
  }
  {
    TEST("IsARPointCloud");
    auto p = stage.GetPrimAtPath("/Cloud");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsARPointCloud(p)) { FAIL("expected ARPointCloud"); return; }
    ARPointCloudData d;
    if (!GetARPointCloudData(stage, p, &d)) { FAIL("GetARPointCloudData"); return; }
    PASS();
  }
  {
    TEST("IsARDevice");
    auto p = stage.GetPrimAtPath("/Device");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsARDevice(p)) { FAIL("expected ARDevice"); return; }
    ARDeviceData d;
    if (!GetARDeviceData(stage, p, &d)) { FAIL("GetARDeviceData"); return; }
    if (d.fieldOfView < 59.0f) { FAIL("expected fieldOfView"); return; }
    PASS();
  }
  {
    TEST("IsARScene");
    auto p = stage.GetPrimAtPath("/Scene");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsARScene(p)) { FAIL("expected ARScene"); return; }
    PASS();
  }
}

void test_ar_api_schemas() {
  auto stage = MakeARStage();

  {
    TEST("HasARAnchorAPI");
    auto p = stage.GetPrimAtPath("/WithAnchorAPI");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!HasARAnchorAPI(p)) { FAIL("expected ARAnchorAPI"); return; }
    ARAnchorAPIData d;
    if (!GetARAnchorAPIData(p, &d)) { FAIL("GetARAnchorAPIData"); return; }
    PASS();
  }
  {
    TEST("HasARImagingAPI");
    auto p = stage.GetPrimAtPath("/WithAnchorAPI");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!HasARImagingAPI(p)) { FAIL("expected ARImagingAPI"); return; }
    ARImagingAPIData d;
    if (!GetARImagingAPIData(p, &d)) { FAIL("GetARImagingAPIData"); return; }
    PASS();
  }
}

// ============================================================
// Mtlx tests
// ============================================================

void test_mtlx_types() {
  auto stage = MakeMtlxStage();

  {
    TEST("IsMaterialXShader");
    auto p = stage.GetPrimAtPath("/Shader");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsMaterialXShader(p)) { FAIL("expected MaterialXShader"); return; }
    MaterialXShaderData d;
    if (!GetMaterialXShaderData(stage, p, &d)) { FAIL("GetMaterialXShaderData"); return; }
    if (d.nodeDefId != "ND_StandardSurface") { FAIL("expected nodeDefId"); return; }
    PASS();
  }
  {
    TEST("IsMaterialXNodeGraph");
    auto p = stage.GetPrimAtPath("/NodeGraph");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsMaterialXNodeGraph(p)) { FAIL("expected MaterialXNodeGraph"); return; }
    PASS();
  }
  {
    TEST("IsMaterialXLightNode");
    auto p = stage.GetPrimAtPath("/LightNode");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsMaterialXLightNode(p)) { FAIL("expected MaterialXLightNode"); return; }
    MaterialXLightNodeData d;
    if (!GetMaterialXLightNodeData(stage, p, &d)) { FAIL("GetMaterialXLightNodeData"); return; }
    PASS();
  }
  {
    TEST("IsMaterialXNodeIO");
    auto p = stage.GetPrimAtPath("/NodeIO");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsMaterialXNodeIO(p)) { FAIL("expected MaterialXNodeIO"); return; }
    MaterialXNodeIOData d;
    if (!GetMaterialXNodeIOData(stage, p, &d)) { FAIL("GetMaterialXNodeIOData"); return; }
    PASS();
  }
}

// ============================================================
// Media tests
// ============================================================

void test_media_types() {
  TEST("IsMediaAudio");
  auto stage = MakeMediaStage();
  auto p = stage.GetPrimAtPath("/Audio");
  if (!p.IsValid()) { FAIL("prim not found"); return; }
  if (!IsMediaAudio(p)) { FAIL("expected MediaAudio"); return; }
  MediaAudioData d;
  if (!GetMediaAudioData(stage, p, &d)) { FAIL("GetMediaAudioData"); return; }
  if (d.gain < 0.7f) { FAIL("expected gain"); return; }
  if (!d.loop) { FAIL("expected loop=true"); return; }
  PASS();
}

// ============================================================
// Physics tests
// ============================================================

void test_physics_scene() {
  TEST("IsPhysicsScene");
  auto stage = MakePhysicsStage();
  auto p = stage.GetPrimAtPath("/Scene");
  if (!p.IsValid()) { FAIL("prim not found"); return; }
  if (!IsPhysicsScene(p)) { FAIL("expected PhysicsScene"); return; }
  PhysicsSceneData d;
  if (!GetPhysicsSceneData(stage, p, &d)) { FAIL("GetPhysicsSceneData"); return; }
  if (d.gravityMagnitude > -9.0f) { FAIL("expected gravityMagnitude"); return; }
  PASS();
}

void test_physics_api_schemas() {
  auto stage = MakePhysicsStage();

  {
    TEST("HasPhysicsRigidBodyAPI");
    auto p = stage.GetPrimAtPath("/RigidBody");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!HasPhysicsRigidBodyAPI(p)) { FAIL("expected RigidBodyAPI"); return; }
    PhysicsRigidBodyData d;
    if (!GetPhysicsRigidBodyData(stage, p, &d)) { FAIL("GetPhysicsRigidBodyData"); return; }
    if (!d.rigidBodyEnabled) { FAIL("expected enabled"); return; }
    PASS();
  }
  {
    TEST("HasPhysicsCollisionAPI");
    auto p = stage.GetPrimAtPath("/Collider");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!HasPhysicsCollisionAPI(p)) { FAIL("expected CollisionAPI"); return; }
    PhysicsCollisionData d;
    if (!GetPhysicsCollisionData(stage, p, &d)) { FAIL("GetPhysicsCollisionData"); return; }
    if (!d.collisionEnabled) { FAIL("expected enabled"); return; }
    PASS();
  }
  {
    TEST("HasPhysicsMaterialAPI");
    auto p = stage.GetPrimAtPath("/Mat");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!HasPhysicsMaterialAPI(p)) { FAIL("expected MaterialAPI"); return; }
    PhysicsMaterialData d;
    if (!GetPhysicsMaterialData(stage, p, &d)) { FAIL("GetPhysicsMaterialData"); return; }
    if (d.staticFriction < 0.4f) { FAIL("expected staticFriction"); return; }
    PASS();
  }
  {
    TEST("HasPhysicsMassAPI");
    auto p = stage.GetPrimAtPath("/Mass");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!HasPhysicsMassAPI(p)) { FAIL("expected MassAPI"); return; }
    PhysicsMassData d;
    if (!GetPhysicsMassData(stage, p, &d)) { FAIL("GetPhysicsMassData"); return; }
    if (d.mass < 9.0f) { FAIL("expected mass"); return; }
    PASS();
  }
}

void test_physics_joints() {
  auto stage = MakePhysicsStage();

  {
    TEST("IsPhysicsJoint");
    auto p = stage.GetPrimAtPath("/Joint");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsPhysicsJoint(p)) { FAIL("expected PhysicsJoint"); return; }
    PhysicsJointData d;
    if (!GetPhysicsJointData(stage, p, &d)) { FAIL("GetPhysicsJointData"); return; }
    PASS();
  }
  {
    TEST("IsPhysicsPrismaticJoint");
    auto p = stage.GetPrimAtPath("/Prismatic");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsPhysicsPrismaticJoint(p)) { FAIL("expected PrismaticJoint"); return; }
    PhysicsPrismaticJointData d;
    if (!GetPhysicsPrismaticJointData(stage, p, &d)) { FAIL("GetPrismaticJointData"); return; }
    if (d.lowerLimit > -9.0f) { FAIL("expected lowerLimit"); return; }
    PASS();
  }
  {
    TEST("IsPhysicsRevoluteJoint");
    auto p = stage.GetPrimAtPath("/Revolute");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsPhysicsRevoluteJoint(p)) { FAIL("expected RevoluteJoint"); return; }
    PhysicsRevoluteJointData d;
    if (!GetPhysicsRevoluteJointData(stage, p, &d)) { FAIL("GetRevoluteJointData"); return; }
    PASS();
  }
  {
    TEST("IsPhysicsSphericalJoint");
    auto p = stage.GetPrimAtPath("/Spherical");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsPhysicsSphericalJoint(p)) { FAIL("expected SphericalJoint"); return; }
    PhysicsSphericalJointData d;
    if (!GetPhysicsSphericalJointData(stage, p, &d)) { FAIL("GetSphericalJointData"); return; }
    PASS();
  }
  {
    TEST("IsPhysicsFixedJoint");
    auto p = stage.GetPrimAtPath("/Fixed");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsPhysicsFixedJoint(p)) { FAIL("expected FixedJoint"); return; }
    PhysicsFixedJointData d;
    if (!GetPhysicsFixedJointData(stage, p, &d)) { FAIL("GetFixedJointData"); return; }
    PASS();
  }
  {
    TEST("IsPhysicsDistanceJoint");
    auto p = stage.GetPrimAtPath("/Distance");
    if (!p.IsValid()) { FAIL("prim not found"); return; }
    if (!IsPhysicsDistanceJoint(p)) { FAIL("expected DistanceJoint"); return; }
    PhysicsDistanceJointData d;
    if (!GetPhysicsDistanceJointData(stage, p, &d)) { FAIL("GetDistanceJointData"); return; }
    if (d.minDistance < 0.4f) { FAIL("expected minDistance"); return; }
    PASS();
  }
}

void test_physics_collision_group() {
  TEST("IsPhysicsCollisionGroup");
  auto stage = MakePhysicsStage();
  auto p = stage.GetPrimAtPath("/ColGroup");
  if (!p.IsValid()) { FAIL("prim not found"); return; }
  if (!IsPhysicsCollisionGroup(p)) { FAIL("expected CollisionGroup"); return; }
  PhysicsCollisionGroupData d;
  if (!GetPhysicsCollisionGroupData(stage, p, &d)) { FAIL("GetCollisionGroupData"); return; }
  if (!d.collisionEnabled) { FAIL("expected enabled"); return; }
  PASS();
}

// ============================================================
// Main
// ============================================================

int main() {
  printf("Extended Schema Tests\n");
  printf("=====================\n\n");

  printf("Skel:\n");
  test_skel_types();
  test_skeleton();
  test_skel_animation();
  test_blend_shape();

  printf("\nAR:\n");
  test_ar_types();
  test_ar_api_schemas();

  printf("\nMaterialX:\n");
  test_mtlx_types();

  printf("\nMedia:\n");
  test_media_types();

  printf("\nPhysics:\n");
  test_physics_scene();
  test_physics_api_schemas();
  test_physics_joints();
  test_physics_collision_group();

  printf("\n%d/%d tests passed\n", pass_count, test_count);
  return pass_count == test_count ? 0 : 1;
}
