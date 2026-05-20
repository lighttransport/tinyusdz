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
#include "usdc-writer.hh"
#include "core/prim.hh"
#include "usdPhysics.hh"
#include "mjcPhysics.hh"
#include "tydra/physics-to-json.hh"
#include "tydra/urdf-to-usd.hh"

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

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

// Author -> USDC bytes -> reload. Exercises sconv-physics.cc + the crate
// writer's generic property-bag pass for physx*:* / state:* attributes.
static bool usdc_roundtrip(const char *usda, Stage *out, std::string *warn,
                           std::string *err) {
  Stage tmp;
  if (!parse_usda(usda, &tmp, warn, err)) return false;
  std::vector<uint8_t> bytes;
  if (!usdc::SaveAsUSDCToMemory(tmp, &bytes, warn, err)) return false;
  return LoadUSDCFromMemory(bytes.data(), bytes.size(), "test.usdc",
                            out, warn, err);
}

// Tolerant float comparison — values authored as `custom float` in USDA
// land in tests as `static_cast<double>(float)`, which is NOT equal to
// the same numeric literal typed in C++ as double (0.01f promoted is
// not 0.01). Anything ≥ 1e-6 wins.
static bool approx_eq(double a, double b) {
  const double tol = 1e-5 * (1.0 + std::abs(b));
  return std::abs(a - b) <= tol;
}

// Pull a numeric value out of a generic property bag (post-parse). Returns
// false if the key is absent or not a float / double attribute. Matches
// the priority-lookup pattern downstream consumers use.
static bool get_prop_num(const std::map<std::string, Property> &props,
                         const std::string &key, double *out) {
  auto it = props.find(key);
  if (it == props.end()) return false;
  if (!it->second.is_attribute()) return false;
  const auto &attr = it->second.get_attribute();
  if (auto v = attr.get_value<float>())  { *out = static_cast<double>(*v); return true; }
  if (auto v = attr.get_value<double>()) { *out = *v; return true; }
  return false;
}

static bool has_api(const Prim *prim, APISchemas::APIName want) {
  if (!prim || !prim->metas().has_apiSchemas()) return false;
  const auto schemas = prim->metas().get_apiSchemas();
  for (const auto &n : schemas.names) {
    if (n.first == want) return true;
  }
  return false;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// 1. PhysicsScene — basic gravity attributes
// ---------------------------------------------------------------------------
void physics_scene_reconstruct_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsScene "SimScene"
{
    vector3f physics:gravityDirection = (0, 0, -1)
    float physics:gravityMagnitude = 9.81
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
    TEST_CHECK(gd[0] == 0.0f);
    TEST_CHECK(gd[1] == 0.0f);
    TEST_CHECK(gd[2] == -1.0f);
  }

  // Check gravity magnitude
  auto gm_opt = scene->gravityMagnitude.get_value();
  TEST_CHECK(gm_opt.has_value());
  if (gm_opt.has_value()) {
    TEST_CHECK(std::fabs(gm_opt.value() - 9.81f) < 1e-5f);
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
    vector3f physics:gravityDirection = (0, 0, -1)
    float physics:gravityMagnitude = 9.81
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
// 2b. PhysicsScene with NewtonSceneAPI / NewtonKaminoSceneAPI
// ---------------------------------------------------------------------------
void physics_scene_newton_api_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsScene "SimScene" (
    prepend apiSchemas = ["NewtonSceneAPI", "NewtonKaminoSceneAPI"]
)
{
    vector3f physics:gravityDirection = (0, 0, -1)
    float physics:gravityMagnitude = 9.81
    uniform int newton:maxSolverIterations = 64
    uniform int newton:timeStepsPerSecond = 500
    bool newton:gravityEnabled = false
    uniform float newton:kamino:constraints:alpha = 0.25
    uniform token newton:kamino:jointCorrection = "none"
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/SimScene", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonSceneAPI));
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonKaminoSceneAPI));

  const auto *scene = prim->as<PhysicsScene>();
  TEST_CHECK(scene != nullptr);
  if (!scene) return;
  TEST_CHECK(scene->newtonScene.has_value());
  TEST_CHECK(scene->newtonKaminoScene.has_value());
  if (scene->newtonScene.has_value()) {
    const auto &n = scene->newtonScene.value();
    TEST_CHECK(n.maxSolverIterations.get_value() == 64);
    TEST_CHECK(n.timeStepsPerSecond.get_value() == 500);
    TEST_CHECK(n.gravityEnabled.get_value() == false);
  }
  if (scene->newtonKaminoScene.has_value()) {
    const auto &n = scene->newtonKaminoScene.value();
    TEST_CHECK(approx_eq(n.constraintsAlpha.get_value(), 0.25));
    TEST_CHECK(n.jointCorrection.get_value().str() == "none");
  }
}

