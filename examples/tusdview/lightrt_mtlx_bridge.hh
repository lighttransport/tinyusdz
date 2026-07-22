// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "gpu_scene.hh"

namespace tusdview {

constexpr int kLightRtOpenPBRVec4s =
    tinyusdz::tydra::kLightRtOpenPBRVec4s;
constexpr int kLightRtOpenPBRFloats =
    tinyusdz::tydra::kLightRtOpenPBRFloats;
constexpr int kRtMaterialTextureParamFloats = 72;
// Rows 20-24 add opacity sampling and scene-wide UDIM-atlas row selectors;
// rows 25-26 carry the independent roughness UV transform. Keep
// in lockstep with MaterialTexParam in every Vulkan mesh shader stage.
// Rows 27-28 carry the real-time PBR coat/occlusion constants.
constexpr int kRasterMaterialTextureParamVec4s = 29;
constexpr int kRasterMaterialTextureParamFloats =
    kRasterMaterialTextureParamVec4s * 4;

// Bake USD/Tydra shader params into the LightRT/OpenPBR parameter layout. The
// raster and RT paths use this as their shared constant fallback around live
// DrawMaterialCPU semantic texture slots.
void BakeLightRtOpenPBR(DrawMaterialCPU* mat);

// Evaluate a MaterialX XML document through LightRT's MaterialX graph evaluator
// into tusdview's LightRT/OpenPBR constant block. Texture image nodes currently
// use their MaterialX `default` input through a no-image texture cache; this is
// still useful for constants and procedural/color graph nodes without adding a
// second image-loader owner.
bool EvaluateMaterialXStringToLightRtOpenPBR(const char* xml,
                                             const char* materialName,
                                             tydra::LightRtOpenPBRParams* out,
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
