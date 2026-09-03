// SPDX-License-Identifier: Apache-2.0
//
// lusdquicklook — offscreen GL 3.3 raster backend.
//
// A latency win, not a quality one: where the CPU tracer fills in progressively,
// this produces a full-resolution frame in one go. It renders into an FBO and
// reads back, so it composites through exactly the same lightui blit as the CPU
// path and needs no window-system integration.
//
// The forward shader mirrors render/shade.cc's BRDF (Lambert + GGX, same light
// rig) so switching backends changes the speed, not the look. No shadow maps in
// v1 — that is the visible difference from the CPU path.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "render/gl_context.hh"
#include "render/renderer.hh"
#include "render/shade.hh"

namespace lusdql {

namespace {

// ---- Minimal GL 3.3 core loader --------------------------------------------
// Sized for exactly what this backend calls. examples/common/glad is sized for
// lusdview and would be a much larger dependency for no benefit here.

using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLboolean = unsigned char;
using GLchar = char;
using GLintptr = long;
using GLsizeiptr = long;

constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_UNSIGNED_INT = 0x1405;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_REPEAT = 0x2901;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_OUT_OF_MEMORY = 0x0505;
constexpr GLenum GL_CONTEXT_LOST = 0x0507;  // GL 4.5 / KHR_robustness
constexpr GLenum GL_EXTENSIONS = 0x1F03;
constexpr GLenum GL_NUM_EXTENSIONS = 0x821D;
constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43;
constexpr GLenum GL_FRAMEBUFFER_SRGB = 0x8DB9;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;

// NVX_gpu_memory_info / ATI_meminfo VRAM queries.
constexpr GLenum GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049;
constexpr GLenum TEXTURE_FREE_MEMORY_ATI = 0x87FC;

struct Gl {
  void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
  void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
  void (*Clear)(GLbitfield) = nullptr;
  void (*Enable)(GLenum) = nullptr;
  void (*Disable)(GLenum) = nullptr;
  void (*DrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
  void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
  void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
  void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
  void (*BindBuffer)(GLenum, GLuint) = nullptr;
  void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
  void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
  void (*DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
  void (*BindVertexArray)(GLuint) = nullptr;
  void (*EnableVertexAttribArray)(GLuint) = nullptr;
  void (*DisableVertexAttribArray)(GLuint) = nullptr;
  void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                              const void*) = nullptr;
  GLuint (*CreateShader)(GLenum) = nullptr;
  void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
  void (*CompileShader)(GLuint) = nullptr;
  void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
  void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
  void (*DeleteShader)(GLuint) = nullptr;
  GLuint (*CreateProgram)() = nullptr;
  void (*AttachShader)(GLuint, GLuint) = nullptr;
  void (*LinkProgram)(GLuint) = nullptr;
  void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
  void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
  void (*UseProgram)(GLuint) = nullptr;
  void (*DeleteProgram)(GLuint) = nullptr;
  GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
  void (*Uniform1i)(GLint, GLint) = nullptr;
  void (*Uniform1f)(GLint, GLfloat) = nullptr;
  void (*Uniform3fv)(GLint, GLsizei, const GLfloat*) = nullptr;
  void (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;
  void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
  void (*DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
  void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
  void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
  void (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
  GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
  void (*GenRenderbuffers)(GLsizei, GLuint*) = nullptr;
  void (*DeleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
  void (*BindRenderbuffer)(GLenum, GLuint) = nullptr;
  void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
  void (*GenTextures)(GLsizei, GLuint*) = nullptr;
  void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
  void (*BindTexture)(GLenum, GLuint) = nullptr;
  void (*ActiveTexture)(GLenum) = nullptr;
  void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                     GLenum, const void*) = nullptr;
  void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
  void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
  const unsigned char* (*GetString)(GLenum) = nullptr;
  void (*GetIntegerv)(GLenum, GLint*) = nullptr;
  GLenum (*GetError)() = nullptr;
  void (*BlendFunc)(GLenum, GLenum) = nullptr;
  void (*DepthMask)(GLboolean) = nullptr;
  void (*VertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;

  bool Load(GlGetProcFn get);
};

bool Gl::Load(GlGetProcFn get) {
  if (!get) return false;
  bool ok = true;
  auto bind = [&](auto& fn, const char* name) {
    void* p = get(name);
    if (!p) ok = false;
    fn = reinterpret_cast<std::decay_t<decltype(fn)>>(p);
  };
#define BIND(name) bind(name, "gl" #name)
  BIND(Viewport); BIND(ClearColor); BIND(Clear); BIND(Enable); BIND(Disable);
  BIND(DrawElements); BIND(DrawArrays); BIND(GenBuffers); BIND(DeleteBuffers); BIND(BindBuffer);
  BIND(BufferData); BIND(GenVertexArrays); BIND(DeleteVertexArrays);
  BIND(BindVertexArray); BIND(EnableVertexAttribArray);
  BIND(DisableVertexAttribArray);
  BIND(VertexAttribPointer); BIND(CreateShader); BIND(ShaderSource);
  BIND(CompileShader); BIND(GetShaderiv); BIND(GetShaderInfoLog);
  BIND(DeleteShader); BIND(CreateProgram); BIND(AttachShader); BIND(LinkProgram);
  BIND(GetProgramiv); BIND(GetProgramInfoLog); BIND(UseProgram);
  BIND(DeleteProgram); BIND(GetUniformLocation); BIND(Uniform1i);
  BIND(Uniform1f); BIND(Uniform3fv); BIND(UniformMatrix4fv);
  BIND(GenFramebuffers); BIND(DeleteFramebuffers); BIND(BindFramebuffer);
  BIND(FramebufferTexture2D); BIND(FramebufferRenderbuffer);
  BIND(CheckFramebufferStatus); BIND(GenRenderbuffers); BIND(DeleteRenderbuffers);
  BIND(BindRenderbuffer); BIND(RenderbufferStorage); BIND(GenTextures);
  BIND(DeleteTextures); BIND(BindTexture); BIND(ActiveTexture);
  BIND(TexImage2D); BIND(TexParameteri);
  BIND(ReadPixels); BIND(GetString); BIND(GetIntegerv); BIND(GetError);
  BIND(BlendFunc); BIND(DepthMask); BIND(VertexAttrib4f);
#undef BIND
  return ok;
}

// ---- Shaders ---------------------------------------------------------------

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

uniform mat4 u_view_proj;

out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_tangent;

void main() {
  v_world = in_position;
  v_normal = in_normal;
  v_uv = in_uv;
  v_tangent = in_tangent;
  gl_Position = u_view_proj * vec4(in_position, 1.0);
}
)";

// Mirrors render/shade.cc: Lambert + GGX with the same Smith visibility and
// Schlick Fresnel, the same hemispheric ambient, and the same three-light rig.
const char* kFragmentShader = R"(#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_tangent;

uniform vec3 u_eye;
uniform vec3 u_base_color;
uniform vec3 u_emissive;
uniform float u_roughness;
uniform float u_metallic;
uniform int u_has_texture;
uniform sampler2D u_base_color_tex;

// Material maps. Each *_map uniform is 0 when the slot is unbound, so the
// shader falls back to the scalar exactly as EvaluateMaterial() does.
uniform int u_has_normal_map;
uniform int u_has_roughness_map;
uniform int u_has_metallic_map;
uniform int u_has_emissive_map;
uniform int u_has_opacity_map;
uniform sampler2D u_normal_tex;
uniform sampler2D u_roughness_tex;
uniform sampler2D u_metallic_tex;
uniform sampler2D u_emissive_tex;
uniform sampler2D u_opacity_tex;
uniform int u_roughness_channel;
uniform int u_metallic_channel;
uniform int u_opacity_channel;
uniform float u_normal_scale;
uniform int u_has_tangent;
uniform int u_alpha_mode;      // 0 opaque, 1 mask, 2 blend
uniform float u_alpha_cutoff;
uniform float u_opacity;

uniform int u_light_count;
uniform vec3 u_light_dir[8];    // direction the light travels
uniform vec3 u_light_color[8];
uniform float u_light_intensity[8];
uniform vec3 u_ambient;
uniform float u_up_is_y;

// Image-based lighting. The SH coefficients and the prefiltered levels are all
// computed on the CPU (BuildEnvironment / BlurEnvLevel) and handed over as-is,
// so GL never integrates or filters the environment itself.
uniform int u_has_ibl;
uniform vec3 u_env_sh[9];
uniform float u_env_rotation;
uniform float u_env_intensity;
uniform sampler2D u_env_level0;
uniform sampler2D u_env_level1;
uniform sampler2D u_env_level2;
uniform sampler2D u_env_level3;

// Debug AOV selector, matching lusdql::ShadingMode. Must reproduce ShadeAov()
// in shade.cc exactly; the smoke test compares the two backends per mode.
uniform int u_mode;          // 0 shaded, 1 albedo, 2 normal, 3 uv,
                             // 4 roughness, 5 metallic, 6 depth
uniform int u_has_uv;
uniform float u_depth_near;
uniform float u_depth_far;

out vec4 frag_color;

const float PI = 3.14159265359;

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-8);
}

float V_Smith(float NoV, float NoL, float a) {
  float a2 = a * a;
  float lv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
  float ll = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
  return 0.5 / max(lv + ll, 1e-8);
}

vec3 F_Schlick(vec3 f0, float VoH) {
  return f0 + (1.0 - f0) * pow(1.0 - min(VoH, 1.0), 5.0);
}

// Mirrors DirectionToLatLong() in shade.cc.
vec2 env_uv(vec3 d) {
  float up = mix(d.z, d.y, u_up_is_y);
  float zc = mix(d.y, d.z, u_up_is_y);
  float phi = atan(zc, d.x) + u_env_rotation;
  const float TWO_PI = 6.28318530718;
  phi = phi - TWO_PI * floor(phi / TWO_PI);
  // SampleTexture flips v, and so does the texture fetch below, so this
  // matches the CPU's (u, v) exactly.
  return vec2(phi / TWO_PI, 0.5 + asin(clamp(up, -1.0, 1.0)) / PI);
}

vec3 env_sample_level(int level, vec2 uv) {
  vec2 t = vec2(uv.x, 1.0 - uv.y);
  if (level <= 0) return texture(u_env_level0, t).rgb;
  if (level == 1) return texture(u_env_level1, t).rgb;
  if (level == 2) return texture(u_env_level2, t).rgb;
  return texture(u_env_level3, t).rgb;
}

// Roughness-blended lookup into the CPU-built chain; same index math as
// SampleEnvPrefiltered().
vec3 env_prefiltered(vec3 d, float roughness) {
  float lod = clamp(roughness, 0.0, 1.0) * 3.0;
  int lo = int(min(lod, 3.0));
  int hi = min(lo + 1, 3);
  float t = lod - float(lo);
  vec2 uv = env_uv(d);
  return mix(env_sample_level(lo, uv), env_sample_level(hi, uv), t) *
         u_env_intensity;
}

// SH9 irradiance reconstruction; same polynomial as EvaluateEnvIrradiance().
vec3 env_irradiance(vec3 n) {
  float b[9];
  b[0] = 0.282095;
  b[1] = 0.488603 * n.y;
  b[2] = 0.488603 * n.z;
  b[3] = 0.488603 * n.x;
  b[4] = 1.092548 * n.x * n.y;
  b[5] = 1.092548 * n.y * n.z;
  b[6] = 0.315392 * (3.0 * n.z * n.z - 1.0);
  b[7] = 1.092548 * n.x * n.z;
  b[8] = 0.546274 * (n.x * n.x - n.y * n.y);
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 9; i++) acc += u_env_sh[i] * b[i];
  return max(acc, vec3(0.0));
}

// Mirrors EvaluateMaterial() in shade.cc. Every slot falls back to its scalar
// when unbound, and the normal map is applied in the interpolated tangent
// frame -- never from screen derivatives, which the tracer cannot reproduce.
float pick_channel(vec4 c, int idx) {
  if (idx == 0) return c.r;
  if (idx == 1) return c.g;
  if (idx == 2) return c.b;
  return c.a;
}

void main() {
  // USD st has v=0 at the bottom, but the image rows uploaded to GL run
  // top-down, so t=0 is the top row. SampleTexture() on the CPU flips for this;
  // GL has to as well or every texture comes out mirrored vertically. v_uv
  // itself stays authored, so the UV debug view still shows the real values.
  vec2 tex_uv = vec2(v_uv.x, 1.0 - v_uv.y);

  vec3 base = u_base_color;
  float alpha = u_opacity;
  if (u_has_texture != 0) {
    vec4 bc = texture(u_base_color_tex, tex_uv);
    base = bc.rgb;
    alpha = u_opacity * bc.a;
  }

  float roughness = u_roughness;
  if (u_has_roughness_map != 0) {
    roughness = pick_channel(texture(u_roughness_tex, tex_uv),
                             u_roughness_channel);
  }
  float metallic = u_metallic;
  if (u_has_metallic_map != 0) {
    metallic = pick_channel(texture(u_metallic_tex, tex_uv), u_metallic_channel);
  }
  if (u_has_opacity_map != 0) {
    alpha = u_opacity * pick_channel(texture(u_opacity_tex, tex_uv),
                                     u_opacity_channel);
  }
  vec3 emissive = u_emissive;
  if (u_has_emissive_map != 0) {
    emissive = u_emissive * texture(u_emissive_tex, tex_uv).rgb;
  }

  roughness = clamp(roughness, 0.02, 1.0);
  metallic = clamp(metallic, 0.0, 1.0);
  alpha = clamp(alpha, 0.0, 1.0);

  // Cutout: below the threshold the surface is not there at all.
  if (u_alpha_mode == 1 && alpha < u_alpha_cutoff) discard;

  vec3 v = normalize(u_eye - v_world);
  vec3 n = normalize(v_normal);
  if (dot(n, v) < 0.0) n = -n;

  if (u_has_normal_map != 0 && u_has_tangent != 0) {
    vec3 tn = texture(u_normal_tex, tex_uv).rgb * 2.0 - 1.0;
    tn.xy *= u_normal_scale;
    vec3 t = v_tangent.xyz - n * dot(v_tangent.xyz, n);
    if (dot(t, t) > 1e-20) {
      t = normalize(t);
      vec3 b = cross(n, t) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
      vec3 m = t * tn.x + b * tn.y + n * tn.z;
      if (dot(m, m) > 1e-20) n = normalize(m);
    }
  }

  if (u_mode != 0) {
    vec3 aov = vec3(0.0);
    if (u_mode == 1) {
      aov = base;
    } else if (u_mode == 2) {
      aov = n * 0.5 + 0.5;
    } else if (u_mode == 3) {
      if (u_has_uv != 0) aov = vec3(v_uv, 0.0);
    } else if (u_mode == 4) {
      aov = vec3(roughness);
    } else if (u_mode == 5) {
      aov = vec3(metallic);
    } else if (u_mode == 6) {
      // Eye distance from the interpolated world position, never
      // gl_FragCoord.z: the CPU tracer has no depth buffer to match.
      float span = max(1e-6, u_depth_far - u_depth_near);
      float t = clamp((length(u_eye - v_world) - u_depth_near) / span,
                      0.0, 1.0);
      aov = vec3(1.0 - t);
    }
    frag_color = vec4(aov, 1.0);
    return;
  }

  float a = max(1e-3, roughness * roughness);
  float NoV = max(dot(n, v), 1e-4);

  vec3 f0 = mix(vec3(0.04), base, metallic);
  vec3 diffuse_albedo = base * (1.0 - metallic);

  vec3 color = vec3(0.0);
  for (int i = 0; i < u_light_count && i < 8; i++) {
    vec3 l = -u_light_dir[i];
    float NoL = dot(n, l);
    if (NoL <= 0.0) continue;
    vec3 h = normalize(l + v);
    float NoH = max(dot(n, h), 0.0);
    float VoH = max(dot(v, h), 0.0);
    vec3 F = F_Schlick(f0, VoH);
    vec3 spec = vec3(D_GGX(NoH, a) * V_Smith(NoV, NoL, a)) * F;
    vec3 diff = diffuse_albedo * (1.0 - F) / PI;
    color += (diff + spec) * u_light_color[i] * u_light_intensity[i] * NoL;
  }

  if (u_has_ibl != 0) {
    vec3 r = reflect(-v, n);
    vec3 spec_env = env_prefiltered(r, roughness);
    float fade = pow(1.0 - clamp(NoV, 0.0, 1.0), 5.0);
    vec3 f90 = max(vec3(1.0 - roughness), f0);
    vec3 fresnel_env = f0 + (f90 - f0) * fade;
    color += diffuse_albedo * env_irradiance(n);
    color += spec_env * fresnel_env;
  } else {
    float up_component = mix(n.z, n.y, u_up_is_y);
    float hemi = 0.5 + 0.5 * up_component;
    color += diffuse_albedo * u_ambient * (0.4 + 0.6 * hemi);
  }
  color += emissive;

  // Blend mode carries alpha out; everything else is opaque by this point
  // (cutout already discarded, opaque is 1.0 by definition).
  frag_color = vec4(color, u_alpha_mode == 2 ? alpha : 1.0);
}
)";

// Fullscreen background pass. Reconstructs the same primary-ray direction the
// CPU tracer generates and evaluates the same vertical gradient, so the two
// backends agree on the ~half of the frame that is usually background. Without
// this the GL path would clear to a flat colour and look obviously different.
const char* kBackgroundVertexShader = R"(#version 330 core
out vec2 v_ndc;
void main() {
  // Fullscreen triangle from gl_VertexID; no vertex buffer needed.
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
  v_ndc = p;
  gl_Position = vec4(p, 0.0, 1.0);
}
)";

const char* kBackgroundFragmentShader = R"(#version 330 core
in vec2 v_ndc;

uniform vec3 u_forward;
uniform vec3 u_right;
uniform vec3 u_up;
uniform float u_tan_half;
uniform float u_aspect;
uniform float u_up_is_y;
uniform vec3 u_bottom;
uniform vec3 u_top;

// With an environment loaded the background IS the environment, matching
// ShadeBackground(). Same mapping as the forward shader's env_uv().
uniform int u_has_env;
uniform sampler2D u_env_tex;
uniform float u_env_rotation;
uniform float u_env_intensity;

out vec4 frag_color;

const float PI = 3.14159265359;

void main() {
  vec3 dir = normalize(u_forward
                     + u_right * (v_ndc.x * u_tan_half * u_aspect)
                     + u_up * (v_ndc.y * u_tan_half));

  if (u_has_env != 0) {
    float up = mix(dir.z, dir.y, u_up_is_y);
    float zc = mix(dir.y, dir.z, u_up_is_y);
    float phi = atan(zc, dir.x) + u_env_rotation;
    const float TWO_PI = 6.28318530718;
    phi = phi - TWO_PI * floor(phi / TWO_PI);
    vec2 uv = vec2(phi / TWO_PI, 0.5 + asin(clamp(up, -1.0, 1.0)) / PI);
    vec3 c = texture(u_env_tex, vec2(uv.x, 1.0 - uv.y)).rgb * u_env_intensity;
    frag_color = vec4(c, 1.0);
    return;
  }

  float up_component = mix(dir.z, dir.y, u_up_is_y);
  float t = clamp(up_component * 0.5 + 0.5, 0.0, 1.0);
  frag_color = vec4(mix(u_bottom, u_top, t), 1.0);
}
)";

