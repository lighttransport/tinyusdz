// SPDX-License-Identifier: Apache-2.0
// tusdrender — environment / IBL precompute (diffuse irradiance, prefiltered
// specular, BRDF LUT, lat-long sampling) and legacy light collection.
#include <algorithm>
#include <cmath>
#include <vector>

#include "image-loader.hh"
#include "tusdr_context.hh"

namespace tusdr {

void AppendPowerCdf(std::vector<PreviewLight> *lights, std::vector<float> *cdf) {
  if (!lights || !cdf) return;
  cdf->clear();
  float sum = 0.0f;
  for (PreviewLight &light : *lights) {
    sum += std::max(0.0f, light.power);
    light.cdf = sum;
    cdf->push_back(sum);
  }
  if (sum > 0.0f) {
    for (float &v : *cdf) {
      v /= sum;
    }
    for (PreviewLight &light : *lights) {
      light.cdf /= sum;
    }
  }
}

Vec3 DirectionFromLatlong(float u, float v) {
  constexpr float kPi = 3.14159265358979323846f;
  float phi = (u - 0.5f) * 2.0f * kPi;
  float theta = v * kPi;
  float st = std::sin(theta);
  return Normalize(Vec3{st * std::sin(phi), std::cos(theta), st * std::cos(phi)});
}

void LatlongUV(const Vec3 &dir, float *u, float *v) {
  constexpr float kPi = 3.14159265358979323846f;
  Vec3 d = Normalize(dir);
  float phi = std::atan2(d.x, d.z);
  float theta = std::acos(ClampFloat(d.y, -1.0f, 1.0f));
  if (u) *u = phi / (2.0f * kPi) + 0.5f;
  if (v) *v = theta / kPi;
}

Vec3 SampleEnvNearest(const EnvImage &img, float u, float v) {
  if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) {
    return Vec3{0.0f, 0.0f, 0.0f};
  }
  u = u - std::floor(u);
  v = ClampFloat(v, 0.0f, 1.0f);
  int x = int(std::floor(u * float(img.width))) % img.width;
  int y = std::min(img.height - 1, int(std::floor(v * float(img.height))));
  return img.pixels[size_t(y) * size_t(img.width) + size_t(x)];
}

Vec3 SampleEnv(const EnvImage &img, const Vec3 &dir) {
  float u = 0.0f;
  float v = 0.0f;
  LatlongUV(dir, &u, &v);
  return SampleEnvNearest(img, u, v);
}

bool DecodeTextureToEnvImage(const RenderScene &scene, int texture_id,
                             EnvImage *out) {
  if (!out || texture_id < 0 || size_t(texture_id) >= scene.images.size()) {
    return false;
  }
  const tinyusdz::tydra::TextureImage &tex = scene.images[size_t(texture_id)];
  if (!tex.decoded || tex.width <= 0 || tex.height <= 0 || tex.channels <= 0 ||
      tex.buffer_id < 0 || size_t(tex.buffer_id) >= scene.buffers.size()) {
    return false;
  }
  const tinyusdz::tydra::BufferData &buf = scene.buffers[size_t(tex.buffer_id)];
  const size_t pixel_count = size_t(tex.width) * size_t(tex.height);
  const size_t channels = size_t(tex.channels);
  EnvImage img;
  img.width = tex.width;
  img.height = tex.height;
  img.pixels.resize(pixel_count);
  if (buf.componentType == tinyusdz::tydra::ComponentType::UInt8) {
    if (buf.data.size() < pixel_count * channels) return false;
    for (size_t i = 0; i < pixel_count; i++) {
      const uint8_t *p = buf.data.data() + i * channels;
      img.pixels[i] = Vec3{float(p[0]) / 255.0f,
                           float(p[std::min<size_t>(1, channels - 1)]) / 255.0f,
                           float(p[std::min<size_t>(2, channels - 1)]) / 255.0f};
    }
  } else if (buf.componentType == tinyusdz::tydra::ComponentType::Float) {
    if (buf.data.size() < pixel_count * channels * sizeof(float)) return false;
    const float *src = reinterpret_cast<const float *>(buf.data.data());
    for (size_t i = 0; i < pixel_count; i++) {
      const float *p = src + i * channels;
      img.pixels[i] = Vec3{p[0], p[std::min<size_t>(1, channels - 1)],
                           p[std::min<size_t>(2, channels - 1)]};
    }
  } else {
    return false;
  }
  *out = std::move(img);
  return true;
}

