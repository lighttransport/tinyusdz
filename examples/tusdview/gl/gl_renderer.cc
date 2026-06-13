// SPDX-License-Identifier: Apache-2.0
#include "gl/gl_renderer.hh"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstring>

#include "gl/gl_util.hh"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "light3d/material.h"
#include "light3d/math.h"

namespace tusdview {

namespace {

GLint GLWrap(int w) {
  switch (w) {
    case 1: return GL_REPEAT;
    case 2: return GL_MIRRORED_REPEAT;
    case 3: return GL_CLAMP_TO_BORDER;
    default: return GL_CLAMP_TO_EDGE;
  }
}

light3d::Mat4 ToMat4(const float* m) {
  light3d::Mat4 r;
  std::memcpy(r.m, m, sizeof(r.m));
  return r;
}

// Normal matrix (column-major 3x3) = transpose(inverse(upper-left 3x3 of world)).
void NormalMatrix3(const float m[16], float out[9]) {
  const float a00 = m[0], a01 = m[4], a02 = m[8];
  const float a10 = m[1], a11 = m[5], a12 = m[9];
  const float a20 = m[2], a21 = m[6], a22 = m[10];
  const float det = a00 * (a11 * a22 - a12 * a21) -
                    a01 * (a10 * a22 - a12 * a20) +
                    a02 * (a10 * a21 - a11 * a20);
  if (std::fabs(det) < 1e-12f) {
    out[0] = 1; out[1] = 0; out[2] = 0;
    out[3] = 0; out[4] = 1; out[5] = 0;
    out[6] = 0; out[7] = 0; out[8] = 1;
    return;
  }
  const float inv = 1.0f / det;
  // inv[row][col]
  out[0] = (a11 * a22 - a12 * a21) * inv;   // i00
  out[1] = -(a01 * a22 - a02 * a21) * inv;  // i01
  out[2] = (a01 * a12 - a02 * a11) * inv;   // i02
  out[3] = -(a10 * a22 - a12 * a20) * inv;  // i10
  out[4] = (a00 * a22 - a02 * a20) * inv;   // i11
  out[5] = -(a00 * a12 - a02 * a10) * inv;  // i12
  out[6] = (a10 * a21 - a11 * a20) * inv;   // i20
  out[7] = -(a00 * a21 - a01 * a20) * inv;  // i21
  out[8] = (a00 * a11 - a01 * a10) * inv;   // i22
}

}  // namespace

GLRenderer::~GLRenderer() { shutdown(); }

bool GLRenderer::init(GLFWwindow* window, std::string* err) {
  window_ = window;
  caps_.backend_name = "OpenGL";
  caps_.usesZeroToOneDepth = false;
  caps_.flipViewportV = true;
  caps_.supportsGpuSkinning = true;

  program_ = glutil::CompileProgram(light3d::getMaterialVertexShaderGL330(),
                                    light3d::getMaterialFragmentShaderGL330(), err);
  if (!program_) {
    if (err && err->empty()) *err = "Failed to build GL material program";
    return false;
  }
  glUseProgram(program_);
  uMVP_ = glGetUniformLocation(program_, "uModelViewProj");
  uModel_ = glGetUniformLocation(program_, "uModel");
  uNormalMat_ = glGetUniformLocation(program_, "uNormalMatrix");
  uCameraPos_ = glGetUniformLocation(program_, "uCameraPos");
  uBaseColor_ = glGetUniformLocation(program_, "uBaseColor");
  uMetallic_ = glGetUniformLocation(program_, "uMetallic");
  uRoughness_ = glGetUniformLocation(program_, "uRoughness");
  uEmissive_ = glGetUniformLocation(program_, "uEmissive");
  uAlpha_ = glGetUniformLocation(program_, "uAlpha");
  uHasBaseColorTex_ = glGetUniformLocation(program_, "uHasBaseColorTex");
  uHasMetalRoughTex_ = glGetUniformLocation(program_, "uHasMetalRoughTex");
  uHasNormalTex_ = glGetUniformLocation(program_, "uHasNormalTex");
  uHasEmissiveTex_ = glGetUniformLocation(program_, "uHasEmissiveTex");
  uSkinningEnabled_ = glGetUniformLocation(program_, "uSkinningEnabled");
  // Fixed sampler -> texture-unit bindings.
  glUniform1i(glGetUniformLocation(program_, "uBaseColorTex"), 0);
  glUniform1i(glGetUniformLocation(program_, "uMetalRoughTex"), 1);
  glUniform1i(glGetUniformLocation(program_, "uNormalTex"), 2);
  glUniform1i(glGetUniformLocation(program_, "uEmissiveTex"), 3);
  glUniform1i(glGetUniformLocation(program_, "uBoneTex"), 4);
  glUseProgram(0);

  // 1x1 white default texture (bound to unused sampler units).
  glGenTextures(1, &whiteTex_);
  glBindTexture(GL_TEXTURE_2D, whiteTex_);
  const uint8_t white[4] = {255, 255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenTextures(1, &boneTex_);
  glBindTexture(GL_TEXTURE_2D, boneTex_);
  const float ident[16] = {
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      0, 0, 0, 1};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 1, 0, GL_RGBA, GL_FLOAT, ident);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  boneTexHeight_ = 1;

  // Unlit, vertex-colored line program for debug helpers (grid/axes/bbox).
  {
    static const char* kLineVS =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "layout(location=1) in vec3 aCol;\n"
        "uniform mat4 uVP;\n"
        "out vec3 vCol;\n"
        "void main(){ vCol=aCol; gl_Position=uVP*vec4(aPos,1.0); }\n";
    static const char* kLineFS =
        "#version 330 core\n"
        "in vec3 vCol; out vec4 fragColor;\n"
        "void main(){ fragColor=vec4(vCol,1.0); }\n";
    std::string lerr;
    lineProgram_ = glutil::CompileProgram(kLineVS, kLineFS, &lerr);
    if (lineProgram_) uLineVP_ = glGetUniformLocation(lineProgram_, "uVP");
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HelperVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(HelperVertex),
                          (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
  }
  return true;
}

bool GLRenderer::initImGui(std::string* err) {
  if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
    if (err) *err = "ImGui_ImplGlfw_InitForOpenGL failed";
    return false;
  }
  if (!ImGui_ImplOpenGL3_Init("#version 330")) {
    if (err) *err = "ImGui_ImplOpenGL3_Init failed";
    return false;
  }
  imguiInited_ = true;
  return true;
}

void GLRenderer::destroyScene() {
  for (auto& m : meshes_) {
    if (m.ebo) glDeleteBuffers(1, &m.ebo);
    if (m.weightVbo) glDeleteBuffers(1, &m.weightVbo);
    if (m.jointVbo) glDeleteBuffers(1, &m.jointVbo);
    if (m.vbo) glDeleteBuffers(1, &m.vbo);
    if (m.vao) glDeleteVertexArrays(1, &m.vao);
  }
  meshes_.clear();
  if (!textures_.empty()) {
    glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
  }
  textures_.clear();
  materials_.clear();
}

void GLRenderer::beginScene(const std::vector<DrawMaterialCPU>& materials,
                            int textureCount) {
  destroyScene();
  // Reserve texture slots (0 = not yet uploaded -> resolved to white at draw).
  textures_.assign(textureCount > 0 ? static_cast<size_t>(textureCount) : 0, 0);
  materials_.reserve(materials.size());
  for (const auto& m : materials) {
    GLMaterial gm;
    gm.baseColor[0] = m.baseColor[0];
    gm.baseColor[1] = m.baseColor[1];
    gm.baseColor[2] = m.baseColor[2];
    gm.metallic = m.metallic;
    gm.roughness = m.roughness;
    gm.emissive[0] = m.emissive[0];
    gm.emissive[1] = m.emissive[1];
    gm.emissive[2] = m.emissive[2];
    gm.alpha = m.alpha;
    gm.baseColorTex = m.baseColorTex;  // slot indices (resolved at draw)
    gm.metalRoughTex = m.metalRoughTex;
    gm.normalTex = m.normalTex;
    gm.emissiveTex = m.emissiveTex;
    materials_.push_back(gm);
  }
}

void GLRenderer::uploadTexture(int slot, const DrawTextureCPU& t) {
  if (slot < 0 || static_cast<size_t>(slot) >= textures_.size()) return;
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  // Upload as plain RGBA8 (texels used as-is; see note: the simple shader and
  // linear RGBA8 target don't re-encode gamma).
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.image.width, t.image.height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, t.image.data.empty() ? nullptr : t.image.data.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLWrap(t.wrapS));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLWrap(t.wrapT));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);
  if (textures_[static_cast<size_t>(slot)]) {
    glDeleteTextures(1, &textures_[static_cast<size_t>(slot)]);
  }
  textures_[static_cast<size_t>(slot)] = tex;
}