// ---------------------------------------------------------------------------
// 2c. PhysicsScene with NewtonXpbdSceneAPI
// ---------------------------------------------------------------------------
void physics_scene_newton_xpbd_api_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsScene "SimScene" (
    prepend apiSchemas = ["NewtonSceneAPI", "NewtonXpbdSceneAPI"]
)
{
    vector3f physics:gravityDirection = (0, 0, -1)
    float physics:gravityMagnitude = 9.81
    uniform int newton:maxSolverIterations = 32
    uniform int newton:timeStepsPerSecond = 240
    uniform float newton:xpbd:softBodyRelaxation = 0.7
    uniform float newton:xpbd:jointLinearCompliance = 0.001
    uniform float newton:xpbd:jointAngularRelaxation = 0.35
    uniform bool newton:xpbd:restitutionEnabled = true
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("USDC roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/SimScene", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonSceneAPI));
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonXpbdSceneAPI));

  const auto *scene = prim->as<PhysicsScene>();
  TEST_CHECK(scene != nullptr);
  if (!scene) return;
  TEST_CHECK(scene->newtonScene.has_value());
  TEST_CHECK(scene->newtonXpbdScene.has_value());
  if (scene->newtonScene.has_value()) {
    TEST_CHECK(scene->newtonScene.value().maxSolverIterations.get_value() == 32);
    TEST_CHECK(scene->newtonScene.value().timeStepsPerSecond.get_value() == 240);
  }
  if (scene->newtonXpbdScene.has_value()) {
    const auto &xpbd = scene->newtonXpbdScene.value();
    TEST_CHECK(approx_eq(xpbd.softBodyRelaxation.get_value(), 0.7));
    TEST_CHECK(approx_eq(xpbd.jointLinearCompliance.get_value(), 0.001));
    TEST_CHECK(approx_eq(xpbd.jointAngularRelaxation.get_value(), 0.35));
    TEST_CHECK(xpbd.restitutionEnabled.get_value() == true);
  }
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
// 6b. PhysicsSphericalJoint
// ---------------------------------------------------------------------------
void physics_spherical_joint_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsSphericalJoint "BallJoint"
{
    rel physics:body0 = </BodyA>
    rel physics:body1 = </BodyB>
    token physics:axis = "Y"
    float physics:coneAngle0Limit = 45
    float physics:coneAngle1Limit = 30
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/BallJoint", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<PhysicsSphericalJoint>());

  const auto *joint = prim->as<PhysicsSphericalJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;

  auto axis_opt = joint->axis.get_value();
  TEST_CHECK(axis_opt.has_value());
  if (axis_opt.has_value()) {
    TEST_CHECK(axis_opt.value().str() == "Y");
  }

  auto cone0_opt = joint->coneAngle0Limit.get_value();
  TEST_CHECK(cone0_opt.has_value());
  if (cone0_opt.has_value()) {
    TEST_CHECK(cone0_opt.value() == 45.0f);
  }

  auto cone1_opt = joint->coneAngle1Limit.get_value();
  TEST_CHECK(cone1_opt.has_value());
  if (cone1_opt.has_value()) {
    TEST_CHECK(cone1_opt.value() == 30.0f);
  }

  // Check body0 relationship
  TEST_CHECK(joint->body0.authored());
  auto paths0 = joint->body0.get_targetPaths();
  TEST_CHECK(paths0.size() == 1);
  if (paths0.size() == 1) {
    TEST_CHECK(paths0[0].prim_part() == "/BodyA");
  }

  // Check body1 relationship
  TEST_CHECK(joint->body1.authored());
  auto paths1 = joint->body1.get_targetPaths();
  TEST_CHECK(paths1.size() == 1);
  if (paths1.size() == 1) {
    TEST_CHECK(paths1[0].prim_part() == "/BodyB");
  }
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
// 7b. PhysicsRevoluteJoint with all four mirror namespaces
//     (physics:* > physxJoint:* / physxLimit:* > state:* > mjc:*)
// ---------------------------------------------------------------------------
void physics_joint_physx_state_mirror_test(void) {
  // This authors the same physical quantity under all four cross-engine
  // namespaces, then verifies each one survives parse and reaches the
  // expected destination:
  //   - mjc:* keys land on the typed MjcJointAPI struct (joint.mjcJoint).
  //   - physxJoint:* / physxLimit:* / state:* keys are NOT consumed into
  //     a typed struct; they remain on joint.props for the reader to walk.
  // The reader-side priority (canonical > PhysX > Newton > MJC) is the
  // responsibility of each downstream consumer (web/sim/src/usd-physics.js,
  // src/usd_mjcf_reader.cc, etc.); here we only assert preservation.
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "MirrorHinge" (
    prepend apiSchemas = ["MjcJointAPI"]
)
{
    rel physics:body0 = </World/A>
    rel physics:body1 = </World/B>
    token physics:axis = "Y"

    uniform double mjc:stiffness = 2.5
    uniform double mjc:damping = 0.75
    uniform double mjc:armature = 0.01
    uniform double mjc:frictionloss = 0.05

    custom float physxJoint:armature = 0.01
    custom float physxJoint:jointFriction = 0.05
    custom float physxJoint:maxJointVelocity = 12.5
    custom float physxLimit:angular:damping = 0.75
    custom float physxLimit:angular:stiffness = 2.5

    custom float state:angular:physics:position = 15.0
    custom float state:angular:physics:velocity = -2.0
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/MirrorHinge", ""));
  TEST_CHECK(bool(result));
  if (!result) return;
  const auto *joint = (*result)->as<PhysicsRevoluteJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;

  // mjc:* — typed MjcJointAPI struct.
  TEST_CHECK(joint->mjcJoint.has_value());
  if (joint->mjcJoint.has_value()) {
    const auto &m = joint->mjcJoint.value();
    TEST_CHECK(m.stiffness.get_value() == 2.5);
    TEST_CHECK(m.damping.get_value() == 0.75);
    TEST_CHECK(m.armature.get_value() == 0.01);
    TEST_CHECK(m.frictionloss.get_value() == 0.05);
  }

  // physxJoint:* / physxLimit:* / state:* — generic props map.
  TEST_CHECK(joint->props.count("physxJoint:armature") > 0);
  TEST_CHECK(joint->props.count("physxJoint:jointFriction") > 0);
  TEST_CHECK(joint->props.count("physxJoint:maxJointVelocity") > 0);
  TEST_CHECK(joint->props.count("physxLimit:angular:damping") > 0);
  TEST_CHECK(joint->props.count("physxLimit:angular:stiffness") > 0);
  TEST_CHECK(joint->props.count("state:angular:physics:position") > 0);
  TEST_CHECK(joint->props.count("state:angular:physics:velocity") > 0);
}

// ---------------------------------------------------------------------------
// 7c. PhysicsRevoluteJoint with all four namespaces — USDC round-trip.
//     Verifies sconv-physics.cc re-emits MjcJointAPI from the typed struct
//     (the reconstruct path consumes mjc:* into mjcJoint and removes them
//     from props), and the generic stage-converter.cc props-map pass
//     preserves physxJoint:* / physxLimit:* / state:* untouched.
// ---------------------------------------------------------------------------
void physics_joint_physx_state_usdc_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "MirrorHinge" (
    prepend apiSchemas = ["MjcJointAPI"]
)
{
    rel physics:body0 = </World/A>
    rel physics:body1 = </World/B>
    token physics:axis = "Y"
    float physics:lowerLimit = -90.0
    float physics:upperLimit = 90.0

    uniform double mjc:stiffness = 2.5
    uniform double mjc:damping = 0.75
    uniform double mjc:armature = 0.01
    uniform double mjc:frictionloss = 0.05

    custom float physxJoint:armature = 0.01
    custom float physxJoint:jointFriction = 0.05
    custom float physxJoint:maxJointVelocity = 12.5
    custom float physxLimit:angular:damping = 0.75
    custom float physxLimit:angular:stiffness = 2.5

    custom float state:angular:physics:position = 15.0
    custom float state:angular:physics:velocity = -2.0
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("USDC roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/MirrorHinge", ""));
  TEST_CHECK(bool(result));
  if (!result) return;
  const auto *joint = (*result)->as<PhysicsRevoluteJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;

  // Canonical UsdPhysics — typed limits.
  auto lo = joint->lowerLimit.get_value();
  auto hi = joint->upperLimit.get_value();
  TEST_CHECK(lo.has_value() && lo.value() == -90.0f);
  TEST_CHECK(hi.has_value() && hi.value() ==  90.0f);

  // MJC — round-trips through the typed MjcJointAPI struct.
  TEST_CHECK(joint->mjcJoint.has_value());
  if (joint->mjcJoint.has_value()) {
    const auto &m = joint->mjcJoint.value();
    TEST_CHECK(m.stiffness.get_value() == 2.5);
    TEST_CHECK(m.damping.get_value() == 0.75);
    TEST_CHECK(m.armature.get_value() == 0.01);
    TEST_CHECK(m.frictionloss.get_value() == 0.05);
  }

  // PhysX + state — round-trip via the generic props map. Check values
  // not just presence so a botched value-encoding in the crate writer
  // would surface immediately.
  double v = 0.0;
  TEST_CHECK(get_prop_num(joint->props, "physxJoint:armature", &v));
  TEST_CHECK(approx_eq(v, 0.01));
  TEST_CHECK(get_prop_num(joint->props, "physxJoint:jointFriction", &v));
  TEST_CHECK(approx_eq(v, 0.05));
  TEST_CHECK(get_prop_num(joint->props, "physxJoint:maxJointVelocity", &v));
  TEST_CHECK(approx_eq(v, 12.5));
  TEST_CHECK(get_prop_num(joint->props, "physxLimit:angular:damping", &v));
  TEST_CHECK(approx_eq(v, 0.75));
  TEST_CHECK(get_prop_num(joint->props, "physxLimit:angular:stiffness", &v));
  TEST_CHECK(approx_eq(v, 2.5));
  TEST_CHECK(get_prop_num(joint->props, "state:angular:physics:position", &v));
  TEST_CHECK(approx_eq(v, 15.0));
  TEST_CHECK(get_prop_num(joint->props, "state:angular:physics:velocity", &v));
  TEST_CHECK(approx_eq(v, -2.0));
}

// ---------------------------------------------------------------------------
// 7d. PhysicsPrismaticJoint with state:linear:physics:* only.
//     Mirrors the physics-state-init.usda test scene — exercises the
//     prismatic path (only the revolute axis is covered by 7b/7c).
// ---------------------------------------------------------------------------
void physics_prismatic_state_init_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsPrismaticJoint "Slider"
{
    rel physics:body0 = </World/Cart>
    rel physics:body1 = </World/Pole>
    token physics:axis = "Z"

    custom float state:linear:physics:position = 0.05
    custom float state:linear:physics:velocity = -0.5
    custom float physxLimit:linear:damping = 1.2
    custom float physxLimit:linear:stiffness = 30.0
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/Slider", ""));
  TEST_CHECK(bool(result));
  if (!result) return;
  const auto *joint = (*result)->as<PhysicsPrismaticJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;

  // Prismatic state — meters / m-per-sec per the USD PhysX convention
  // (no degrees conversion, unlike revolute).
  double v = 0.0;
  TEST_CHECK(get_prop_num(joint->props, "state:linear:physics:position", &v));
  TEST_CHECK(approx_eq(v, 0.05));
  TEST_CHECK(get_prop_num(joint->props, "state:linear:physics:velocity", &v));
  TEST_CHECK(approx_eq(v, -0.5));
  TEST_CHECK(get_prop_num(joint->props, "physxLimit:linear:damping", &v));
  TEST_CHECK(approx_eq(v, 1.2));
  TEST_CHECK(get_prop_num(joint->props, "physxLimit:linear:stiffness", &v));
  TEST_CHECK(approx_eq(v, 30.0));

  // No MjcJointAPI in this scene — the reconstruct path should NOT have
  // synthesized one from the physx*:* / state:* prefixes.
  TEST_CHECK(!joint->mjcJoint.has_value());
}

// ---------------------------------------------------------------------------
// 7e. Tydra physics-to-JSON includes the physx / state sub-blocks for
//     revolute joints (per the cross-engine mirror section in doc/usd.md).
// ---------------------------------------------------------------------------
void physics_joint_physx_state_to_json_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "Hinge"
{
    rel physics:body0 = </A>
    rel physics:body1 = </B>
    token physics:axis = "Z"

    custom float physxJoint:armature = 0.01
    custom float physxJoint:jointFriction = 0.05
    custom float physxLimit:angular:damping = 0.75
    custom float physxLimit:angular:stiffness = 2.5
    custom float state:angular:physics:position = 15.0
    custom float state:angular:physics:velocity = -2.0
}

def PhysicsPrismaticJoint "Slider"
{
    rel physics:body0 = </C>
    rel physics:body1 = </D>
    token physics:axis = "X"

    custom float physxLimit:linear:damping = 1.2
    custom float state:linear:physics:position = 0.05
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
  if (json.empty()) return;

  // Revolute joint surfaces a `physx` and `state` sub-block.
  TEST_CHECK(json.find("\"physx\"")           != std::string::npos);
  TEST_CHECK(json.find("\"state\"")           != std::string::npos);
  TEST_CHECK(json.find("\"armature\"")        != std::string::npos);
  TEST_CHECK(json.find("\"jointFriction\"")   != std::string::npos);
  TEST_CHECK(json.find("\"damping\"")         != std::string::npos);
  TEST_CHECK(json.find("\"stiffness\"")       != std::string::npos);
  TEST_CHECK(json.find("\"position\"")        != std::string::npos);
  TEST_CHECK(json.find("\"velocity\"")        != std::string::npos);
  // Numeric round-trip — at least one of the values from above lands.
  TEST_CHECK(json.find("0.01")   != std::string::npos);
  TEST_CHECK(json.find("15")     != std::string::npos);
}

// ---------------------------------------------------------------------------
// 7f. MjcJointAPI round-trips through USDC. The reconstruct path consumes
//     mjc:* into MjcJointAPI and *removes* the keys from props, so without
//     the sconv-physics.cc re-emission, USDC -> USDA -> USDC would silently
//     drop joint damping / stiffness / armature / frictionloss.
// ---------------------------------------------------------------------------
void physics_joint_mjc_usdc_roundtrip_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "MjcOnlyHinge" (
    prepend apiSchemas = ["MjcJointAPI"]
)
{
    rel physics:body0 = </A>
    rel physics:body1 = </B>
    token physics:axis = "X"
    uniform double mjc:stiffness = 100.0
    uniform double mjc:damping = 10.0
    uniform double mjc:armature = 0.01
    uniform double mjc:frictionloss = 0.5
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("USDC roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/MjcOnlyHinge", ""));
  TEST_CHECK(bool(result));
  if (!result) return;
  const auto *joint = (*result)->as<PhysicsRevoluteJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;
  TEST_CHECK(joint->mjcJoint.has_value());
  if (!joint->mjcJoint.has_value()) return;
  const auto &m = joint->mjcJoint.value();
  TEST_CHECK(m.stiffness.get_value()    == 100.0);
  TEST_CHECK(m.damping.get_value()      == 10.0);
  TEST_CHECK(m.armature.get_value()     == 0.01);
  TEST_CHECK(m.frictionloss.get_value() == 0.5);
}

