// SPDX-License-Identifier: Apache-2.0
// tusdview - vendored texture-tools wrapper implementation. See
// texture_tools.hh; keep all textools includes confined to this TU.
#include "texture_tools.hh"

#if defined(TUSDVIEW_WITH_TEXTOOLS)

#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "envmap.h"
#include "texcomp.h"
#include "texpipe.h"
#include "tir.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace tusdview {

namespace {

int TexToolsThreads() {
  const unsigned hw = std::thread::hardware_concurrency();
  return static_cast<int>(hw > 8 ? 8 : hw);
}

float SrgbToLinear(float c) {
  return (c <= 0.04045f) ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float c) {
  if (c <= 0.0f) return 0.0f;
  if (c >= 1.0f) return 1.0f;
  return (c <= 0.0031308f) ? 12.92f * c
                           : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

const float* SrgbDecodeLut() {
  static float lut[256];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < 256; ++i) {
      lut[i] = SrgbToLinear(static_cast<float>(i) / 255.0f);
    }
    init = true;
  }
  return lut;
}

bool ValidRGBA8(const light3d::Image& img) {
  return img.width > 0 && img.height > 0 && img.channels == 4 &&
         img.data.size() >= static_cast<size_t>(img.width) *
                                static_cast<size_t>(img.height) * 4u;
}

}  // namespace

bool TexToolsAvailable() { return true; }

bool TexToolsResizeRGBA8(light3d::Image* img, int dstW, int dstH, bool srgb,
                         std::string* err) {
  if (!img || dstW <= 0 || dstH <= 0 || !ValidRGBA8(*img)) {
    if (err) *err = "textools resize: invalid image or size";
    return false;
  }
  if (img->width == dstW && img->height == dstH) return true;

  tir_options opt;
  tir_options_init(&opt);
  opt.alpha = TIR_ALPHA_PREMULTIPLY;
  opt.clamp_min = 0.0f;
  opt.clamp_max = 1.0f;
  opt.num_threads = TexToolsThreads();

  const size_t srcPixels =
      static_cast<size_t>(img->width) * static_cast<size_t>(img->height);
  const size_t dstPixels =
      static_cast<size_t>(dstW) * static_cast<size_t>(dstH);

  tir_result r = TIR_SUCCESS;
  std::vector<uint8_t> dstBytes(dstPixels * 4);
  if (srgb) {
    // tir filters linearly; decode sRGB RGB -> linear floats around the
    // resize (alpha stays linear).
    const float* lut = SrgbDecodeLut();
    std::vector<float> srcF(srcPixels * 4);
    for (size_t i = 0; i < srcPixels; ++i) {
      srcF[i * 4 + 0] = lut[img->data[i * 4 + 0]];
      srcF[i * 4 + 1] = lut[img->data[i * 4 + 1]];
      srcF[i * 4 + 2] = lut[img->data[i * 4 + 2]];
      srcF[i * 4 + 3] = static_cast<float>(img->data[i * 4 + 3]) / 255.0f;
    }
    std::vector<float> dstF(dstPixels * 4);
    tir_image_view src{srcF.data(), img->width, img->height, 4, TIR_F32, 0};
    tir_image_view dst{dstF.data(), dstW, dstH, 4, TIR_F32, 0};
    r = tir_resize(nullptr, &src, &dst, &opt);
    if (TIR_OK(r)) {
      for (size_t i = 0; i < dstPixels; ++i) {
        auto enc = [](float v) -> uint8_t {
          return static_cast<uint8_t>(LinearToSrgb(v) * 255.0f + 0.5f);
        };
        dstBytes[i * 4 + 0] = enc(dstF[i * 4 + 0]);
        dstBytes[i * 4 + 1] = enc(dstF[i * 4 + 1]);
        dstBytes[i * 4 + 2] = enc(dstF[i * 4 + 2]);
        float a = dstF[i * 4 + 3];
        a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
        dstBytes[i * 4 + 3] = static_cast<uint8_t>(a * 255.0f + 0.5f);
      }
    }
  } else {
    tir_image_view src{img->data.data(), img->width, img->height, 4, TIR_U8,
                       0};
    tir_image_view dst{dstBytes.data(), dstW, dstH, 4, TIR_U8, 0};
    r = tir_resize(nullptr, &src, &dst, &opt);
  }
  if (!TIR_OK(r)) {
    if (err) *err = std::string("tir_resize: ") + tir_result_string(r);
    return false;
  }
  img->width = dstW;
  img->height = dstH;
  img->channels = 4;
  img->data = std::move(dstBytes);
  return true;
}