void GLRenderer::uploadSkinningFrame(const SkinningFrameCPU& skin) {
  if (!boneTex_) return;
  const bool valid = skin.enabled && skin.matrixCount > 0 &&
                     skin.rgba32f.size() >= static_cast<size_t>(skin.matrixCount) * 16;
  skinningFrameEnabled_ = valid;
  const int h = valid ? skin.matrixCount : 1;
  const float ident[16] = {
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      0, 0, 0, 1};
  const float* data = valid ? skin.rgba32f.data() : ident;
  glBindTexture(GL_TEXTURE_2D, boneTex_);
  if (h != boneTexHeight_) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, h, 0, GL_RGBA, GL_FLOAT, data);
    boneTexHeight_ = h;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, h, GL_RGBA, GL_FLOAT, data);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
}

void GLRenderer::updateMeshVertices(int meshIndex,
                                    const std::vector<DrawVertex>& verts) {
  if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes_.size())) return;
  GLMesh& gm = meshes_[static_cast<size_t>(meshIndex)];
  if (!gm.vbo || verts.size() != gm.vertexCount) return;  // count must match
  glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  static_cast<GLsizeiptr>(verts.size() * sizeof(DrawVertex)),
                  verts.data());
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GLRenderer::updateMeshWorld(int meshIndex, const float world[16]) {
  if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes_.size())) return;
  // GL recomputes the normal matrix from `world` each draw, so storing the new
  // world is enough.
  std::memcpy(meshes_[static_cast<size_t>(meshIndex)].world, world,
              sizeof(float) * 16);
}