// ---------------------------------------------------------------------------
// 7g. NewtonMimicAPI on a physics joint
// ---------------------------------------------------------------------------
void physics_joint_newton_mimic_api_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "Follower" (
    prepend apiSchemas = ["NewtonMimicAPI"]
)
{
    rel physics:body0 = </A>
    rel physics:body1 = </B>
    token physics:axis = "X"
    bool newton:mimicEnabled = true
    rel newton:mimicJoint = </Leader>
    float newton:mimicCoef0 = 0.25
    float newton:mimicCoef1 = -1
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("USDC roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/Follower", ""));
  TEST_CHECK(bool(result));
  if (!result) return;
  const Prim *prim = *result;
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonMimicAPI));

  const auto *joint = prim->as<PhysicsRevoluteJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;
  TEST_CHECK(joint->newtonMimic.has_value());
  if (!joint->newtonMimic.has_value()) return;

  const auto &n = joint->newtonMimic.value();
  TEST_CHECK(n.mimicEnabled.get_value() == true);
  TEST_CHECK(approx_eq(n.mimicCoef0.get_value(), 0.25));
  TEST_CHECK(approx_eq(n.mimicCoef1.get_value(), -1.0));
  TEST_CHECK(n.mimicJoint.authored());
  auto paths = n.mimicJoint.get_targetPaths();
  TEST_CHECK(paths.size() == 1);
  if (paths.size() == 1) {
    TEST_CHECK(paths[0].prim_part() == "/Leader");
  }
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
// 8b. NewtonActuator
// ---------------------------------------------------------------------------
void newton_actuator_test(void) {
  const char *usda = R"(#usda 1.0

def NewtonActuator "FingerDrive" (
    prepend apiSchemas = ["NewtonPDControlAPI", "NewtonMaxEffortClampingAPI"]
)
{
    rel newton:targets = [</World/Joint1>, </World/Joint2>]
    float newton:kp = 120
    float newton:kd = 4
    float newton:constEffort = 0.5
    float newton:maxEffort = 30
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("USDC roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/FingerDrive", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<NewtonActuator>());
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonPDControlAPI));
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonMaxEffortClampingAPI));

  const auto *act = prim->as<NewtonActuator>();
  TEST_CHECK(act != nullptr);
  if (!act) return;
  TEST_CHECK(approx_eq(act->kp.get_value(), 120.0));
  TEST_CHECK(approx_eq(act->kd.get_value(), 4.0));
  TEST_CHECK(approx_eq(act->constEffort.get_value(), 0.5));
  TEST_CHECK(approx_eq(act->maxEffort.get_value(), 30.0));
  TEST_CHECK(act->targets.authored());
  auto paths = act->targets.get_targetPaths();
  TEST_CHECK(paths.size() == 2);
  if (paths.size() == 2) {
    TEST_CHECK(paths[0].prim_part() == "/World/Joint1");
    TEST_CHECK(paths[1].prim_part() == "/World/Joint2");
  }
}