// ---------------------------------------------------------------------------

struct GlMesh {
  GLuint vao = 0;
  GLuint vbo_pos = 0;
  GLuint vbo_nrm = 0;
  GLuint vbo_uv = 0;
  GLuint vbo_tan = 0;
  GLuint ebo = 0;
  GLsizei index_count = 0;
  int material_id = -1;
  bool has_uv = false;
  bool has_tangent = false;
  // World-space centroid, captured at upload. meshes_ skips empty meshes, so
  // its indices do not line up with scene_->meshes -- carrying the centroid
  // here avoids having to map back.
  float centroid[3] = {0, 0, 0};
};

class GlRasterRenderer final : public Renderer {
 public:
  GlRasterRenderer(uint64_t required_vram, uint64_t max_gpu_mem,
                   GlProbeResult* probe)
      : required_vram_(required_vram),
        max_gpu_mem_(max_gpu_mem),
        probe_(probe) {}
  ~GlRasterRenderer() override { ReleaseGpu(); }

  bool Init(int width, int height, const RenderSettings& settings,
            std::string* err) override;
  void Resize(int width, int height) override;
  void SetScene(const QlScene* scene) override;
  void SyncScene() override;
  void SetCamera(const OrbitCamera& camera) override;
  void SetSettings(const RenderSettings& settings) override;
  RenderStatus RenderStep(double budget_ms) override;