void GLRenderer::appendMesh(const DrawMeshCPU& sm) {
  GLMesh gm;
  gm.submeshes = sm.submeshes;
  std::memcpy(gm.world, sm.world, sizeof(gm.world));
  gm.doubleSided = sm.doubleSided;
  gm.skinned = sm.jointIdx.size() == sm.vertices.size() * 4 &&
               sm.jointWt.size() == sm.vertices.size() * 4;
  gm.vertexCount = sm.vertices.size();

  glGenVertexArrays(1, &gm.vao);
  glBindVertexArray(gm.vao);
  glGenBuffers(1, &gm.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, gm.vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(sm.vertices.size() * sizeof(DrawVertex)),
               sm.vertices.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &gm.ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(sm.indices.size() * sizeof(uint32_t)),
               sm.indices.data(), GL_STATIC_DRAW);
  const GLsizei stride = sizeof(DrawVertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
  if (gm.skinned) {
    glGenBuffers(1, &gm.jointVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.jointVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointIdx.size() * sizeof(uint32_t)),
                 sm.jointIdx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_INT, 4 * sizeof(uint32_t), (void*)0);
    glGenBuffers(1, &gm.weightVbo);
    glBindBuffer(GL_ARRAY_BUFFER, gm.weightVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sm.jointWt.size() * sizeof(float)),
                 sm.jointWt.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  } else {
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glVertexAttribI4ui(3, 0, 0, 0, 0);
    glVertexAttrib4f(4, 0.0f, 0.0f, 0.0f, 0.0f);
  }
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  meshes_.push_back(gm);
}

