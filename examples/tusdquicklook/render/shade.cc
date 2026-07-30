// SPDX-License-Identifier: Apache-2.0
#include "render/shade.hh"

#include <algorithm>
#include <cmath>

namespace tusdql {

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
  out->scene_radius = std::max(scene.bounds.Radius(), 1e-4f);
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

void ShadeBackground(const ShadingContext& ctx, const float direction[3],
                     float out_rgb[3]) {
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

  const QlMaterial& mat = MaterialFor(*ctx.scene, hit.material_id);

  float base[3] = {mat.base_color[0], mat.base_color[1], mat.base_color[2]};
  if (mat.base_color_tex >= 0 && hit.has_uv &&
      mat.base_color_tex < static_cast<int>(ctx.scene->textures.size())) {
    float rgba[4];
    SampleTexture(ctx.scene->textures[size_t(mat.base_color_tex)], hit.uv[0],
                  hit.uv[1], rgba);
    base[0] = rgba[0];
    base[1] = rgba[1];
    base[2] = rgba[2];
  }

  // v points from the surface toward the eye.
  float v[3] = {-view_dir[0], -view_dir[1], -view_dir[2]};
  Normalize(v);

  float n[3] = {hit.normal[0], hit.normal[1], hit.normal[2]};
  // Always shade the side we can see. Preview content is full of single-sided
  // geometry with inconsistent winding; flipping is far friendlier than showing
  // black holes.
  if (Dot(n, v) < 0.0f) {
    n[0] = -n[0];
    n[1] = -n[1];
    n[2] = -n[2];
  }

  const float alpha = std::max(1e-3f, mat.roughness * mat.roughness);
  const float n_dot_v = std::max(Dot(n, v), 1e-4f);

  // Metals take their reflectance from the base color; dielectrics use 4%.
  float f0[3];
  for (int i = 0; i < 3; i++) {
    f0[i] = 0.04f * (1.0f - mat.metallic) + base[i] * mat.metallic;
  }
  float diffuse_albedo[3];
  for (int i = 0; i < 3; i++) {
    diffuse_albedo[i] = base[i] * (1.0f - mat.metallic);
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

  // Ambient: a cheap hemispheric term so unlit faces are not pure black.
  const float up_component = ctx.y_up ? n[1] : n[2];
  const float hemi = 0.5f + 0.5f * up_component;
  for (int i = 0; i < 3; i++) {
    out_rgb[i] += diffuse_albedo[i] * ctx.ambient[i] * (0.4f + 0.6f * hemi);
    out_rgb[i] += mat.emissive[i];
  }
}

uint32_t PackLinearToArgb(const float rgb[3]) {
  const uint32_t r = static_cast<uint32_t>(LinearToSrgb(rgb[0]) * 255.0f + 0.5f);
  const uint32_t g = static_cast<uint32_t>(LinearToSrgb(rgb[1]) * 255.0f + 0.5f);
  const uint32_t b = static_cast<uint32_t>(LinearToSrgb(rgb[2]) * 255.0f + 0.5f);
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

}  // namespace tusdql
