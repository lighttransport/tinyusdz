// SPDX-License-Identifier: Apache-2.0
#include "dome_light.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <vector>

#include "image-loader.hh"
#include "texture_tools.hh"

namespace lusdview {
namespace {

float HalfToFloat(uint16_t h) {
  const uint32_t sign = (uint32_t(h & 0x8000u)) << 16;
  uint32_t exp = (h >> 10) & 0x1fu;
  uint32_t mant = h & 0x3ffu;
  uint32_t bits = 0;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      exp = 127u - 15u + 1u;
      while ((mant & 0x400u) == 0) { mant <<= 1; --exp; }
      bits = sign | (exp << 23) | ((mant & 0x3ffu) << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7f800000u | (mant << 13);
  } else {
    bits = sign | ((exp + 112u) << 23) | (mant << 13);
  }
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void InitDome(const char* name, DrawLightCPU* out) {
  *out = DrawLightCPU{};
  out->name = name;
  out->displayName = name;
  out->type = DrawLightCPU::Type::Dome;
  for (int i = 0; i < 16; ++i) out->transform[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

bool Bake(std::vector<float> rgb, int width, int height,
          DrawLightCPU::DomeTextureFormat format, bool highQuality,
          DrawLightCPU* out, std::string* err) {
  if (!TexToolsAvailable()) {
    if (err) *err = "environment-map tools are unavailable in this build";
    return false;
  }
  if (format == DrawLightCPU::DomeTextureFormat::MirroredBall ||
      format == DrawLightCPU::DomeTextureFormat::Angular) {
    std::vector<float> equirect;
    int equirectHeight = 0;
    const int equirectWidth = std::min(2048, std::max(256, width * 2));
    if (!TexToolsProbeToEquirect(rgb.data(), width, height,
                                static_cast<int>(format), equirectWidth,
                                &equirect, &equirectHeight)) {
      if (err) *err = "could not convert the selected probe mapping";
      return false;
    }
    rgb = std::move(equirect);
    width = equirectWidth;
    height = equirectHeight;
  }
  if (!TexToolsBuildDomeIbl(rgb.data(), width, height, highQuality, &out->ibl)) {
    if (err) *err = "environment-map IBL bake failed";
    return false;
  }
  return true;
}

bool DecodeFile(const std::string& path, std::vector<float>* rgb, int* width,
                int* height, std::string* err) {
  auto loaded = lightusd::image::LoadImageFromFile(path);
  if (!loaded) {
    if (err) *err = loaded.error();
    return false;
  }
  lightusd::Image& image = loaded.value().image;
  if (image.width <= 0 || image.height <= 0 || image.channels <= 0) {
    if (err) *err = "decoded image is empty";
    return false;
  }
  const int maxEdge = 4096;
  const float scale = std::min(1.0f, float(maxEdge) /
      float(std::max(image.width, image.height)));
  *width = std::max(1, int(std::floor(image.width * scale)));
  *height = std::max(1, int(std::floor(image.height * scale)));
  const size_t bytes = image.bpp == 32 ? 4u : image.bpp == 16 ? 2u :
                       image.bpp == 8 ? 1u : 0u;
  const size_t pixels = size_t(image.width) * size_t(image.height);
  if (bytes == 0 || pixels > (std::numeric_limits<size_t>::max)() /
                                  (size_t(image.channels) * bytes) ||
      image.data.size() < pixels * size_t(image.channels) * bytes) {
    if (err) *err = "unsupported image component layout";
    return false;
  }
  rgb->resize(size_t(*width) * size_t(*height) * 3u);
  auto channel = [&](int x, int y, int c) {
    const size_t i = (size_t(y) * size_t(image.width) + size_t(x)) *
                         size_t(image.channels) +
                     size_t(std::min(c, image.channels - 1));
    if (image.bpp == 32)
      return reinterpret_cast<const float*>(image.data.data())[i];
    if (image.bpp == 16)
      return HalfToFloat(reinterpret_cast<const uint16_t*>(image.data.data())[i]);
    const float v = float(image.data[i]) / 255.0f;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
  };
  for (int y = 0; y < *height; ++y) {
    const int sy = std::min(image.height - 1, int(int64_t(y) * image.height / *height));
    for (int x = 0; x < *width; ++x) {
      const int sx = std::min(image.width - 1, int(int64_t(x) * image.width / *width));
      const size_t d = (size_t(y) * size_t(*width) + size_t(x)) * 3u;
      const float r = channel(sx, sy, 0);
      (*rgb)[d] = r;
      (*rgb)[d + 1] = image.channels > 1 ? channel(sx, sy, 1) : r;
      (*rgb)[d + 2] = image.channels > 2 ? channel(sx, sy, 2) : r;
    }
  }
  return true;
}

}  // namespace

bool BuildDomeLightFromFile(const std::string& path,
                            DrawLightCPU::DomeTextureFormat format,
                            bool highQuality, DrawLightCPU* out,
                            std::string* err) {
  if (!out) return false;
  InitDome("File environment", out);
  out->textureFile = path;
  out->domeTextureFormat = format;
  std::vector<float> rgb;
  int width = 0, height = 0;
  if (!DecodeFile(path, &rgb, &width, &height, err)) return false;
  // UsdLux's `automatic` convention uses latlong for panoramic images and a
  // mirrored-ball probe for roughly square images.
  const DrawLightCPU::DomeTextureFormat bakeFormat =
      format != DrawLightCPU::DomeTextureFormat::Automatic
          ? format
          : (width >= (height * 3) / 2
                 ? DrawLightCPU::DomeTextureFormat::Latlong
                 : DrawLightCPU::DomeTextureFormat::MirroredBall);
  return Bake(std::move(rgb), width, height, bakeFormat, highQuality, out, err);
}

bool BuildWhiteFurnaceDome(bool highQuality, DrawLightCPU* out,
                           std::string* err) {
  if (!out) return false;
  InitDome("White furnace", out);
  out->domeTextureFormat = DrawLightCPU::DomeTextureFormat::Latlong;
  return Bake(std::vector<float>(32u * 16u * 3u, 1.0f), 32, 16,
              out->domeTextureFormat, highQuality, out, err);
}

bool BuildSunSkyDome(bool highQuality, DrawLightCPU* out, std::string* err) {
  if (!out) return false;
  InitDome("Sun and sky", out);
  out->domeTextureFormat = DrawLightCPU::DomeTextureFormat::Latlong;
  constexpr int width = 512, height = 256;
  std::vector<float> rgb(size_t(width) * height * 3u);
  const float sunAzimuth = 0.35f, sunElevation = 0.55f;
  const float sx = std::cos(sunElevation) * std::sin(sunAzimuth);
  const float sy = std::sin(sunElevation);
  const float sz = std::cos(sunElevation) * std::cos(sunAzimuth);
  for (int y = 0; y < height; ++y) {
    const float elevation = (0.5f - (float(y) + 0.5f) / height) * 3.14159265f;
    for (int x = 0; x < width; ++x) {
      const float azimuth = ((float(x) + 0.5f) / width - 0.5f) * 6.28318531f;
      const float dx = std::cos(elevation) * std::sin(azimuth);
      const float dy = std::sin(elevation);
      const float dz = std::cos(elevation) * std::cos(azimuth);
      const float horizon = std::pow(std::max(dy, 0.0f), 0.35f);
      float r = 0.22f + 0.28f * horizon;
      float g = 0.30f + 0.42f * horizon;
      float b = 0.42f + 0.72f * horizon;
      if (dy < 0.0f) r = g = b = 0.025f;
      const float sun = std::max(0.0f, dx * sx + dy * sy + dz * sz);
      const float disk = std::pow(sun, 24000.0f) * 80.0f;
      r += disk; g += disk * 0.82f; b += disk * 0.55f;
      const size_t i = (size_t(y) * width + size_t(x)) * 3u;
      rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
    }
  }
  return Bake(std::move(rgb), width, height, out->domeTextureFormat,
              highQuality, out, err);
}

void ApplyDomeLightControls(float intensity, float rotationDegrees,
                            const DrawLightCPU& base, DrawLightCPU* out) {
  if (!out) return;
  *out = base;
  const float scale = std::max(0.0f, intensity);
  for (int c = 0; c < 3; ++c) {
    out->effectiveColor[c] = base.effectiveColor[c] * scale;
    out->normalizedColor[c] = base.normalizedColor[c] * scale;
  }
  out->effectiveIntensity = base.effectiveIntensity * scale;
  const float a = rotationDegrees * 3.14159265f / 180.0f;
  const float c = std::cos(a), s = std::sin(a);
  // Pre-multiply an additional world-space yaw; zero keeps the authored dome
  // transform exactly intact (including its existing orientation).
  const float yaw[16] = {c, 0.0f, -s, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                         s, 0.0f, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float rotated[16]{};
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      for (int k = 0; k < 4; ++k) {
        rotated[column * 4 + row] +=
            yaw[k * 4 + row] * base.transform[column * 4 + k];
      }
    }
  }
  std::copy(rotated, rotated + 16, out->transform);
}

}  // namespace lusdview