// ---------------------------------------------------------------------------
// 8c. NewtonActuator extended control/clamping APIs
// ---------------------------------------------------------------------------
void newton_actuator_extended_api_test(void) {
  const char *usda = R"(#usda 1.0

def NewtonActuator "FingerDrive" (
    prepend apiSchemas = [
        "NewtonActuatorDelayAPI",
        "NewtonPIDControlAPI",
        "NewtonDCMotorClampingAPI",
        "NewtonPositionBasedClampingAPI"
    ]
)
{
    rel newton:targets = [</World/Joint1>]
    uniform int newton:delaySteps = 3
    float newton:kp = 120
    float newton:kd = 4
    float newton:ki = 0.75
    float newton:integralMax = 2
    float newton:maxMotorEffort = 30
    float newton:saturationEffort = 25
    float newton:velocityLimit = 12
    float[] newton:lookupPositions = [-1, 0, 1]
    float[] newton:lookupEfforts = [8, 10, 8]
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("USDC roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto result = stage.GetPrimAtPath(Path("/FingerDrive", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<NewtonActuator>());
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonActuatorDelayAPI));
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonPIDControlAPI));
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonDCMotorClampingAPI));
  TEST_CHECK(has_api(prim, APISchemas::APIName::NewtonPositionBasedClampingAPI));

  const auto *act = prim->as<NewtonActuator>();
  TEST_CHECK(act != nullptr);
  if (!act) return;
  TEST_CHECK(act->delaySteps.get_value() == 3);
  TEST_CHECK(approx_eq(act->ki.get_value(), 0.75));
  TEST_CHECK(approx_eq(act->integralMax.get_value(), 2.0));
  TEST_CHECK(approx_eq(act->maxMotorEffort.get_value(), 30.0));
  TEST_CHECK(approx_eq(act->saturationEffort.get_value(), 25.0));
  TEST_CHECK(approx_eq(act->velocityLimit.get_value(), 12.0));
  auto positions = act->lookupPositions.get_value();
  auto efforts = act->lookupEfforts.get_value();
  TEST_CHECK(positions.has_value());
  TEST_CHECK(efforts.has_value());
  if (positions.has_value()) {
    TEST_CHECK(positions.value().size() == 3);
    TEST_CHECK(approx_eq(positions.value()[0], -1.0));
    TEST_CHECK(approx_eq(positions.value()[2], 1.0));
  }
  if (efforts.has_value()) {
    TEST_CHECK(efforts.value().size() == 3);
    TEST_CHECK(approx_eq(efforts.value()[1], 10.0));
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
    vector3f physics:gravityDirection = (0, -1, 0)
    float physics:gravityMagnitude = 9.81
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
    vector3f physics:gravityDirection = (0, 0, -1)
    float physics:gravityMagnitude = 9.81
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

// ---------------------------------------------------------------------------
// 14. PhysicsCollisionGroup
// ---------------------------------------------------------------------------
void physics_collision_group_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsCollisionGroup "GroupA"
{
    token physics:mergeGroup = "default"
    bool physics:invertFilteredGroups = 0
    rel physics:filteredGroups = </GroupB>
}

def PhysicsCollisionGroup "GroupB"
{
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/GroupA", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<PhysicsCollisionGroup>());
  const auto *grp = prim->as<PhysicsCollisionGroup>();
  TEST_CHECK(grp != nullptr);
  if (!grp) return;

  // Check mergeGroup
  auto mg = grp->mergeGroup.get_value();
  TEST_CHECK(mg.has_value());
  if (mg.has_value()) {
    TEST_CHECK(mg.value().str() == "default");
  }

  // Check invertFilteredGroups
  TEST_CHECK(grp->invertFilteredGroups.get_value() == false);

  // Check filteredGroups relationship
  TEST_CHECK(grp->filteredGroups.authored());
}

// ---------------------------------------------------------------------------
// 14b. PhysicsCollisionGroup: collection:colliders:includes accessor.
// Exercises GetPhysicsCollidersCollection() — the auto-applied
// CollectionAPI:colliders membership rel.
// ---------------------------------------------------------------------------
void physics_collision_group_colliders_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsCollisionGroup "Rig" (
    prepend apiSchemas = ["CollectionAPI:colliders"]
)
{
    rel physics:filteredGroups = </Rig>
    rel collection:colliders:includes = [</Body1>, </Body2>, </Body3>]
}

def Cube "Body1" {}
def Cube "Body2" {}
def Cube "Body3" {}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/Rig", ""));
  TEST_CHECK(bool(result));
  if (!result) return;
  const Prim *prim = *result;
  TEST_CHECK(prim->is<PhysicsCollisionGroup>());

  std::vector<Path> includes;
  std::vector<Path> excludes;
  bool got = GetPhysicsCollidersCollection(*prim, &includes, &excludes);
  TEST_CHECK(got);
  TEST_CHECK(includes.size() == 3);
  TEST_CHECK(excludes.empty());
  if (includes.size() == 3) {
    TEST_CHECK(includes[0].full_path_name() == "/Body1");
    TEST_CHECK(includes[1].full_path_name() == "/Body2");
    TEST_CHECK(includes[2].full_path_name() == "/Body3");
  }

  // Self-filter rel must round-trip.
  const auto *grp = prim->as<PhysicsCollisionGroup>();
  TEST_CHECK(grp->filteredGroups.authored());
}

