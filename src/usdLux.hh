// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdLux LightSource
#pragma once

#include "prim-types.hh"
#include "xform.hh"

namespace tinyusdz {

constexpr auto kSphereLight = "SphereLight";
constexpr auto kCylinderLight = "CylinderLight";
constexpr auto kDomeLight = "DomeLight";
constexpr auto kDiskLight = "DiskLight";
constexpr auto kRectLight = "RectLight";
constexpr auto kDistantLight = "DistantLight";
constexpr auto kGeometryLight = "GeometryLight";
constexpr auto kPortalLight = "PortalLight";
constexpr auto kPluginLight = "PluginLight";


struct SphereLight : public Xformable {
  std::string name;
  Specifier spec{Specifier::Def};

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

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

struct CylinderLight : public Xformable {
  std::string name;
  Specifier spec{Specifier::Def};

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

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};


struct RectLight : public Xformable {
  std::string name;
  Specifier spec{Specifier::Def};

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

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

struct DiskLight : public Xformable {
  std::string name;
  Specifier spec{Specifier::Def};

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

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// NOTE: Make Distance xformable?
struct DistantLight : public Xformable {
  std::string name;
  Specifier spec{Specifier::Def};

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

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

struct DomeLight : public Xformable {
  std::string name;
  Specifier spec{Specifier::Def};
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

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// TODO:
struct GeometryLight : public Xformable {
  Specifier spec{Specifier::Def};
  nonstd::optional<Relationship> geometry; // `rel geometry`

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// TODO
struct PortalLight : public Xformable {
  Specifier spec{Specifier::Def};
  TypedAttribute<Animatable<Extent>> extent; // float3[]

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// TODO
struct PluginLight : public Xformable {
  Specifier spec{Specifier::Def};

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};

// TODO
struct PluginLightFilter : public Xformable {
  Specifier spec{Specifier::Def};

  const std::vector<value::token> &primChildrenNames() const { return _primChildren; }
  const std::vector<value::token> &propertyNames() const { return _properties; }
  std::vector<value::token> &primChildrenNames() { return _primChildren; }
  std::vector<value::token> &propertyNames() { return _properties; }

 private:
  std::vector<value::token> _primChildren;
  std::vector<value::token> _properties;
};


// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

DEFINE_TYPE_TRAIT(DomeLight, kDomeLight, TYPE_ID_LUX_DOME, 1);
DEFINE_TYPE_TRAIT(CylinderLight, kCylinderLight, TYPE_ID_LUX_CYLINDER, 1);
DEFINE_TYPE_TRAIT(SphereLight, kSphereLight, TYPE_ID_LUX_SPHERE, 1);
DEFINE_TYPE_TRAIT(DiskLight, kDiskLight, TYPE_ID_LUX_DISK, 1);
DEFINE_TYPE_TRAIT(DistantLight, kDistantLight, TYPE_ID_LUX_DISTANT, 1);
DEFINE_TYPE_TRAIT(RectLight, kRectLight, TYPE_ID_LUX_RECT, 1);
DEFINE_TYPE_TRAIT(GeometryLight, kGeometryLight, TYPE_ID_LUX_GEOMETRY, 1);
DEFINE_TYPE_TRAIT(PortalLight, kPortalLight, TYPE_ID_LUX_PORTAL, 1);
DEFINE_TYPE_TRAIT(PluginLight, kPluginLight, TYPE_ID_LUX_PLUGIN, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

} // namespace value

}  // namespace tinyusdz
