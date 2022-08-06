// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdLux LightSource
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

struct LuxSphereLight {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  value::color3f color{1.0f, 1.0f, 1.0f}; // inputs.color Light energy in linear color space.
  float colorTemperature{6500.0f};  // inputs:colorTemperature
  float diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  bool enableColorTemperature{false}; // inputs:enableColorTemperature
  float exposure{0.0f}; // inputs:exposure EV
  float intensity{1.0f}; // inputs:intensity
  bool normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  float specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  float radius{0.5f}; // inputs:radius

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` may exist(in that ase, please ignore `widths`
  // parameter)
  std::map<std::string, PrimAttrib> attribs;
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
  value::color3f color{1.0f, 1.0f, 1.0f}; // inputs.color Light energy in linear color space.
  float colorTemperature{6500.0f};  // inputs:colorTemperature
  float diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  bool enableColorTemperature{false}; // inputs:enableColorTemperature
  float exposure{0.0f}; // inputs:exposure EV
  float intensity{1.0f}; // inputs:intensity
  bool normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  float specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters


  // DomeLight specific
  float guideRadius{1.0e5f};
  // asset inputs:texture:file
  TextureFormat textureFormat{TextureFormat::Automatic}; // token inputs:texture:format
  // rel portals
  // rel proxyPrim

  //
  // Properties
  //
  AnimatableExtent extent;  // bounding extent(in local coord?).
  AnimatableVisibility visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};
  std::vector<value::token> xformOpOrder;

  // List of Primitive attributes(primvars)
  // NOTE: `primvar:widths` may exist(in that ase, please ignore `widths`
  // parameter)
  std::map<std::string, PrimAttrib> attribs;
};


// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {


#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

} // namespace value

}  // namespace tinyusdz