bool TexToolsCompress(const light3d::Image& img, bool srgb,
                      DrawCompressedFormat format,
                      DrawCompressedImageCPU* out) {
  if (!out || !ValidRGBA8(img)) return false;
  const uint32_t w = static_cast<uint32_t>(img.width);
  const uint32_t h = static_cast<uint32_t>(img.height);
  const uint8_t* rgba = img.data.data();

  size_t size = 0;
  tc_result r = TC_ERROR_UNSUPPORTED;
  std::vector<uint8_t> blocks;
  switch (format) {
    case DrawCompressedFormat::BC1: {
      tc_bc1_options o;
      tc_bc1_options_init(&o);
      o.srgb = srgb ? 1 : 0;
      size = tc_bc1_compressed_size(w, h);
      blocks.resize(size);
      r = tc_bc1_compress_rgba8(rgba, w, h, size_t(w) * 4u, &o, blocks.data(), size);
      break;
    }
    case DrawCompressedFormat::BC3: {
      tc_bc3_options o;
      tc_bc3_options_init(&o);
      o.srgb = srgb ? 1 : 0;
      size = tc_bc3_compressed_size(w, h);
      blocks.resize(size);
      r = tc_bc3_compress_rgba8(rgba, w, h, size_t(w) * 4u, &o, blocks.data(), size);
      break;
    }
    case DrawCompressedFormat::BC5: {
      // Two-channel (R,G) — normal maps. Encoder reads R,G of the RGBA8 input.
      tc_bc5_options o;
      tc_bc5_options_init(&o);
      size = tc_bc5_compressed_size(w, h);
      blocks.resize(size);
      r = tc_bc5_compress_rgba8(rgba, w, h, size_t(w) * 4u, &o, blocks.data(), size);
      break;
    }
    case DrawCompressedFormat::BC7: {
      tc_bc7_options o;
      tc_bc7_options_init(&o);
      o.quality = TC_BC7_QUALITY_FAST;
      o.srgb = srgb ? 1 : 0;
      o.threads = TexToolsThreads();
      size = tc_bc7_compressed_size(w, h);
      blocks.resize(size);
      r = tc_bc7_compress_rgba8(rgba, w, h, size_t(w) * 4u, &o, blocks.data(), size);
      break;
    }
    case DrawCompressedFormat::ETC2_RGB: {
      tc_etc2_options o;
      tc_etc2_options_init(&o);
      o.srgb = srgb ? 1 : 0;
      o.alpha = 0;
      size = tc_etc2_rgb_compressed_size(w, h);
      blocks.resize(size);
      r = tc_etc2_compress_rgba8(rgba, w, h, size_t(w) * 4u, &o, blocks.data(), size);
      break;
    }
    case DrawCompressedFormat::ETC2_RGBA: {
      tc_etc2_options o;
      tc_etc2_options_init(&o);
      o.srgb = srgb ? 1 : 0;
      o.alpha = 1;
      size = tc_etc2_rgba_compressed_size(w, h);
      blocks.resize(size);
      r = tc_etc2_compress_rgba8(rgba, w, h, size_t(w) * 4u, &o, blocks.data(), size);
      break;
    }
    case DrawCompressedFormat::ASTC_4x4: {
      tc_astc_options o;
      tc_astc_options_init(&o);
      o.block_x = 4;
      o.block_y = 4;
      o.srgb = srgb ? 1 : 0;
      o.threads = TexToolsThreads();
      size = tc_astc_compressed_size(w, h, &o);
      blocks.resize(size);
      r = tc_astc_compress_rgba8(rgba, w, h, size_t(w) * 4u, &o, blocks.data(), size);
      break;
    }
    case DrawCompressedFormat::BC6H:
      // HDR (float RGB) — the RGBA8 compress-on-load path can't feed BC6H.
      // Reserved for the HDR/EXR texture path (follow-up).
      return false;
    case DrawCompressedFormat::None:
      return false;
  }
  if (r != TC_SUCCESS) return false;
  out->format = format;
  out->width = img.width;
  out->height = img.height;
  out->data = std::move(blocks);
  return true;
}

