// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — offscreen GL 3.3 raster backend.
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
#include <string>
#include <vector>

#include "render/gl_context.hh"
#include "render/renderer.hh"
#include "render/shade.hh"

namespace tusdql {

namespace {

// ---- Minimal GL 3.3 core loader --------------------------------------------
// Sized for exactly what this backend calls. examples/common/glad is sized for
// tusdview and would be a much larger dependency for no benefit here.

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
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum GL_REPEAT = 0x2901;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_EXTENSIONS = 0x1F03;
constexpr GLenum GL_NUM_EXTENSIONS = 0x821D;
constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43;
constexpr GLenum GL_FRAMEBUFFER_SRGB = 0x8DB9;

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
  void (*GenerateMipmap)(GLenum) = nullptr;
  void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
  const unsigned char* (*GetString)(GLenum) = nullptr;
  void (*GetIntegerv)(GLenum, GLint*) = nullptr;

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
  BIND(TexImage2D); BIND(TexParameteri); BIND(GenerateMipmap);
  BIND(ReadPixels); BIND(GetString); BIND(GetIntegerv);
#undef BIND
  return ok;
}

// ---- Shaders ---------------------------------------------------------------

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

uniform mat4 u_view_proj;

out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;

void main() {
  v_world = in_position;
  v_normal = in_normal;
  v_uv = in_uv;
  gl_Position = u_view_proj * vec4(in_position, 1.0);
}
)";

// Mirrors render/shade.cc: Lambert + GGX with the same Smith visibility and
// Schlick Fresnel, the same hemispheric ambient, and the same three-light rig.
const char* kFragmentShader = R"(#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;

uniform vec3 u_eye;
uniform vec3 u_base_color;
uniform vec3 u_emissive;
uniform float u_roughness;
uniform float u_metallic;
uniform int u_has_texture;
uniform sampler2D u_base_color_tex;

uniform int u_light_count;
uniform vec3 u_light_dir[8];    // direction the light travels
uniform vec3 u_light_color[8];
uniform float u_light_intensity[8];
uniform vec3 u_ambient;
uniform float u_up_is_y;

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

