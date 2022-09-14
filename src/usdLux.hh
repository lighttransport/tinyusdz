// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdLux LightSource
#pragma once

#include "prim-types.hh"

namespace tinyusdz {

constexpr auto kLuxSphereLight = "SphereLight";
constexpr auto kLuxCylinderLight = "CylinderLight";
constexpr auto kLuxDomeLight = "DomeLight";
constexpr auto kLuxDiskLight = "DiskLight";
constexpr auto kLuxRectLight = "RectLight";
constexpr auto kLuxDistantLight = "DistantLight";
constexpr auto kLuxGeometryLight = "GeometryLight";
constexpr auto kLuxPortalLight = "PortalLight";
constexpr auto kLuxPluginLight = "PluginLight";


struct LuxSphereLight : public Xformable {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  TypedAttributeWithFallback<Animatable<value::color3f>> color{value::color3f({1.0f, 1.0f, 1.0f})}; // inputs.color Light energy in linear color space.
  TypedAttributeWithFallback<Animatable<float>> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttributeWithFallback<Animatable<float>> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttributeWithFallback<Animatable<bool>> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttributeWithFallback<Animatable<float>> exposure{0.0f}; // inputs:exposure EV
  TypedAttributeWithFallback<Animatable<float>> intensity{1.0f}; // inputs:intensity
  TypedAttributeWithFallback<Animatable<bool>> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttributeWithFallback<Animatable<float>> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  TypedAttributeWithFallback<Animatable<float>> radius{0.5f}; // inputs:radius
  TypedAttribute<Animatable<Extent>> extent; // float3[] 

  //
  // Properties
  //
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, Property> props;
  PrimMeta meta;
};

struct LuxCylinderLight : public Xformable {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  TypedAttributeWithFallback<Animatable<value::color3f>> color{value::color3f({1.0f, 1.0f, 1.0f})}; // inputs.color Light energy in linear color space.
  TypedAttributeWithFallback<Animatable<float>> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttributeWithFallback<Animatable<float>> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttributeWithFallback<Animatable<bool>> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttributeWithFallback<Animatable<float>> exposure{0.0f}; // inputs:exposure EV
  TypedAttributeWithFallback<Animatable<float>> intensity{1.0f}; // inputs:intensity
  TypedAttributeWithFallback<Animatable<bool>> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttributeWithFallback<Animatable<float>> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  TypedAttributeWithFallback<Animatable<float>> length{1.0f}; // inputs:length size in Y axis
  TypedAttributeWithFallback<Animatable<float>> radius{0.5f}; // inputs:radius  size in X axis
  TypedAttribute<Animatable<Extent>> extent; // float3[] 

  // asset inputs:texture:file

  //
  // Properties
  //
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, Property> props;
  PrimMeta meta;
};


struct LuxRectLight : public Xformable {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  TypedAttributeWithFallback<Animatable<value::color3f>> color{value::color3f({1.0f, 1.0f, 1.0f})}; // inputs.color Light energy in linear color space.
  TypedAttributeWithFallback<Animatable<float>> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttributeWithFallback<Animatable<float>> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttributeWithFallback<Animatable<bool>> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttributeWithFallback<Animatable<float>> exposure{0.0f}; // inputs:exposure EV
  TypedAttributeWithFallback<Animatable<float>> intensity{1.0f}; // inputs:intensity
  TypedAttributeWithFallback<Animatable<bool>> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttributeWithFallback<Animatable<float>> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  TypedAttribute<Animatable<value::AssetPath>> file; // asset inputs:texture:file
  TypedAttributeWithFallback<Animatable<float>> height{1.0f}; // inputs:height size in Y axis
  TypedAttributeWithFallback<Animatable<float>> width{1.0f}; // inputs:width  size in X axis
  TypedAttribute<Animatable<Extent>> extent; // float3[] 

  // asset inputs:texture:file

  //
  // Properties
  //
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, Property> props;
  PrimMeta meta;
};

struct LuxDiskLight : public Xformable {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  TypedAttributeWithFallback<Animatable<value::color3f>> color{value::color3f({1.0f, 1.0f, 1.0f})}; // inputs.color Light energy in linear color space.
  TypedAttributeWithFallback<Animatable<float>> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttributeWithFallback<Animatable<float>> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttributeWithFallback<Animatable<bool>> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttributeWithFallback<Animatable<float>> exposure{0.0f}; // inputs:exposure EV
  TypedAttributeWithFallback<Animatable<float>> intensity{1.0f}; // inputs:intensity
  TypedAttributeWithFallback<Animatable<bool>> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttributeWithFallback<Animatable<float>> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  TypedAttributeWithFallback<Animatable<float>> radius{0.5f}; // inputs:radius
  TypedAttribute<Animatable<Extent>> extent; // float3[] 