void GLRenderer::ensureFbo(int w, int h) {
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (fbo_ && w == vpW_ && h == vpH_) return;
  vpW_ = w;
  vpH_ = h;
  if (!fbo_) glGenFramebuffers(1, &fbo_);
  if (!colorTex_) glGenTextures(1, &colorTex_);
  if (!depthRbo_) glGenRenderbuffers(1, &depthRbo_);

  glBindTexture(GL_TEXTURE_2D, colorTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void GLRenderer::resizeViewport(int width, int height) { ensureFbo(width, height); }

void GLRenderer::newFrame() { ImGui_ImplOpenGL3_NewFrame(); }

void GLRenderer::drawMeshes(const RenderFrameParams& params, bool wireframe,
                            const float* overrideEmissive) {
  static const GLMaterial kDefault;
  light3d::Mat4 P = ToMat4(params.proj);
  light3d::Mat4 V = ToMat4(params.view);

  for (size_t mi = 0; mi < meshes_.size(); ++mi) {
    if (params.meshVisible && mi < static_cast<size_t>(params.meshVisibleCount) &&
        !params.meshVisible[mi]) {
      continue;  // hidden by the viewer's per-mesh visibility mask
    }
    const GLMesh& mesh = meshes_[mi];
    light3d::Mat4 W = ToMat4(mesh.world);
    light3d::Mat4 MVP = P * V * W;
    float nmat[9];
    NormalMatrix3(mesh.world, nmat);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, MVP.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, W.m);
    glUniformMatrix3fv(uNormalMat_, 1, GL_FALSE, nmat);
    glUniform1i(uSkinningEnabled_, (mesh.skinned && skinningFrameEnabled_) ? 1 : 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, boneTex_ ? boneTex_ : whiteTex_);

    if (mesh.doubleSided || wireframe) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
    }

    glBindVertexArray(mesh.vao);
    for (const auto& sub : mesh.submeshes) {
      const GLMaterial& mat =
          (sub.materialId >= 0 && static_cast<size_t>(sub.materialId) < materials_.size())
              ? materials_[static_cast<size_t>(sub.materialId)]
              : kDefault;
      if (overrideEmissive) {
        glUniform3f(uBaseColor_, 0.f, 0.f, 0.f);
        glUniform1f(uMetallic_, 0.f);
        glUniform1f(uRoughness_, 1.f);
        glUniform3fv(uEmissive_, 1, overrideEmissive);
        glUniform1f(uAlpha_, 1.f);
        glUniform1i(uHasBaseColorTex_, 0);
        glUniform1i(uHasMetalRoughTex_, 0);
        glUniform1i(uHasNormalTex_, 0);
        glUniform1i(uHasEmissiveTex_, 0);
      } else {
        // Resolve a material texture slot to a GPU texture; white if the slot is
        // out of range or not yet uploaded (lazy texture streaming).
        auto slotTex = [&](int slot) -> GLuint {
          if (slot >= 0 && static_cast<size_t>(slot) < textures_.size() &&
              textures_[static_cast<size_t>(slot)]) {
            return textures_[static_cast<size_t>(slot)];
          }
          return whiteTex_;
        };
        glUniform3fv(uBaseColor_, 1, mat.baseColor);
        glUniform1f(uMetallic_, mat.metallic);
        glUniform1f(uRoughness_, mat.roughness);
        glUniform3fv(uEmissive_, 1, mat.emissive);
        glUniform1f(uAlpha_, mat.alpha);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.baseColorTex));
        glUniform1i(uHasBaseColorTex_, mat.baseColorTex >= 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.metalRoughTex));
        glUniform1i(uHasMetalRoughTex_, mat.metalRoughTex >= 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.normalTex));
        glUniform1i(uHasNormalTex_, mat.normalTex >= 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, slotTex(mat.emissiveTex));
        glUniform1i(uHasEmissiveTex_, mat.emissiveTex >= 0 ? 1 : 0);
      }
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                     (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)));
    }
  }
  glBindVertexArray(0);
}

