// SPDX-License-Identifier: Apache-2.0
// tusdview - OpenGL 3.3 backend. Renders the scene into an offscreen FBO whose
// color texture is shown by the GUI via ImGui::Image.
#pragma once

#include <glad/glad.h>

#include <vector>

#include "gpu_scene.hh"
#include "renderer.hh"

namespace tusdview {

class GLRenderer final : public Renderer {
 public:
  GLRenderer() = default;
  ~GLRenderer() override;

  bool init(GLFWwindow* window, std::string* err) override;
  bool initImGui(std::string* err) override;
  void beginScene(const std::vector<DrawMaterialCPU>& materials, int textureCount) override;
  void appendMesh(const DrawMeshCPU& mesh) override;
  void uploadTexture(int slot, const DrawTextureCPU& tex) override;
  void uploadSkinningFrame(const SkinningFrameCPU& skin) override;
  void updateMeshVertices(int meshIndex,
                          const std::vector<DrawVertex>& verts) override;
  void resizeViewport(int width, int height) override;
  void newFrame() override;
  void renderFrame(const RenderFrameParams& params) override;
  ViewportTexHandle viewportTexture() const override;
  void present() override;
  bool captureViewport(std::vector<uint8_t>* rgba, int* w, int* h) override;
  void requestWindowCapture() override { wantWindowCapture_ = true; }
  bool captureWindow(std::vector<uint8_t>* rgba, int* w, int* h) override;
  const RendererCaps& caps() const override { return caps_; }
  void shutdown() override;

 private:
  struct GLMesh {
    GLuint vao{0}, vbo{0}, ebo{0}, jointVbo{0}, weightVbo{0};
    std::vector<DrawSubmesh> submeshes;
    float world[16];
    bool doubleSided{false};
    bool skinned{false};
    size_t vertexCount{0};  // for per-frame morph vertex re-upload guard
  };
  struct GLMaterial {
    float baseColor[3]{0.8f, 0.8f, 0.8f};
    float metallic{0.0f}, roughness{0.5f};
    float emissive[3]{0, 0, 0};
    float alpha{1.0f};
    // Texture slot indices into textures_ (-1 = none). Resolved at draw time so
    // lazily-uploaded textures appear without re-touching materials.
    int baseColorTex{-1}, metalRoughTex{-1}, normalTex{-1}, emissiveTex{-1};
  };

  void destroyScene();
  void ensureFbo(int w, int h);
  void drawMeshes(const RenderFrameParams& params, bool wireframe,
                  const float* overrideEmissive);

  GLFWwindow* window_{nullptr};
  RendererCaps caps_{};
  bool imguiInited_{false};

  GLuint program_{0};
  // uniform locations
  GLint uMVP_{-1}, uModel_{-1}, uNormalMat_{-1}, uCameraPos_{-1};
  GLint uBaseColor_{-1}, uMetallic_{-1}, uRoughness_{-1}, uEmissive_{-1}, uAlpha_{-1};
  GLint uHasBaseColorTex_{-1}, uHasMetalRoughTex_{-1}, uHasNormalTex_{-1}, uHasEmissiveTex_{-1};
  GLint uSkinningEnabled_{-1};

  GLuint whiteTex_{0}, boneTex_{0};
  int boneTexHeight_{0};
  bool skinningFrameEnabled_{false};

  // Unlit line program for debug helpers (grid/axes/bbox).
  GLuint lineProgram_{0};
  GLint uLineVP_{-1};
  GLuint lineVao_{0}, lineVbo_{0};
  size_t lineVboCap_{0};

  // Offscreen target
  GLuint fbo_{0}, colorTex_{0}, depthRbo_{0};
  int vpW_{0}, vpH_{0};

  std::vector<GLuint> textures_;
  std::vector<GLMaterial> materials_;
  std::vector<GLMesh> meshes_;

  // Window (back buffer) capture grabbed in present() before swap.
  bool wantWindowCapture_{false};
  std::vector<uint8_t> windowCapture_;  // bottom-up RGBA8
  int winCapW_{0}, winCapH_{0};
};

}  // namespace tusdview
