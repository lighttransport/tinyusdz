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


class MtlxAutodeskStandardSurface : ShaderNode
{
  //TypedAttributeWithFallback
  // TODO
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