void GLRenderer::renderFrame(const RenderFrameParams& params) {
  if (!fbo_ || !program_ || !params.view || !params.proj) return;
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, vpW_, vpH_);
  glClearColor(params.clearColor[0], params.clearColor[1], params.clearColor[2],
               params.clearColor[3]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  glUseProgram(program_);
  glUniform3fv(uCameraPos_, 1, params.cameraPos);

  const bool wire = (params.mode == RenderMode::Wireframe);
  glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
  drawMeshes(params, wire, nullptr);

  // Highlight overlay (wireframe, emissive orange) on the selected mesh.
  if (params.highlightMeshIndex >= 0 &&
      static_cast<size_t>(params.highlightMeshIndex) < meshes_.size() && !wire) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);
    const float orange[3] = {1.0f, 0.55f, 0.1f};
    const GLMesh& mesh = meshes_[static_cast<size_t>(params.highlightMeshIndex)];
    light3d::Mat4 P = ToMat4(params.proj), V = ToMat4(params.view), W = ToMat4(mesh.world);
    light3d::Mat4 MVP = P * V * W;
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, MVP.m);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, W.m);
    glUniform1i(uSkinningEnabled_, (mesh.skinned && skinningFrameEnabled_) ? 1 : 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, boneTex_ ? boneTex_ : whiteTex_);
    glUniform3f(uBaseColor_, 0, 0, 0);
    glUniform3fv(uEmissive_, 1, orange);
    glUniform1f(uAlpha_, 1.f);
    glUniform1i(uHasBaseColorTex_, 0);
    glUniform1i(uHasMetalRoughTex_, 0);
    glUniform1i(uHasNormalTex_, 0);
    glUniform1i(uHasEmissiveTex_, 0);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(mesh.vao);
    for (const auto& sub : mesh.submeshes) {
      glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                     (void*)(static_cast<uintptr_t>(sub.indexOffset) * sizeof(uint32_t)));
    }
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_LINE);
  }

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  // Debug helper lines (grid/axes/bbox), world space, depth-tested so they are
  // occluded by geometry.
  if (params.helperLines && params.helperLineVertexCount > 0 && lineProgram_) {
    glUseProgram(lineProgram_);
    const light3d::Mat4 VP = ToMat4(params.proj) * ToMat4(params.view);
    glUniformMatrix4fv(uLineVP_, 1, GL_FALSE, VP.m);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    const size_t bytes =
        static_cast<size_t>(params.helperLineVertexCount) * sizeof(HelperVertex);
    if (bytes > lineVboCap_) {
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), params.helperLines,
                   GL_DYNAMIC_DRAW);
      lineVboCap_ = bytes;
    } else {
      glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                      params.helperLines);
    }
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_LINES, 0, params.helperLineVertexCount);
    glBindVertexArray(0);
  }

  // Overlay lines (skeleton bones): drawn on top with depth testing disabled so
  // they remain visible through the mesh (X-ray). Reuses the line program/VBO.
  if (params.overlayLines && params.overlayLineVertexCount > 0 && lineProgram_) {
    glUseProgram(lineProgram_);
    const light3d::Mat4 VP = ToMat4(params.proj) * ToMat4(params.view);
    glUniformMatrix4fv(uLineVP_, 1, GL_FALSE, VP.m);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    const size_t bytes =
        static_cast<size_t>(params.overlayLineVertexCount) * sizeof(HelperVertex);
    if (bytes > lineVboCap_) {
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), params.overlayLines,
                   GL_DYNAMIC_DRAW);
      lineVboCap_ = bytes;
    } else {
      glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                      params.overlayLines);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, params.overlayLineVertexCount);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
  }

  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ViewportTexHandle GLRenderer::viewportTexture() const {
  return static_cast<ViewportTexHandle>(colorTex_);
}