  const uint32_t* Pixels() const override { return pixels_.data(); }
  int width() const override { return width_; }
  int height() const override { return height_; }
  const char* Name() const override { return "gl"; }
  const char* DeviceName() const override { return device_name_.c_str(); }

  // Free VRAM in bytes; 0 when the driver does not report it.
  uint64_t QueryFreeVram();

 private:
  bool CreateProgram(std::string* err);
  bool CreateTargets(std::string* err);
  void ReleaseTargets();
  void ReleaseGpu();
  void UploadScene();
  void BuildViewProj(float out[16]) const;
  uint64_t EstimateVram() const;
  bool CheckResourceBudget();

  std::unique_ptr<GlContext> ctx_;
  Gl gl_;
  RenderSettings settings_;
  const QlScene* scene_ = nullptr;
  OrbitCamera camera_;

  int width_ = 1;
  int height_ = 1;
  std::vector<uint32_t> pixels_;
  std::vector<uint8_t> row_scratch_;

  GLuint program_ = 0;
  GLuint bg_program_ = 0;
  GLuint empty_vao_ = 0;
  GLuint fbo_ = 0;
  GLuint color_tex_ = 0;
  GLuint depth_rb_ = 0;

  std::vector<GlMesh> meshes_;
  std::vector<GLuint> textures_;
  size_t uploaded_mesh_count_ = 0;

