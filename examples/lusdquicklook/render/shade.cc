// SPDX-License-Identifier: Apache-2.0
#include "render/shade.hh"

#include <algorithm>
#include <cmath>

namespace lusdql {

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline float Dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline void Normalize(float v[3]) {
  const float len = std::sqrt(Dot(v, v));
  if (len > 1e-20f) {
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
  }
}

// Same, but reports whether the vector was long enough to be meaningful, so a
// degenerate tangent frame can be skipped rather than producing garbage.
inline bool NormalizeChecked(float v[3]) {
  const float len = std::sqrt(Dot(v, v));
  if (len <= 1e-20f) return false;
  v[0] /= len;
  v[1] /= len;
  v[2] /= len;
  return true;
}

inline float SrgbToLinear(float c) {
  return c <= 0.04045f ? c / 12.92f
                       : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float LinearToSrgb(float c) {
  if (c <= 0.0f) return 0.0f;
  if (c >= 1.0f) return 1.0f;
  return c <= 0.0031308f ? c * 12.92f
                         : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// GGX / Trowbridge-Reitz normal distribution.
inline float DistributionGgx(float n_dot_h, float alpha) {
  const float a2 = alpha * alpha;
  const float d = n_dot_h * n_dot_h * (a2 - 1.0f) + 1.0f;
  return a2 / std::max(kPi * d * d, 1e-8f);
}

// Smith height-correlated visibility term (already divided by the 4*NoL*NoV of
// the microfacet denominator).
inline float VisibilitySmith(float n_dot_v, float n_dot_l, float alpha) {
  const float a2 = alpha * alpha;
  const float lv = n_dot_l * std::sqrt(n_dot_v * n_dot_v * (1.0f - a2) + a2);
  const float ll = n_dot_v * std::sqrt(n_dot_l * n_dot_l * (1.0f - a2) + a2);
  return 0.5f / std::max(lv + ll, 1e-8f);
}

inline void FresnelSchlick(const float f0[3], float v_dot_h, float out[3]) {
  const float f = std::pow(1.0f - std::min(v_dot_h, 1.0f), 5.0f);
  for (int i = 0; i < 3; i++) out[i] = f0[i] + (1.0f - f0[i]) * f;
}

const QlMaterial& MaterialFor(const QlScene& scene, int material_id) {
  static const QlMaterial kDefault{};
  if (material_id < 0 ||
      material_id >= static_cast<int>(scene.materials.size())) {
    return kDefault;
  }
  return scene.materials[size_t(material_id)];
}

}  // namespace

void BuildLightRig(const QlScene& scene, const float eye[3],
                   const float forward[3], const float right[3],
                   const float up[3], ShadingContext* out) {
  out->scene = &scene;
  out->y_up = scene.y_up;
  out->ambient[0] = 0.03f;
  out->ambient[1] = 0.035f;
  out->ambient[2] = 0.04f;
  out->scene_radius = std::max(scene.bounds.Radius(), 1e-4f);
  out->has_ground = false;
  out->ground_y = 0.0f;
  out->lights.clear();
  out->headlight_rig = false;

  // Authored lights win, but dome lights alone light nothing in this shader, so
  // they only contribute ambient.
  bool have_directional_or_punctual = false;
  for (const QlLight& l : scene.lights) {
    if (l.type == QlLight::Type::Dome) {
      // Fold the dome into ambient rather than dropping it entirely.
      for (int i = 0; i < 3; i++) {
        out->ambient[i] += l.color[i] * std::min(l.intensity, 4.0f) * 0.15f;
      }
      continue;
    }
    have_directional_or_punctual = true;
    out->lights.push_back(l);
    if (out->lights.size() >= 8) break;  // preview cap
  }

  if (have_directional_or_punctual) return;

  // No usable authored lights: build a three-point rig locked to the camera, so
  // spinning the object keeps it lit. This is the case for the large majority of
  // asset files, which author geometry and materials but no lighting.
  out->headlight_rig = true;

  auto add = [&](float dx, float dy, float dz, float r, float g, float b,
                 float intensity, bool shadow) {
    QlLight l;
    l.type = QlLight::Type::Distant;
    // The rig is expressed in camera basis; direction is where light travels.
    for (int i = 0; i < 3; i++) {
      l.direction[i] = forward[i] * dz + right[i] * dx + up[i] * dy;
    }
    Normalize(l.direction);
    l.color[0] = r;
    l.color[1] = g;
    l.color[2] = b;
    l.intensity = intensity;
    l.casts_shadow = shadow;
    out->lights.push_back(l);
  };

  // Key from over the viewer's shoulder, fill from the other side, rim from
  // above and behind the subject. Slightly warm key, cool fill: reads as "lit"
  // without looking tinted.
  //
  // Each vector is where the light TRAVELS, in camera basis, so a light that
  // should sit above the subject travels downward (negative `up`). Getting that
  // sign wrong puts a blown-out lobe under the object.
  add(-0.45f, -0.55f, 1.0f, 1.00f, 0.97f, 0.92f, 3.0f, true);
  add(0.70f, -0.15f, 0.75f, 0.55f, 0.60f, 0.70f, 0.9f, false);
  add(0.25f, -0.35f, -1.0f, 0.70f, 0.72f, 0.80f, 0.7f, false);
  (void)eye;
}

void SampleTexture(const QlTexture& tex, float u, float v, float out_rgba[4]) {
  out_rgba[0] = out_rgba[1] = out_rgba[2] = 0.0f;
  out_rgba[3] = 1.0f;
  if (!tex.valid()) return;

  // USD st has v=0 at the bottom; image rows run top-down.
  float su = u;
  float sv = 1.0f - v;

  auto wrap = [](float c, bool repeat) {
    if (repeat) {
      c = c - std::floor(c);
      return c;
    }
    return std::max(0.0f, std::min(1.0f, c));
  };
  su = wrap(su, tex.wrap_repeat_s);
  sv = wrap(sv, tex.wrap_repeat_t);

  const float fx = su * float(tex.width) - 0.5f;
  const float fy = sv * float(tex.height) - 0.5f;
  const int x0 = static_cast<int>(std::floor(fx));
  const int y0 = static_cast<int>(std::floor(fy));
  const float tx = fx - float(x0);
  const float ty = fy - float(y0);

  auto texel = [&](int x, int y, float out[4]) {
    if (tex.wrap_repeat_s) {
      x %= int(tex.width);
      if (x < 0) x += int(tex.width);
    } else {
      x = std::max(0, std::min(int(tex.width) - 1, x));
    }
    if (tex.wrap_repeat_t) {
      y %= int(tex.height);
      if (y < 0) y += int(tex.height);
    } else {
      y = std::max(0, std::min(int(tex.height) - 1, y));
    }
    const uint8_t* p =
        tex.rgba.data() + (size_t(y) * tex.width + size_t(x)) * 4;
    for (int i = 0; i < 4; i++) out[i] = float(p[i]) * (1.0f / 255.0f);
  };

  float c00[4], c10[4], c01[4], c11[4];
  texel(x0, y0, c00);
  texel(x0 + 1, y0, c10);
  texel(x0, y0 + 1, c01);
  texel(x0 + 1, y0 + 1, c11);

  for (int i = 0; i < 4; i++) {
    const float a = c00[i] + (c10[i] - c00[i]) * tx;
    const float b = c01[i] + (c11[i] - c01[i]) * tx;
    out_rgba[i] = a + (b - a) * ty;
  }

  if (tex.srgb) {
    for (int i = 0; i < 3; i++) out_rgba[i] = SrgbToLinear(out_rgba[i]);
  }
}

namespace {
constexpr float kBackgroundBottom[3] = {0.020f, 0.021f, 0.024f};
constexpr float kBackgroundTop[3] = {0.075f, 0.085f, 0.105f};
}  // namespace

void BackgroundGradient(float out_bottom[3], float out_top[3]) {
  for (int i = 0; i < 3; i++) {
    out_bottom[i] = kBackgroundBottom[i];
    out_top[i] = kBackgroundTop[i];
  }
}

// Roughness-selected sample from the CPU-built prefiltered chain, blended
// between two adjacent levels. GL does the identical lookup into the identical
// uploaded pixels -- nothing is prefiltered on the GPU.
void SampleEnvPrefiltered(const ShadingContext& ctx, const float dir[3],
                          float roughness, float out_rgb[3]) {
  out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
  if (!ctx.ibl) return;

  const int levels = QlScene::kEnvPrefilterLevels;
  const float lod =
      std::max(0.0f, std::min(1.0f, roughness)) * float(levels - 1);
  const int lo = std::min(int(lod), levels - 1);
  const int hi = std::min(lo + 1, levels - 1);
  const float t = lod - float(lo);

  float u, v;
  DirectionToLatLong(ctx, dir, &u, &v);

  const QlTexture* a = ctx.env_prefiltered[lo] ? ctx.env_prefiltered[lo]
                                               : ctx.env_tex;
  const QlTexture* b = ctx.env_prefiltered[hi] ? ctx.env_prefiltered[hi]
                                               : ctx.env_tex;
  if (!a || !b) return;

  float ca[4], cb[4];
  SampleTexture(*a, u, v, ca);
  SampleTexture(*b, u, v, cb);
  for (int i = 0; i < 3; i++) {
    out_rgb[i] = (ca[i] + (cb[i] - ca[i]) * t) * ctx.env_intensity;
  }
}

void DirectionToLatLong(const ShadingContext& ctx, const float dir[3],
                        float* out_u, float* out_v) {
  // Up axis follows the stage, so a Z-up scene does not get a sideways sky.
  const float up = ctx.y_up ? dir[1] : dir[2];
  const float x = ctx.y_up ? dir[0] : dir[0];
  const float z = ctx.y_up ? dir[2] : dir[1];

  float phi = std::atan2(z, x) + ctx.env_rotation;
  // Wrap into [0, 2pi) so the seam lands in the same place on both backends.
  const float two_pi = 2.0f * kPi;
  phi = phi - two_pi * std::floor(phi / two_pi);

  *out_u = phi / two_pi;
  // v = 1 at the zenith; SampleTexture flips v, so this lands row 0 at the top.
  *out_v = 0.5f + std::asin(std::max(-1.0f, std::min(1.0f, up))) / kPi;
}

void EvaluateEnvIrradiance(const ShadingContext& ctx, const float n[3],
                           float out_rgb[3]) {
  out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
  if (!ctx.ibl) return;

  // Ramamoorthi/Hanrahan irradiance reconstruction. The convolution constants
  // are folded into the coefficients at build time, so this is a plain
  // polynomial that the GLSL reproduces verbatim.
  const float x = n[0], y = n[1], z = n[2];
  const float basis[9] = {
      0.282095f,
      0.488603f * y, 0.488603f * z, 0.488603f * x,
      1.092548f * x * y, 1.092548f * y * z,
      0.315392f * (3.0f * z * z - 1.0f),
      1.092548f * x * z, 0.546274f * (x * x - y * y),
  };
  for (int c = 0; c < 3; c++) {
    float v = 0.0f;
    for (int i = 0; i < 9; i++) v += ctx.env_sh[i][c] * basis[i];
    out_rgb[c] = std::max(0.0f, v);
  }
}

void BuildEnvironment(const QlScene& scene, bool ibl_enabled,
                      ShadingContext* out) {
  out->ibl = false;
  out->env_tex = nullptr;
  for (int i = 0; i < QlScene::kEnvPrefilterLevels; i++) {
    out->env_prefiltered[i] = nullptr;
  }
  for (int i = 0; i < 9; i++) out->env_sh[i][0] = out->env_sh[i][1] =
      out->env_sh[i][2] = 0.0f;

  if (!ibl_enabled || !scene.has_env()) return;
  if (size_t(scene.env_texture) >= scene.textures.size()) return;
  const QlTexture& env = scene.textures[size_t(scene.env_texture)];
  if (!env.valid()) return;

  out->env_tex = &env;
  out->env_rotation = scene.env_rotation;
  out->env_intensity = scene.env_intensity;
  for (int i = 0; i < QlScene::kEnvPrefilterLevels; i++) {
    const int id = scene.env_prefiltered[i];
    if (id >= 0 && size_t(id) < scene.textures.size() &&
        scene.textures[size_t(id)].valid()) {
      out->env_prefiltered[i] = &scene.textures[size_t(id)];
    }
  }

  // Project to SH9. Deterministic scan over every texel, weighted by solid
  // angle -- not importance sampled, which would break both the thread
  // determinism and the CPU/GL parity the tests hold us to.
  double acc[9][3] = {};
  double total_weight = 0.0;
  const int w = int(env.width);
  const int h = int(env.height);
  for (int y = 0; y < h; y++) {
    // Texel row centre -> polar angle. Row 0 is the top of the image, which
    // DirectionToLatLong maps to v = 1, i.e. the zenith.
    const float v = 1.0f - (float(y) + 0.5f) / float(h);
    const float theta = (v - 0.5f) * kPi;      // -pi/2 .. pi/2
    const float sin_t = std::sin(theta);
    const float cos_t = std::cos(theta);
    // Solid angle shrinks toward the poles.
    const double row_weight = double(cos_t);

    for (int x = 0; x < w; x++) {
      const float u = (float(x) + 0.5f) / float(w);
      const float phi = u * 2.0f * kPi - out->env_rotation;

      float d[3];
      const float horiz = cos_t;
      if (out->y_up) {
        d[0] = horiz * std::cos(phi);
        d[1] = sin_t;
        d[2] = horiz * std::sin(phi);
      } else {
        d[0] = horiz * std::cos(phi);
        d[1] = horiz * std::sin(phi);
        d[2] = sin_t;
      }

      const uint8_t* p =
          env.rgba.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
      float rgb[3];
      for (int c = 0; c < 3; c++) {
        rgb[c] = float(p[c]) * (1.0f / 255.0f);
        if (env.srgb) rgb[c] = SrgbToLinear(rgb[c]);
        rgb[c] *= out->env_intensity;
      }

      const float basis[9] = {
          0.282095f,
          0.488603f * d[1], 0.488603f * d[2], 0.488603f * d[0],
          1.092548f * d[0] * d[1], 1.092548f * d[1] * d[2],
          0.315392f * (3.0f * d[2] * d[2] - 1.0f),
          1.092548f * d[0] * d[2], 0.546274f * (d[0] * d[0] - d[1] * d[1]),
      };
      for (int i = 0; i < 9; i++) {
        for (int c = 0; c < 3; c++) {
          acc[i][c] += double(basis[i]) * double(rgb[c]) * row_weight;
        }
      }
      total_weight += row_weight;
    }
  }

  if (total_weight <= 0.0) return;

  // Normalize to the sphere, then fold in the cosine-convolution constants so
  // the reconstruction above is a bare polynomial.
  const double norm = 4.0 * double(kPi) / total_weight;
  static const float kConv[9] = {
      3.141593f,                                  // l=0
      2.094395f, 2.094395f, 2.094395f,            // l=1
      0.785398f, 0.785398f, 0.785398f, 0.785398f, 0.785398f,  // l=2
  };
  for (int i = 0; i < 9; i++) {
    for (int c = 0; c < 3; c++) {
      // The 1/pi turns irradiance into outgoing Lambertian radiance, which is
      // what the shading loop wants to multiply by the albedo.
      out->env_sh[i][c] =
          float(acc[i][c] * norm) * kConv[i] * (1.0f / kPi);
    }
  }
  out->ibl = true;
}

void EvaluateMaterial(const ShadingContext& ctx, const SurfaceHit& hit,
                      const float view_dir[3], EvaluatedMaterial* out) {
  *out = EvaluatedMaterial{};
  if (!ctx.scene) return;

  const QlMaterial& mat = MaterialFor(*ctx.scene, hit.material_id);
  const auto& textures = ctx.scene->textures;
  const bool can_sample = hit.has_uv;

  auto sample = [&](int tex, float rgba[4]) -> bool {
    if (!can_sample || tex < 0 || tex >= static_cast<int>(textures.size())) {
      return false;
    }
    SampleTexture(textures[size_t(tex)], hit.uv[0], hit.uv[1], rgba);
    return true;
  };
  auto channel = [](const float rgba[4], uint8_t c) {
    return rgba[c < 4 ? c : 0];
  };

  float rgba[4];

  out->base[0] = mat.base_color[0];
  out->base[1] = mat.base_color[1];
  out->base[2] = mat.base_color[2];
  out->alpha = mat.opacity;
  if (sample(mat.base_color_tex, rgba)) {
    out->base[0] = rgba[0];
    out->base[1] = rgba[1];
    out->base[2] = rgba[2];
    // A base-colour map's alpha is the surface's alpha unless a dedicated
    // opacity map overrides it below.
    out->alpha = mat.opacity * rgba[3];
  }

  out->emissive[0] = mat.emissive[0];
  out->emissive[1] = mat.emissive[1];
  out->emissive[2] = mat.emissive[2];
  if (sample(mat.emissive_tex, rgba)) {
    out->emissive[0] = mat.emissive[0] * rgba[0];
    out->emissive[1] = mat.emissive[1] * rgba[1];
    out->emissive[2] = mat.emissive[2] * rgba[2];
  }

  out->roughness = mat.roughness;
  if (sample(mat.roughness_tex, rgba)) {
    out->roughness = channel(rgba, mat.roughness_channel);
  }
  out->metallic = mat.metallic;
  if (sample(mat.metallic_tex, rgba)) {
    out->metallic = channel(rgba, mat.metallic_channel);
  }
  if (sample(mat.opacity_tex, rgba)) {
    out->alpha = mat.opacity * channel(rgba, mat.opacity_channel);
  }

  out->roughness = std::max(0.02f, std::min(1.0f, out->roughness));
  out->metallic = std::max(0.0f, std::min(1.0f, out->metallic));
  out->alpha = std::max(0.0f, std::min(1.0f, out->alpha));

  // Shading normal: interpolated, flipped toward the viewer, then perturbed by
  // the normal map in the interpolated tangent frame.
  float v[3] = {-view_dir[0], -view_dir[1], -view_dir[2]};
  Normalize(v);
  float n[3] = {hit.normal[0], hit.normal[1], hit.normal[2]};
  if (Dot(n, v) < 0.0f) {
    n[0] = -n[0];
    n[1] = -n[1];
    n[2] = -n[2];
  }

  if (mat.normal_tex >= 0 && hit.has_tangent && sample(mat.normal_tex, rgba)) {
    // Tangent-space normal, [0,1] -> [-1,1]. The map is sampled linearly
    // (the loader forces srgb off for data slots).
    float tn[3] = {rgba[0] * 2.0f - 1.0f, rgba[1] * 2.0f - 1.0f,
                   rgba[2] * 2.0f - 1.0f};
    tn[0] *= mat.normal_scale;
    tn[1] *= mat.normal_scale;

    // Gram-Schmidt the tangent against the (already flipped) normal.
    float t[3] = {hit.tangent[0], hit.tangent[1], hit.tangent[2]};
    const float td = Dot(t, n);
    for (int i = 0; i < 3; i++) t[i] -= n[i] * td;
    if (NormalizeChecked(t)) {
      const float sign = hit.tangent[3] < 0.0f ? -1.0f : 1.0f;
      float b[3] = {(n[1] * t[2] - n[2] * t[1]) * sign,
                    (n[2] * t[0] - n[0] * t[2]) * sign,
                    (n[0] * t[1] - n[1] * t[0]) * sign};
      float m[3];
      for (int i = 0; i < 3; i++) {
        m[i] = t[i] * tn[0] + b[i] * tn[1] + n[i] * tn[2];
      }
      if (NormalizeChecked(m)) {
        n[0] = m[0];
        n[1] = m[1];
        n[2] = m[2];
      }
    }
  }

  out->normal[0] = n[0];
  out->normal[1] = n[1];
  out->normal[2] = n[2];
}

void ShadeAov(const ShadingContext& ctx, const SurfaceHit& hit,
              const float view_dir[3], float eye_distance, float out_rgb[3]) {
  out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
  if (!ctx.scene) return;

  // Every AOV reads the same resolved material the shaded path does, so a
  // debug view shows what shading is actually using -- including the maps.
  EvaluatedMaterial em;
  EvaluateMaterial(ctx, hit, view_dir, &em);

  switch (ctx.mode) {
    case ShadingMode::Albedo:
      out_rgb[0] = em.base[0];
      out_rgb[1] = em.base[1];
      out_rgb[2] = em.base[2];
      break;
    case ShadingMode::Normal:
      // The shading normal, so a normal map is visible here rather than the
      // smooth interpolated one.
      for (int i = 0; i < 3; i++) out_rgb[i] = em.normal[i] * 0.5f + 0.5f;
      break;
    case ShadingMode::Uv:
      // Untextured geometry has no UVs at all; show it as black rather than
      // as a bogus (0,0) corner sample.
      if (hit.has_uv) {
        out_rgb[0] = hit.uv[0];
        out_rgb[1] = hit.uv[1];
      }
      break;
    case ShadingMode::Roughness:
      out_rgb[0] = out_rgb[1] = out_rgb[2] = em.roughness;
      break;
    case ShadingMode::Metallic:
      out_rgb[0] = out_rgb[1] = out_rgb[2] = em.metallic;
      break;
    case ShadingMode::Depth: {
      // Distance from the eye, normalized into the camera's clip range and
      // inverted so near is bright. Computed from the hit position on both
      // backends -- never from a depth buffer, which would not match.
      const float span = std::max(1e-6f, ctx.depth_far - ctx.depth_near);
      float t = (eye_distance - ctx.depth_near) / span;
      t = std::max(0.0f, std::min(1.0f, t));
      out_rgb[0] = out_rgb[1] = out_rgb[2] = 1.0f - t;
      break;
    }
    case ShadingMode::Shaded:
      break;
  }
}

void ShadeBackground(const ShadingContext& ctx, const float direction[3],
                     float out_rgb[3]) {
  // Debug modes get a black background so the AOV reads on its own, and so the
  // two backends cannot disagree about a gradient that is not being measured.
  if (ctx.mode != ShadingMode::Shaded) {
    out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
    return;
  }

  // With an environment loaded, the background IS the environment -- showing a
  // gradient behind an IBL-lit object would look obviously wrong.
  if (ctx.ibl && ctx.env_tex) {
    float u, v;
    DirectionToLatLong(ctx, direction, &u, &v);
    float rgba[4];
    SampleTexture(*ctx.env_tex, u, v, rgba);
    for (int i = 0; i < 3; i++) out_rgb[i] = rgba[i] * ctx.env_intensity;
    return;
  }

  // Vertical gradient in the stage's up axis, so a Z-up scene does not get a
  // sideways sky.
  const float up_component = ctx.y_up ? direction[1] : direction[2];
  const float t = std::max(0.0f, std::min(1.0f, up_component * 0.5f + 0.5f));

  for (int i = 0; i < 3; i++) {
    out_rgb[i] =
        kBackgroundBottom[i] + (kBackgroundTop[i] - kBackgroundBottom[i]) * t;
  }
}

void ShadeSurface(const ShadingContext& ctx, const SurfaceHit& hit,
                  const float view_dir[3], OcclusionFn occluded, void* user,
                  float out_rgb[3]) {
  out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
  if (!ctx.scene) return;

  EvaluatedMaterial em;
  EvaluateMaterial(ctx, hit, view_dir, &em);
  const QlMaterial& mat = MaterialFor(*ctx.scene, hit.material_id);

  const float* base = em.base;

  // v points from the surface toward the eye.
  float v[3] = {-view_dir[0], -view_dir[1], -view_dir[2]};
  Normalize(v);

  // EvaluateMaterial already flipped the normal toward the viewer (preview
  // content is full of single-sided geometry with inconsistent winding) and
  // applied any normal map.
  float n[3] = {em.normal[0], em.normal[1], em.normal[2]};

  const float alpha = std::max(1e-3f, em.roughness * em.roughness);
  const float n_dot_v = std::max(Dot(n, v), 1e-4f);

  // Metals take their reflectance from the base color; dielectrics use 4%.
  float f0[3];
  for (int i = 0; i < 3; i++) {
    f0[i] = 0.04f * (1.0f - em.metallic) + base[i] * em.metallic;
  }
  float diffuse_albedo[3];
  for (int i = 0; i < 3; i++) {
    diffuse_albedo[i] = base[i] * (1.0f - em.metallic);
  }

  const float shadow_eps =
      std::max(ctx.scene_radius * 1e-4f, 1e-5f);
  float shadow_origin[3];
  for (int i = 0; i < 3; i++) shadow_origin[i] = hit.position[i] + n[i] * shadow_eps;

  for (const QlLight& light : ctx.lights) {
    float l[3];
    float attenuation = 1.0f;
    float distance = 1e30f;

    if (light.type == QlLight::Type::Distant) {
      // `direction` is where the light travels; l points toward the light.
      l[0] = -light.direction[0];
      l[1] = -light.direction[1];
      l[2] = -light.direction[2];
    } else {
      l[0] = light.position[0] - hit.position[0];
      l[1] = light.position[1] - hit.position[1];
      l[2] = light.position[2] - hit.position[2];
      const float d2 = std::max(Dot(l, l), 1e-8f);
      distance = std::sqrt(d2);
      l[0] /= distance;
      l[1] /= distance;
      l[2] /= distance;
      // Inverse-square, softened near the source so a light sitting on the
      // surface does not blow out the frame.
      const float soft = std::max(light.radius, ctx.scene_radius * 1e-3f);
      attenuation = 1.0f / std::max(d2, soft * soft);
    }

    const float n_dot_l = Dot(n, l);
    if (n_dot_l <= 0.0f) continue;

    if (ctx.shadows && light.casts_shadow && occluded) {
      const float max_dist =
          light.type == QlLight::Type::Distant
              ? ctx.scene_radius * 8.0f
              : std::max(distance - shadow_eps * 2.0f, 0.0f);
      if (max_dist > 0.0f && occluded(user, shadow_origin, l, max_dist)) {
        continue;
      }
    }

    float h[3] = {l[0] + v[0], l[1] + v[1], l[2] + v[2]};
    Normalize(h);
    const float n_dot_h = std::max(Dot(n, h), 0.0f);
    const float v_dot_h = std::max(Dot(v, h), 0.0f);

    float fresnel[3];
    FresnelSchlick(f0, v_dot_h, fresnel);
    const float dist_term = DistributionGgx(n_dot_h, alpha);
    const float vis = VisibilitySmith(n_dot_v, n_dot_l, alpha);

    const float radiance_scale = light.intensity * attenuation * n_dot_l;
    for (int i = 0; i < 3; i++) {
      const float spec = dist_term * vis * fresnel[i];
      const float diff = diffuse_albedo[i] * (1.0f - fresnel[i]) / kPi;
      out_rgb[i] += (diff + spec) * light.color[i] * radiance_scale;
    }
  }

  if (ctx.ibl) {
    // Real environment lighting replaces the flat ambient guess: SH9
    // irradiance for the diffuse lobe, and a roughness-selected level of the
    // prefiltered chain for the specular one.
    float irradiance[3];
    EvaluateEnvIrradiance(ctx, n, irradiance);

    float reflected[3];
    const float ndv = Dot(n, v);
    for (int i = 0; i < 3; i++) reflected[i] = 2.0f * ndv * n[i] - v[i];
    Normalize(reflected);

    float spec_env[3] = {0, 0, 0};
    SampleEnvPrefiltered(ctx, reflected, em.roughness, spec_env);

    // Single-scatter split-sum approximation with an analytic DFG fit, so no
    // BRDF LUT texture has to be built or kept in sync between backends.
    float fresnel_env[3];
    const float fade = std::pow(1.0f - std::min(std::max(ndv, 0.0f), 1.0f),
                                5.0f);
    for (int i = 0; i < 3; i++) {
      const float f90 = std::max(1.0f - em.roughness, f0[i]);
      fresnel_env[i] = f0[i] + (f90 - f0[i]) * fade;
    }

    for (int i = 0; i < 3; i++) {
      out_rgb[i] += diffuse_albedo[i] * irradiance[i];
      out_rgb[i] += spec_env[i] * fresnel_env[i];
    }
  } else {
    // Ambient: a cheap hemispheric term so unlit faces are not pure black.
    const float up_component = ctx.y_up ? n[1] : n[2];
    const float hemi = 0.5f + 0.5f * up_component;
    for (int i = 0; i < 3; i++) {
      out_rgb[i] += diffuse_albedo[i] * ctx.ambient[i] * (0.4f + 0.6f * hemi);
    }
  }

  for (int i = 0; i < 3; i++) out_rgb[i] += em.emissive[i];
}

uint32_t PackLinearToArgb(const float rgb[3]) {
  const uint32_t r = static_cast<uint32_t>(LinearToSrgb(rgb[0]) * 255.0f + 0.5f);
  const uint32_t g = static_cast<uint32_t>(LinearToSrgb(rgb[1]) * 255.0f + 0.5f);
  const uint32_t b = static_cast<uint32_t>(LinearToSrgb(rgb[2]) * 255.0f + 0.5f);
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

}  // namespace lusdql