EnvImage ConvolveDiffuseEnv(const EnvImage &env, int width, int height) {
  constexpr float kPi = 3.14159265358979323846f;
  EnvImage out;
  out.width = width;
  out.height = height;
  out.pixels.resize(size_t(width) * size_t(height));
  const int theta_steps = 8;
  const int phi_steps = 16;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      Vec3 n = DirectionFromLatlong((float(x) + 0.5f) / float(width),
                                    (float(y) + 0.5f) / float(height));
      Vec3 up = (std::abs(n.y) < 0.999f) ? Vec3{0.0f, 1.0f, 0.0f}
                                         : Vec3{1.0f, 0.0f, 0.0f};
      Vec3 tangent = Normalize(Cross(up, n));
      Vec3 bitangent = Cross(n, tangent);
      Vec3 sum{0.0f, 0.0f, 0.0f};
      float weight_sum = 0.0f;
      for (int ti = 0; ti < theta_steps; ti++) {
        float theta = (float(ti) + 0.5f) / float(theta_steps) * 0.5f * kPi;
        float st = std::sin(theta);
        float ct = std::cos(theta);
        for (int pi = 0; pi < phi_steps; pi++) {
          float phi = (float(pi) + 0.5f) / float(phi_steps) * 2.0f * kPi;
          Vec3 h = Add(Add(Mul(tangent, std::cos(phi) * st),
                           Mul(bitangent, std::sin(phi) * st)),
                       Mul(n, ct));
          float w = ct * st;
          sum = Add(sum, Mul(SampleEnv(env, h), w));
          weight_sum += w;
        }
      }
      out.pixels[size_t(y) * size_t(width) + size_t(x)] = Div(sum, weight_sum);
    }
  }
  return out;
}

EnvImage PrefilterEnvMip(const EnvImage &env, int width, int height,
                         float roughness) {
  EnvImage out;
  out.width = width;
  out.height = height;
  out.pixels.resize(size_t(width) * size_t(height));
  int radius = std::max(0, int(std::round(roughness * 8.0f)));
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float u = (float(x) + 0.5f) / float(width);
      float v = (float(y) + 0.5f) / float(height);
      Vec3 sum{0.0f, 0.0f, 0.0f};
      float weight_sum = 0.0f;
      for (int yy = -radius; yy <= radius; yy++) {
        for (int xx = -radius; xx <= radius; xx++) {
          float du = float(xx) / float(std::max(1, width));
          float dv = float(yy) / float(std::max(1, height));
          float d2 = float(xx * xx + yy * yy);
          float sigma = std::max(1.0f, float(radius) * 0.5f);
          float w = radius == 0 ? 1.0f : std::exp(-d2 / (2.0f * sigma * sigma));
          sum = Add(sum, Mul(SampleEnvNearest(env, u + du, v + dv), w));
          weight_sum += w;
        }
      }
      out.pixels[size_t(y) * size_t(width) + size_t(x)] = Div(sum, weight_sum);
    }
  }
  return out;
}

float RadicalInverseVdc(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f;
}

