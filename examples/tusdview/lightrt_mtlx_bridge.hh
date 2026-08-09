// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "gpu_scene.hh"

namespace tusdview {

constexpr int kLightRtOpenPBRVec4s =
    tinyusdz::tydra::kLightRtOpenPBRVec4s;
constexpr int kLightRtOpenPBRFloats =
    tinyusdz::tydra::kLightRtOpenPBRFloats;
// Floats 0-71 are the original 6-slot layout (slots 0-5 = base, metallic,
// roughness, normal, emissive, opacity). Floats 72-95 add UV transforms for
// slot 12 = occlusion, 13 = coat weight, 14 = coat color, 15 = coat roughness
// (slot*6 offsets, so they land at 72/78/84/90). Float 70/71 = occlusion
// tex scale/bias, 96/97/98 = occlusion / coat-weight / coat-roughness channel
// selectors, 99 = a second UV-set bitmask (1=occl, 2=coatW, 4=coatC, 8=coatR).
// Floats 100-123 carry scale/bias vec4s for coat weight, coat color, and coat
// roughness. 124-129 carry specular-color UV transform, 130 carries its UV set,
// 131 the specular-workflow flag, and 132-139 its scale/bias.
// 140-145 carry coat-normal UV transform, 146 its UV set, and 147-154 its
// scale/bias vectors.
constexpr int kRtMaterialTextureParamFloats = 155;
// Per-material texture-id slots in RtHostScene::matTex. 0-5 as above, then
// 6 = occlusion, 7 = coat weight, 8 = coat color, 9 = coat roughness,
// 10 = specular-workflow color.
// 11 = dedicated coat-normal map.
constexpr int kRtMaterialTexSlots = 12;
// Rows 20-24 add opacity sampling and scene-wide UDIM-atlas row selectors;
// rows 25-26 carry the independent roughness UV transform. Keep
// in lockstep with MaterialTexParam in every Vulkan mesh shader stage.
// Rows 27-28 carry the real-time PBR coat/occlusion constants.
// Rows 32-39 carry the UV transforms for the specular-color / coat-weight /
// coat-color / coat-roughness texture slots (two rows each, in that order);
// row 40 is (coatWeightChannel, coatRoughnessChannel, coatWeightUvSet,
// coatRoughnessUvSet), row 41 carries the remaining UV-set selectors, and rows
// 42-49 carry scale/bias for specular color and the three coat slots.
// Rows 50-53 carry coat-normal UV transform, scale/bias, UV set, and presence.
constexpr int kRasterMaterialTextureParamVec4s = 67;
constexpr int kRasterMaterialTextureParamFloats =
    kRasterMaterialTextureParamVec4s * 4;

// Bake loader-adapted shader params into Tydra's backend-neutral real-time PBR
// block. Raster and RT consume the same resulting constant fallback around live
// DrawMaterialCPU semantic texture slots.
void BakeRealtimePbrMaterial(DrawMaterialCPU* mat);

// Compatibility entry point retained for out-of-tree callers.
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
// floats. The order must match raytracer_kernel_src.txt.
void PackRtMaterialTextureParams(const DrawMaterialCPU& mat, float* dst);

// Pack the Vulkan raster/tessellation material texture SSBO layout. `dst` must
// hold kRasterMaterialTextureParamFloats floats. The order must match
// vk/shaders/mesh*.{vert,frag,tese}.
void PackRasterMaterialTextureParams(const DrawMaterialCPU& mat, float* dst);

}  // namespace tusdview
