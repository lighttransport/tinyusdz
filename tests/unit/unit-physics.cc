// SPDX-License-Identifier: Apache 2.0
// USD Physics and MuJoCo physics annotation unit tests.
// Exercises the full pipeline: ASCII parser -> prim-reconstruct -> Stage,
// plus pprint roundtrip and Tydra JSON export.

#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-physics.h"
#include "tinyusdz.hh"
#include "core/prim.hh"
#include "usdPhysics.hh"
#include "mjcPhysics.hh"
#include "tydra/physics-to-json.hh"

#include <cstring>

using namespace tinyusdz;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
namespace {

static bool parse_usda(const char *usda, Stage *stage, std::string *warn,
                       std::string *err) {
  return LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(usda), std::strlen(usda), "test.usda",
      stage, warn, err);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// 1. PhysicsScene — basic gravity attributes
// ---------------------------------------------------------------------------
void physics_scene_reconstruct_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsScene "SimScene"
{
    double3 physics:gravityDirection = (0, 0, -1)
    double physics:gravityMagnitude = 9.81
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/SimScene", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim != nullptr);
  if (!prim) return;
  TEST_CHECK(prim->is<PhysicsScene>());

  const auto *scene = prim->as<PhysicsScene>();
  TEST_CHECK(scene != nullptr);
  if (!scene) return;

  // Check gravity direction
  auto gd_opt = scene->gravityDirection.get_value();
  TEST_CHECK(gd_opt.has_value());
  if (gd_opt.has_value()) {
    auto gd = gd_opt.value();
    TEST_CHECK(gd[0] == 0.0);
    TEST_CHECK(gd[1] == 0.0);
    TEST_CHECK(gd[2] == -1.0);
  }

  // Check gravity magnitude
  auto gm_opt = scene->gravityMagnitude.get_value();
  TEST_CHECK(gm_opt.has_value());
  if (gm_opt.has_value()) {
    TEST_CHECK(gm_opt.value() == 9.81);
  }

  // No MjcSceneAPI since no mjc: properties
  TEST_CHECK(!scene->mjcScene.has_value());
}

// ---------------------------------------------------------------------------
// 2. PhysicsScene with MjcSceneAPI
// ---------------------------------------------------------------------------
void physics_scene_mjc_scene_api_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsScene "SimScene" (
    prepend apiSchemas = ["MjcSceneAPI"]
)
{
    double3 physics:gravityDirection = (0, 0, -1)
    double physics:gravityMagnitude = 9.81
    uniform double mjc:option:timestep = 0.005
    uniform token mjc:option:integrator = "implicit"
    uniform token mjc:option:solver = "newton"
    uniform int mjc:option:iterations = 200
    uniform bool mjc:flag:gravity = 1
    uniform bool mjc:flag:contact = 1
    uniform bool mjc:flag:override = 0
    uniform bool mjc:compiler:autoLimits = 1
    uniform token mjc:compiler:angle = "radian"
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/SimScene", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim != nullptr);
  if (!prim) return;

  const auto *scene = prim->as<PhysicsScene>();
  TEST_CHECK(scene != nullptr);
  if (!scene) return;

  // MjcSceneAPI should be populated
  TEST_CHECK(scene->mjcScene.has_value());
  if (!scene->mjcScene.has_value()) return;

  const auto &mjc = scene->mjcScene.value();
  TEST_CHECK(mjc.timestep.get_value() == 0.005);
  TEST_CHECK(mjc.integrator.get_value().str() == "implicit");
  TEST_CHECK(mjc.solver.get_value().str() == "newton");
  TEST_CHECK(mjc.iterations.get_value() == 200);
  TEST_CHECK(mjc.flag_gravity.get_value() == true);
  TEST_CHECK(mjc.flag_contact.get_value() == true);
  TEST_CHECK(mjc.flag_override.get_value() == false);
  TEST_CHECK(mjc.compiler_autoLimits.get_value() == true);
  TEST_CHECK(mjc.compiler_angle.get_value().str() == "radian");
}

// ---------------------------------------------------------------------------
// 3. PhysicsRevoluteJoint
// ---------------------------------------------------------------------------
void physics_revolute_joint_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "HingeJoint"
{
    rel physics:body0 = </World/Body0>
    rel physics:body1 = </World/Body1>
    token physics:axis = "Z"
    float physics:lowerLimit = -90
    float physics:upperLimit = 90
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/HingeJoint", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<PhysicsRevoluteJoint>());

  const auto *joint = prim->as<PhysicsRevoluteJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;

  auto axis_opt = joint->axis.get_value();
  TEST_CHECK(axis_opt.has_value());
  if (axis_opt.has_value()) {
    TEST_CHECK(axis_opt.value().str() == "Z");
  }

  // Check body0 relationship
  TEST_CHECK(joint->body0.authored());
  auto paths0 = joint->body0.get_targetPaths();
  TEST_CHECK(paths0.size() == 1);
  if (paths0.size() == 1) {
    TEST_CHECK(paths0[0].prim_part() == "/World/Body0");
  }
}

// ---------------------------------------------------------------------------
// 4. PhysicsPrismaticJoint
// ---------------------------------------------------------------------------
void physics_prismatic_joint_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsPrismaticJoint "SlideJoint"
{
    rel physics:body0 = </World/Base>
    rel physics:body1 = </World/Slider>
    token physics:axis = "Y"
    float physics:lowerLimit = 0
    float physics:upperLimit = 10
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/SlideJoint", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<PhysicsPrismaticJoint>());
  const auto *joint = prim->as<PhysicsPrismaticJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;
  auto axis_opt = joint->axis.get_value();
  TEST_CHECK(axis_opt.has_value());
  if (axis_opt.has_value()) {
    TEST_CHECK(axis_opt.value().str() == "Y");
  }
}

// ---------------------------------------------------------------------------
// 5. PhysicsFixedJoint
// ---------------------------------------------------------------------------
void physics_fixed_joint_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsFixedJoint "WeldJoint"
{
    rel physics:body0 = </World/A>
    rel physics:body1 = </World/B>
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/WeldJoint", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  TEST_CHECK((*result)->is<PhysicsFixedJoint>());
}

// ---------------------------------------------------------------------------
// 6. PhysicsDistanceJoint
// ---------------------------------------------------------------------------
void physics_distance_joint_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsDistanceJoint "DistJoint"
{
    rel physics:body0 = </World/A>
    rel physics:body1 = </World/B>
    float physics:minDistance = 1.0
    float physics:maxDistance = 5.0
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/DistJoint", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  TEST_CHECK((*result)->is<PhysicsDistanceJoint>());
}

// ---------------------------------------------------------------------------
// 7. PhysicsRevoluteJoint with MjcJointAPI
// ---------------------------------------------------------------------------
void physics_joint_mjc_api_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "MjcHinge" (
    prepend apiSchemas = ["MjcJointAPI"]
)
{
    rel physics:body0 = </World/Body0>
    rel physics:body1 = </World/Body1>
    token physics:axis = "X"
    uniform double mjc:stiffness = 100
    uniform double mjc:damping = 10
    uniform double mjc:armature = 0.01
    uniform double mjc:frictionloss = 0.5
    uniform double mjc:ref = 0.5
    uniform int mjc:group = 2
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/MjcHinge", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const auto *joint = (*result)->as<PhysicsRevoluteJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;

  TEST_CHECK(joint->mjcJoint.has_value());
  if (!joint->mjcJoint.has_value()) return;

  const auto &mjc = joint->mjcJoint.value();
  TEST_CHECK(mjc.stiffness.get_value() == 100.0);
  TEST_CHECK(mjc.damping.get_value() == 10.0);
  TEST_CHECK(mjc.armature.get_value() == 0.01);
  TEST_CHECK(mjc.frictionloss.get_value() == 0.5);
  TEST_CHECK(mjc.ref.get_value() == 0.5);
  TEST_CHECK(mjc.group.get_value() == 2);
}

// ---------------------------------------------------------------------------
// 8. MjcActuator
// ---------------------------------------------------------------------------
void mjc_actuator_test(void) {
  const char *usda = R"(#usda 1.0

def MjcActuator "MotorActuator"
{
    rel mjc:target = </World/Joint1>
    uniform int mjc:group = 1
    uniform token mjc:dynType = "integrator"
    uniform token mjc:gainType = "fixed"
    uniform token mjc:biasType = "affine"
    uniform double mjc:crankLength = 0.1
    uniform bool mjc:actEarly = 1
    uniform double mjc:ctrlRange:min = -1
    uniform double mjc:ctrlRange:max = 1
    uniform token mjc:ctrlLimited = "true"
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/MotorActuator", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<MjcActuator>());

  const auto *act = prim->as<MjcActuator>();
  TEST_CHECK(act != nullptr);
  if (!act) return;

  TEST_CHECK(act->group.get_value() == 1);
  TEST_CHECK(act->dynType.get_value().str() == "integrator");
  TEST_CHECK(act->gainType.get_value().str() == "fixed");
  TEST_CHECK(act->biasType.get_value().str() == "affine");
  TEST_CHECK(act->crankLength.get_value() == 0.1);
  TEST_CHECK(act->actEarly.get_value() == true);
  TEST_CHECK(act->ctrlRange_min.get_value() == -1.0);
  TEST_CHECK(act->ctrlRange_max.get_value() == 1.0);
  TEST_CHECK(act->ctrlLimited.get_value().str() == "true");

  // Check target relationship
  TEST_CHECK(act->target.authored());
  auto paths = act->target.get_targetPaths();
  TEST_CHECK(paths.size() == 1);
  if (paths.size() == 1) {
    TEST_CHECK(paths[0].prim_part() == "/World/Joint1");
  }
}

// ---------------------------------------------------------------------------
// 9. MjcTendon
// ---------------------------------------------------------------------------
void mjc_tendon_test(void) {
  const char *usda = R"(#usda 1.0

def MjcTendon "SpatialTendon"
{
    uniform token mjc:type = "spatial"
    uniform int mjc:group = 0
    uniform double mjc:stiffness = 50
    uniform double mjc:damping = 5
    uniform double mjc:width = 0.005
    uniform color4f mjc:rgba = (0.8, 0.2, 0.2, 1)
    uniform token mjc:limited = "true"
    uniform double mjc:range:min = 0.1
    uniform double mjc:range:max = 0.5
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/SpatialTendon", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<MjcTendon>());

  const auto *tendon = prim->as<MjcTendon>();
  TEST_CHECK(tendon != nullptr);
  if (!tendon) return;

  TEST_CHECK(tendon->type.get_value().str() == "spatial");
  TEST_CHECK(tendon->stiffness.get_value() == 50.0);
  TEST_CHECK(tendon->damping.get_value() == 5.0);
  TEST_CHECK(tendon->width.get_value() == 0.005);
  TEST_CHECK(tendon->limited.get_value().str() == "true");
  TEST_CHECK(tendon->range_min.get_value() == 0.1);
  TEST_CHECK(tendon->range_max.get_value() == 0.5);
}

// ---------------------------------------------------------------------------
// 10. MjcKeyframe
// ---------------------------------------------------------------------------
void mjc_keyframe_test(void) {
  const char *usda = R"(#usda 1.0

def MjcKeyframe "InitialState"
{
    double[] mjc:qpos = [0, 0, 0.5, 1, 0, 0, 0]
    double[] mjc:qvel = [0, 0, 0, 0, 0, 0]
    double[] mjc:ctrl = [0.1, 0.2, 0.3]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/InitialState", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<MjcKeyframe>());

  const auto *kf = prim->as<MjcKeyframe>();
  TEST_CHECK(kf != nullptr);
  if (!kf) return;

  // Check qpos
  auto qpos_opt = kf->qpos.get_value();
  TEST_CHECK(qpos_opt.has_value());
  if (qpos_opt.has_value()) {
    const auto &qpos = qpos_opt.value();
    TEST_CHECK(qpos.size() == 7);
    if (qpos.size() >= 3) {
      TEST_CHECK(qpos[2] == 0.5);
    }
  }

  // Check ctrl
  auto ctrl_opt = kf->ctrl.get_value();
  TEST_CHECK(ctrl_opt.has_value());
  if (ctrl_opt.has_value()) {
    const auto &ctrl = ctrl_opt.value();
    TEST_CHECK(ctrl.size() == 3);
    if (ctrl.size() >= 3) {
      TEST_CHECK(ctrl[0] == 0.1);
      TEST_CHECK(ctrl[1] == 0.2);
      TEST_CHECK(ctrl[2] == 0.3);
    }
  }
}

// ---------------------------------------------------------------------------
// 11. PrettyPrint round-trip: parse -> export -> reparse
// ---------------------------------------------------------------------------
void physics_pprint_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsScene "Scene"
{
    double3 physics:gravityDirection = (0, -1, 0)
    double physics:gravityMagnitude = 9.81
}

def PhysicsRevoluteJoint "Joint1"
{
    rel physics:body0 = </Body0>
    rel physics:body1 = </Body1>
    token physics:axis = "X"
}

def MjcActuator "Motor"
{
    rel mjc:target = </Joint1>
    uniform int mjc:group = 0
}

def MjcKeyframe "Key0"
{
    double[] mjc:qpos = [0, 0, 1]
}
)";
  // Parse first time
  Stage stage1;
  std::string warn1, err1;
  bool ok1 = parse_usda(usda, &stage1, &warn1, &err1);
  if (!ok1) { TEST_MSG("first parse failed: %s", err1.c_str()); }
  TEST_CHECK(ok1);
  if (!ok1) return;

  // Export to string
  std::string exported = stage1.ExportToString();
  TEST_CHECK(!exported.empty());
  if (exported.empty()) {
    TEST_MSG("ExportToString returned empty");
    return;
  }

  // Parse again from exported string
  Stage stage2;
  std::string warn2, err2;
  bool ok2 = LoadUSDAFromMemory(
      reinterpret_cast<const uint8_t *>(exported.c_str()),
      exported.size(), "roundtrip.usda", &stage2, &warn2, &err2);
  if (!ok2) { TEST_MSG("roundtrip parse failed: %s\nExported:\n%s", err2.c_str(), exported.c_str()); }
  TEST_CHECK(ok2);
  if (!ok2) return;

  // Verify types are preserved
  {
    auto r = stage2.GetPrimAtPath(Path("/Scene", ""));
    TEST_CHECK(bool(r));
    if (r) TEST_CHECK((*r)->is<PhysicsScene>());
  }
  {
    auto r = stage2.GetPrimAtPath(Path("/Joint1", ""));
    TEST_CHECK(bool(r));
    if (r) TEST_CHECK((*r)->is<PhysicsRevoluteJoint>());
  }
  {
    auto r = stage2.GetPrimAtPath(Path("/Motor", ""));
    TEST_CHECK(bool(r));
    if (r) TEST_CHECK((*r)->is<MjcActuator>());
  }
  {
    auto r = stage2.GetPrimAtPath(Path("/Key0", ""));
    TEST_CHECK(bool(r));
    if (r) TEST_CHECK((*r)->is<MjcKeyframe>());
  }
}

// ---------------------------------------------------------------------------
// 12. Tydra physics-to-JSON export
// ---------------------------------------------------------------------------
void physics_to_json_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsScene "Scene"
{
    double3 physics:gravityDirection = (0, 0, -1)
    double physics:gravityMagnitude = 9.81
    uniform double mjc:option:timestep = 0.002
    uniform token mjc:option:integrator = "euler"
}

def Scope "Joints"
{
    def PhysicsRevoluteJoint "Hinge"
    {
        rel physics:body0 = </Body0>
        rel physics:body1 = </Body1>
        token physics:axis = "Z"
    }
}

def Scope "Actuators"
{
    def MjcActuator "Motor"
    {
        rel mjc:target = </Joints/Hinge>
        uniform int mjc:group = 0
    }
}

def MjcKeyframe "Key0"
{
    double[] mjc:qpos = [0, 0.5]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  std::string json;
  std::string json_err;
  tydra::PhysicsJsonExportOptions opts;
  opts.include_mjc = true;
  bool json_ok = tydra::ConvertPhysicsToJson(stage, &json, &json_err, opts);
  if (!json_ok) { TEST_MSG("JSON export failed: %s", json_err.c_str()); }
  TEST_CHECK(json_ok);
  TEST_CHECK(!json.empty());

  if (!json.empty()) {
    // Basic content checks (substring search)
    TEST_CHECK(json.find("\"physicsScene\"") != std::string::npos);
    TEST_CHECK(json.find("\"gravityMagnitude\"") != std::string::npos);
    TEST_CHECK(json.find("\"joints\"") != std::string::npos);
    TEST_CHECK(json.find("\"actuators\"") != std::string::npos);
    TEST_CHECK(json.find("\"keyframes\"") != std::string::npos);
    TEST_CHECK(json.find("\"timestep\"") != std::string::npos);
    TEST_CHECK(json.find("\"euler\"") != std::string::npos);
  }
}
