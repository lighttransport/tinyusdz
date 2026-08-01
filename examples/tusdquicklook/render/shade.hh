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

#include "options.hh"
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

  // Debug visualization. Anything but Shaded bypasses lighting entirely and
  // shows one input to the shading model directly.
  ShadingMode mode = ShadingMode::Shaded;
  // Distances used to normalize the Depth mode. Set per frame from the camera.
  float depth_near = 0.1f;
  float depth_far = 100.0f;
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
  // Interpolated tangent (xyz) and bitangent sign (w). Only meaningful when
  // has_tangent -- normal mapping uses this rather than screen derivatives,
  // which the raster path would compute differently from the tracer.
  float tangent[4] = {1, 0, 0, 1};
  bool has_tangent = false;
};

// A material with all of its maps resolved at one hit.
struct EvaluatedMaterial {
  float base[3] = {0.8f, 0.8f, 0.8f};
  float emissive[3] = {0, 0, 0};
  float roughness = 0.5f;
  float metallic = 0.0f;
  float alpha = 1.0f;
  float normal[3] = {0, 1, 0};  // shading normal, after normal mapping
};

// Resolve every texture slot at a hit. Shared by ShadeSurface and ShadeAov so
// the debug views show exactly what the shaded image is using.
void EvaluateMaterial(const ShadingContext& ctx, const SurfaceHit& hit,
                      const float view_dir[3], EvaluatedMaterial* out);

// Occlusion test callback: returns true when `origin -> direction` is blocked
// within `max_distance`. Supplied by the tracer so shading stays backend-free.
using OcclusionFn = bool (*)(void* user, const float origin[3],
                             const float direction[3], float max_distance);

// Shade one hit into linear RGB.
void ShadeSurface(const ShadingContext& ctx, const SurfaceHit& hit,
                  const float view_dir[3], OcclusionFn occluded, void* user,
                  float out_rgb[3]);

// Debug visualization of one shading input, in linear RGB. Used instead of
// ShadeSurface whenever ctx.mode != Shaded.
//
// PARITY: the GL fragment shader in gl_raster.cc reproduces these expressions
// exactly, and the smoke test holds the two backends to a tighter tolerance
// here than for shaded output (there is no lighting to disagree about). Any
// term either side would derive independently -- screen derivatives,
// gl_FragCoord.z -- must not appear.
//
// `eye_distance` is |eye - hit.position|, which the GL side recomputes from the
// interpolated world position rather than from the depth buffer.
void ShadeAov(const ShadingContext& ctx, const SurfaceHit& hit,
              const float view_dir[3], float eye_distance, float out_rgb[3]);

// Background for rays that miss everything, in linear RGB. In a debug mode
// this is black on both backends, so the AOV is not diluted by the gradient.
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
