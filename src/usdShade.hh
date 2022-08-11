// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// Material and Shader. And more, TinyUSDZ implmenents some usdImaging stuff here.
#pragma once

#include "prim-types.hh"

namespace tinyusdz {


// import DEFINE_TYPE_TRAIT and DEFINE_ROLE_TYPE_TRAIT
#include "define-type-trait.inc"

namespace value {

// Mateiral
DEFINE_TYPE_TRAIT(Material, "Material",
                  TYPE_ID_MATERIAL, 1);

// Shader
DEFINE_TYPE_TRAIT(Shader, "Shader",
                  TYPE_ID_SHADER, 1);

DEFINE_TYPE_TRAIT(PreviewSurface, "PreviewSurface",
                  TYPE_ID_IMAGING_PREVIEWSURFACE, 1);
DEFINE_TYPE_TRAIT(UVTexture, "UVTexture", TYPE_ID_IMAGING_UVTEXTURE, 1);
DEFINE_TYPE_TRAIT(PrimvarReader_float, "PrimvarReader_float",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT, 1);
DEFINE_TYPE_TRAIT(PrimvarReader_float2, "PrimvarReader_float2",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT2, 1);
DEFINE_TYPE_TRAIT(PrimvarReader_float3, "PrimvarReader_float3",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT3, 1);
DEFINE_TYPE_TRAIT(PrimvarReader_float4, "PrimvarReader_float4",
                  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT4, 1);
DEFINE_TYPE_TRAIT(PrimvarReader_int, "PrimvarReader_int",
                  TYPE_ID_IMAGING_PRIMVAR_READER_INT, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
