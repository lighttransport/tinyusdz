// SPDX-License-Identifier: Apache-2.0
// Backend-neutral real-time PBR material constants shared by Tydra consumers.
// Texture bindings remain in each renderer's scene representation; this block
// is the single typed source for the evaluated scalar/color material response.
#pragma once

#include <algorithm>

namespace tinyusdz {
namespace tydra {

constexpr int kLightRtOpenPBRVec4s = 14;
constexpr int kLightRtOpenPBRFloats = kLightRtOpenPBRVec4s * 4;

struct RealtimePbrMaterial {
  float baseWeight{1.0f};
  float baseColor[3]{0.8f, 0.8f, 0.8f};
  float diffuseRoughness{0.0f};
  float metalness{0.0f};
  float specularWeight{1.0f};
  float specularColor[3]{1.0f, 1.0f, 1.0f};
  float specularRoughness{0.3f};
  float specularIor{1.5f};
  float transmission{0.0f};
  float transmissionColor[3]{1.0f, 1.0f, 1.0f};
  float transmissionDepth{0.0f};
  float transmissionScatter[3]{0.0f, 0.0f, 0.0f};
  float transmissionScatterAnisotropy{0.0f};
  float subsurface{0.0f};
  float subsurfaceColor[3]{0.8f, 0.8f, 0.8f};
  float subsurfaceRadius[3]{1.0f, 1.0f, 1.0f};
  float subsurfaceScale{1.0f};
  float coatWeight{0.0f};
  float coatColor[3]{1.0f, 1.0f, 1.0f};
  float coatRoughness{0.1f};
  float coatIor{1.5f};
  float sheenWeight{0.0f};
  float sheenColor[3]{1.0f, 1.0f, 1.0f};
  float sheenRoughness{0.3f};
  float thinFilmWeight{0.0f};
  float thinFilmThicknessNm{0.0f};
  float thinFilmIor{1.5f};
  float emission{0.0f};
  float emissionColor[3]{1.0f, 1.0f, 1.0f};
  float normal[3]{0.0f, 0.0f, 1.0f};
  float opacity{1.0f};
  bool hasTextureInputs{false};
  bool hasNormalInput{false};
};

inline void ClampRealtimePbrMaterial(RealtimePbrMaterial* p) {
  if (!p) return;
  auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
  p->baseWeight = clamp01(p->baseWeight);
  p->metalness = clamp01(p->metalness);
  p->specularWeight = clamp01(p->specularWeight);
  p->specularRoughness = std::max(0.0f, p->specularRoughness);
  p->specularIor = std::max(1.0f, p->specularIor);
  p->transmission = clamp01(p->transmission);
  p->transmissionDepth = std::max(0.0f, p->transmissionDepth);
  p->subsurface = clamp01(p->subsurface);
  p->subsurfaceScale = std::max(0.0f, p->subsurfaceScale);
  p->coatWeight = clamp01(p->coatWeight);
  p->coatRoughness = std::max(0.0f, p->coatRoughness);
  p->coatIor = std::max(1.0f, p->coatIor);
  p->sheenWeight = clamp01(p->sheenWeight);
  p->sheenRoughness = std::max(0.0f, p->sheenRoughness);
  p->thinFilmWeight = clamp01(p->thinFilmWeight);
  p->thinFilmThicknessNm = std::max(0.0f, p->thinFilmThicknessNm);
  p->thinFilmIor = std::max(1.0f, p->thinFilmIor);
  p->emission = std::max(0.0f, p->emission);
  p->opacity = clamp01(p->opacity);
}

inline void StoreOpenPBR3(const float src[3], float* dst) {
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
}

inline void PackRealtimePbrMaterial(const RealtimePbrMaterial& p,
                                     bool valid, float alpha_mode,
                                     float alpha_cutoff, float* dst) {
  if (!dst) return;
  std::fill(dst, dst + kLightRtOpenPBRFloats, 0.0f);
  StoreOpenPBR3(p.baseColor, dst + 0);            dst[3] = p.baseWeight;
  StoreOpenPBR3(p.specularColor, dst + 4);        dst[7] = p.specularWeight;
  StoreOpenPBR3(p.transmissionColor, dst + 8);    dst[11] = p.transmission;
  StoreOpenPBR3(p.transmissionScatter, dst + 12); dst[15] = p.transmissionDepth;
  StoreOpenPBR3(p.subsurfaceColor, dst + 16);     dst[19] = p.subsurface;
  StoreOpenPBR3(p.subsurfaceRadius, dst + 20);    dst[23] = p.subsurfaceScale;
  StoreOpenPBR3(p.coatColor, dst + 24);           dst[27] = p.coatWeight;
  StoreOpenPBR3(p.sheenColor, dst + 28);          dst[31] = p.sheenWeight;
  StoreOpenPBR3(p.emissionColor, dst + 32);       dst[35] = p.emission;
  StoreOpenPBR3(p.normal, dst + 36);              dst[39] = p.opacity;
  dst[40] = p.metalness;
  dst[41] = p.diffuseRoughness;
  dst[42] = p.specularRoughness;
  dst[43] = p.specularIor;
  dst[44] = p.coatRoughness;
  dst[45] = p.coatIor;
  dst[46] = p.sheenRoughness;
  dst[47] = p.transmissionScatterAnisotropy;
  dst[48] = p.thinFilmWeight;
  dst[49] = p.thinFilmThicknessNm;
  dst[50] = p.thinFilmIor;
  dst[51] = valid ? 1.0f : 0.0f;
  dst[52] = p.hasTextureInputs ? 1.0f : 0.0f;
  dst[53] = p.hasNormalInput ? 1.0f : 0.0f;
  dst[54] = alpha_mode;
  dst[55] = alpha_cutoff;
}


// Compatibility aliases retain the LightRT-facing public spelling while new
// loaders and adapters use the renderer-neutral name above.
using LightRtOpenPBRParams = RealtimePbrMaterial;
using DrawLightRtOpenPBRCPU = RealtimePbrMaterial;

inline void ClampLightRtOpenPBRParams(LightRtOpenPBRParams* p) {
  ClampRealtimePbrMaterial(p);
}

inline void PackLightRtOpenPBRParams(const LightRtOpenPBRParams& p,
                                     bool valid, float alpha_mode,
                                     float alpha_cutoff, float* dst) {
  PackRealtimePbrMaterial(p, valid, alpha_mode, alpha_cutoff, dst);
}

}  // namespace tydra
}  // namespace tinyusdz