void GLRenderer::present() {
  int fbw = 0, fbh = 0;
  glfwGetFramebufferSize(window_, &fbw, &fbh);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, fbw, fbh);
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  // Grab the composited window from the back buffer before swapping (reliable
  // under headless/Xvfb, unlike reading GL_FRONT after the swap).
  if (wantWindowCapture_) {
    winCapW_ = fbw;
    winCapH_ = fbh;
    windowCapture_.resize(static_cast<size_t>(fbw) * static_cast<size_t>(fbh) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, fbw, fbh, GL_RGBA, GL_UNSIGNED_BYTE, windowCapture_.data());
    wantWindowCapture_ = false;
  }

  glfwSwapBuffers(window_);
}

bool GLRenderer::captureViewport(std::vector<uint8_t>* rgba, int* w, int* h) {
  if (!fbo_ || vpW_ < 1 || vpH_ < 1) return false;
  *w = vpW_;
  *h = vpH_;
  const size_t rowBytes = static_cast<size_t>(vpW_) * 4;
  std::vector<uint8_t> tmp(rowBytes * static_cast<size_t>(vpH_));
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, vpW_, vpH_, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  // GL is bottom-up; return top-down rows (consistent with the Vulkan backend).
  rgba->resize(tmp.size());
  for (int y = 0; y < vpH_; ++y) {
    std::memcpy(&(*rgba)[static_cast<size_t>(y) * rowBytes],
                &tmp[static_cast<size_t>(vpH_ - 1 - y) * rowBytes], rowBytes);
  }
  return true;
}

bool GLRenderer::captureWindow(std::vector<uint8_t>* rgba, int* w, int* h) {
  if (windowCapture_.empty() || winCapW_ < 1 || winCapH_ < 1) return false;
  *w = winCapW_;
  *h = winCapH_;
  const size_t rowBytes = static_cast<size_t>(winCapW_) * 4;
  rgba->resize(windowCapture_.size());
  // Stored bottom-up; emit top-down.
  for (int y = 0; y < winCapH_; ++y) {
    std::memcpy(&(*rgba)[static_cast<size_t>(y) * rowBytes],
                &windowCapture_[static_cast<size_t>(winCapH_ - 1 - y) * rowBytes],
                rowBytes);
  }
  return true;
}

void GLRenderer::shutdown() {
  destroyScene();
  if (whiteTex_) { glDeleteTextures(1, &whiteTex_); whiteTex_ = 0; }
  if (boneTex_) { glDeleteTextures(1, &boneTex_); boneTex_ = 0; }
  if (colorTex_) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
  if (depthRbo_) { glDeleteRenderbuffers(1, &depthRbo_); depthRbo_ = 0; }
  if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
  if (program_) { glDeleteProgram(program_); program_ = 0; }
  if (lineProgram_) { glDeleteProgram(lineProgram_); lineProgram_ = 0; }
  if (lineVbo_) { glDeleteBuffers(1, &lineVbo_); lineVbo_ = 0; }
  if (lineVao_) { glDeleteVertexArrays(1, &lineVao_); lineVao_ = 0; }
  if (imguiInited_) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    imguiInited_ = false;
  }
}

std::unique_ptr<Renderer> CreateGLRenderer() {
  return std::unique_ptr<Renderer>(new GLRenderer());
}

}  // namespace tusdview
