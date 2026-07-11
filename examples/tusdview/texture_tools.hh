// SPDX-License-Identifier: Apache-2.0
// tusdview - wrapper around the vendored texture tools (src/external/textools:
// tir resize, texcomp block compression, texpipe mip pipeline, envmap IBL).
//
// This is the only tusdview header/TU pair that touches the textools C API, so
// the rest of the app stays independent of TINYUSDZ_WITH_TEXTOOLS. Every entry
// point returns false (leaving outputs untouched) when tusdview is built
// without TUSDVIEW_WITH_TEXTOOLS; callers fall back to the legacy paths.
#pragma once

#include <string>

#include "gpu_scene.hh"

namespace tusdview {

// True when built with the vendored texture tools.
bool TexToolsAvailable();

// High-quality resize of an RGBA8 image via tir. When `srgb` is set the RGB
// channels are decoded to linear, filtered there, and re-encoded (correct
// color-space filtering); alpha is filtered premultiplied in both cases.
bool TexToolsResizeRGBA8(light3d::Image* img, int dstW, int dstH, bool srgb,
                         std::string* err);

// Block-compress an RGBA8 image with texcomp. `format` selects BC1/BC3/BC7.
// On success fills `out` (format/width/height/data).
bool TexToolsCompress(const light3d::Image& img, bool srgb,
                      DrawCompressedFormat format, DrawCompressedImageCPU* out);

// Adapt a pre-compressed KTX2 block payload to what the device supports, without
// re-encoding from decoded texels. `srcIsUni` = the blocks are the tinyexr uni
// intermediate (valid ASTC 4x4, transcodable to BC7/ETC2, decodable to RGBA8);
// otherwise `srcFmt` names the stored block format for a direct upload.
// On success fills exactly one of `*outCompressed` (device GPU blocks — upload
// as-is) or `*outRGBA` (uncompressed fallback where no compressed format fits),
// leaving the other empty. Returns false if the format cannot be adapted
// (e.g. a BC1/ETC2 payload on a device that supports neither and has no
// decoder). See DrawCompressedFormat for `srcFmt`.
bool TexToolsAdaptCompressed(const uint8_t* blocks, size_t nbytes, bool srcIsUni,
                             DrawCompressedFormat srcFmt, uint32_t w, uint32_t h,
                             const TextureCompressCaps& caps,
                             DrawCompressedImageCPU* outCompressed,
                             light3d::Image* outRGBA);

// How a texture is consumed by the materials; drives the content-aware mip
// build (sRGB filtering, alpha-coverage preservation, normal renormalize,
// packed-channel rules, wrap-aware filter edges).
struct TexUsage {
  bool srgb{false};
  bool normalMap{false};
  bool alphaTested{false};
  float alphaCutoff{0.5f};
  int wrapS{static_cast<int>(WrapMode::Repeat)};
  int wrapT{static_cast<int>(WrapMode::Repeat)};
  int channelOp[4]{0, 0, 0, 0};  // tp_channel_op per RGBA channel
};

// Build a content-aware mip chain for an RGBA8 base image via texpipe.
// `outMips` receives RGBA8 levels 1..N (level 0 = `base` is not included),
// halving down to 1x1.
bool TexToolsBuildMips(const light3d::Image& base, const TexUsage& usage,
                       std::vector<light3d::Image>* outMips);

// Resample a mirrored-ball or angular (light-probe) dome image (float RGB)
// into an equirect latlong image of outW x outW/2, so the IBL bake and env
// backgrounds can consume it. `format` matches DrawLightCPU::DomeTextureFormat
// (2 = MirroredBall, 3 = Angular); probe camera faces -Z (latlong center).
bool TexToolsProbeToEquirect(const float* rgb, int width, int height,
                             int format, int outW, std::vector<float>* outRgb,
                             int* outH);

// Bake split-sum DomeLight IBL from an HDR equirect (latlong) float RGB image
// via the vendored envmap library: GGX-prefiltered specular cube chain,
// diffuse irradiance cube, and the (env-independent, cached) BRDF LUT.
// `highQuality` selects 64px/5-level (vs 32px/4-level) cube faces.
bool TexToolsBuildDomeIbl(const float* equirectRgb, int width, int height,
                          bool highQuality, DomeIblCPU* out);

}  // namespace tusdview