Vec3 ImportanceSampleGGX(float xi0, float xi1, float roughness, const Vec3 &n) {
  constexpr float kPi = 3.14159265358979323846f;
  float a = roughness * roughness;
  float phi = 2.0f * kPi * xi0;
  float cos_theta = std::sqrt((1.0f - xi1) / (1.0f + (a * a - 1.0f) * xi1));
  float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
  Vec3 h{std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta};
  Vec3 up = (std::abs(n.z) < 0.999f) ? Vec3{0.0f, 0.0f, 1.0f}
                                     : Vec3{1.0f, 0.0f, 0.0f};
  Vec3 tangent = Normalize(Cross(up, n));
  Vec3 bitangent = Cross(n, tangent);
  return Normalize(Add(Add(Mul(tangent, h.x), Mul(bitangent, h.y)), Mul(n, h.z)));
}

float GeometrySchlickGGX(float ndotv, float roughness) {
  float a = roughness;
  float k = (a * a) / 2.0f;
  return ndotv / std::max(1.0e-6f, ndotv * (1.0f - k) + k);
}

float GeometrySmith(float ndotv, float ndotl, float roughness) {
  return GeometrySchlickGGX(ndotv, roughness) *
         GeometrySchlickGGX(ndotl, roughness);
}

void BuildBrdfLut(int size, IblCache *ibl) {
  if (!ibl || size <= 0) return;
  ibl->brdf_size = size;
  ibl->brdf_lut.assign(size_t(size) * size_t(size) * 2, 0.0f);
  const uint32_t sample_count = 64;
  for (int y = 0; y < size; y++) {
    float roughness = (float(y) + 0.5f) / float(size);
    for (int x = 0; x < size; x++) {
      float ndotv = (float(x) + 0.5f) / float(size);
      Vec3 v{std::sqrt(std::max(0.0f, 1.0f - ndotv * ndotv)), 0.0f, ndotv};
      float a = 0.0f;
      float b = 0.0f;
      Vec3 n{0.0f, 0.0f, 1.0f};
      for (uint32_t i = 0; i < sample_count; i++) {
        Vec3 h = ImportanceSampleGGX(float(i) / float(sample_count),
                                     RadicalInverseVdc(i), roughness, n);
        Vec3 l = Normalize(Sub(Mul(h, 2.0f * Dot(v, h)), v));
        float ndotl = std::max(0.0f, l.z);
        float ndoth = std::max(0.0f, h.z);
        float vdoth = std::max(0.0f, Dot(v, h));
        if (ndotl > 0.0f) {
          float g = GeometrySmith(ndotv, ndotl, roughness);
          float g_vis = (g * vdoth) / std::max(1.0e-6f, ndoth * ndotv);
          float fc = std::pow(1.0f - vdoth, 5.0f);
          a += (1.0f - fc) * g_vis;
          b += fc * g_vis;
        }
      }
      size_t ofs = (size_t(y) * size_t(size) + size_t(x)) * 2;
      ibl->brdf_lut[ofs + 0] = a / float(sample_count);
      ibl->brdf_lut[ofs + 1] = b / float(sample_count);
    }
  }
}

// Build the IBL pyramids (diffuse irradiance + prefiltered specular mips + BRDF
// LUT) from a ready environment map. Shared by the legacy (tydra texture) and
// next (file / dome light) paths.
bool BuildIblFromEnv(EnvImage &&env, IblCache *ibl) {
  if (!ibl || env.width <= 0 || env.height <= 0 || env.pixels.empty()) {
    return false;
  }
  ibl->env = std::move(env);
  ibl->diffuse = ConvolveDiffuseEnv(ibl->env, 32, 16);
  ibl->prefiltered.clear();
  const int levels = 5;
  for (int level = 0; level < levels; level++) {
    int w = std::max(4, 64 >> level);
    int h = std::max(2, 32 >> level);
    float roughness = float(level) / float(levels - 1);
    ibl->prefiltered.push_back(PrefilterEnvMip(ibl->env, w, h, roughness));
  }
  BuildBrdfLut(64, ibl);
  ibl->valid = true;
  return true;
}