bool TexToolsBuildMips(const light3d::Image& base, const TexUsage& usage,
                       std::vector<light3d::Image>* outMips) {
  if (!outMips || !ValidRGBA8(base)) return false;
  outMips->clear();
  if (base.width == 1 && base.height == 1) return true;  // no levels below 1x1

  auto tirEdge = [](int wrap) -> tir_edge_mode {
    switch (static_cast<WrapMode>(wrap)) {
      case WrapMode::Repeat: return TIR_EDGE_WRAP;
      case WrapMode::Mirror: return TIR_EDGE_REFLECT;
      case WrapMode::ClampToEdge:
      case WrapMode::ClampToBorder:
      default: return TIR_EDGE_CLAMP;
    }
  };

  const tp_content content =
      usage.normalMap ? TP_CONTENT_NORMAL
                      : (usage.alphaTested ? TP_CONTENT_ALPHA_TESTED
                                           : TP_CONTENT_COLOR);
  tp_options opt;
  tp_options_init(&opt, content, TP_CODEC_BC1);  // codec unused by mip build
  opt.srgb = usage.srgb ? 1 : 0;
  opt.srgb_aware = usage.srgb ? 1 : 0;
  opt.alpha = TIR_ALPHA_PREMULTIPLY;
  opt.alpha_test_threshold = usage.alphaCutoff;
  opt.normal_encoding = TIR_NORMAL_UNORM;
  opt.edge_x = tirEdge(usage.wrapS);
  opt.edge_y = tirEdge(usage.wrapT);
  for (int c = 0; c < 4; ++c) {
    opt.channel_op[c] = static_cast<tp_channel_op>(usage.channelOp[c]);
  }
  opt.threads = TexToolsThreads();

  tir_image_view baseView{const_cast<uint8_t*>(base.data.data()), base.width,
                          base.height, 4, TIR_U8, 0};
  tp_mip_chain chain{};
  const tp_result r = tp_build_mips(nullptr, &baseView, 1, &opt, &chain);
  if (!TP_OK(r)) return false;

  outMips->reserve(static_cast<size_t>(
      chain.num_levels > 1 ? chain.num_levels - 1 : 0));
  for (int l = 1; l < chain.num_levels; ++l) {
    const tp_surface& s = chain.level[l];
    light3d::Image img;
    img.width = s.width;
    img.height = s.height;
    img.channels = 4;
    img.data.resize(static_cast<size_t>(s.width) *
                    static_cast<size_t>(s.height) * 4u);
    auto quant = [](float v) -> uint8_t {
      v = v * 255.0f + 0.5f;
      if (v < 0.0f) v = 0.0f;
      if (v > 255.0f) v = 255.0f;
      return static_cast<uint8_t>(v);
    };
    for (int y = 0; y < s.height; ++y) {
      const float* row = reinterpret_cast<const float*>(
          reinterpret_cast<const uint8_t*>(s.data) +
          static_cast<size_t>(y) * s.stride);
      uint8_t* drow = img.data.data() +
                      static_cast<size_t>(y) * static_cast<size_t>(s.width) * 4u;
      for (int x = 0; x < s.width; ++x) {
        const float* px = row + static_cast<size_t>(x) * s.channels;
        // Normal-map chains are 3-channel; grayscale chains 1-2. Expand to
        // RGBA8 to match the DrawTextureCPU convention.
        const float c0 = px[0];
        const float c1 = s.channels > 1 ? px[1] : c0;
        const float c2 = s.channels > 2 ? px[2] : c0;
        const float c3 = s.channels > 3 ? px[3] : 1.0f;
        drow[x * 4 + 0] = quant(c0);
        drow[x * 4 + 1] = quant(c1);
        drow[x * 4 + 2] = quant(c2);
        drow[x * 4 + 3] = quant(c3);
      }
    }
    outMips->push_back(std::move(img));
  }
  tp_mip_chain_free(nullptr, &chain);
  return true;
}

