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
  void updateInstanceVisibility(size_t meshIndex, const float* xforms,
                                const float* colors, uint32_t count) override;
  void updateMeshWorld(int meshIndex, const float world[16]) override;
  void replaceMesh(int meshIndex, const DrawMeshCPU& mesh) override;
  int meshCount() const override { return static_cast<int>(meshes_.size()); }
  void resizeViewport(int width, int height) override;
  void newFrame() override;
  void renderFrame(const RenderFrameParams& params) override;
  ViewportTexHandle viewportTexture() const override;
  void present() override;
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  void presentThreaded(ImDrawData* drawData, int fbW, int fbH) override;
  bool initImGuiPlatform(GLFWwindow* window, std::string* err) override;
  bool initImGuiBackend(std::string* err) override;
#endif
  bool captureViewport(std::vector<uint8_t>* rgba, int* w, int* h) override;
  void requestWindowCapture() override { wantWindowCapture_ = true; }
  bool captureWindow(std::vector<uint8_t>* rgba, int* w, int* h) override;
  const RendererCaps& caps() const override { return caps_; }
  void appendVolume(const DrawVolumeCPU& vol) override;
  void shutdown() override;

 private:
  // UsdVol volume (OpenVDB): density grid as a GL_TEXTURE_3D, raymarched in a
  // proxy-box pass over its object-space AABB.
  struct GLVolume {
    GLuint tex3d{0};
    float world[16];     // object -> world (column-major)
    float invWorld[16];  // world -> object (column-major)
    float bmin[3]{0, 0, 0};
    float bmax[3]{0, 0, 0};
    float densityScale{1.0f};
    float albedo[3]{0.6f, 0.6f, 0.65f};
    float emission[3]{0, 0, 0};
    float background{0.0f};
  };
  struct GLMesh {
    GLuint vao{0}, vbo{0}, ebo{0}, jointVbo{0}, weightVbo{0};
    GLuint influenceVbo{0}, influenceTex{0};
    GLuint instanceVbo{0};   // per-instance 3x4 model matrices; 0 = none
    int instanceCount{0};    // >0 => drawn with glDrawElementsInstanced
    int drawInstanceCount{0};  // visible subset drawn this frame (per-instance cull)
    GLuint instanceColorVbo{0};      // per-instance displayColor; 0 = none
    bool hasInstanceColors{false};   // true => attrib 9 is array-backed
    float flatColor[3]{0.8f, 0.8f, 0.8f};  // per-draw color when no per-instance
    GLuint vertexColorVbo{0};        // per-vertex displayColor (non-instanced); 0 = none
    GLuint uv1Vbo{0};                // 2nd texcoord set (attrib 6, non-instanced); 0 = none
    GLuint morphInflVbo{0};          // blendshape influence (attrib 7, non-instanced); 0 = none
    GLuint faceIdBuf{0};             // texture buffer: per-triangle source face id; 0 = none
    GLuint faceIdTex{0};             // GL_TEXTURE_BUFFER view (R32UI) of faceIdBuf
    bool geometricNormal{false};     // shade with screen-derivative normal
    int purposeId{0};                // USD purpose AOV: 0=default/1=render/2=proxy/3=guide
    int kindId{0};                   // USD kind AOV: 0=none/1=component/2=group/3=assembly/4=subcomponent
    std::vector<DrawSubmesh> submeshes;
    float world[16];
    bool doubleSided{false};
    bool skinned{false};
    bool extendedSkinned{false};
    int influenceTexWidth{0};
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
#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  void presentImpl(ImDrawData* drawData, int fbw, int fbh);
#endif

  GLFWwindow* window_{nullptr};
  RendererCaps caps_{};
  bool imguiInited_{false};

  GLuint program_{0};
  // uniform locations
  GLint uMVP_{-1}, uModel_{-1}, uNormalMat_{-1}, uCameraPos_{-1}, uGeometricNormal_{-1};
  GLint uRenderMode_{-1}, uMatId_{-1}, uDepthScale_{-1};  // AOV visualizations
  GLint uSceneMin_{-1}, uSceneExtent_{-1};                // position AOV bounds
  GLint uMeshId_{-1}, uDoubleSided_{-1};                  // mesh-id / double-sided AOV
  GLint uPurpose_{-1};                                    // purpose AOV (per-draw)
  GLint uKind_{-1};                                       // kind AOV (per-draw)
  GLint uFaceIdTex_{-1}, uFaceBase_{-1}, uHasFaceId_{-1}; // source-face-id AOV
  GLint uBaseColor_{-1}, uMetallic_{-1}, uRoughness_{-1}, uEmissive_{-1}, uAlpha_{-1};
  GLint uHasBaseColorTex_{-1}, uHasMetalRoughTex_{-1}, uHasNormalTex_{-1}, uHasEmissiveTex_{-1};
  GLint uSkinningEnabled_{-1};
  GLint uExtendedSkinningEnabled_{-1};
  GLint uBoneTexWidth_{-1}, uBoneMatrixCount_{-1}, uInfluenceTexWidth_{-1};

  // Instanced flat-shaded program (GPU instancing for PointInstancer prototypes):
  // per-instance 3x4 model (attribs 6-8) + displayColor (attrib 9); view-proj +
  // camera uniforms. The fragment shader is a self-contained flat shader using
  // the per-instance vColor (no material uniforms).
  GLuint instProgram_{0};
  GLint iUViewProj_{-1}, iCameraPos_{-1}, iEmissive_{-1};
  // Instanced-program debug-AOV uniforms (mirror the non-instanced material shader).
  GLint iRenderMode_{-1}, iDepthScale_{-1}, iSceneMin_{-1}, iSceneExtent_{-1};
  GLint iMeshId_{-1}, iGeometricNormal_{-1}, iDoubleSided_{-1}, iPurpose_{-1}, iKind_{-1};

  GLuint whiteTex_{0}, boneTex_{0};
  int boneTexWidth_{0}, boneTexHeight_{0}, boneMatrixCount_{0};
  int maxTextureSize_{4096};
  std::vector<float> boneUploadScratch_;
  bool skinningFrameEnabled_{false};

  // Unlit line program for debug helpers (grid/axes/bbox).
  GLuint lineProgram_{0};
  GLint uLineVP_{-1};
  GLuint lineVao_{0}, lineVbo_{0};
  size_t lineVboCap_{0};
  GLuint highlightEbo_{0};  // dynamic index buffer for GeomSubset highlight

  // UsdVol volume raymarch pass.
  GLuint volumeProgram_{0};
  GLuint volumeCubeVao_{0}, volumeCubeVbo_{0}, volumeCubeEbo_{0};
  GLint uVolVP_{-1}, uVolModel_{-1}, uVolInvModel_{-1}, uVolCameraPos_{-1};
  GLint uVolBmin_{-1}, uVolBmax_{-1}, uVolDensity_{-1}, uVolDensityScale_{-1};
  GLint uVolAlbedo_{-1}, uVolEmission_{-1}, uVolBackground_{-1};
  std::vector<GLVolume> volumes_;

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