bool BuildIblCache(const RenderScene &scene, const LightCache &lights,
                   IblCache *ibl) {
  if (!ibl || !lights.has_dome) return false;
  EnvImage env;
  if (!DecodeTextureToEnvImage(scene, lights.dome.texture_id, &env)) {
    return false;
  }
  return BuildIblFromEnv(std::move(env), ibl);
}

// Load a lat-long environment map (HDR float, or 8-bit) from a file into an
// EnvImage, scaled by `scale` (dome intensity * color). 8-bit is treated as
// already-linear (matching DecodeTextureToEnvImage).
bool LoadEnvImageFromFile(const std::string &path, const Vec3 &scale,
                          EnvImage *out) {
  auto res = tinyusdz::image::LoadImageFromFile(path);
  if (!res) {
    std::cerr << "WARN: failed to load environment map: " << path << "\n";
    return false;
  }
  const tinyusdz::Image &img = res.value().image;
  if (img.width <= 0 || img.height <= 0 || img.channels <= 0 ||
      img.data.empty()) {
    return false;
  }
  const size_t n = size_t(img.width) * size_t(img.height);
  const size_t ch = size_t(img.channels);
  EnvImage e;
  e.width = img.width;
  e.height = img.height;
  e.pixels.resize(n);
  if (img.format == tinyusdz::Image::PixelFormat::Float && img.bpp == 32) {
    if (img.data.size() < n * ch * sizeof(float)) return false;
    const float *src = reinterpret_cast<const float *>(img.data.data());
    for (size_t i = 0; i < n; i++) {
      const float *p = src + i * ch;
      e.pixels[i] = Vec3{p[0] * scale.x, p[std::min<size_t>(1, ch - 1)] * scale.y,
                         p[std::min<size_t>(2, ch - 1)] * scale.z};
    }
  } else if (img.bpp == 8) {
    if (img.data.size() < n * ch) return false;
    for (size_t i = 0; i < n; i++) {
      const uint8_t *p = img.data.data() + i * ch;
      e.pixels[i] =
          Vec3{float(p[0]) / 255.0f * scale.x,
               float(p[std::min<size_t>(1, ch - 1)]) / 255.0f * scale.y,
               float(p[std::min<size_t>(2, ch - 1)]) / 255.0f * scale.z};
    }
  } else {
    std::cerr << "WARN: unsupported environment-map format: " << path << "\n";
    return false;
  }
  *out = std::move(e);
  return true;
}

Vec3 SampleIblMip(const std::vector<EnvImage> &mips, const Vec3 &dir,
                  float roughness) {
  if (mips.empty()) return Vec3{0.0f, 0.0f, 0.0f};
  float f = ClampFloat(roughness, 0.0f, 1.0f) * float(mips.size() - 1);
  size_t l0 = size_t(std::floor(f));
  size_t l1 = std::min(mips.size() - 1, l0 + 1);
  float t = f - float(l0);
  return Lerp(SampleEnv(mips[l0], dir), SampleEnv(mips[l1], dir), t);
}

void SampleBrdfLut(const IblCache &ibl, float ndotv, float roughness, float *a,
                   float *b) {
  if (!a || !b || ibl.brdf_size <= 0 || ibl.brdf_lut.empty()) {
    if (a) *a = 1.0f;
    if (b) *b = 0.0f;
    return;
  }
  int x = std::min(ibl.brdf_size - 1,
                   std::max(0, int(ClampFloat(ndotv, 0.0f, 1.0f) *
                                   float(ibl.brdf_size))));
  int y = std::min(ibl.brdf_size - 1,
                   std::max(0, int(ClampFloat(roughness, 0.0f, 1.0f) *
                                   float(ibl.brdf_size))));
  size_t ofs = (size_t(y) * size_t(ibl.brdf_size) + size_t(x)) * 2;
  *a = ibl.brdf_lut[ofs + 0];
  *b = ibl.brdf_lut[ofs + 1];
}

float RectArea(const RenderLight &light) {
  return std::max(0.0f, light.width) * std::max(0.0f, light.height);
}