bool TexToolsProbeToEquirect(const float* rgb, int width, int height,
                             int format, int outW, std::vector<float>* outRgb,
                             int* outH) {
  if (!rgb || !outRgb || !outH || width <= 0 || height <= 0 || outW < 4 ||
      (format != 2 && format != 3)) {
    return false;
  }
  const int W = outW;
  const int H = outW / 2;
  outRgb->assign(static_cast<size_t>(W) * H * 3u, 0.0f);

  auto sample = [&](float u, float v, float* out3) {
    // Bilinear, clamped to the probe image.
    const float fx = u * static_cast<float>(width) - 0.5f;
    const float fy = v * static_cast<float>(height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    auto at = [&](int x, int y) -> const float* {
      x = x < 0 ? 0 : (x >= width ? width - 1 : x);
      y = y < 0 ? 0 : (y >= height ? height - 1 : y);
      return rgb + (static_cast<size_t>(y) * width + x) * 3u;
    };
    const float* p00 = at(x0, y0);
    const float* p10 = at(x0 + 1, y0);
    const float* p01 = at(x0, y0 + 1);
    const float* p11 = at(x0 + 1, y0 + 1);
    for (int c = 0; c < 3; ++c) {
      const float a = p00[c] + (p10[c] - p00[c]) * tx;
      const float b = p01[c] + (p11[c] - p01[c]) * tx;
      out3[c] = a + (b - a) * ty;
    }
  };

  const float kPi = 3.14159265358979323846f;
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      float dir[3];
      em_uv_to_dir(EM_PROJ_EQUIRECT, 0,
                   (static_cast<float>(x) + 0.5f) / static_cast<float>(W),
                   (static_cast<float>(y) + 0.5f) / static_cast<float>(H), dir);
      float u = 0.5f, v = 0.5f;
      if (format == 2) {
        // Mirrored ball, photographed along -Z: the ball normal reflecting
        // `dir` back to the camera is normalize(dir + (0,0,1)); the normal's
        // xy IS the image position within the unit disc.
        float nx = dir[0], ny = dir[1], nz = dir[2] + 1.0f;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
          u = 0.5f + 0.5f * (nx / len);
          v = 0.5f - 0.5f * (ny / len);
        }
      } else {
        // Angular / light-probe (Debevec): image radius = angle from the
        // forward axis (-Z) / pi, full sphere at the disc edge.
        float z = dir[2];
        z = z < -1.0f ? -1.0f : (z > 1.0f ? 1.0f : z);
        const float theta = std::acos(-z);
        const float lxy = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
        const float r = (lxy > 1e-8f) ? (theta / kPi) : 0.0f;
        if (lxy > 1e-8f) {
          u = 0.5f + 0.5f * r * (dir[0] / lxy);
          v = 0.5f - 0.5f * r * (dir[1] / lxy);
        }
      }
      sample(u, v, outRgb->data() + (static_cast<size_t>(y) * W + x) * 3u);
    }
  }
  *outH = H;
  return true;
}