  uint64_t required_vram_ = 0;
  uint64_t max_gpu_mem_ = 0;
  GlProbeResult* probe_ = nullptr;

  ShadingContext shading_;
  bool dirty_ = true;
  std::string device_name_;
  std::string resource_error_;
};

bool GlRasterRenderer::Init(int width, int height,
                            const RenderSettings& settings, std::string* err) {
  settings_ = settings;

  ctx_ = CreateHeadlessGlContext(err);
  if (!ctx_) {
    if (probe_) {
      probe_->available = false;
      probe_->error = err ? *err : "no headless GL context";
    }
    return false;
  }
  if (!gl_.Load(ctx_->GetProcLoader())) {
    if (err) *err = "could not resolve the required GL 3.3 entry points";
    if (probe_) {
      probe_->available = false;
      probe_->error = err ? *err : "could not resolve GL entry points";
    }
    return false;
  }

  if (const unsigned char* r = gl_.GetString(GL_RENDERER)) {
    device_name_ = reinterpret_cast<const char*>(r);
  }

  const bool software_renderer =
      device_name_.find("llvmpipe") != std::string::npos ||
      device_name_.find("softpipe") != std::string::npos ||
      device_name_.find("Software") != std::string::npos;
  // Mesa's software drivers sometimes expose the NVX/ATI memory enums with a
  // process-dependent host-memory number. It is not VRAM headroom, so do not
  // turn that unstable value into an intermittent auto-backend decision.
  const uint64_t free_vram = software_renderer ? 0 : QueryFreeVram();

  if (probe_) {
    probe_->available = true;
    probe_->device = device_name_;
    if (const unsigned char* v = gl_.GetString(GL_VERSION)) {
      probe_->version = reinterpret_cast<const char*>(v);
    }
    probe_->required_vram = required_vram_;
    probe_->gpu_budget = max_gpu_mem_;
    probe_->free_vram = free_vram;
  }

  uint64_t effective_budget = max_gpu_mem_;
  constexpr uint64_t kDriverReserve = 256ull << 20;
  if (free_vram > 0) {
    effective_budget = free_vram > kDriverReserve
                           ? std::min(effective_budget,
                                      free_vram - kDriverReserve)
                           : 0;
  }
  if (required_vram_ > effective_budget) {
    if (err) {
      *err = "not enough GPU memory (estimate " +
             std::to_string(required_vram_ >> 20) + " MB, budget " +
             std::to_string(effective_budget >> 20) + " MB)";
    }
    if (probe_) {
      probe_->available = false;
      probe_->error = err ? *err : "not enough GPU memory";
      probe_->gpu_budget = effective_budget;
    }
    return false;
  }
  // Keep the effective (free-VRAM-aware) value for later scene/resize checks;
  // the initial configured cap is no longer the actual usable budget.
  max_gpu_mem_ = effective_budget;
  if (probe_) probe_->gpu_budget = effective_budget;

  if (!CreateProgram(err)) return false;

  width_ = std::max(1, width);
  height_ = std::max(1, height);
  if (!CreateTargets(err)) return false;
  return true;
}

bool GlRasterRenderer::CreateProgram(std::string* err) {
  auto compile = [&](GLenum type, const char* src, GLuint* out) {
    GLuint sh = gl_.CreateShader(type);
    gl_.ShaderSource(sh, 1, &src, nullptr);
    gl_.CompileShader(sh);
    GLint ok = 0;
    gl_.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[1024] = {0};
      gl_.GetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
      if (err) *err = std::string("shader compile failed: ") + log;
      gl_.DeleteShader(sh);
      return false;
    }
    *out = sh;
    return true;
  };

  GLuint vs = 0, fs = 0;
  if (!compile(GL_VERTEX_SHADER, kVertexShader, &vs)) return false;
  if (!compile(GL_FRAGMENT_SHADER, kFragmentShader, &fs)) {
    gl_.DeleteShader(vs);
    return false;
  }

  program_ = gl_.CreateProgram();
  gl_.AttachShader(program_, vs);
  gl_.AttachShader(program_, fs);
  gl_.LinkProgram(program_);
  gl_.DeleteShader(vs);
  gl_.DeleteShader(fs);

  GLint ok = 0;
  gl_.GetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    gl_.GetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
    if (err) *err = std::string("shader link failed: ") + log;
    gl_.DeleteProgram(program_);
    program_ = 0;
    return false;
  }

  // Background program.
  GLuint bvs = 0, bfs = 0;
  if (!compile(GL_VERTEX_SHADER, kBackgroundVertexShader, &bvs)) return false;
  if (!compile(GL_FRAGMENT_SHADER, kBackgroundFragmentShader, &bfs)) {
    gl_.DeleteShader(bvs);
    return false;
  }
  bg_program_ = gl_.CreateProgram();
  gl_.AttachShader(bg_program_, bvs);
  gl_.AttachShader(bg_program_, bfs);
  gl_.LinkProgram(bg_program_);
  gl_.DeleteShader(bvs);
  gl_.DeleteShader(bfs);
  gl_.GetProgramiv(bg_program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    gl_.GetProgramInfoLog(bg_program_, sizeof(log) - 1, nullptr, log);
    if (err) *err = std::string("background shader link failed: ") + log;
    gl_.DeleteProgram(bg_program_);
    bg_program_ = 0;
    return false;
  }

  // Core profile requires a bound VAO even for a vertex-buffer-free draw.
  gl_.GenVertexArrays(1, &empty_vao_);
  return true;
}

bool GlRasterRenderer::CreateTargets(std::string* err) {
  ReleaseTargets();

  gl_.GenTextures(1, &color_tex_);
  gl_.BindTexture(GL_TEXTURE_2D, color_tex_);
  // sRGB attachment: GL_FRAMEBUFFER_SRGB only performs the linear->sRGB encode
  // when the attachment is an sRGB format. With a plain RGBA8 target the shader
  // would write linear values straight out and the image would not match the
  // CPU path, which encodes explicitly in PackLinearToArgb.
  gl_.TexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width_, height_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
  gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  gl_.GenRenderbuffers(1, &depth_rb_);
  gl_.BindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
  gl_.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);

  gl_.GenFramebuffers(1, &fbo_);
  gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_tex_, 0);
  gl_.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rb_);
  if (gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    if (err) *err = "incomplete framebuffer";
    return false;
  }

  pixels_.assign(size_t(width_) * height_, 0xFF101014u);
  row_scratch_.assign(size_t(width_) * 4, 0);
  dirty_ = true;
  return true;
}

void GlRasterRenderer::ReleaseTargets() {
  if (fbo_) gl_.DeleteFramebuffers(1, &fbo_), fbo_ = 0;
  if (color_tex_) gl_.DeleteTextures(1, &color_tex_), color_tex_ = 0;
  if (depth_rb_) gl_.DeleteRenderbuffers(1, &depth_rb_), depth_rb_ = 0;
}

