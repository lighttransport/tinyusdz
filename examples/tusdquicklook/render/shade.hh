// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — direct-lighting shading.
//
// Lambert diffuse + GGX specular over a handful of lights. No global
// illumination: this is a previewer, and the goal is that every asset is
// instantly legible, not that it is physically converged.
#pragma once

#include <cstdint>
#include <vector>

#include "ql_scene.hh"

namespace tusdql {

struct ShadingContext {
  const QlScene* scene = nullptr;

  // Lights actually used for shading. When the stage authors none, this holds a
  // camera-relative headlight rig built by BuildLightRig().
  std::vector<QlLight> lights;
  bool headlight_rig = false;

  float ambient[3] = {0.03f, 0.035f, 0.04f};
  bool shadows = true;

  // Scene scale, used to offset shadow rays and size the ground plane.
  float scene_radius = 1.0f;
  float ground_y = 0.0f;
  bool has_ground = false;
  bool y_up = true;
};

// Pick the lights to shade with. Authored lights win; otherwise a key/fill/rim
// rig is generated in camera space so an unlit asset is still readable.
void BuildLightRig(const QlScene& scene, const float eye[3],
                   const float forward[3], const float right[3],
                   const float up[3], ShadingContext* out);

// Everything a hit needs to be shaded, filled in by the tracer.
struct SurfaceHit {
  float position[3] = {0, 0, 0};
  float normal[3] = {0, 1, 0};       // geometric or interpolated, normalized
  float geometric_normal[3] = {0, 1, 0};
  float uv[2] = {0, 0};
  bool has_uv = false;
  int material_id = -1;
  bool backfacing = false;
};

// Occlusion test callback: returns true when `origin -> direction` is blocked
// within `max_distance`. Supplied by the tracer so shading stays backend-free.
using OcclusionFn = bool (*)(void* user, const float origin[3],
                             const float direction[3], float max_distance);

// Shade one hit into linear RGB.
void ShadeSurface(const ShadingContext& ctx, const SurfaceHit& hit,
                  const float view_dir[3], OcclusionFn occluded, void* user,
                  float out_rgb[3]);

// Background for rays that miss everything, in linear RGB.
void ShadeBackground(const ShadingContext& ctx, const float direction[3],
                     float out_rgb[3]);

// The two gradient endpoints ShadeBackground interpolates between, in linear
// RGB. Exposed so the GL backend can reproduce the identical gradient instead
// of clearing to a flat colour.
void BackgroundGradient(float out_bottom[3], float out_top[3]);

// Bilinear texture fetch, sRGB-decoded to linear when the texture says so.
void SampleTexture(const QlTexture& tex, float u, float v, float out_rgba[4]);

// Linear -> sRGB, packed 0xAARRGGBB for a lightvg surface.
uint32_t PackLinearToArgb(const float rgb[3]);

}  // namespace tusdql
