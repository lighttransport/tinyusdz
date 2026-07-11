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

}  // namespace

SchemaRegistry::SchemaRegistry() {
  parents_ = {
      {"Xform", "Xformable"}, {"Xformable", "Imageable"},
      {"Scope", "Imageable"},
      {"Boundable", "Xformable"}, {"Gprim", "Boundable"},
      {"Mesh", "Gprim"}, {"Points", "PointBased"},
      {"PointBased", "Boundable"}, {"BasisCurves", "Curves"},
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
    if (def.name != property_name) continue;
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
    if (FindProperty(prim, def.name) != &def) continue;
    if (std::find(result.begin(), result.end(), def.name) == result.end()) {
      result.push_back(def.name);
    }
  }
  return result;
}

const SchemaRegistry& GetSchemaRegistry() {
  static const SchemaRegistry registry;
  return registry;
}

}  // namespace next
}  // namespace tinyusdz