// ---------------------------------------------------------------------------
// 14c. PhysicsFilteredPairsAPI typed accessor on a Cube.
// Exercises GetPhysicsFilteredPairsAPI() and
// PhysicsFilteredPairsAPI::get_filtered_pair_paths().
// ---------------------------------------------------------------------------
void physics_filtered_pairs_api_test(void) {
  const char *usda = R"(#usda 1.0

def Cube "BodyA" (
    prepend apiSchemas = ["PhysicsFilteredPairsAPI"]
)
{
    rel physics:filteredPairs = [</BodyB>, </BodyC>]
}

def Cube "BodyB" {}
def Cube "BodyC" {}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/BodyA", ""));
  TEST_CHECK(bool(result));
  if (!result) return;
  const Prim *prim = *result;

  PhysicsFilteredPairsAPI api;
  bool got = GetPhysicsFilteredPairsAPI(*prim, &api);
  TEST_CHECK(got);
  TEST_CHECK(api.filteredPairs.authored());

  std::vector<Path> targets = api.get_filtered_pair_paths();
  TEST_CHECK(targets.size() == 2);
  if (targets.size() == 2) {
    TEST_CHECK(targets[0].full_path_name() == "/BodyB");
    TEST_CHECK(targets[1].full_path_name() == "/BodyC");
  }

  // Negative case: a prim *without* the API schema must yield false.
  auto bres = stage.GetPrimAtPath(Path("/BodyB", ""));
  TEST_CHECK(bool(bres));
  if (bres) {
    PhysicsFilteredPairsAPI api2;
    TEST_CHECK(!GetPhysicsFilteredPairsAPI(**bres, &api2));
  }
}

// ---------------------------------------------------------------------------
// 14d. PhysicsCollisionGroup: invertFilteredGroups + multi-target rel.
// ---------------------------------------------------------------------------
void physics_collision_group_invert_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsCollisionGroup "G1"
{
    bool physics:invertFilteredGroups = 1
    rel physics:filteredGroups = </G2>
}

def PhysicsCollisionGroup "G2"
{
}

def PhysicsCollisionGroup "G3"
{
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto r = stage.GetPrimAtPath(Path("/G1", ""));
  TEST_CHECK(bool(r));
  if (!r) return;
  const auto *g1 = (*r)->as<PhysicsCollisionGroup>();
  TEST_CHECK(g1 != nullptr);
  if (!g1) return;
  TEST_CHECK(g1->invertFilteredGroups.get_value() == true);
  TEST_CHECK(g1->filteredGroups.authored());

  // G2's invertFilteredGroups defaults to false.
  auto r2 = stage.GetPrimAtPath(Path("/G2", ""));
  if (r2) {
    const auto *g2 = (*r2)->as<PhysicsCollisionGroup>();
    if (g2) {
      TEST_CHECK(g2->invertFilteredGroups.get_value() == false);
      TEST_CHECK(!g2->filteredGroups.authored());
    }
  }
}

// ---------------------------------------------------------------------------
// 15. DriveAPI + LimitAPI on joint
// ---------------------------------------------------------------------------
void physics_drive_limit_api_test(void) {
  const char *usda = R"(#usda 1.0

def PhysicsRevoluteJoint "Joint" (
    prepend apiSchemas = ["PhysicsDriveAPI:rotX", "PhysicsLimitAPI:rotX"]
)
{
    rel physics:body0 = </Arm>
    rel physics:body1 = </Forearm>
    token physics:axis = "X"

    token physics:drive:rotX:type = "force"
    float physics:drive:rotX:maxForce = 100
    float physics:drive:rotX:targetPosition = 1.57
    float physics:drive:rotX:stiffness = 50
    float physics:drive:rotX:damping = 10

    float physics:limit:rotX:low = -1.57
    float physics:limit:rotX:high = 1.57
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);

  auto result = stage.GetPrimAtPath(Path("/Joint", ""));
  TEST_CHECK(bool(result));
  if (!result) return;

  const Prim *prim = *result;
  TEST_CHECK(prim->is<PhysicsRevoluteJoint>());
  const auto *joint = prim->as<PhysicsRevoluteJoint>();
  TEST_CHECK(joint != nullptr);
  if (!joint) return;

  // DriveAPI and LimitAPI attributes are stored in props since they use
  // multi-apply namespace syntax (physics:drive:rotX:*)
  // Verify the props map contains them
  TEST_CHECK(joint->props.count("physics:drive:rotX:type") > 0 ||
             joint->props.count("physics:drive:rotX:maxForce") > 0 ||
             joint->props.count("physics:drive:rotX:stiffness") > 0);
  TEST_CHECK(joint->props.count("physics:limit:rotX:low") > 0 ||
             joint->props.count("physics:limit:rotX:high") > 0);
}

// ---------------------------------------------------------------------------
// 15. Mesh-per-geom collider convention.
//
// Exercises the schema convention adopted by lightgeom `export_usdz.py`,
// tinyusdz `urdf-to-usd`, and NVIDIA / Newton's `mujoco-usd-converter`:
//
//   * One UsdGeom.Mesh per source geom (no `visual_*` / `collision_*`
//     duplication).
//   * Applied APIs on collider meshes:
//       PhysicsCollisionAPI + PhysicsMeshCollisionAPI
//       MjcCollisionAPI + MjcImageableAPI    (codeless typed schemas)
//   * `purpose = "guide"` when MuJoCo geom.group ∉ {0,1,2}
//     (collision-only mesh).
//   * `physics:collisionEnabled = false` is the `mujoco-usd-converter`
//     opt-out: every Mesh carries PhysicsCollisionAPI, visual-only ones
//     disable it instead of dropping the API.
//
// This test parses a Mesh authored under all three sub-cases (visual-
// only, dual-purpose, collision-only, disabled) and checks that
// apiSchemas + purpose + relevant attrs are recoverable, then exports
// to physics-JSON and checks the surfaced fields.
// ---------------------------------------------------------------------------
void physics_mesh_collider_convention_test(void) {
  const char *usda = R"(#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Z"
)

