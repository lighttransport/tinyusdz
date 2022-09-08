// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdLux LightSource
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

constexpr auto kLuxSphereLight = "SphereLight";
constexpr auto kLuxDomeLight = "DomeLight";

struct LuxSphereLight : public Xformable {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  TypedProperty<value::color3f> color{{1.0f, 1.0f, 1.0f}}; // inputs.color Light energy in linear color space.
  TypedProperty<float> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedProperty<float> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedProperty<bool> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedProperty<float> exposure{0.0f}; // inputs:exposure EV
  TypedProperty<float> intensity{1.0f}; // inputs:intensity
  TypedProperty<bool> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedProperty<float> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  TypedProperty<float> radius{0.5f}; // inputs:radius

  //
  // Properties
  //
  Animatable<Extent> extent;  // bounding extent(in local coord?).
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, Property> props;
  PrimMeta meta;
};

struct LuxDomeLight : public Xformable {
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
  TypedProperty<value::color3f> color{{1.0f, 1.0f, 1.0f}}; // inputs.color Light energy in linear color space.
  TypedProperty<float> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedProperty<float> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedProperty<bool> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedProperty<float> exposure{0.0f}; // inputs:exposure EV
  TypedProperty<float> intensity{1.0f}; // inputs:intensity
  TypedProperty<bool> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedProperty<float> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters


  // DomeLight specific
  TypedProperty<float> guideRadius{1.0e5f};
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
