// SPDX-License-Identifier: MIT
//
// Predefined MaterialX shadingmodel & Built-in MaterialX XML import plugIn.
// Import only. Export is not supported(yet).
//
// example usage
//
// def Shader "mesh" (
//   prepend references = @myshader.mtlx@
// )
// {
//    ...
// }
//
// Based on MaterialX spec v1.38

#pragma once

#include <string>
#include "usdShade.hh"

namespace tinyusdz {

constexpr auto kMtlxUsdPreviewSurface = "MtlxUsdPreviewSurface";
constexpr auto kMtlxAutodeskStandardSurface = "MtlxAutodeskStandaradSurface";

struct MtlxModel
{
  std::string asset_name;
  // TODO

};

class MtlxUsdPreviewSurface : ShaderNode
{
  //TypedAttributeWithFallback
  // TODO
};


// https://github.com/Autodesk/standard-surface/blob/master/reference/standard_surface.mtlx
// We only support v1.0.1
class MtlxAutodeskStandardSurface : ShaderNode
{

  TypedAttributeWithFallback<Animatable<float>> base{1.0f};
  TypedAttributeWithFallback<Animatable<value::color3f>> baseColor{value::color3f{0.8f, 0.8f, 0.8f}}; // color3

  // TODO
  // ...
  

  // (coat_affect_roughness * coat) * coat_roughness
  TypedAttribute<Animatable<float>> coat_affect_roughness;
  TypedAttribute<Animatable<float>> coat; 
  TypedAttribute<Animatable<float>> coat_roughness; 

  // (specular_roughness + transmission_extra_roughness)
  TypedAttribute<Animatable<float>> specular_roughness; 
  TypedAttribute<Animatable<float>> transmission_extra_roughness; 
  TypedAttribute<Animatable<float>> transmission_roughness_add; 

  // tangent_rotate_normalize
  // normalize(rotate3d(/* in */tangent, /*amount*/(specular_rotation * 360), /* axis */normal))
  TypedAttribute<Animatable<float>> specular_rotation; 


  // Output
  TypedTerminalAttribute<value::token> out; // 'out'
};


// IO
bool ReadMaterialXFromString(const std::string &str, MtlxModel *mtlx, std::string *err = nullptr);
bool ReadMaterialXFromFile(const std::string &filepath, MtlxModel *mtlx, std::string *err = nullptr);


// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// ShaderNodes
DEFINE_TYPE_TRAIT(MtlxUsdPreviewSurface, kMtlxUsdPreviewSurface,
                  TYPE_ID_IMAGING_MTLX_PREVIEWSURFACE, 1);
DEFINE_TYPE_TRAIT(MtlxAutodeskStandardSurface, kMtlxAutodeskStandardSurface,
                  TYPE_ID_IMAGING_MTLX_STANDARDSURFACE, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value


} // namespace tinyusdz