def Xform "World"
{
    def Xform "Robot" (
        prepend apiSchemas = ["PhysicsRigidBodyAPI"]
    )
    {
        def Mesh "VisualOnly" (
            prepend apiSchemas = ["MjcImageableAPI"]
        )
        {
            point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            uniform int mjc:group = 0
        }

        def Mesh "VisualAndCollider" (
            prepend apiSchemas = [
                "PhysicsCollisionAPI",
                "PhysicsMeshCollisionAPI",
                "MjcCollisionAPI",
                "MjcImageableAPI",
            ]
        )
        {
            point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
            int[] faceVertexCounts = [4]
            int[] faceVertexIndices = [0, 1, 2, 3]
            uniform token physics:approximation = "convexHull"
            uniform int mjc:group = 2
            uniform int mjc:condim = 3
        }

        def Mesh "ColliderOnly" (
            prepend apiSchemas = [
                "PhysicsCollisionAPI",
                "PhysicsMeshCollisionAPI",
                "MjcCollisionAPI",
                "MjcImageableAPI",
            ]
        )
        {
            point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
            int[] faceVertexCounts = [4]
            int[] faceVertexIndices = [0, 1, 2, 3]
            uniform token purpose = "guide"
            uniform token physics:approximation = "convexHull"
            uniform int mjc:group = 3
            uniform int mjc:condim = 6
            uniform double mjc:margin = 0.001
        }

        def Mesh "DisabledCollider" (
            prepend apiSchemas = [
                "PhysicsCollisionAPI",
                "PhysicsMeshCollisionAPI",
            ]
        )
        {
            point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
            int[] faceVertexCounts = [3]
            int[] faceVertexIndices = [0, 1, 2]
            uniform bool physics:collisionEnabled = 0
            uniform token physics:approximation = "convexHull"
        }
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = parse_usda(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("parse failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto has_api = [](const Prim *prim, APISchemas::APIName want) -> bool {
    if (!prim->metas().has_apiSchemas()) return false;
    const auto schemas = prim->metas().get_apiSchemas();
    for (const auto &n : schemas.names) {
      if (n.first == want) return true;
    }
    return false;
  };

  // ---- 1) Visual-only ----------------------------------------------------
  auto vo_r = stage.GetPrimAtPath(Path("/World/Robot/VisualOnly", ""));
  TEST_CHECK(bool(vo_r));
  if (vo_r) {
    const Prim *p = *vo_r;
    TEST_CHECK(p->is<GeomMesh>());
    TEST_CHECK(!has_api(p, APISchemas::APIName::PhysicsCollisionAPI));
    TEST_CHECK( has_api(p, APISchemas::APIName::MjcImageableAPI));
    if (const auto *m = p->as<GeomMesh>()) {
      TEST_CHECK(m->purpose.get_value() == Purpose::Default);
    }
  }

  // ---- 2) Dual-purpose (visual + collider, default group) ---------------
  auto vc_r = stage.GetPrimAtPath(Path("/World/Robot/VisualAndCollider", ""));
  TEST_CHECK(bool(vc_r));
  if (vc_r) {
    const Prim *p = *vc_r;
    TEST_CHECK(p->is<GeomMesh>());
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsMeshCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::MjcCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::MjcImageableAPI));
    if (const auto *m = p->as<GeomMesh>()) {
      // purpose NOT authored as guide for visible-default geoms.
      TEST_CHECK(m->purpose.get_value() == Purpose::Default);
      // physics:approximation = "convexHull" must round-trip into props.
      auto it = m->props.find("physics:approximation");
      TEST_CHECK(it != m->props.end());
    }
  }

  // ---- 3) Collision-only (purpose=guide, mjc:group=3) -------------------
  auto co_r = stage.GetPrimAtPath(Path("/World/Robot/ColliderOnly", ""));
  TEST_CHECK(bool(co_r));
  if (co_r) {
    const Prim *p = *co_r;
    TEST_CHECK(p->is<GeomMesh>());
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::MjcCollisionAPI));
    if (const auto *m = p->as<GeomMesh>()) {
      TEST_CHECK(m->purpose.get_value() == Purpose::Guide);
      // mjc:group authored as 3 — round-trips via props bag.
      auto it = m->props.find("mjc:group");
      TEST_CHECK(it != m->props.end());
    }
  }

  // ---- 4) Disabled-collider (collisionEnabled=false opt-out) ------------
  auto dc_r = stage.GetPrimAtPath(Path("/World/Robot/DisabledCollider", ""));
  TEST_CHECK(bool(dc_r));
  if (dc_r) {
    const Prim *p = *dc_r;
    TEST_CHECK(p->is<GeomMesh>());
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsCollisionAPI));
    if (const auto *m = p->as<GeomMesh>()) {
      auto it = m->props.find("physics:collisionEnabled");
      TEST_CHECK(it != m->props.end());
    }
  }

  // Note: tydra::ConvertPhysicsToJson scopes to PhysicsScene / Joint /
  // Actuator / Tendon / Keyframe — Mesh-collider surfacing lives in
  // web/binding.cc's extractPhysicsSceneJSON (`AppendPhysicsPrimJson`),
  // which is exercised from the web/js test harness, not here.
}

// ---------------------------------------------------------------------------
// 24. Newton collision/material APIs are known and generic attrs preserve.
// ---------------------------------------------------------------------------
void physics_newton_collision_material_api_test(void) {
  const char *usda = R"(#usda 1.0

def "World"
{
    def Material "Rubber" (
        prepend apiSchemas = ["PhysicsMaterialAPI", "NewtonMaterialAPI"]
    )
    {
        float physics:dynamicFriction = 0.9
        float newton:rollingFriction = 0.002
        float newton:torsionalFriction = 0.01
    }

    def Mesh "Collider" (
        prepend apiSchemas = [
            "PhysicsCollisionAPI",
            "PhysicsMeshCollisionAPI",
            "NewtonCollisionAPI",
            "NewtonMeshCollisionAPI"
        ]
    )
    {
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        uniform token physics:approximation = "convexHull"
        float newton:contactMargin = 0.005
        float newton:contactGap = 0
        uniform int newton:maxHullVertices = 64
    }
}
)";
  Stage stage;
  std::string warn, err;
  bool ok = usdc_roundtrip(usda, &stage, &warn, &err);
  if (!ok) { TEST_MSG("USDC roundtrip failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto mat_r = stage.GetPrimAtPath(Path("/World/Rubber", ""));
  TEST_CHECK(bool(mat_r));
  if (mat_r) {
    const Prim *p = *mat_r;
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsMaterialAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonMaterialAPI));
    if (const auto *m = p->as<Material>()) {
      double v = 0.0;
      TEST_CHECK(get_prop_num(m->props, "newton:rollingFriction", &v));
      TEST_CHECK(approx_eq(v, 0.002));
      TEST_CHECK(get_prop_num(m->props, "newton:torsionalFriction", &v));
      TEST_CHECK(approx_eq(v, 0.01));
    }
  }

  auto mesh_r = stage.GetPrimAtPath(Path("/World/Collider", ""));
  TEST_CHECK(bool(mesh_r));
  if (mesh_r) {
    const Prim *p = *mesh_r;
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsMeshCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonMeshCollisionAPI));
    if (const auto *m = p->as<GeomMesh>()) {
      double v = 0.0;
      TEST_CHECK(get_prop_num(m->props, "newton:contactMargin", &v));
      TEST_CHECK(approx_eq(v, 0.005));
      TEST_CHECK(get_prop_num(m->props, "newton:contactGap", &v));
      TEST_CHECK(approx_eq(v, 0.0));
      auto it = m->props.find("newton:maxHullVertices");
      TEST_CHECK(it != m->props.end());
    }
  }
}