float DiskArea(const RenderLight &light) {
  constexpr float kPi = 3.14159265358979323846f;
  return kPi * std::max(0.0f, light.radius) * std::max(0.0f, light.radius);
}

float SphereArea(const RenderLight &light) {
  constexpr float kPi = 3.14159265358979323846f;
  return 4.0f * kPi * std::max(0.0f, light.radius) *
         std::max(0.0f, light.radius);
}

float CylinderArea(const RenderLight &light) {
  constexpr float kPi = 3.14159265358979323846f;
  return 2.0f * kPi * std::max(0.0f, light.radius) *
         std::max(0.0f, light.length);
}

Vec3 LightColor(const RenderLight &light) {
  float scale = light.intensity * std::pow(2.0f, light.exposure);
  return Mul(FromFloat3(light.color), scale);
}

void AddFiniteLight(const RenderLight &light, PreviewLight::Kind kind,
                    LightCache *cache) {
  if (!cache) return;
  PreviewLight dst;
  dst.kind = kind;
  dst.position = FromFloat3(light.position);
  dst.direction = Normalize(FromFloat3(light.direction));
  if (Length(dst.direction) < 1.0e-6f) {
    dst.direction = Vec3{0.0f, -1.0f, 0.0f};
  }
  dst.normal = Mul(dst.direction, -1.0f);
  dst.radiance = LightColor(light);
  dst.radius = light.radius;
  dst.width = light.width;
  dst.height = light.height;
  if (kind == PreviewLight::Kind::Sphere) {
    dst.area = SphereArea(light);
  } else if (kind == PreviewLight::Kind::Rect) {
    dst.area = RectArea(light);
  } else if (kind == PreviewLight::Kind::Disk) {
    dst.area = DiskArea(light);
  } else if (kind == PreviewLight::Kind::Cylinder) {
    dst.area = CylinderArea(light);
  }
  dst.power = std::max(0.0f, Luminance(dst.radiance) *
                                 std::max(1.0f, dst.area));
  cache->finite.push_back(dst);
}

void CollectLights(const RenderScene &scene, LightCache *cache) {
  if (!cache) return;
  for (const RenderLight &light : scene.lights) {
    switch (light.type) {
      case RenderLight::Type::Distant:
        AddFiniteLight(light, PreviewLight::Kind::Distant, cache);
        break;
      case RenderLight::Type::Point:
        AddFiniteLight(light, PreviewLight::Kind::Point, cache);
        break;
      case RenderLight::Type::Sphere:
        AddFiniteLight(light, PreviewLight::Kind::Sphere, cache);
        break;
      case RenderLight::Type::Rect:
        AddFiniteLight(light, PreviewLight::Kind::Rect, cache);
        break;
      case RenderLight::Type::Disk:
        AddFiniteLight(light, PreviewLight::Kind::Disk, cache);
        break;
      case RenderLight::Type::Cylinder:
        AddFiniteLight(light, PreviewLight::Kind::Cylinder, cache);
        break;
      case RenderLight::Type::Geometry:
        AddFiniteLight(light, PreviewLight::Kind::Mesh, cache);
        break;
      case RenderLight::Type::Dome:
        cache->has_dome = true;
        cache->dome.kind = PreviewLight::Kind::Dome;
        cache->dome.radiance = LightColor(light);
        cache->dome.power = std::max(0.0f, Luminance(cache->dome.radiance));
        cache->dome.texture_id = light.envmap_texture_id;
        cache->dome.texture_file = light.textureFile;
        cache->env_color = Add(cache->env_color, cache->dome.radiance);
        cache->env_cdf.clear();
        break;
      case RenderLight::Type::Portal:
        std::cerr << "WARN: PortalLight ignored: " << light.name << "\n";
        break;
    }
  }
  AppendPowerCdf(&cache->finite, &cache->finite_cdf);
  AppendPowerCdf(&cache->mesh, &cache->mesh_cdf);
}

}  // namespace tusdr
