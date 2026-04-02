// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.

///
/// @file usdPhysics.hh
/// @brief USD Physics schema definitions
///
/// Implements physics simulation primitives following USD's UsdPhysics schema.
/// Includes support for rigid bodies, collisions, joints, materials, and scenes.
///
/// Key classes:
/// - PhysicsScene: Global simulation settings (gravity, etc.)
/// - PhysicsRigidBodyAPI: Rigid body properties (applied API)
/// - PhysicsCollisionAPI: Collision properties (applied API)
/// - PhysicsMaterialAPI: Physical material properties (applied API)
/// - PhysicsMeshCollisionAPI: Mesh collision approximation (applied API)
/// - PhysicsRevoluteJoint, PhysicsPrismaticJoint, etc.: Joint types
///
#pragma once

#include "value-types.hh"
#include "nonstd/optional.hpp"
#include "nonstd/expected.hpp"
#include "core/prim-enums.hh"
#include "core/path.hh"
#include "core/extent.hh"
#include "core/composition-types.hh"
#include "core/prim-metas.hh"
#include "core/animatable.hh"
#include "core/typed-attribute.hh"
#include "core/relationship.hh"
#include "core/attribute.hh"
#include "core/property.hh"
#include "core/variant-types.hh"

// MuJoCo API schemas are used by value in optional<> fields, so full definition needed
#include "mjcPhysics.hh"

namespace tinyusdz {

// Prim type name constants
constexpr auto kPhysicsScene = "PhysicsScene";
constexpr auto kPhysicsRevoluteJoint = "PhysicsRevoluteJoint";
constexpr auto kPhysicsPrismaticJoint = "PhysicsPrismaticJoint";
constexpr auto kPhysicsSphericalJoint = "PhysicsSphericalJoint";
constexpr auto kPhysicsFixedJoint = "PhysicsFixedJoint";
constexpr auto kPhysicsDistanceJoint = "PhysicsDistanceJoint";

//
// API Schema structs (applied to host prims, not standalone)
//

struct PhysicsRigidBodyAPI {
  TypedAttributeWithFallback<bool> rigidBodyEnabled{true};  // physics:rigidBodyEnabled
  TypedAttribute<float> mass;                                // physics:mass
  TypedAttribute<float> density;                             // physics:density
  TypedAttribute<value::point3f> centerOfMass;               // physics:centerOfMass
  TypedAttribute<value::float3> diagonalInertia;             // physics:diagonalInertia
  TypedAttribute<value::quatf> principalAxes;                // physics:principalAxes
  TypedAttribute<Animatable<value::vector3f>> velocity;      // physics:velocity
  TypedAttribute<Animatable<value::vector3f>> angularVelocity; // physics:angularVelocity
  TypedAttributeWithFallback<bool> startsAsleep{false};      // physics:startsAsleep
};

struct PhysicsCollisionAPI {
  TypedAttributeWithFallback<bool> collisionEnabled{true};   // physics:collisionEnabled
  RelationshipProperty simulationOwner;                       // physics:simulationOwner
};

struct PhysicsMaterialAPI {
  TypedAttribute<float> staticFriction;    // physics:staticFriction
  TypedAttribute<float> dynamicFriction;   // physics:dynamicFriction
  TypedAttribute<float> restitution;       // physics:restitution
  TypedAttribute<float> density;           // physics:density
};

struct PhysicsMeshCollisionAPI {
  // "convexDecomposition", "convexHull", "meshSimplification",
  // "boundingCube", "boundingSphere", "none"
  TypedAttributeWithFallback<value::token> approximation{value::token("none")}; // physics:approximation
};

//
// Concrete Prim types
//

struct PhysicsScene {
  std::string name;
  Specifier spec{Specifier::Def};
  int64_t parent_id{-1};

  void set_name(const std::string &name_) { name = name_; }
  const std::string &get_name() const { return name; }
  Specifier &specifier() { return spec; }
  const Specifier &specifier() const { return spec; }

  // Standard UsdPhysics attributes (varying — no uniform required)
  TypedAttribute<value::vector3f> gravityDirection;  // physics:gravityDirection (vector3f per USD spec)
  TypedAttribute<float> gravityMagnitude;            // physics:gravityMagnitude (float per USD spec)

  // MuJoCo scene API (optional)
  nonstd::optional<MjcSceneAPI> mjcScene;

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;
  std::map<std::string, VariantSet> variantSet;
  std::map<std::string, Property> props;

  PrimMeta meta;
  PrimMeta &metas() { return meta; }
  const PrimMeta &metas() const { return meta; }

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// Base for all physics joint types
struct PhysicsJointBase {
  RelationshipProperty body0;      // physics:body0
  RelationshipProperty body1;      // physics:body1
  TypedAttribute<value::point3f> localPos0;   // physics:localPos0
  TypedAttribute<value::point3f> localPos1;   // physics:localPos1
  TypedAttribute<value::quatf> localRot0;     // physics:localRot0
  TypedAttribute<value::quatf> localRot1;     // physics:localRot1
  TypedAttribute<bool> jointEnabled;         // physics:jointEnabled
  TypedAttribute<bool> collisionEnabled;     // physics:collisionEnabled
  TypedAttribute<float> breakForce;          // physics:breakForce
  TypedAttribute<float> breakTorque;         // physics:breakTorque
  TypedAttribute<bool> excludeFromArticulation;  // physics:excludeFromArticulation