void main() {
  vec3 base = u_base_color;
  if (u_has_texture != 0) {
    base = texture(u_base_color_tex, v_uv).rgb;
  }

  vec3 v = normalize(u_eye - v_world);
  vec3 n = normalize(v_normal);
  if (dot(n, v) < 0.0) n = -n;

  float a = max(1e-3, u_roughness * u_roughness);
  float NoV = max(dot(n, v), 1e-4);

  vec3 f0 = mix(vec3(0.04), base, u_metallic);
  vec3 diffuse_albedo = base * (1.0 - u_metallic);

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

  float up_component = mix(n.z, n.y, u_up_is_y);
  float hemi = 0.5 + 0.5 * up_component;
  color += diffuse_albedo * u_ambient * (0.4 + 0.6 * hemi);
  color += u_emissive;

  frag_color = vec4(color, 1.0);
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

out vec4 frag_color;

void main() {
  vec3 dir = normalize(u_forward
                     + u_right * (v_ndc.x * u_tan_half * u_aspect)
                     + u_up * (v_ndc.y * u_tan_half));
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
  GLuint ebo = 0;
  GLsizei index_count = 0;
  int material_id = -1;
  bool has_uv = false;
};

class GlRasterRenderer final : public Renderer {
 public:
  ~GlRasterRenderer() override { ReleaseGpu(); }

  bool Init(int width, int height, const RenderSettings& settings,
            std::string* err) override;
  void Resize(int width, int height) override;
  void SetScene(const QlScene* scene) override;
  void SyncScene() override;
  void SetCamera(const OrbitCamera& camera) override;
  RenderStatus RenderStep(double budget_ms) override;

  const uint32_t* Pixels() const override { return pixels_.data(); }
  int width() const override { return width_; }
  int height() const override { return height_; }
  const char* Name() const override { return "gl"; }

  // Free VRAM in bytes; 0 when the driver does not report it.
  uint64_t QueryFreeVram();

 private:
  bool CreateProgram(std::string* err);
  bool CreateTargets(std::string* err);
  void ReleaseTargets();
  void ReleaseGpu();
  void UploadScene();
  void BuildViewProj(float out[16]) const;

  std::unique_ptr<GlContext> ctx_;
  Gl gl_;
  RenderSettings settings_;
  const QlScene* scene_ = nullptr;
  OrbitCamera camera_;

  int width_ = 1;
  int height_ = 1;
  std::vector<uint32_t> pixels_;
  std::vector<uint8_t> readback_;

  GLuint program_ = 0;
  GLuint bg_program_ = 0;
  GLuint empty_vao_ = 0;
  GLuint fbo_ = 0;
  GLuint color_tex_ = 0;
  GLuint depth_rb_ = 0;

  std::vector<GlMesh> meshes_;
  std::vector<GLuint> textures_;
  size_t uploaded_mesh_count_ = 0;

  ShadingContext shading_;
  bool dirty_ = true;
};

bool GlRasterRenderer::Init(int width, int height,
                            const RenderSettings& settings, std::string* err) {
  settings_ = settings;

  ctx_ = CreateHeadlessGlContext(err);
  if (!ctx_) return false;
  if (!gl_.Load(ctx_->GetProcLoader())) {
    if (err) *err = "could not resolve the required GL 3.3 entry points";
    return false;
  }

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
  readback_.assign(size_t(width_) * height_ * 4, 0);
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
  if (scene_->meshes.size() == uploaded_mesh_count_ && !textures_.empty()) return;
  if (scene_->meshes.size() == uploaded_mesh_count_ &&
      scene_->textures.empty()) {
    return;
  }
  ctx_->MakeCurrent();
  UploadScene();
  dirty_ = true;
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
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        t.wrap_repeat_s ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        t.wrap_repeat_t ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      gl_.GenerateMipmap(GL_TEXTURE_2D);
    }
  }

  for (size_t i = uploaded_mesh_count_; i < scene_->meshes.size(); i++) {
    const QlMesh& src = scene_->meshes[i];
    if (src.triangle_count() == 0) continue;

    GlMesh m;
    m.material_id = src.material_id;
    m.index_count = GLsizei(src.indices.size());
    m.has_uv = !src.uvs.empty();

    gl_.GenVertexArrays(1, &m.vao);
    gl_.BindVertexArray(m.vao);

    gl_.GenBuffers(1, &m.vbo_pos);
    gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_pos);
    gl_.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(src.positions.size() * sizeof(float)),
                   src.positions.data(), GL_STATIC_DRAW);
    gl_.EnableVertexAttribArray(0);
    gl_.VertexAttribPointer(0, 3, GL_FLOAT, 0, 0, nullptr);

    // A mesh without authored normals still needs the attribute bound; fill it
    // with the position so the shader's normalize() is defined, then rely on
    // the per-face flat fallback being close enough for a preview.
    std::vector<float> normals;
    const float* normal_src = src.normals.data();
    size_t normal_bytes = src.normals.size() * sizeof(float);
    if (src.normals.empty()) {
      normals.assign(src.positions.begin(), src.positions.end());
      normal_src = normals.data();
      normal_bytes = normals.size() * sizeof(float);
    }
    gl_.GenBuffers(1, &m.vbo_nrm);
    gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_nrm);
    gl_.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(normal_bytes), normal_src,
                   GL_STATIC_DRAW);
    gl_.EnableVertexAttribArray(1);
    gl_.VertexAttribPointer(1, 3, GL_FLOAT, 0, 0, nullptr);

    std::vector<float> uvs;
    const float* uv_src = src.uvs.data();
    size_t uv_bytes = src.uvs.size() * sizeof(float);
    if (src.uvs.empty()) {
      uvs.assign(src.vertex_count() * 2, 0.0f);
      uv_src = uvs.data();
      uv_bytes = uvs.size() * sizeof(float);
    }
    gl_.GenBuffers(1, &m.vbo_uv);
    gl_.BindBuffer(GL_ARRAY_BUFFER, m.vbo_uv);
    gl_.BufferData(GL_ARRAY_BUFFER, GLsizeiptr(uv_bytes), uv_src, GL_STATIC_DRAW);
    gl_.EnableVertexAttribArray(2);
    gl_.VertexAttribPointer(2, 2, GL_FLOAT, 0, 0, nullptr);

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

  // The raster path is not progressive: one full-resolution frame per camera or
  // scene change, then nothing until something moves.
  if (!dirty_) {
    status.converged = true;
    return status;
  }

  ctx_->MakeCurrent();

  float eye[3], right[3], up[3], fwd[3];
  camera_.Eye(eye);
  camera_.Basis(right, up, fwd);
  BuildLightRig(*scene_, eye, fwd, right, up, &shading_);

  gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
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
  gl_.Uniform1i(gl_.GetUniformLocation(program_, "u_base_color_tex"), 0);

  static const QlMaterial kDefaultMaterial{};
  for (const GlMesh& m : meshes_) {
    const QlMaterial& mat =
        (m.material_id >= 0 &&
         m.material_id < static_cast<int>(scene_->materials.size()))
            ? scene_->materials[size_t(m.material_id)]
            : kDefaultMaterial;

    gl_.Uniform3fv(u_base, 1, mat.base_color);
    gl_.Uniform3fv(u_emis, 1, mat.emissive);
    gl_.Uniform1f(u_rough, mat.roughness);
    gl_.Uniform1f(u_metal, mat.metallic);

    const bool use_tex = m.has_uv && mat.base_color_tex >= 0 &&
                         size_t(mat.base_color_tex) < textures_.size() &&
                         textures_[size_t(mat.base_color_tex)] != 0;
    gl_.Uniform1i(u_has_tex, use_tex ? 1 : 0);
    if (use_tex) {
      gl_.ActiveTexture(GL_TEXTURE0);
      gl_.BindTexture(GL_TEXTURE_2D, textures_[size_t(mat.base_color_tex)]);
    }

    gl_.BindVertexArray(m.vao);
    gl_.DrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, nullptr);
  }
  gl_.BindVertexArray(0);

  gl_.ReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
                 readback_.data());
  gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);

  // GL origin is bottom-left; lightvg surfaces are top-down. Flip while packing
  // RGBA8 into 0xAARRGGBB.
  for (int y = 0; y < height_; y++) {
    const uint8_t* src =
        readback_.data() + size_t(height_ - 1 - y) * size_t(width_) * 4;
    uint32_t* dst = pixels_.data() + size_t(y) * size_t(width_);
    for (int x = 0; x < width_; x++) {
      dst[x] = 0xFF000000u | (uint32_t(src[x * 4 + 0]) << 16) |
               (uint32_t(src[x * 4 + 1]) << 8) | uint32_t(src[x * 4 + 2]);
    }
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
                                           std::string* err) {
  auto r = std::unique_ptr<GlRasterRenderer>(new GlRasterRenderer());
  if (!r->Init(width, height, settings, err)) return nullptr;

  // Only claim the GPU when the driver reports enough headroom. When it reports
  // nothing (common on Mesa/llvmpipe and on macOS) we accept, since the CPU
  // fallback is what a software rasterizer would give us anyway.
  const uint64_t free_vram = r->QueryFreeVram();
  if (free_vram > 0) {
    const uint64_t needed = required_vram_bytes + (256ull << 20);
    if (free_vram < needed) {
      if (err) {
        *err = "not enough free VRAM for the scene (" +
               std::to_string(free_vram >> 20) + " MB free, need " +
               std::to_string(needed >> 20) + " MB)";
      }
      return nullptr;
    }
  }
  return std::unique_ptr<Renderer>(r.release());
}

}  // namespace tusdql