void GlRasterRenderer::ReleaseGpu() {
  if (!ctx_) return;
  ctx_->MakeCurrent();
  for (GlMesh& m : meshes_) {
    if (m.vbo_pos) gl_.DeleteBuffers(1, &m.vbo_pos);
    if (m.vbo_nrm) gl_.DeleteBuffers(1, &m.vbo_nrm);
    if (m.vbo_uv) gl_.DeleteBuffers(1, &m.vbo_uv);
    if (m.vbo_tan) gl_.DeleteBuffers(1, &m.vbo_tan);
    if (m.ebo) gl_.DeleteBuffers(1, &m.ebo);
    if (m.vao) gl_.DeleteVertexArrays(1, &m.vao);
  }
  meshes_.clear();
  if (!textures_.empty()) {
    gl_.DeleteTextures(GLsizei(textures_.size()), textures_.data());
    textures_.clear();
  }
  ReleaseTargets();
  if (empty_vao_) gl_.DeleteVertexArrays(1, &empty_vao_), empty_vao_ = 0;
  if (bg_program_) gl_.DeleteProgram(bg_program_), bg_program_ = 0;
  if (program_) gl_.DeleteProgram(program_), program_ = 0;
}

void GlRasterRenderer::Resize(int width, int height) {
  width = std::max(1, width);
  height = std::max(1, height);
  if (width == width_ && height == height_) return;
  width_ = width;
  height_ = height;
  if (ctx_) {
    ctx_->MakeCurrent();
    if (!CheckResourceBudget()) return;
    std::string err;
    CreateTargets(&err);
  }
}

void GlRasterRenderer::SetScene(const QlScene* scene) {
  scene_ = scene;
  if (ctx_) ctx_->MakeCurrent();
  for (GlMesh& m : meshes_) {
    if (m.vbo_pos) gl_.DeleteBuffers(1, &m.vbo_pos);
    if (m.vbo_nrm) gl_.DeleteBuffers(1, &m.vbo_nrm);
    if (m.vbo_uv) gl_.DeleteBuffers(1, &m.vbo_uv);
    if (m.vbo_tan) gl_.DeleteBuffers(1, &m.vbo_tan);
    if (m.ebo) gl_.DeleteBuffers(1, &m.ebo);
    if (m.vao) gl_.DeleteVertexArrays(1, &m.vao);
  }
  meshes_.clear();
  if (!textures_.empty()) {
    gl_.DeleteTextures(GLsizei(textures_.size()), textures_.data());
    textures_.clear();
  }
  uploaded_mesh_count_ = 0;
  dirty_ = true;
}

void GlRasterRenderer::SyncScene() {
  if (!scene_ || !ctx_) return;
  if (!CheckResourceBudget()) return;
  if (scene_->meshes.size() == uploaded_mesh_count_ &&
      textures_.size() == scene_->textures.size()) {
    return;
  }
  ctx_->MakeCurrent();
  UploadScene();
  dirty_ = true;
}

uint64_t GlRasterRenderer::EstimateVram() const {
  const uint64_t pixels = uint64_t(std::max(1, width_)) *
                          uint64_t(std::max(1, height_));
  constexpr uint64_t kBytesPerPixel = 8;  // RGBA8 + depth24, conservatively.
  const uint64_t framebuffer =
      pixels > std::numeric_limits<uint64_t>::max() / kBytesPerPixel
          ? std::numeric_limits<uint64_t>::max()
          : pixels * kBytesPerPixel;
  const uint64_t scene_bytes = scene_ ? scene_->ByteSize() : 0;
  if (scene_bytes > std::numeric_limits<uint64_t>::max() - framebuffer) {
    return std::numeric_limits<uint64_t>::max();
  }
  return scene_bytes + framebuffer;
}

bool GlRasterRenderer::CheckResourceBudget() {
  required_vram_ = EstimateVram();
  if (probe_) probe_->required_vram = required_vram_;
  if (required_vram_ <= max_gpu_mem_) {
    resource_error_.clear();
    return true;
  }
  resource_error_ = "not enough GPU memory (estimate " +
                    std::to_string(required_vram_ >> 20) + " MB, budget " +
                    std::to_string(max_gpu_mem_ >> 20) + " MB)";
  if (probe_) {
    probe_->available = false;
    probe_->error = resource_error_;
  }
  return false;
}

void GlRasterRenderer::UploadScene() {
  // Textures first, so a mesh's material can reference one.
  if (textures_.size() != scene_->textures.size()) {
    if (!textures_.empty()) {
      gl_.DeleteTextures(GLsizei(textures_.size()), textures_.data());
      textures_.clear();
    }
    textures_.resize(scene_->textures.size(), 0);
    for (size_t i = 0; i < scene_->textures.size(); i++) {
      const QlTexture& t = scene_->textures[i];
      if (!t.valid()) continue;
      gl_.GenTextures(1, &textures_[i]);
      gl_.BindTexture(GL_TEXTURE_2D, textures_[i]);
      // Preview textures are already sRGB-encoded 8-bit; let the sampler do the
      // decode so the shader works in linear like the CPU path.
      gl_.TexImage2D(GL_TEXTURE_2D, 0,
                     t.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, GLsizei(t.width),
                     GLsizei(t.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     t.rgba.data());
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        t.wrap_repeat_s ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        t.wrap_repeat_t ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      // The CPU sampler is bilinear without mip levels. Keeping only the base
      // level both matches it more closely and saves roughly one third of the
      // driver's texture allocation on a constrained GPU.
    }
  }

  for (size_t i = uploaded_mesh_count_; i < scene_->meshes.size(); i++) {
    const QlMesh& src = scene_->meshes[i];
    if (src.triangle_count() == 0) continue;

    GlMesh m;
    m.material_id = src.material_id;
    m.index_count = GLsizei(src.indices.size());
    m.has_uv = !src.uvs.empty();
    m.has_tangent = !src.tangents.empty();
    src.bounds.Center(m.centroid);

    gl_.GenVertexArrays(1, &m.vao);
    gl_.BindVertexArray(m.vao);

    gl_.GenBuffers(1, &m.vbo_pos);
    gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_pos);
    gl_.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(src.positions.size() * sizeof(float)),
                   src.positions.data(), GL_STATIC_DRAW);
    gl_.EnableVertexAttribArray(0);
    gl_.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);

    // A mesh without authored normals still needs the attribute bound. Reuse
    // the position VBO as a cheap defined fallback instead of allocating a
    // second CPU/GPU array just to carry a duplicate stream.
    if (src.normals.empty()) {
      gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_pos);
    } else {
      gl_.GenBuffers(1, &m.vbo_nrm);
      gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_nrm);
      gl_.BufferData(GL_ARRAY_BUFFER,
                     GLsizeiptr(src.normals.size() * sizeof(float)),
                     src.normals.data(), GL_STATIC_DRAW);
    }
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(1, 3, GL_FLOAT, 0, 0, nullptr);

    if (!src.uvs.empty()) {
      gl_.GenBuffers(1, &m.vbo_uv);
      gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_uv);
      gl_.BufferData(GL_ARRAY_BUFFER,
                     GLsizeiptr(src.uvs.size() * sizeof(float)),
                     src.uvs.data(), GL_STATIC_DRAW);
      gl_.EnableVertexAttribArray(2);
      gl_.VertexAttribPointer(2, 2, GL_FLOAT, 0, 0, nullptr);
    } else {
      gl_.DisableVertexAttribArray(2);
      gl_.VertexAttrib4f(2, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Tangents are only present for normal-mapped materials. The attribute
    // still has to be bound for every mesh or the shader reads garbage, so an
    // unmapped mesh gets a cheap constant frame it will never sample.
    if (!src.tangents.empty()) {
      gl_.GenBuffers(1, &m.vbo_tan);
      gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_tan);
      gl_.BufferData(GL_ARRAY_BUFFER,
                     GLsizeiptr(src.tangents.size() * sizeof(float)),
                     src.tangents.data(), GL_STATIC_DRAW);
      gl_.EnableVertexAttribArray(3);
      gl_.VertexAttribPointer(3, 4, GL_FLOAT, 0, 0, nullptr);
    } else {
      gl_.DisableVertexAttribArray(3);
      gl_.VertexAttrib4f(3, 1.0f, 0.0f, 0.0f, 1.0f);
    }

    gl_.GenBuffers(1, &m.ebo);
    gl_.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    gl_.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                   GLsizeiptr(src.indices.size() * sizeof(uint32_t)),
                   src.indices.data(), GL_STATIC_DRAW);

    gl_.BindVertexArray(0);
    meshes_.push_back(m);
  }
  uploaded_mesh_count_ = scene_->meshes.size();
}

