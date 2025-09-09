// SPDX-License-Identifier: Apache 2.0

///
/// @file enum-types.hh
/// @brief USD enum types and constants
///
/// Contains fundamental USD enumeration types used throughout the system
/// for specifying behavior, properties, and metadata.
///
#pragma once

#include <string>

namespace tinyusdz {

///
/// USD specification type for property specs
///
enum class SpecType {
  Unknown = 0,  // must be 0
  Attribute,
  Connection,
  Expression,
  Mapper,
  MapperArg,
  Prim,
  PseudoRoot,
  Relationship,
  RelationshipTarget,
  Variant,
  VariantSet,
  Invalid,  // or NumSpecTypes
};

///
/// Scene orientation (handed-ness)
///
enum class Orientation {
  RightHanded,  // 0
  LeftHanded,
  Invalid
};

///
/// Visibility inheritance behavior  
///
enum class Visibility {
  Inherited,  // "inherited" (default)
  Invisible,  // "invisible"
  Invalid
};

///
/// Primitive purpose classification
///
enum class Purpose {
  Default,  // 0
  Render,   // "render"
  Proxy,    // "proxy"
  Guide,    // "guide"
};

///
/// Model hierarchy classification
///
/// USDZ extension: sceneLibrary
/// https://developer.apple.com/documentation/arkit/usdz_schemas_for_ar/scenelibrary
///
enum class Kind {
  Model,
  Group,
  Assembly,
  Component,
  Subcomponent,
  SceneLibrary,  // USDZ extension
  UserDef, // Unknown or user defined Kind
  Invalid
};

///
/// Attribute interpolation methods
///
enum class Interpolation {
  Constant,     // "constant"
  Uniform,      // "uniform"
  Varying,      // "varying"
  Vertex,       // "vertex"
  FaceVarying,  // "faceVarying"
  Invalid
};

///
/// List editing qualifiers (NOTE: Attribute cannot have ListEdit qualifier)
///
enum class ListEditQual {
  ResetToExplicit,  // "unqualified"(no qualifier)
  Append,           // "append"
  Add,              // "add"
  Delete,           // "delete"
  Prepend,          // "prepend"
  Order,            // "order"
  Invalid
};

///
/// Coordinate axis enumeration
///
enum class Axis { X, Y, Z, Invalid };

///
/// Primitive specification type
///
enum class Specifier {
  Def,  // 0
  Over,
  Class,
  Invalid
};

///
/// Property access permissions
///
enum class Permission {
  Public,  // 0
  Private,
  Invalid
};

///
/// Property variability classification
///
enum class Variability {
  Varying,  // 0
  Uniform,
  Config,
  Invalid
};

///
/// Material binding strength for usdShade
///
enum class MaterialBindingStrength
{
  WeakerThanDescendants, // default
  StrongerThanDescendants
};

// for bindMaterialAs
constexpr auto kWeaderThanDescendants = "weakerThanDescendants";
constexpr auto kStrongerThanDescendants = "strongerThanDescendants";

///
/// @brief Unit conversion constants for USD geometry
///
/// UsdGeomLinearUnits equivalent constants for converting between different
/// length units. All values are scale factors relative to meters.
/// To avoid linkage error, defined as static constexpr function.
///
struct Units {
  static constexpr double Nanometers = 1e-9;      ///< Nanometers to meters
  static constexpr double Micrometers = 1e-6;     ///< Micrometers to meters  
  static constexpr double Millimeters = 0.001;    ///< Millimeters to meters
  static constexpr double Centimeters = 0.01;     ///< Centimeters to meters
  static constexpr double Meters = 1.0;           ///< Meters (base unit)
  static constexpr double Kilometers = 1000;      ///< Kilometers to meters
  static constexpr double LightYears = 9.4607304725808e15; ///< Light years to meters
  static constexpr double Inches = 0.0254;        ///< Inches to meters
  static constexpr double Feet = 0.3048;          ///< Feet to meters
  static constexpr double Yards = 0.9144;         ///< Yards to meters
  static constexpr double Miles = 1609.344;       ///< Miles to meters
};

//
// String conversion functions
//

// SpecType conversions
std::string to_string(const SpecType spec_type);
bool from_string(const std::string &str, SpecType *spec_type);

// Orientation conversions  
std::string to_string(const Orientation orientation);
bool from_string(const std::string &str, Orientation *orientation);

// Visibility conversions
std::string to_string(const Visibility visibility);
bool from_string(const std::string &str, Visibility *visibility);

// Purpose conversions
std::string to_string(const Purpose purpose);
bool from_string(const std::string &str, Purpose *purpose);

// Kind conversions
std::string to_string(const Kind kind);
bool from_string(const std::string &str, Kind *kind);

// Interpolation conversions
std::string to_string(const Interpolation interpolation);
bool from_string(const std::string &str, Interpolation *interpolation);

// ListEditQual conversions
std::string to_string(const ListEditQual list_edit_qual);
bool from_string(const std::string &str, ListEditQual *list_edit_qual);

// Axis conversions
std::string to_string(const Axis axis);
bool from_string(const std::string &str, Axis *axis);

// Specifier conversions
std::string to_string(const Specifier specifier);
bool from_string(const std::string &str, Specifier *specifier);

// Permission conversions
std::string to_string(const Permission permission);
bool from_string(const std::string &str, Permission *permission);

// Variability conversions
std::string to_string(const Variability variability);
bool from_string(const std::string &str, Variability *variability);

// MaterialBindingStrength conversions
std::string to_string(const MaterialBindingStrength strength);
bool from_string(const std::string &str, MaterialBindingStrength *strength);


// For backward compatibility.

//nonstd::optional<Interpolation> InterpolationFromString(const std::string &v);
//nonstd::optional<Orientation> OrientationFromString(const std::string &v);
//nonstd::optional<Kind> KindFromString(const std::string &v);

}  // namespace tinyusdz

