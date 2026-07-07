// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "gpu_scene.hh"

namespace tusdview {

constexpr int kLightRtOpenPBRVec4s =
    tinyusdz::tydra::kLightRtOpenPBRVec4s;
constexpr int kLightRtOpenPBRFloats =
    tinyusdz::tydra::kLightRtOpenPBRFloats;
constexpr int kRtMaterialTextureParamFloats = 56;
constexpr int kRasterMaterialTextureParamVec4s = 18;
constexpr int kRasterMaterialTextureParamFloats =
    kRasterMaterialTextureParamVec4s * 4;

// Bake USD/Tydra shader params into the LightRT/OpenPBR parameter layout. The
// raster path can use this as a constant fallback now; RT paths can later sample
// DrawMaterialParamCPU texture connections before invoking the LightRT BSDF.
void BakeLightRtOpenPBR(DrawMaterialCPU* mat);

// Evaluate a MaterialX XML document through LightRT's MaterialX graph evaluator
// into tusdview's LightRT/OpenPBR constant block. Texture image nodes currently
// use their MaterialX `default` input through a no-image texture cache; this is
// still useful for constants and procedural/color graph nodes, and keeps the
// viewer free of another stb_image owner until the full RT texture path lands.
bool EvaluateMaterialXStringToLightRtOpenPBR(const char* xml,
                                             const char* materialName,
                                             DrawLightRtOpenPBRCPU* out,
                                             std::string* err);

// Pack DrawLightRtOpenPBRCPU into the vec4-friendly SSBO/kernel layout consumed
// by the Vulkan, CUDA and HIP RT preview paths. `dst` must hold
// kLightRtOpenPBRFloats floats.
void PackLightRtOpenPBR(const DrawMaterialCPU& mat, float* dst);

// Pack material texture transforms, channel selectors, and scale/bias values
// into the RT kernel layout. `dst` must hold kRtMaterialTextureParamFloats
// floats. The order must match raytracer_kernel.inc.
void PackRtMaterialTextureParams(const DrawMaterialCPU& mat, float* dst);

// Pack the Vulkan raster/tessellation material texture SSBO layout. `dst` must
// hold kRasterMaterialTextureParamFloats floats. The order must match
// vk/shaders/mesh*.{vert,frag,tese}.
void PackRasterMaterialTextureParams(const DrawMaterialCPU& mat, float* dst);

}  // namespace tusdview
