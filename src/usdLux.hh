// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdLux LightSource
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

constexpr auto kLuxSphereLight = "LuxSphereLight";
constexpr auto kLuxDomeLight = "LuxDomeLight";

struct LuxSphereLight {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  TypedAttribute<value::color3f> color{{1.0f, 1.0f, 1.0f}}; // inputs.color Light energy in linear color space.
  TypedAttribute<float> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttribute<float> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttribute<bool> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttribute<float> exposure{0.0f}; // inputs:exposure EV
  TypedAttribute<float> intensity{1.0f}; // inputs:intensity
  TypedAttribute<bool> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttribute<float> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  TypedAttribute<float> radius{0.5f}; // inputs:radius

  //
  // Properties
  //
  Animatable<Extent> extent;  // bounding extent(in local coord?).
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, Property> props;
  PrimMeta meta;
};

struct LuxDomeLight {
  std::string name;
  int64_t parent_id{-1};  // Index to xform node

  enum class TextureFormat {
    Automatic, // "automatic"
    Latlong, // "latlong"
    MirroredBall, // "mirroredBall"
    Angular // "angular"
  };

  //
  // Predefined attribs.
  //
  // TODO: Support texture

  // Light API
  TypedAttribute<value::color3f> color{{1.0f, 1.0f, 1.0f}}; // inputs.color Light energy in linear color space.
  TypedAttribute<float> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttribute<float> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttribute<bool> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttribute<float> exposure{0.0f}; // inputs:exposure EV
  TypedAttribute<float> intensity{1.0f}; // inputs:intensity
  TypedAttribute<bool> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttribute<float> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters


  // DomeLight specific
  TypedAttribute<float> guideRadius{1.0e5f};
  // asset inputs:texture:file
  TextureFormat textureFormat{TextureFormat::Automatic}; // token inputs:texture:format
  // rel portals
  // rel proxyPrim

  //
  // Properties
  //
  Animatable<Extent> extent;  // bounding extent(in local coord?).
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};
  std::vector<value::token> xformOpOrder;

  std::map<std::string, Property> props;
  PrimMeta meta;
};


// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

DEFINE_TYPE_TRAIT(LuxDomeLight, kLuxDomeLight, TYPE_ID_LUX_DOME, 1);
DEFINE_TYPE_TRAIT(LuxSphereLight, kLuxSphereLight, TYPE_ID_LUX_SPHERE, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

} // namespace value

}  // namespace tinyusdz
