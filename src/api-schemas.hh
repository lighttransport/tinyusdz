// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.

///
/// @file api-schemas.hh
/// @brief USD API Schema definitions and USDZ AR extensions
///
/// Contains API Schema related classes including standard USD API schemas
/// and preliminary USDZ schemas for AR (Augmented Reality) support.
/// Based on Apple's USDZ Schemas for AR specification.
///
#pragma once

#include <string>
#include <vector>
#include <utility>
#include <limits>
#include "value-types.hh"
#include "enum-types.hh"

namespace tinyusdz {

// Forward declarations
struct Path;
struct Extent;

///
/// @brief API Schemas container for USD primitives
///
/// Manages applied API schemas for USD prims. API schemas provide additional
/// functionality and properties to prims without changing their core type.
///
struct APISchemas {
  // TinyUSDZ does not allow user-supplied API schema for now
  enum class APIName {
    // usdShade
    MaterialBindingAPI,  // "MaterialBindingAPI"
    ConnectableAPI, // "ConnectableAPI"
    CoordSysAPI, // "CoordSysAPI"
    NodeDefAPI, // "NodeDefAPI"

    CollectionAPI,      // "CollectionAPI"
    // usdGeom
    GeomModelAPI, // "GeomModelAPI"
    MotionAPI, // "MotionAPI"
    PrimvarsAPI, // "PrimvarsAPI"
    VisibilityAPI, // "VisibilityAPI"
    XformCommonAPI, // "XformCommonAPI"

    // usdLux
    LightAPI, // "LightAPI"
    LightListAPI, // "LightListAPI"
    ListAPI, // "ListAPI"
    MeshLightAPI, // "MeshLightAPI"
    ShapingAPI, // "ShapingAPI"
    ShadowAPI,  // "ShadowAPI"
    VolumeLightAPI,  // "VolumeLightAPI"

    // usdSkel
    SkelBindingAPI,      // "SkelBindingAPI"
    
    // USDZ AR extensions
    Preliminary_AnchoringAPI,
    Preliminary_PhysicsColliderAPI,
    Preliminary_PhysicsMaterialAPI,
    Preliminary_PhysicsRigidBodyAPI,
  };

  ListEditQual listOpQual{ListEditQual::ResetToExplicit};  // must be 'prepend'

  // std::get<1>: instance name. For Multi-apply API Schema e.g.
  // `material:MainMaterial` for `CollectionAPI:material:MainMaterial`
  std::vector<std::pair<APIName, std::string>> names;
};

//
// USDZ Schemas for AR
// https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/schema_definitions_for_third-party_digital_content_creation_dcc
//

// UsdPhysics

///
/// @brief Physics gravitational force schema
///
/// Defines gravitational acceleration for physics simulations.
///
struct Preliminary_PhysicsGravitationalForce {
  /// physics::gravitatioalForce::acceleration
  value::double3 acceleration{{0.0, -9.81, 0.0}};  // [m/s^2]
};

///
/// @brief Physics material properties API schema
///
/// Defines physical material properties for physics simulations including
/// restitution (bounciness) and friction coefficients.
///
struct Preliminary_PhysicsMaterialAPI {
  /// preliminary:physics:material:restitution
  double restitution;  // [0.0, 1.0]

  /// preliminary:physics:material:friction:static
  double friction_static;

  /// preliminary:physics:material:friction:dynamic
  double friction_dynamic;
};

///
/// @brief Physics rigid body API schema
///
/// Defines rigid body properties for physics simulations including
/// mass and initial activation state.
///
struct Preliminary_PhysicsRigidBodyAPI {
  /// preliminary:physics:rigidBody:mass
  double mass{1.0};

  /// preliminary:physics:rigidBody:initiallyActive
  bool initiallyActive{true};
};

///
/// @brief Physics collider API schema
///
/// Defines collision shape properties for physics simulations.
///
struct Preliminary_PhysicsColliderAPI {
  /// preliminary::physics::collider::convexShape
  Path convexShape;
};

///
/// @brief Infinite collider plane schema
///
/// Defines an infinite plane for collision detection with position,
/// normal, and extent properties.
///
struct Preliminary_InfiniteColliderPlane {
  value::double3 position{{0.0, 0.0, 0.0}};
  value::double3 normal{{0.0, 0.0, 0.0}};

  Extent extent;  // [-FLT_MAX, FLT_MAX]

  Preliminary_InfiniteColliderPlane() {
    extent.lower[0] = -(std::numeric_limits<float>::max)();
    extent.lower[1] = -(std::numeric_limits<float>::max)();
    extent.lower[2] = -(std::numeric_limits<float>::max)();
    extent.upper[0] = (std::numeric_limits<float>::max)();
    extent.upper[1] = (std::numeric_limits<float>::max)();
    extent.upper[2] = (std::numeric_limits<float>::max)();
  }
};

// UsdInteractive

///
/// @brief Anchoring API schema for AR
///
/// Defines anchoring properties for AR content including anchor type,
/// alignment, and reference image.
///
struct Preliminary_AnchoringAPI {
  /// preliminary:anchoring:type
  std::string type;  // "plane", "image", "face", "none";

  std::string alignment;  // "horizontal", "vertical", "any";

  Path referenceImage;
};

///
/// @brief Reference image schema for AR
///
/// Defines reference image properties for image-based AR anchoring.
///
struct Preliminary_ReferenceImage {
  int64_t image_id{-1};  // asset image

  double physicalWidth{0.0};
};

///
/// @brief Behavior schema for interactive content
///
/// Defines behavior properties including triggers, actions, and exclusivity.
///
struct Preliminary_Behavior {
  Path triggers;
  Path actions;
  bool exclusive{false};
};

///
/// @brief Trigger schema for interactive behaviors
///
/// Defines trigger properties for interactive content behaviors.
///
struct Preliminary_Trigger {
  /// uniform token info:id
  std::string info;  // Store decoded string from token id
};

///
/// @brief Action schema for interactive behaviors
///
/// Defines action properties for interactive content behaviors including
/// multiple performance operation handling.
///
struct Preliminary_Action {
  /// uniform token info:id
  std::string info;  // Store decoded string from token id

  std::string multiplePerformOperation{
      "ignore"};  // ["ignore", "allow", "stop"]
};

///
/// @brief Text schema for 3D text rendering
///
/// Defines text rendering properties including content, font, size,
/// and alignment options.
///
struct Preliminary_Text {
  std::string content;
  std::vector<std::string> font;  // An array of font names

  float pointSize{144.0f};
  float width;
  float height;
  float depth{0.0f};

  std::string wrapMode{"flowing"};  // ["singleLine", "hardBreaks", "flowing"]
  std::string horizontalAlignmment{
      "center"};  // ["left", "center", "right", "justified"]
  std::string verticalAlignmment{
      "middle"};  // ["top", "middle", "lowerMiddle", "baseline", "bottom"]
};

}  // namespace tinyusdz