// ---------------------------------------------------------------------------
// 25. URDF JSON conversion authors Newton APIs for scene/colliders/actuators.
// ---------------------------------------------------------------------------
void urdf_json_newton_api_export_test(void) {
  const char *robot_json = R"JSON({
  "name": "NewtonJsonBot",
  "upAxis": "Z",
  "gravity": [0, 0, -1],
  "timestep": 0.01,
  "newton": {
    "maxSolverIterations": 48,
    "gravityEnabled": true,
    "selfCollisionEnabled": false
  },
  "links": [
    {
      "name": "base",
      "inertial": {
        "mass": 1.0,
        "centerOfMass": [0, 0, 0],
        "diagonalInertia": [1, 1, 1]
      },
      "collisions": [
        {
          "name": "base_col",
          "matrix": [1, 0, 0, 0,
                     0, 1, 0, 0,
                     0, 0, 1, 0,
                     0, 0, 0, 1],
          "shape": { "type": "box" },
          "newton": {
            "contactMargin": 0.01,
            "contactGap": 0.002
          }
        }
      ]
    },
    {
      "name": "finger",
      "inertial": {
        "mass": 0.25,
        "centerOfMass": [0, 0, 0],
        "diagonalInertia": [0.1, 0.1, 0.1]
      }
    }
  ],
  "joints": [
    {
      "name": "hinge",
      "type": "revolute",
      "parent": "base",
      "child": "finger",
      "axis": [0, 0, 1],
      "axisToken": "Z",
      "originMatrix": [1, 0, 0, 0,
                       0, 1, 0, 0,
                       0, 0, 1, 0,
                       0, 0, 0, 1],
      "limit": { "lower": -1.0, "upper": 1.0 },
      "dynamics": { "damping": 0.1, "friction": 0.02 },
      "mimic": { "joint": "hinge", "multiplier": 1.0, "offset": 0.0 }
    }
  ],
  "actuators": [
    {
      "name": "hinge_drive",
      "joint": "hinge",
      "control": "pd",
      "kp": 80.0,
      "kd": 3.0,
      "maxEffort": 12.0,
      "delaySteps": 2
    }
  ]
})JSON";

  Stage tmp;
  std::string warn, err;
  bool ok = tinyusdz::tydra::ConvertURDFJsonToUSDStage(
      robot_json, &tmp, &warn, &err);
  if (!ok) { TEST_MSG("ConvertURDFJsonToUSDStage failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  std::vector<uint8_t> bytes;
  ok = usdc::SaveAsUSDCToMemory(tmp, &bytes, &warn, &err);
  if (!ok) { TEST_MSG("SaveAsUSDCToMemory failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  Stage stage;
  ok = LoadUSDCFromMemory(bytes.data(), bytes.size(), "newton-json.usdc",
                          &stage, &warn, &err);
  if (!ok) { TEST_MSG("LoadUSDCFromMemory failed: %s", err.c_str()); }
  TEST_CHECK(ok);
  if (!ok) return;

  auto scene_r = stage.GetPrimAtPath(Path("/World/PhysicsScene", ""));
  TEST_CHECK(bool(scene_r));
  if (scene_r) {
    const Prim *p = *scene_r;
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonSceneAPI));
    const auto *scene = p->as<PhysicsScene>();
    TEST_CHECK(scene != nullptr);
    if (scene && scene->newtonScene.has_value()) {
      TEST_CHECK(scene->newtonScene.value().maxSolverIterations.get_value() == 48);
      TEST_CHECK(scene->newtonScene.value().timeStepsPerSecond.get_value() == 100);
    }
  }

  auto base_r = stage.GetPrimAtPath(Path("/World/Links/base", ""));
  TEST_CHECK(bool(base_r));
  if (base_r) {
    const Prim *p = *base_r;
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsArticulationRootAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonArticulationRootAPI));
    TEST_CHECK(p->as<Xform>() != nullptr);
    if (const auto *x = p->as<Xform>()) {
      TEST_CHECK(x->props.find("newton:selfCollisionEnabled") != x->props.end());
    }
  }

  auto col_r = stage.GetPrimAtPath(Path("/World/Links/base/base_col", ""));
  TEST_CHECK(bool(col_r));
  if (col_r) {
    const Prim *p = *col_r;
    TEST_CHECK(has_api(p, APISchemas::APIName::PhysicsCollisionAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonCollisionAPI));
    if (const auto *cube = p->as<GeomCube>()) {
      double v = 0.0;
      TEST_CHECK(get_prop_num(cube->props, "newton:contactMargin", &v));
      TEST_CHECK(approx_eq(v, 0.01));
      TEST_CHECK(get_prop_num(cube->props, "newton:contactGap", &v));
      TEST_CHECK(approx_eq(v, 0.002));
    }
  }

  auto joint_r = stage.GetPrimAtPath(Path("/World/Joints/hinge", ""));
  TEST_CHECK(bool(joint_r));
  if (joint_r) {
    const Prim *p = *joint_r;
    TEST_CHECK(has_api(p, APISchemas::APIName::MjcJointAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonMimicAPI));
    const auto *joint = p->as<PhysicsRevoluteJoint>();
    TEST_CHECK(joint != nullptr);
    if (joint) {
      TEST_CHECK(joint->newtonMimic.has_value());
      if (joint->newtonMimic.has_value()) {
        auto targets = joint->newtonMimic.value().mimicJoint.get_targetPaths();
        TEST_CHECK(targets.size() == 1);
        if (targets.size() == 1) {
          TEST_CHECK(targets[0].prim_part() == "/World/Joints/hinge");
        }
      }
    }
  }

  auto act_r = stage.GetPrimAtPath(Path("/World/Actuators/hinge_drive", ""));
  TEST_CHECK(bool(act_r));
  if (act_r) {
    const Prim *p = *act_r;
    TEST_CHECK(p->is<NewtonActuator>());
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonPDControlAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonMaxEffortClampingAPI));
    TEST_CHECK(has_api(p, APISchemas::APIName::NewtonActuatorDelayAPI));
    const auto *act = p->as<NewtonActuator>();
    TEST_CHECK(act != nullptr);
    if (act) {
      TEST_CHECK(approx_eq(act->kp.get_value(), 80.0));
      TEST_CHECK(approx_eq(act->kd.get_value(), 3.0));
      TEST_CHECK(approx_eq(act->maxEffort.get_value(), 12.0));
      TEST_CHECK(act->delaySteps.get_value() == 2);
      auto targets = act->targets.get_targetPaths();
      TEST_CHECK(targets.size() == 1);
      if (targets.size() == 1) {
        TEST_CHECK(targets[0].prim_part() == "/World/Joints/hinge");
      }
    }
  }
}