bool TexToolsBuildDomeIbl(const float* equirectRgb, int width, int height,
                          bool highQuality, DomeIblCPU* out) {
  if (!out || !equirectRgb || width <= 0 || height <= 0) return false;
  *out = DomeIblCPU{};

  em_image equirect{};
  equirect.proj = EM_PROJ_EQUIRECT;
  equirect.width = width;
  equirect.height = height;
  equirect.channels = 3;
  equirect.faces = 1;
  equirect.data = const_cast<float*>(equirectRgb);

  // Bound the per-sample source cost: prefilter/irradiance sample a modest
  // cube resample of the (possibly multi-K) equirect.
  const int srcCube = highQuality ? 256 : 128;
  em_image cube{};
  if (!EM_OK(em_convert(nullptr, &equirect, EM_PROJ_CUBE, srcCube, &cube))) {
    return false;
  }

  // Keep the full-resolution, un-clamped cube for env backgrounds (RT miss
  // shading samples it directly; clamping would visibly dim bright lights in
  // the backdrop).
  {
    const size_t n =
        static_cast<size_t>(cube.faces) * cube.width * cube.height * 3u;
    out->envCubeSize = cube.width;
    out->envCube.assign(cube.data, cube.data + n);
  }

  // Firefly clamp: tiny ultra-bright sources (night HDRI lanterns, sun disks)
  // alias badly through the modest importance-sample counts below. Clamp the
  // source radiance to a multiple of the mean luminance — standard preview
  // trade (slightly dims pin-point lights, kills speckle noise).
  {
    const size_t n =
        static_cast<size_t>(cube.faces) * cube.width * cube.height;
    double meanLum = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const float* t = cube.data + i * 3;
      meanLum += 0.2126 * t[0] + 0.7152 * t[1] + 0.0722 * t[2];
    }
    meanLum /= static_cast<double>(n);
    const float maxLum = static_cast<float>(meanLum) * 64.0f;
    if (maxLum > 0.0f) {
      for (size_t i = 0; i < n; ++i) {
        float* t = cube.data + i * 3;
        const float lum = 0.2126f * t[0] + 0.7152f * t[1] + 0.0722f * t[2];
        if (lum > maxLum) {
          const float s = maxLum / lum;
          t[0] *= s;
          t[1] *= s;
          t[2] *= s;
        }
      }
    }
  }

  const int specFace = highQuality ? 64 : 32;
  const int specLevels = highQuality ? 5 : 4;
  const int specSamples = highQuality ? 64 : 32;
  const int irrFace = highQuality ? 32 : 16;
  const int irrSamples = highQuality ? 512 : 256;
  const int lutSize = 64;

  std::vector<em_image> levels(static_cast<size_t>(specLevels));
  for (em_image& l : levels) l = em_image{};
  bool ok = EM_OK(em_prefilter_specular(nullptr, &cube, specFace, specLevels,
                                        specSamples, levels.data()));
  // Irradiance is cosine-sampled; tiny ultra-bright sources (night HDRIs)
  // produce fireflies when sampled from the sharp cube. Box-average the cube
  // down first (energy-preserving, unlike em_convert's point resample), then
  // integrate with a higher sample count.
  em_image irrSrc{};
  em_image irr{};
  if (ok) {
    const int srcFs = cube.width;
    const int dstFs = highQuality ? 32 : 16;
    const int block = srcFs / dstFs;
    ok = EM_OK(em_image_alloc(nullptr, &irrSrc, EM_PROJ_CUBE, dstFs, dstFs, 3));
    if (ok && block >= 1) {
      const float invN = 1.0f / (static_cast<float>(block) * block);
      for (int f = 0; f < 6; ++f) {
        for (int y = 0; y < dstFs; ++y) {
          for (int x = 0; x < dstFs; ++x) {
            float acc[3] = {0, 0, 0};
            for (int sy = 0; sy < block; ++sy) {
              const float* row =
                  em_image_texel(&cube, f, x * block, y * block + sy);
              for (int sx = 0; sx < block; ++sx) {
                acc[0] += row[sx * 3 + 0];
                acc[1] += row[sx * 3 + 1];
                acc[2] += row[sx * 3 + 2];
              }
            }
            float* t = em_image_texel(&irrSrc, f, x, y);
            t[0] = acc[0] * invN;
            t[1] = acc[1] * invN;
            t[2] = acc[2] * invN;
          }
        }
      }
    }
  }
  if (ok) {
    ok = EM_OK(em_irradiance_cube(nullptr, &irrSrc, irrFace, irrSamples, &irr));
  }
  if (ok) {
    // Order-2 SH projection of the irradiance cube for the RT surface
    // ambient. Projection and shader evaluation use the same fixed 9-term
    // real-SH polynomial basis, so the axis convention cancels out.
    out->shIrradiance.assign(27, 0.0f);
    auto basis9 = [](const float d[3], float* b) {
      const float x = d[0], y = d[1], z = d[2];
      b[0] = 0.282095f;
      b[1] = 0.488603f * y;
      b[2] = 0.488603f * z;
      b[3] = 0.488603f * x;
      b[4] = 1.092548f * x * y;
      b[5] = 1.092548f * y * z;
      b[6] = 0.315392f * (3.0f * z * z - 1.0f);
      b[7] = 1.092548f * x * z;
      b[8] = 0.546274f * (x * x - y * y);
    };
    for (int f = 0; f < 6; ++f) {
      for (int y = 0; y < irr.height; ++y) {
        for (int x = 0; x < irr.width; ++x) {
          float dir[3];
          em_uv_to_dir(EM_PROJ_CUBE, f,
                       (static_cast<float>(x) + 0.5f) / irr.width,
                       (static_cast<float>(y) + 0.5f) / irr.height, dir);
          const float sa =
              em_texel_solid_angle(EM_PROJ_CUBE, irr.width, irr.height, f, x, y);
          const float* t = em_image_texel(&irr, f, x, y);
          float b[9];
          basis9(dir, b);
          for (int k = 0; k < 9; ++k) {
            const float w = b[k] * sa;
            out->shIrradiance[static_cast<size_t>(k) * 3 + 0] += t[0] * w;
            out->shIrradiance[static_cast<size_t>(k) * 3 + 1] += t[1] * w;
            out->shIrradiance[static_cast<size_t>(k) * 3 + 2] += t[2] * w;
          }
        }
      }
    }
  }
  if (ok) {
    out->specFaceSize = specFace;
    out->specLevels.resize(static_cast<size_t>(specLevels));
    for (int l = 0; l < specLevels; ++l) {
      const em_image& li = levels[static_cast<size_t>(l)];
      const size_t n = static_cast<size_t>(li.faces) * li.width * li.height * 3u;
      out->specLevels[static_cast<size_t>(l)].assign(li.data, li.data + n);
    }
    const size_t irrN = static_cast<size_t>(irr.faces) * irr.width * irr.height * 3u;
    out->irrFaceSize = irrFace;
    out->irradiance.assign(irr.data, irr.data + irrN);
    // BRDF LUT is environment-independent: compute once per process.
    static std::vector<float> s_lut;
    static int s_lutSize = 0;
    if (s_lutSize != lutSize) {
      s_lut.resize(static_cast<size_t>(lutSize) * lutSize * 2u);
      em_brdf_lut(lutSize, 1024, s_lut.data());
      s_lutSize = lutSize;
    }
    out->lutSize = lutSize;
    out->brdfLut = s_lut;
    out->valid = true;
  }
  for (em_image& l : levels) em_image_free(nullptr, &l);
  em_image_free(nullptr, &irr);
  em_image_free(nullptr, &irrSrc);
  em_image_free(nullptr, &cube);
  return out->valid;
}

}  // namespace tusdview

#else  // !TUSDVIEW_WITH_TEXTOOLS

namespace tusdview {

bool TexToolsAvailable() { return false; }

bool TexToolsResizeRGBA8(light3d::Image*, int, int, bool, std::string*) {
  return false;
}

bool TexToolsCompress(const light3d::Image&, bool, DrawCompressedFormat,
                      DrawCompressedImageCPU*) {
  return false;
}

bool TexToolsBuildMips(const light3d::Image&, const TexUsage&,
                       std::vector<light3d::Image>*) {
  return false;
}

bool TexToolsBuildDomeIbl(const float*, int, int, bool, DomeIblCPU*) {
  return false;
}

bool TexToolsProbeToEquirect(const float*, int, int, int, int,
                             std::vector<float>*, int*) {
  return false;
}

}  // namespace tusdview

#endif  // TUSDVIEW_WITH_TEXTOOLS