void GlRasterRenderer::SetCamera(const OrbitCamera& camera) {
  if (!camera.Differs(camera_)) return;
  camera_ = camera;
  dirty_ = true;
}

void GlRasterRenderer::SetSettings(const RenderSettings& settings) {
  settings_ = settings;
  // The light rig is rebuilt from settings in RenderStep, and every uniform is
  // re-sent on a dirty frame, so a redraw is all this needs.
  dirty_ = true;
}

void GlRasterRenderer::BuildViewProj(float out[16]) const {
  float eye[3], right[3], up[3], fwd[3];
  camera_.Eye(eye);
  camera_.Basis(right, up, fwd);

  // Column-major for GL, row-vector-free: standard look-at then perspective.
  const float view[16] = {
      right[0], up[0], -fwd[0], 0.0f,
      right[1], up[1], -fwd[1], 0.0f,
      right[2], up[2], -fwd[2], 0.0f,
      -(right[0] * eye[0] + right[1] * eye[1] + right[2] * eye[2]),
      -(up[0] * eye[0] + up[1] * eye[1] + up[2] * eye[2]),
      (fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2]), 1.0f};

  const float aspect = float(width_) / float(std::max(1, height_));
  const float f = 1.0f / std::tan(camera_.fov_y * 0.5f);
  const float zn = camera_.near_clip;
  const float zf = camera_.far_clip;
  const float proj[16] = {f / aspect, 0, 0, 0,
                          0, f, 0, 0,
                          0, 0, (zf + zn) / (zn - zf), -1.0f,
                          0, 0, (2.0f * zf * zn) / (zn - zf), 0};

  // out = proj * view (column-major multiply).
  for (int c = 0; c < 4; c++) {
    for (int r = 0; r < 4; r++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++) s += proj[k * 4 + r] * view[c * 4 + k];
      out[c * 4 + r] = s;
    }
  }
}

uint64_t GlRasterRenderer::QueryFreeVram() {
  if (!ctx_ || !gl_.GetIntegerv) return 0;
  ctx_->MakeCurrent();
  GLint kb = 0;
  gl_.GetIntegerv(GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &kb);
  if (kb > 0) return uint64_t(kb) * 1024ull;
  GLint ati[4] = {0, 0, 0, 0};
  gl_.GetIntegerv(TEXTURE_FREE_MEMORY_ATI, ati);
  if (ati[0] > 0) return uint64_t(ati[0]) * 1024ull;
  return 0;
}