// ---------------------------------------------------------------------------
// upAxis handling for URDF/MJCF export (see doc/usd-physics-upAxis.md):
//  - `physics:axis` is a LOCAL-frame token: it must be passed through unchanged
//    regardless of the stage upAxis (remapping it would double-rotate).
//  - URDF/MJCF source data is Z-up; a Y-up stage must be reconciled with a
//    SINGLE corrective root rotation Rx(-90deg) on /World, never per-property.
// ---------------------------------------------------------------------------
void physics_urdf_upaxis_axis_invariant_test(void) {
  // Same Z-up robot (joint axis token "Z"), exported once per up axis.
  auto convert = [](const char *up_axis, const char *gravity_json,
                    Stage *out) -> bool {
    const std::string robot_json =
        std::string("{\n  \"name\": \"AxisBot\",\n  \"upAxis\": \"") + up_axis +
        "\",\n  \"gravity\": " + gravity_json +
        ",\n"
        "  \"links\": [\n"
        "    { \"name\": \"base\", \"inertial\": { \"mass\": 1.0 } },\n"
        "    { \"name\": \"arm\", \"inertial\": { \"mass\": 0.25 } }\n"
        "  ],\n"
        "  \"joints\": [\n"
        "    { \"name\": \"hinge\", \"type\": \"revolute\",\n"
        "      \"parent\": \"base\", \"child\": \"arm\",\n"
        "      \"axis\": [0, 0, 1], \"axisToken\": \"Z\",\n"
        "      \"originMatrix\": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],\n"
        "      \"limit\": { \"lower\": -1.0, \"upper\": 1.0 } }\n"
        "  ]\n}";
    std::string warn, err;
    bool ok =
        tinyusdz::tydra::ConvertURDFJsonToUSDStage(robot_json, out, &warn, &err);
    if (!ok) { TEST_MSG("convert (%s) failed: %s", up_axis, err.c_str()); }
    return ok;
  };

  // Reads /World/Joints/hinge's physics:axis token. The invariant under test:
  // it is ALWAYS "Z" (never remapped to the stage up axis).
  auto joint_axis = [](Stage *stage, std::string *out_axis) -> bool {
    auto r = stage->GetPrimAtPath(Path("/World/Joints/hinge", ""));
    if (!r) return false;
    const auto *joint = (*r)->as<PhysicsRevoluteJoint>();
    if (!joint) return false;
    auto a = joint->axis.get_value();
    if (!a.has_value()) return false;
    *out_axis = a.value().str();
    return true;
  };

  // --- Z-up: native target, no corrective rotation ---
  {
    Stage stage;
    TEST_CHECK(convert("Z", "[0, 0, -1]", &stage));
    TEST_CHECK(stage.metas().upAxis.get_value() == Axis::Z);

    std::string axis;
    TEST_CHECK(joint_axis(&stage, &axis));
    TEST_CHECK(axis == "Z");  // pass-through

    auto wr = stage.GetPrimAtPath(Path("/World", ""));
    TEST_CHECK(bool(wr));
    if (wr) {
      const auto *world = (*wr)->as<Xform>();
      TEST_CHECK(world != nullptr);
      if (world) {
        // Z-up source -> Z-up stage: no corrective root rotation.
        TEST_CHECK(world->xformOps.empty());
      }
    }

    auto sr = stage.GetPrimAtPath(Path("/World/PhysicsScene", ""));
    TEST_CHECK(bool(sr));
    if (sr) {
      const auto *scene = (*sr)->as<PhysicsScene>();
      TEST_CHECK(scene != nullptr);
      auto g = scene->gravityDirection.get_value();
      TEST_CHECK(g.has_value());
      if (g.has_value()) {
        TEST_CHECK(approx_eq(g.value()[0], 0.0));
        TEST_CHECK(approx_eq(g.value()[1], 0.0));
        TEST_CHECK(approx_eq(g.value()[2], -1.0));
      }
    }
  }

  // --- Y-up: single corrective root rotation Rx(-90), axis token UNCHANGED ---
  {
    Stage stage;
    TEST_CHECK(convert("Y", "[0, -1, 0]", &stage));
    TEST_CHECK(stage.metas().upAxis.get_value() == Axis::Y);

    std::string axis;
    TEST_CHECK(joint_axis(&stage, &axis));
    TEST_CHECK(axis == "Z");  // STILL "Z" — not remapped to Y (no double-rotate)

    auto wr = stage.GetPrimAtPath(Path("/World", ""));
    TEST_CHECK(bool(wr));
    if (wr) {
      const auto *world = (*wr)->as<Xform>();
      TEST_CHECK(world != nullptr);
      if (world) {
        // Exactly one corrective op: xformOp:rotateX = -90 (+Z -> +Y).
        TEST_CHECK(world->xformOps.size() == 1);
        if (world->xformOps.size() == 1) {
          const XformOp &op = world->xformOps[0];
          TEST_CHECK(op.op_type == XformOp::OpType::RotateX);
          auto angle = op.get_value<double>();
          TEST_CHECK(angle.has_value());
          if (angle.has_value()) {
            TEST_CHECK(approx_eq(angle.value(), -90.0));
          }
        }
      }
    }

    auto sr = stage.GetPrimAtPath(Path("/World/PhysicsScene", ""));
    TEST_CHECK(bool(sr));
    if (sr) {
      const auto *scene = (*sr)->as<PhysicsScene>();
      TEST_CHECK(scene != nullptr);
      auto g = scene->gravityDirection.get_value();
      TEST_CHECK(g.has_value());
      if (g.has_value()) {
        TEST_CHECK(approx_eq(g.value()[0], 0.0));
        TEST_CHECK(approx_eq(g.value()[1], -1.0));
        TEST_CHECK(approx_eq(g.value()[2], 0.0));
      }
    }
  }
}