  // asset inputs:texture:file

  //
  // Properties
  //
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};

  std::map<std::string, Property> props;
  PrimMeta meta;
};

struct LuxDistantLight : public Xformable {
  std::string name;

  int64_t parent_id{-1};  // Index to xform node

  //
  // Predefined attribs.
  //

  // Light API
  TypedAttributeWithFallback<Animatable<value::color3f>> color{value::color3f({1.0f, 1.0f, 1.0f})}; // inputs.color Light energy in linear color space.
  TypedAttributeWithFallback<Animatable<float>> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttributeWithFallback<Animatable<float>> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttributeWithFallback<Animatable<bool>> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttributeWithFallback<Animatable<float>> exposure{0.0f}; // inputs:exposure EV
  TypedAttributeWithFallback<Animatable<float>> intensity{50000.0f}; // inputs:intensity
  TypedAttributeWithFallback<Animatable<bool>> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttributeWithFallback<Animatable<float>> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters

  TypedAttributeWithFallback<Animatable<float>> angle{0.53f}; // inputs:angle in degrees
  TypedAttribute<Animatable<Extent>> extent; // float3[] 

  // asset inputs:texture:file

  //
  // Properties
  //
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
  TypedAttributeWithFallback<Animatable<value::color3f>> color{value::color3f({1.0f, 1.0f, 1.0f})}; // inputs.color Light energy in linear color space.
  TypedAttributeWithFallback<Animatable<float>> colorTemperature{6500.0f};  // inputs:colorTemperature
  TypedAttributeWithFallback<Animatable<float>> diffuse{1.0f}; // inputs:diffuse diffuse multiplier
  TypedAttributeWithFallback<Animatable<bool>> enableColorTemperature{false}; // inputs:enableColorTemperature
  TypedAttributeWithFallback<Animatable<float>> exposure{0.0f}; // inputs:exposure EV
  TypedAttributeWithFallback<Animatable<float>> intensity{1.0f}; // inputs:intensity
  TypedAttributeWithFallback<Animatable<bool>> normalize{false}; // inputs:normalize normalize power by the surface area of the light.
  TypedAttributeWithFallback<Animatable<float>> specular{1.0f}; // inputs:specular specular multiplier
  // rel light:filters


  // DomeLight specific
  TypedAttributeWithFallback<Animatable<float>> guideRadius{1.0e5f};
  // asset inputs:texture:file
  TextureFormat textureFormat{TextureFormat::Automatic}; // token inputs:texture:format
  TypedAttribute<Animatable<Extent>> extent; // float3[] 
  // rel portals
  // rel proxyPrim

  //
  // Properties
  //
  Animatable<Visibility> visibility{Visibility::Inherited};
  Purpose purpose{Purpose::Default};
  std::vector<value::token> xformOpOrder;

  std::map<std::string, Property> props;
  PrimMeta meta;
};

// TODO:
struct LuxGeometryLight : public Xformable {
  nonstd::optional<Relation> geometry; // `rel geometry`
};

// TODO
struct LuxPortalLight : public Xformable {
  TypedAttribute<Animatable<Extent>> extent; // float3[] 
};

// TODO
struct LuxPluginLight : public Xformable {
};

// TODO
struct PluginLightFilter {
};


// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

DEFINE_TYPE_TRAIT(LuxDomeLight, kLuxDomeLight, TYPE_ID_LUX_DOME, 1);
DEFINE_TYPE_TRAIT(LuxCylinderLight, kLuxCylinderLight, TYPE_ID_LUX_CYLINDER, 1);
DEFINE_TYPE_TRAIT(LuxSphereLight, kLuxSphereLight, TYPE_ID_LUX_SPHERE, 1);
DEFINE_TYPE_TRAIT(LuxDiskLight, kLuxDiskLight, TYPE_ID_LUX_DISK, 1);
DEFINE_TYPE_TRAIT(LuxDistantLight, kLuxDistantLight, TYPE_ID_LUX_DISTANT, 1);
DEFINE_TYPE_TRAIT(LuxRectLight, kLuxRectLight, TYPE_ID_LUX_RECT, 1);
DEFINE_TYPE_TRAIT(LuxGeometryLight, kLuxGeometryLight, TYPE_ID_LUX_GEOMETRY, 1);
DEFINE_TYPE_TRAIT(LuxPortalLight, kLuxPortalLight, TYPE_ID_LUX_PORTAL, 1);
DEFINE_TYPE_TRAIT(LuxPluginLight, kLuxPluginLight, TYPE_ID_LUX_PLUGIN, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

} // namespace value

}  // namespace tinyusdz