RenderStatus GlRasterRenderer::RenderStep(double /*budget_ms*/) {
  RenderStatus status;
  status.samples_target = 1;
  status.samples_done = 1;
  status.tiles_total = 1;
  status.tiles_done = 1;

  if (!ctx_ || !scene_) {
    status.converged = true;
    return status;
  }

  if (!resource_error_.empty()) {
    status.device_lost = true;
    status.error = resource_error_;
    status.converged = true;
    return status;
  }

  // The raster path is not progressive: one full-resolution frame per camera or
  // scene change, then nothing until something moves.
  if (!dirty_) {
    status.converged = true;
    return status;
  }

  // From here on the GPU can go away under us — a suspended session, a driver
  // reset, an evicted context. Every failure below is reported as device_lost
  // so the app demotes to the CPU tracer instead of showing a frozen frame.
  if (!ctx_->MakeCurrent()) {
    status.device_lost = true;
    status.error = "GL context could not be made current";
    status.converged = true;
    return status;
  }

  float eye[3], right[3], up[3], fwd[3];
  camera_.Eye(eye);
  camera_.Basis(right, up, fwd);
  BuildLightRig(*scene_, eye, fwd, right, up, &shading_);
  BuildEnvironment(*scene_, settings_.ibl, &shading_);

  gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  if (gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    status.device_lost = true;
    status.error = "GL framebuffer became incomplete";
    status.converged = true;
    return status;
  }
  gl_.Viewport(0, 0, width_, height_);
  gl_.Enable(GL_DEPTH_TEST);
  gl_.Disable(GL_CULL_FACE);  // preview content has inconsistent winding
  gl_.Enable(GL_FRAMEBUFFER_SRGB);

  gl_.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Background gradient, evaluated from the same primary-ray direction the CPU
  // tracer uses, so the two backends agree on the background.
  if (bg_program_ && empty_vao_) {
    gl_.Disable(GL_DEPTH_TEST);
    gl_.UseProgram(bg_program_);
    float bottom[3], top[3];
    BackgroundGradient(bottom, top);
    // ShadeBackground() returns black in a debug mode; collapsing both
    // gradient endpoints reproduces that without a second shader, and keeps
    // shade.cc the single source of truth for what the background is.
    if (settings_.mode != ShadingMode::Shaded) {
      for (int i = 0; i < 3; i++) bottom[i] = top[i] = 0.0f;
    }
    gl_.Uniform3fv(gl_.GetUniformLocation(bg_program_, "u_forward"), 1, fwd);
    gl_.Uniform3fv(gl_.GetUniformLocation(bg_program_, "u_right"), 1, right);
    gl_.Uniform3fv(gl_.GetUniformLocation(bg_program_, "u_up"), 1, up);
    gl_.Uniform1f(gl_.GetUniformLocation(bg_program_, "u_tan_half"),
                  std::tan(camera_.fov_y * 0.5f));
    gl_.Uniform1f(gl_.GetUniformLocation(bg_program_, "u_aspect"),
                  float(width_) / float(std::max(1, height_)));
    gl_.Uniform1f(gl_.GetUniformLocation(bg_program_, "u_up_is_y"),
                  shading_.y_up ? 1.0f : 0.0f);
    gl_.Uniform3fv(gl_.GetUniformLocation(bg_program_, "u_bottom"), 1, bottom);
    gl_.Uniform3fv(gl_.GetUniformLocation(bg_program_, "u_top"), 1, top);

    // The environment replaces the gradient, except in a debug mode where the
    // background is black on both backends.
    const bool bg_env = shading_.ibl && scene_->env_texture >= 0 &&
                        settings_.mode == ShadingMode::Shaded &&
                        size_t(scene_->env_texture) < textures_.size() &&
                        textures_[size_t(scene_->env_texture)] != 0;
    gl_.Uniform1i(gl_.GetUniformLocation(bg_program_, "u_has_env"),
                  bg_env ? 1 : 0);
    if (bg_env) {
      gl_.Uniform1i(gl_.GetUniformLocation(bg_program_, "u_env_tex"), 0);
      gl_.Uniform1f(gl_.GetUniformLocation(bg_program_, "u_env_rotation"),
                    shading_.env_rotation);
      gl_.Uniform1f(gl_.GetUniformLocation(bg_program_, "u_env_intensity"),
                    shading_.env_intensity);
      gl_.ActiveTexture(GL_TEXTURE0);
      gl_.BindTexture(GL_TEXTURE_2D, textures_[size_t(scene_->env_texture)]);
    }
    gl_.BindVertexArray(empty_vao_);
    gl_.DrawArrays(GL_TRIANGLES, 0, 3);
    gl_.BindVertexArray(0);
    gl_.Enable(GL_DEPTH_TEST);
  }

  gl_.UseProgram(program_);

  float view_proj[16];
  BuildViewProj(view_proj);
  gl_.UniformMatrix4fv(gl_.GetUniformLocation(program_, "u_view_proj"), 1, 0,
                       view_proj);
  gl_.Uniform3fv(gl_.GetUniformLocation(program_, "u_eye"), 1, eye);
  gl_.Uniform3fv(gl_.GetUniformLocation(program_, "u_ambient"), 1,
                 shading_.ambient);
  gl_.Uniform1f(gl_.GetUniformLocation(program_, "u_up_is_y"),
                shading_.y_up ? 1.0f : 0.0f);

  const int light_count =
      std::min<int>(8, static_cast<int>(shading_.lights.size()));
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_light_count"), light_count);
  for (int i = 0; i < light_count; i++) {
    char name[64];
    const QlLight& l = shading_.lights[size_t(i)];
    std::snprintf(name, sizeof(name), "u_light_dir[%d]", i);
    gl_.Uniform3fv(gl_.GetUniformLocation(program_, name), 1, l.direction);
    std::snprintf(name, sizeof(name), "u_light_color[%d]", i);
    gl_.Uniform3fv(gl_.GetUniformLocation(program_, name), 1, l.color);
    std::snprintf(name, sizeof(name), "u_light_intensity[%d]", i);
    gl_.Uniform1f(gl_.GetUniformLocation(program_, name), l.intensity);
  }

  const GLint u_base = gl_.GetUniformLocation(program_, "u_base_color");
  const GLint u_emis = gl_.GetUniformLocation(program_, "u_emissive");
  const GLint u_rough = gl_.GetUniformLocation(program_, "u_roughness");
  const GLint u_metal = gl_.GetUniformLocation(program_, "u_metallic");
  const GLint u_has_tex = gl_.GetUniformLocation(program_, "u_has_texture");
  const GLint u_has_uv = gl_.GetUniformLocation(program_, "u_has_uv");
  const GLint u_has_tan = gl_.GetUniformLocation(program_, "u_has_tangent");
  const GLint u_opacity = gl_.GetUniformLocation(program_, "u_opacity");
  const GLint u_alpha_mode = gl_.GetUniformLocation(program_, "u_alpha_mode");
  const GLint u_alpha_cutoff =
      gl_.GetUniformLocation(program_, "u_alpha_cutoff");
  const GLint u_normal_scale =
      gl_.GetUniformLocation(program_, "u_normal_scale");
  const GLint u_rough_ch =
      gl_.GetUniformLocation(program_, "u_roughness_channel");
  const GLint u_metal_ch =
      gl_.GetUniformLocation(program_, "u_metallic_channel");
  const GLint u_opacity_ch =
      gl_.GetUniformLocation(program_, "u_opacity_channel");
  const GLint u_has_nrm_map =
      gl_.GetUniformLocation(program_, "u_has_normal_map");
  const GLint u_has_rough_map =
      gl_.GetUniformLocation(program_, "u_has_roughness_map");
  const GLint u_has_metal_map =
      gl_.GetUniformLocation(program_, "u_has_metallic_map");
  const GLint u_has_emis_map =
      gl_.GetUniformLocation(program_, "u_has_emissive_map");
  const GLint u_has_opac_map =
      gl_.GetUniformLocation(program_, "u_has_opacity_map");

  // Fixed sampler units, matching the bind_map calls below.
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_base_color_tex"), 0);
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_normal_tex"), 1);
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_roughness_tex"), 2);
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_metallic_tex"), 3);
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_emissive_tex"), 4);
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_opacity_tex"), 5);

  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_mode"),
                static_cast<int>(settings_.mode));

  // IBL: 9 SH coefficients plus the prefiltered chain, all computed on the CPU.
  // Units 6-9 are reserved for the chain so they never collide with the
  // material maps bound per mesh on units 0-5.
  bool ibl_ready = shading_.ibl;
  if (ibl_ready) {
    for (int i = 0; i < QlScene::kEnvPrefilterLevels; i++) {
      const int id = scene_->env_prefiltered[i] >= 0
                         ? scene_->env_prefiltered[i]
                         : scene_->env_texture;
      if (id < 0 || size_t(id) >= textures_.size() ||
          textures_[size_t(id)] == 0) {
        ibl_ready = false;
        break;
      }
    }
  }
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_has_ibl"),
                ibl_ready ? 1 : 0);
  if (ibl_ready) {
    for (int i = 0; i < 9; i++) {
      char name[32];
      std::snprintf(name, sizeof(name), "u_env_sh[%d]", i);
      gl_.Uniform3fv(gl_.GetUniformLocation(program_, name), 1,
                     shading_.env_sh[i]);
    }
    gl_.Uniform1f(gl_.GetUniformLocation(program_, "u_env_rotation"),
                  shading_.env_rotation);
    gl_.Uniform1f(gl_.GetUniformLocation(program_, "u_env_intensity"),
                  shading_.env_intensity);
    static const char* kLevelNames[QlScene::kEnvPrefilterLevels] = {
        "u_env_level0", "u_env_level1", "u_env_level2", "u_env_level3"};
    for (int i = 0; i < QlScene::kEnvPrefilterLevels; i++) {
      const int unit = 6 + i;
      const int id = scene_->env_prefiltered[i] >= 0
                         ? scene_->env_prefiltered[i]
                         : scene_->env_texture;
      gl_.Uniform1i(gl_.GetUniformLocation(program_, kLevelNames[i]), unit);
      gl_.ActiveTexture(GL_TEXTURE0 + GLenum(unit));
      gl_.BindTexture(GL_TEXTURE_2D, textures_[size_t(id)]);
    }
  }
  gl_.Uniform1f(gl_.GetUniformLocation(program_, "u_depth_near"),
                camera_.near_clip);
  gl_.Uniform1f(gl_.GetUniformLocation(program_, "u_depth_far"),
                camera_.far_clip);

  static const QlMaterial kDefaultMaterial{};

  auto material_of = [&](const GlMesh& m) -> const QlMaterial& {
    return (m.material_id >= 0 &&
            m.material_id < static_cast<int>(scene_->materials.size()))
               ? scene_->materials[size_t(m.material_id)]
               : kDefaultMaterial;
  };

  // Bind a map to its texture unit, reporting whether the slot is live so the
  // shader can fall back to the scalar exactly as EvaluateMaterial does.
  auto bind_map = [&](int tex, int unit, GLint has_uniform, bool has_uv) {
    const bool live = has_uv && tex >= 0 && size_t(tex) < textures_.size() &&
                      textures_[size_t(tex)] != 0;
    gl_.Uniform1i(has_uniform, live ? 1 : 0);
    if (live) {
      gl_.ActiveTexture(GL_TEXTURE0 + GLenum(unit));
      gl_.BindTexture(GL_TEXTURE_2D, textures_[size_t(tex)]);
    }
    return live;
  };

  auto draw_mesh = [&](const GlMesh& m) {
    const QlMaterial& mat = material_of(m);

    gl_.Uniform3fv(u_base, 1, mat.base_color);
    gl_.Uniform3fv(u_emis, 1, mat.emissive);
    gl_.Uniform1f(u_rough, mat.roughness);
    gl_.Uniform1f(u_metal, mat.metallic);
    gl_.Uniform1f(u_opacity, mat.opacity);
    gl_.Uniform1i(u_alpha_mode, static_cast<int>(mat.alpha_mode));
    gl_.Uniform1f(u_alpha_cutoff, mat.alpha_cutoff);
    gl_.Uniform1f(u_normal_scale, mat.normal_scale);
    gl_.Uniform1i(u_rough_ch, mat.roughness_channel);
    gl_.Uniform1i(u_metal_ch, mat.metallic_channel);
    gl_.Uniform1i(u_opacity_ch, mat.opacity_channel);

    bind_map(mat.base_color_tex, 0, u_has_tex, m.has_uv);
    bind_map(mat.normal_tex, 1, u_has_nrm_map, m.has_uv);
    bind_map(mat.roughness_tex, 2, u_has_rough_map, m.has_uv);
    bind_map(mat.metallic_tex, 3, u_has_metal_map, m.has_uv);
    bind_map(mat.emissive_tex, 4, u_has_emis_map, m.has_uv);
    bind_map(mat.opacity_tex, 5, u_has_opac_map, m.has_uv);

    // The CPU side reports has_uv from the mesh's UV array, not from whether a
    // texture is bound, so the UV AOV must use the same test.
    gl_.Uniform1i(u_has_uv, m.has_uv ? 1 : 0);
    gl_.Uniform1i(u_has_tan, m.has_tangent ? 1 : 0);

    gl_.BindVertexArray(m.vao);
    gl_.DrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, nullptr);
  };

  // Opaque and cutout first with depth writes on, then blended geometry
  // back-to-front with writes off. The tracer resolves transparency by walking
  // the ray instead, so the two will not agree pixel-for-pixel on overlapping
  // transparent shells -- the smoke test gives blended assets their own,
  // looser tolerance for that reason.
  std::vector<const GlMesh*> blended;
  for (const GlMesh& m : meshes_) {
    if (material_of(m).alpha_mode == QlMaterial::AlphaMode::Blend) {
      blended.push_back(&m);
      continue;
    }
    draw_mesh(m);
  }

  if (!blended.empty()) {
    float eye_pos[3];
    camera_.Eye(eye_pos);
    auto centroid_distance = [&](const GlMesh* m) {
      const float dx = m->centroid[0] - eye_pos[0];
      const float dy = m->centroid[1] - eye_pos[1];
      const float dz = m->centroid[2] - eye_pos[2];
      return dx * dx + dy * dy + dz * dz;
    };
    std::sort(blended.begin(), blended.end(),
              [&](const GlMesh* a, const GlMesh* b) {
                return centroid_distance(a) > centroid_distance(b);
              });

    gl_.Enable(GL_BLEND);
    gl_.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl_.DepthMask(0);
    for (const GlMesh* m : blended) draw_mesh(*m);
    gl_.DepthMask(1);
    gl_.Disable(GL_BLEND);
  }
  gl_.BindVertexArray(0);

  gl_.ReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels_.data());
  gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);

  // Drain the error queue. Transient GL errors do not invalidate the pixels we
  // just read, but GL_OUT_OF_MEMORY and a context reset do — treat those as
  // device loss and leave `pixels_` untouched so the last good frame stays up
  // until the CPU renderer takes over.
  if (gl_.GetError) {
    GLenum first = GL_NO_ERROR;
    for (int i = 0; i < 16; i++) {
      const GLenum e = gl_.GetError();
      if (e == GL_NO_ERROR) break;
      if (first == GL_NO_ERROR) first = e;
    }
    if (first == GL_OUT_OF_MEMORY || first == GL_CONTEXT_LOST) {
      status.device_lost = true;
      status.error = (first == GL_OUT_OF_MEMORY) ? "GL reported out of memory"
                                                 : "GL context was lost";
      status.converged = true;
      return status;
    }
  }

  // GL origin is bottom-left; lightvg surfaces are top-down. Swap row pairs
  // while packing RGBA8 into 0xAARRGGBB.
  auto pack_row = [&](const uint8_t* src, uint32_t* dst) {
    for (int x = 0; x < width_; x++) {
      dst[x] = 0xFF000000u | (uint32_t(src[x * 4 + 0]) << 16) |
               (uint32_t(src[x * 4 + 1]) << 8) | uint32_t(src[x * 4 + 2]);
    }
  };
  uint8_t* raw = reinterpret_cast<uint8_t*>(pixels_.data());
  for (int y = 0; y < height_ / 2; y++) {
    uint8_t* top = raw + size_t(height_ - 1 - y) * size_t(width_) * 4;
    uint8_t* bottom = raw + size_t(y) * size_t(width_) * 4;
    std::memcpy(row_scratch_.data(), top, row_scratch_.size());
    pack_row(bottom,
             pixels_.data() + size_t(height_ - 1 - y) * size_t(width_));
    pack_row(row_scratch_.data(), pixels_.data() + size_t(y) * size_t(width_));
  }
  if (height_ & 1) {
    const int y = height_ / 2;
    pack_row(raw + size_t(y) * size_t(width_) * 4,
             pixels_.data() + size_t(y) * size_t(width_));
  }

  dirty_ = false;
  status.produced_pixels = true;
  status.converged = true;
  return status;
}

}  // namespace

std::unique_ptr<Renderer> CreateGlRenderer(int width, int height,
                                           const RenderSettings& settings,
                                           uint64_t required_vram_bytes,
                                           uint64_t max_gpu_mem_bytes,
                                           GlProbeResult* probe,
                                           std::string* err) {
  auto r = std::unique_ptr<GlRasterRenderer>(new GlRasterRenderer(
      required_vram_bytes, max_gpu_mem_bytes, probe));
  if (!r->Init(width, height, settings, err)) return nullptr;
  return std::unique_ptr<Renderer>(r.release());
}

}  // namespace lusdql
