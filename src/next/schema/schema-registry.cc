// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#include "schema-registry.hh"

#include <algorithm>
#include <limits>

namespace tinyusdz {
namespace next {

namespace {

Value Token(const char* value) { return Value::MakeToken(value); }

bool HasAppliedSchema(const PrimSpec& prim, const std::string& schema) {
  for (const std::string& applied : prim.meta().apiSchemas()) {
    if (applied == schema ||
        (applied.size() > schema.size() &&
         applied.compare(0, schema.size(), schema) == 0 &&
         applied[schema.size()] == ':')) {
      return true;
    }
  }
  return false;
}

bool MatchDefinitionName(const PrimSpec& prim,
                         const SchemaPropertyDefinition& def,
                         const std::string& property_name) {
  const std::string marker = "__INSTANCE__";
  const size_t marker_pos = def.name.find(marker);
  if (marker_pos == std::string::npos) return def.name == property_name;
  for (const std::string& applied : prim.meta().apiSchemas()) {
    const std::string prefix = def.schema_type + ":";
    if (applied.compare(0, prefix.size(), prefix) != 0) continue;
    const std::string instance = applied.substr(prefix.size());
    if (instance.empty()) continue;
    std::string instantiated = def.name;
    instantiated.replace(marker_pos, marker.size(), instance);
    if (instantiated == property_name) return true;
  }
  return false;
}

}  // namespace

SchemaRegistry::SchemaRegistry() {
  parents_ = {
      {"Xform", "Xformable"}, {"Xformable", "Imageable"},
      {"Curves", "PointBased"}, {"BasisCurves", "Curves"},
      {"NurbsCurves", "Curves"}, {"Plane", "Gprim"},
      {"Scope", "Imageable"},
      {"Boundable", "Xformable"}, {"Gprim", "Boundable"},
      {"Mesh", "Gprim"}, {"Points", "PointBased"},
      // pxr: PointBased inherits Gprim (NOT Boundable directly), so curves and
      // points must pick up Gprim's doubleSided/orientation fallbacks.
      {"PointBased", "Gprim"}, {"BasisCurves", "Curves"},
      {"NurbsCurves", "Curves"}, {"Curves", "PointBased"},
      {"Sphere", "Gprim"}, {"Cube", "Gprim"},
      {"Cylinder", "Gprim"}, {"Cone", "Gprim"},
      {"Capsule", "Gprim"}, {"Plane", "Gprim"},
      {"Camera", "Xformable"}, {"PointInstancer", "Boundable"},
      {"GeomSubset", "Imageable"}, {"DistantLight", "Xformable"},
      {"DomeLight", "Xformable"}, {"RectLight", "Xformable"},
      {"DiskLight", "Xformable"}, {"SphereLight", "Xformable"},
      {"CylinderLight", "Xformable"},
  };

  auto add = [&](const char* schema, const char* name, const char* type,
                 Value fallback) {
    SchemaPropertyDefinition d;
    d.schema_type = schema;
    d.name = name;
    d.type_name = type;
    d.fallback = std::move(fallback);
    d.has_fallback = true;
    properties_.push_back(std::move(d));
  };
  auto declare = [&](const char* schema, const char* name, const char* type) {
    SchemaPropertyDefinition d;
    d.schema_type = schema;
    d.name = name;
    d.type_name = type;
    properties_.push_back(std::move(d));
  };

#define AOUSD_SCHEMA_FALLBACK(schema, name, type, fallback) \
  add(schema, name, type, fallback);
#define AOUSD_SCHEMA_DECLARE(schema, name, type) declare(schema, name, type);
#include "generated/aousd-core-schema-definitions.inc"
#undef AOUSD_SCHEMA_DECLARE
#undef AOUSD_SCHEMA_FALLBACK
  add("Imageable", "visibility", "token", Token("inherited"));
  add("Imageable", "purpose", "token", Token("default"));
  add("Xformable", "xformOpOrder", "token[]",
      Value::MakeTokenArray(std::vector<std::string>()));
  add("Gprim", "doubleSided", "bool", Value(false));
  add("Gprim", "orientation", "token", Token("rightHanded"));
  add("Mesh", "subdivisionScheme", "token", Token("catmullClark"));
  add("Mesh", "interpolateBoundary", "token", Token("edgeAndCorner"));
  add("Mesh", "faceVaryingLinearInterpolation", "token",
      Token("cornersPlus1"));
  add("Mesh", "triangleSubdivisionRule", "token", Token("catmullClark"));
  add("Sphere", "radius", "double", Value(1.0));
  add("Cube", "size", "double", Value(2.0));
  add("Cylinder", "radius", "double", Value(1.0));
  add("Cylinder", "height", "double", Value(2.0));
  add("Cylinder", "axis", "token", Token("Z"));
  add("Cone", "radius", "double", Value(1.0));
  add("Cone", "height", "double", Value(2.0));
  add("Cone", "axis", "token", Token("Z"));
  add("Capsule", "radius", "double", Value(0.5));
  add("Capsule", "height", "double", Value(1.0));
  add("Capsule", "axis", "token", Token("Z"));
  add("Camera", "projection", "token", Token("perspective"));
  add("Camera", "horizontalAperture", "float", Value(20.955f));
  add("Camera", "verticalAperture", "float", Value(15.2908f));
  add("Camera", "focalLength", "float", Value(50.0f));
  add("Camera", "focusDistance", "float", Value(5.0f));
  add("Camera", "fStop", "float", Value(0.0f));
  add("PhysicsScene", "physics:gravityDirection", "vector3f",
      Value::MakeVector3f(0.0f, 0.0f, 0.0f));
  add("PhysicsScene", "physics:gravityMagnitude", "float",
      Value(-std::numeric_limits<float>::infinity()));

  add("PhysicsRigidBodyAPI", "physics:rigidBodyEnabled", "bool", Value(true));
  add("PhysicsRigidBodyAPI", "physics:kinematicEnabled", "bool", Value(false));
  add("PhysicsRigidBodyAPI", "physics:startsAsleep", "bool", Value(false));
  add("PhysicsRigidBodyAPI", "physics:velocity", "vector3f",
      Value::MakeVector3f(0.0f, 0.0f, 0.0f));
  add("PhysicsRigidBodyAPI", "physics:angularVelocity", "vector3f",
      Value::MakeVector3f(0.0f, 0.0f, 0.0f));
  add("PhysicsCollisionAPI", "physics:collisionEnabled", "bool", Value(true));
  add("PhysicsMassAPI", "physics:mass", "float", Value(0.0f));
  add("PhysicsMassAPI", "physics:density", "float", Value(0.0f));

  // UsdGeom Imageable/Camera/curves/subset fallbacks (values verified
  // against pxr's generated usdGeom schema.usda).
  add("Imageable", "visibility", "token", Token("inherited"));
  add("Imageable", "purpose", "token", Token("default"));
  add("Camera", "clippingRange", "float2", Value::MakeFloat2(1.0f, 1000000.0f));
  add("Camera", "horizontalApertureOffset", "float", Value(0.0f));
  add("Camera", "verticalApertureOffset", "float", Value(0.0f));
  add("Camera", "shutter:open", "double", Value(0.0));
  add("Camera", "shutter:close", "double", Value(0.0));
  add("Camera", "stereoRole", "token", Token("mono"));
  add("BasisCurves", "type", "token", Token("cubic"));
  add("BasisCurves", "basis", "token", Token("bezier"));
  add("BasisCurves", "wrap", "token", Token("nonperiodic"));
  add("Plane", "width", "double", Value(2.0));
  add("Plane", "length", "double", Value(2.0));
  add("Plane", "axis", "token", Token("Z"));
  add("GeomSubset", "elementType", "token", Token("face"));
  add("GeomSubset", "familyName", "token", Token(""));

  // UsdLux common light inputs (each concrete light carries them; values
  // from pxr's usdLux schema.usda). DistantLight overrides intensity.
  {
    const char* kLights[] = {"DistantLight", "DomeLight",  "RectLight",
                             "DiskLight",    "SphereLight", "CylinderLight"};
    for (const char* lt : kLights) {
      add(lt, "inputs:intensity", "float",
          Value(std::string(lt) == "DistantLight" ? 50000.0f : 1.0f));
      add(lt, "inputs:exposure", "float", Value(0.0f));
      add(lt, "inputs:diffuse", "float", Value(1.0f));
      add(lt, "inputs:specular", "float", Value(1.0f));
      add(lt, "inputs:normalize", "bool", Value(false));
      add(lt, "inputs:color", "color3f", Value::MakeColor3f(1.0f, 1.0f, 1.0f));
      add(lt, "inputs:enableColorTemperature", "bool", Value(false));
      add(lt, "inputs:colorTemperature", "float", Value(6500.0f));
    }
  }
  add("DistantLight", "inputs:angle", "float", Value(0.53f));
  add("DomeLight", "inputs:texture:format", "token", Token("automatic"));
  add("SphereLight", "inputs:radius", "float", Value(0.5f));
  add("SphereLight", "treatAsPoint", "bool", Value(false));
  add("RectLight", "inputs:width", "float", Value(1.0f));
  add("RectLight", "inputs:height", "float", Value(1.0f));
  add("DiskLight", "inputs:radius", "float", Value(0.5f));
  add("CylinderLight", "inputs:length", "float", Value(1.0f));
  add("CylinderLight", "inputs:radius", "float", Value(0.5f));
  add("CylinderLight", "treatAsLine", "bool", Value(false));

  // UsdLux applied APIs.
  add("ShapingAPI", "inputs:shaping:cone:angle", "float", Value(90.0f));
  add("ShapingAPI", "inputs:shaping:cone:softness", "float", Value(0.0f));
  add("ShapingAPI", "inputs:shaping:focus", "float", Value(0.0f));
  add("ShadowAPI", "inputs:shadow:enable", "bool", Value(true));
  add("ShadowAPI", "inputs:shadow:color", "color3f",
      Value::MakeColor3f(0.0f, 0.0f, 0.0f));
  add("ShadowAPI", "inputs:shadow:distance", "float", Value(-1.0f));
  add("ShadowAPI", "inputs:shadow:falloff", "float", Value(-1.0f));
  add("ShadowAPI", "inputs:shadow:falloffGamma", "float", Value(1.0f));

  // Data-driven declarations for the remaining schemas implemented by next.
  // Declarations populate HasProperty()/property-name queries without
  // inventing a fallback where the schema intentionally has none.
  const struct Decl { const char* schema; const char* name; const char* type; }
      declarations[] = {
          {"Boundable", "extent", "float3[]"},
          {"PointBased", "points", "point3f[]"},
          {"PointBased", "velocities", "vector3f[]"},
          {"PointBased", "accelerations", "vector3f[]"},
          {"PointBased", "normals", "normal3f[]"},
          {"PointBased", "widths", "float[]"},
          {"Mesh", "faceVertexCounts", "int[]"},
          {"Mesh", "faceVertexIndices", "int[]"},
          {"PointInstancer", "protoIndices", "int[]"},
          {"PointInstancer", "positions", "point3f[]"},
          {"PointInstancer", "orientations", "quath[]"},
          {"PointInstancer", "scales", "float3[]"},
          {"PointInstancer", "ids", "int64[]"},
          {"PointInstancer", "invisibleIds", "int64[]"},
          {"PointInstancer", "prototypes", "relationship"},
          {"Shader", "info:id", "token"},
          {"NodeGraph", "outputs", "namespace"},
          {"Material", "outputs:surface", "token"},
          {"Material", "outputs:displacement", "token"},
          {"Material", "outputs:volume", "token"},
          {"Skeleton", "joints", "token[]"},
          {"Skeleton", "bindTransforms", "matrix4d[]"},
          {"Skeleton", "restTransforms", "matrix4d[]"},
          {"SkelAnimation", "joints", "token[]"},
          {"SkelAnimation", "translations", "float3[]"},
          {"SkelAnimation", "rotations", "quath[]"},
          {"SkelAnimation", "scales", "half3[]"},
          {"BlendShape", "offsets", "vector3f[]"},
          {"BlendShape", "normalOffsets", "vector3f[]"},
          {"BlendShape", "pointIndices", "int[]"},
          {"SkelBindingAPI", "skel:joints", "token[]"},
          {"SkelBindingAPI", "skel:jointIndices", "int[]"},
          {"SkelBindingAPI", "skel:jointWeights", "float[]"},
          {"SkelBindingAPI", "skel:skeleton", "relationship"},
          {"SkelBindingAPI", "skel:animationSource", "relationship"},
          {"PhysicsDriveAPI", "physics:drive:type", "token"},
          {"PhysicsDriveAPI", "physics:drive:maxForce", "float"},
          {"PhysicsDriveAPI", "physics:drive:targetPosition", "float"},
          {"PhysicsDriveAPI", "physics:drive:targetVelocity", "float"},
          {"PhysicsLimitAPI", "physics:limit:low", "float"},
          {"PhysicsLimitAPI", "physics:limit:high", "float"},
      };
  for (const Decl& declaration : declarations) {
    declare(declaration.schema, declaration.name, declaration.type);
  }

  add("Shader", "info:implementationSource", "token", Token("id"));
  add("PhysicsMaterialAPI", "physics:staticFriction", "float", Value(0.0f));
  add("PhysicsMaterialAPI", "physics:dynamicFriction", "float", Value(0.0f));
  add("PhysicsMaterialAPI", "physics:restitution", "float", Value(0.0f));
}

const SchemaPropertyDefinition* SchemaRegistry::FindProperty(
    const PrimSpec& prim, const std::string& property_name) const {
  std::vector<std::string> schemas;
  std::string current = prim.type_name();
  while (!current.empty()) {
    schemas.push_back(current);
    auto it = std::find_if(parents_.begin(), parents_.end(),
                           [&](const auto& p) { return p.first == current; });
    if (it == parents_.end()) break;
    current = it->second;
  }
  for (const SchemaPropertyDefinition& def : properties_) {
    if (!MatchDefinitionName(prim, def, property_name)) continue;
    if (std::find(schemas.begin(), schemas.end(), def.schema_type) !=
        schemas.end()) {
      return &def;
    }
    if (HasAppliedSchema(prim, def.schema_type)) return &def;
  }
  return nullptr;
}

std::vector<std::string> SchemaRegistry::PropertyNames(
    const PrimSpec& prim) const {
  std::vector<std::string> result;
  for (const SchemaPropertyDefinition& def : properties_) {
    std::string name = def.name;
    const std::string marker = "__INSTANCE__";
    const size_t marker_pos = name.find(marker);
    if (marker_pos != std::string::npos) {
      bool emitted = false;
      for (const std::string& applied : prim.meta().apiSchemas()) {
        const std::string prefix = def.schema_type + ":";
        if (applied.compare(0, prefix.size(), prefix) != 0) continue;
        std::string instantiated = name;
        instantiated.replace(marker_pos, marker.size(),
                             applied.substr(prefix.size()));
        if (FindProperty(prim, instantiated) == &def &&
            std::find(result.begin(), result.end(), instantiated) ==
                result.end()) {
          result.push_back(std::move(instantiated));
        }
        emitted = true;
      }
      if (emitted) continue;
    }
    if (FindProperty(prim, name) != &def) continue;
    if (std::find(result.begin(), result.end(), name) == result.end()) {
      result.push_back(std::move(name));
    }
  }
  return result;
}

bool SchemaRegistry::IsKnownSchema(const std::string& schema_type) const {
  if (std::find_if(properties_.begin(), properties_.end(), [&](const auto& p) {
        return p.schema_type == schema_type;
      }) != properties_.end()) return true;
  return std::find_if(parents_.begin(), parents_.end(), [&](const auto& p) {
           return p.first == schema_type || p.second == schema_type;
         }) != parents_.end();
}

std::vector<std::string> SchemaRegistry::SchemaTypes() const {
  std::vector<std::string> result;
  auto add_unique = [&](const std::string& schema) {
    if (std::find(result.begin(), result.end(), schema) == result.end())
      result.push_back(schema);
  };
  for (const auto& parent : parents_) {
    add_unique(parent.first);
    add_unique(parent.second);
  }
  for (const auto& property : properties_) add_unique(property.schema_type);
  std::sort(result.begin(), result.end());
  return result;
}

const SchemaRegistry& GetSchemaRegistry() {
  static const SchemaRegistry registry;
  return registry;
}

}  // namespace next
}  // namespace tinyusdz