  // MuJoCo joint API (optional)
  nonstd::optional<MjcJointAPI> mjcJoint;
};

struct PhysicsRevoluteJoint : PhysicsJointBase {
  std::string name;
  Specifier spec{Specifier::Def};
  int64_t parent_id{-1};

  void set_name(const std::string &name_) { name = name_; }
  const std::string &get_name() const { return name; }
  Specifier &specifier() { return spec; }
  const Specifier &specifier() const { return spec; }

  TypedAttribute<value::token> axis;   // physics:axis
  TypedAttribute<float> lowerLimit;    // physics:lowerLimit
  TypedAttribute<float> upperLimit;    // physics:upperLimit

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;
  std::map<std::string, VariantSet> variantSet;
  std::map<std::string, Property> props;

  PrimMeta meta;
  PrimMeta &metas() { return meta; }
  const PrimMeta &metas() const { return meta; }

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

struct PhysicsPrismaticJoint : PhysicsJointBase {
  std::string name;
  Specifier spec{Specifier::Def};
  int64_t parent_id{-1};

  void set_name(const std::string &name_) { name = name_; }
  const std::string &get_name() const { return name; }
  Specifier &specifier() { return spec; }
  const Specifier &specifier() const { return spec; }

  TypedAttribute<value::token> axis;   // physics:axis
  TypedAttribute<float> lowerLimit;
  TypedAttribute<float> upperLimit;

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;
  std::map<std::string, VariantSet> variantSet;
  std::map<std::string, Property> props;

  PrimMeta meta;
  PrimMeta &metas() { return meta; }
  const PrimMeta &metas() const { return meta; }

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

struct PhysicsSphericalJoint : PhysicsJointBase {
  std::string name;
  Specifier spec{Specifier::Def};
  int64_t parent_id{-1};

  void set_name(const std::string &name_) { name = name_; }
  const std::string &get_name() const { return name; }
  Specifier &specifier() { return spec; }
  const Specifier &specifier() const { return spec; }

  TypedAttribute<value::token> axis;            // physics:axis
  TypedAttribute<float> coneAngle0Limit;       // physics:coneAngle0Limit
  TypedAttribute<float> coneAngle1Limit;       // physics:coneAngle1Limit

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;
  std::map<std::string, VariantSet> variantSet;
  std::map<std::string, Property> props;

  PrimMeta meta;
  PrimMeta &metas() { return meta; }
  const PrimMeta &metas() const { return meta; }

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

struct PhysicsFixedJoint : PhysicsJointBase {
  std::string name;
  Specifier spec{Specifier::Def};
  int64_t parent_id{-1};

  void set_name(const std::string &name_) { name = name_; }
  const std::string &get_name() const { return name; }
  Specifier &specifier() { return spec; }
  const Specifier &specifier() const { return spec; }

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;
  std::map<std::string, VariantSet> variantSet;
  std::map<std::string, Property> props;

  PrimMeta meta;
  PrimMeta &metas() { return meta; }
  const PrimMeta &metas() const { return meta; }

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

struct PhysicsDistanceJoint : PhysicsJointBase {
  std::string name;
  Specifier spec{Specifier::Def};
  int64_t parent_id{-1};

  void set_name(const std::string &name_) { name = name_; }
  const std::string &get_name() const { return name; }
  Specifier &specifier() { return spec; }
  const Specifier &specifier() const { return spec; }

  TypedAttribute<float> minDistance;  // physics:minDistance
  TypedAttribute<float> maxDistance;  // physics:maxDistance

  std::pair<ListEditQual, std::vector<Reference>> references;
  std::pair<ListEditQual, std::vector<Payload>> payload;
  std::map<std::string, VariantSet> variantSet;
  std::map<std::string, Property> props;

  PrimMeta meta;
  PrimMeta &metas() { return meta; }
  const PrimMeta &metas() const { return meta; }

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

namespace value {

// Register UsdPhysics Prim types.
#include "define-type-trait.inc"
DEFINE_TYPE_TRAIT(PhysicsScene, kPhysicsScene, TYPE_ID_PHYSICS_SCENE, 1);
DEFINE_TYPE_TRAIT(PhysicsRevoluteJoint, kPhysicsRevoluteJoint, TYPE_ID_PHYSICS_REVOLUTE_JOINT, 1);
DEFINE_TYPE_TRAIT(PhysicsPrismaticJoint, kPhysicsPrismaticJoint, TYPE_ID_PHYSICS_PRISMATIC_JOINT, 1);
DEFINE_TYPE_TRAIT(PhysicsSphericalJoint, kPhysicsSphericalJoint, TYPE_ID_PHYSICS_SPHERICAL_JOINT, 1);
DEFINE_TYPE_TRAIT(PhysicsFixedJoint, kPhysicsFixedJoint, TYPE_ID_PHYSICS_FIXED_JOINT, 1);
DEFINE_TYPE_TRAIT(PhysicsDistanceJoint, kPhysicsDistanceJoint, TYPE_ID_PHYSICS_DISTANCE_JOINT, 1);

#undef DEFINE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
