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
  void updateMorphWeights(int meshIndex,
                          const std::vector<float>& coeffs) override;
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
  bool gpuMemoryMB(size_t* usedMB, size_t* totalMB) const override;
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
    GLuint wireEbo{0};               // GL_LINES indices: original polygon edges; 0 = none
    GLsizei wireCount{0};            // index count in wireEbo (2 per edge)
    // GPU blendshape morph (raster): per-vertex (offset,count) attribute (loc 8) +
    // a static delta texture-buffer (RGBA32F: channelId,dx,dy,dz) + a tiny per-frame
    // coefficient texture-buffer (R32F). hasMorph gates it on; 0 handles = none.
    GLuint morphOffsetVbo{0};        // attrib 8: uvec2 (offset, count); 0 = none
    GLuint morphDeltaBuf{0}, morphDeltaTex{0};   // RGBA16F GL_TEXTURE_BUFFER
    GLuint morphCoeffBuf{0}, morphCoeffTex{0};   // R32F GL_TEXTURE_BUFFER (dynamic)
    GLuint morphChanBuf{0}, morphChanTex{0};     // R16UI: per-entry channelId (skip)
    int morphChannelCount{0};
    bool hasMorph{false};
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
    int displacementTex{-1};
    float displacementConst{0.0f};
    float displacementTexScale{1.0f};
    float displacementTexBias{0.0f};
    bool hasDisplacement() const { return displacementTex >= 0 || displacementConst != 0.0f; }
  };

  void destroyScene();
  void buildTessProgram();  // GL>=4.0 tessellation displacement program (best-effort)
  void ensureFbo(int w, int h);
  void drawMeshes(const RenderFrameParams& params, bool wireframe,
                  const float* overrideEmissive);
  // Draw each mesh's original-polygon edge set (wireEbo) as GL_LINES, both the
  // non-instanced and instanced prototypes, in a flat color with a small NDC depth
  // bias so the lines sit just in front of the surface.
  void buildWireProgram();
  void drawWireframe(const RenderFrameParams& params, const float wireColor[3]);
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
  GLint uHasDisplacement_{-1}, uHasDisplacementTex_{-1};  // displacement (coarse)
  GLint uDisplacementConst_{-1}, uDisplacementScale_{-1};
  GLint uDisplacementTexScale_{-1}, uDisplacementTexBias_{-1};
  GLint uHasMorph_{-1};  // GPU blendshape morph enable (per-draw)
  GLint iHasMorph_{-1};  // GPU morph enable in the instanced program (per-draw)

  // GPU tessellation displacement program (built only on GL >= 4.0). Adaptive
  // sub-triangle subdivision in the TCS + per-sample displacement in the TES, so a
  // coarse mesh shows fine height-map detail without any persistent extra geometry.
  // Used for displaced submeshes in Shaded mode when Max-tess > 1; otherwise the
  // coarse per-vertex program above is used.
  bool tessAvailable_{false};
  GLuint tessProgram_{0};
  GLint tMVP_{-1}, tModel_{-1}, tNormalMat_{-1}, tCameraPos_{-1};
  GLint tBaseColor_{-1}, tHasBaseColorTex_{-1};
  GLint tHasDisplacementTex_{-1}, tDisplacementConst_{-1}, tDisplacementScale_{-1};
  GLint tDisplacementTexScale_{-1}, tDisplacementTexBias_{-1};
  GLint tMaxTessLevel_{-1};
  GLint tHasMorph_{-1};  // GPU morph in the tess vertex stage
  // Skinning in the tess vertex stage (mirrors the coarse program's uniforms).
  GLint tSkinningEnabled_{-1}, tExtendedSkinningEnabled_{-1};
  GLint tBoneTexWidth_{-1}, tBoneMatrixCount_{-1}, tInfluenceTexWidth_{-1};
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

  // Flat wireframe programs (polygon-edge GL_LINES). Non-instanced takes a full
  // MVP; instanced reuses the per-instance 3x4 rows (attribs 3/4/5) + a view-proj.
  // Both share a flat color + an NDC depth bias to lift lines off the surface.
  GLuint wireProgram_{0};
  GLint wMVP_{-1}, wWireColor_{-1}, wDepthBias_{-1};
  GLuint wireInstProgram_{0};
  GLint wiViewProj_{-1}, wiWireColor_{-1}, wiDepthBias_{-1};

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
