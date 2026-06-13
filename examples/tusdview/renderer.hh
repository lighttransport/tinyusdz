// SPDX-License-Identifier: Apache-2.0
// tusdview - abstract Renderer interface (the GL/Vulkan boundary).
//
// Both backends render the 3D scene into an *offscreen* color target and expose
// it as an opaque texture handle. The GUI shows that handle via ImGui::Image in
// the "Viewport" dock window, so the 3D view participates in docking. All
// backend-specific ImGui wiring (NewFrame/Init/RenderDrawData, swap/present)
// lives behind this interface so the app main-loop is backend-agnostic.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "gpu_scene.hh"

struct GLFWwindow;

namespace tusdview {

enum class Backend { GL, Vulkan };

enum class RenderMode : int { Shaded = 0, Wireframe = 1, Normals = 2 };
enum class SkinningMode : int { Auto = 0, CPU = 1, GPU = 2 };

// Unlit, vertex-colored line vertex for debug helpers (grid, axes, bboxes).
// Drawn as GL_LINES / VK_PRIMITIVE_TOPOLOGY_LINE_LIST in world space.
struct HelperVertex {
  float pos[3];
  float col[3];
};

struct RendererCaps {
  const char* backend_name{""};
  bool usesZeroToOneDepth{false};  // Vulkan clip space Z in [0,1]; GL in [-1,1]
  bool flipViewportV{false};       // GL FBO textures are bottom-up
  bool supportsRayTracing{false};  // device has the RT extensions (Vulkan only)
  bool supportsGpuSkinning{false};
};

struct RenderFrameParams {
  const float* view{nullptr};  // column-major 4x4 (light3d::Mat4 layout)
  const float* proj{nullptr};  // column-major 4x4 (GL: Z[-1,1]; VK: Z[0,1])
  float cameraPos[3]{0, 0, 0};
  RenderMode mode{RenderMode::Shaded};
  float clearColor[4]{0.12f, 0.12f, 0.13f, 1.0f};
  int highlightMeshIndex{-1};  // draw a wireframe overlay on this mesh (-1 = none)

  // Debug helper lines (grid / axes / bounding boxes), world space, depth-tested
  // so geometry occludes them.
  const HelperVertex* helperLines{nullptr};
  int helperLineVertexCount{0};  // total vertices (2 per segment)

  // Overlay lines (skeleton bones), world space, drawn on top with depth testing
  // disabled so they stay visible through solid geometry (X-ray).
  const HelperVertex* overlayLines{nullptr};
  int overlayLineVertexCount{0};  // total vertices (2 per segment)

  // Per-mesh visibility mask (index i <-> the i-th appended mesh, same order as
  // highlightMeshIndex). null = all meshes visible. Applies to the raster paths
  // (GL + VK raster); the VK ray-tracing path traces the whole TLAS.
  const uint8_t* meshVisible{nullptr};
  int meshVisibleCount{0};
};

// Opaque texture handle for ImGui::Image. GL: a GLuint texture id. Vulkan: a
// VkDescriptorSet. Both fit in 64 bits (ImTextureID is ImU64).
using ViewportTexHandle = uint64_t;

class Renderer {
 public:
  virtual ~Renderer() = default;

  // Create device resources. `window` is the GLFW window (Vulkan creates its
  // surface from it; GL assumes its context is already current). A null window
  // requests a windowless/offscreen backend (Vulkan only): no surface, no
  // swapchain — the composited frame is rendered to an offscreen target sized by
  // setHeadlessSize() and retrieved via captureWindow().
  virtual bool init(GLFWwindow* window, std::string* err) = 0;

  // Set the headless composite (full-window) size in pixels. Must be called
  // before init() when `window` is null. No-op for backends that need a window.
  virtual void setHeadlessSize(int /*w*/, int /*h*/) {}

  // Wire up the ImGui platform+renderer backends. Call after ImGui::CreateContext().
  virtual bool initImGui(std::string* err) = 0;

  // --- Incremental scene upload (for progressive / lazy loading) ---
  // Reset the GPU scene, set materials, and reserve `textureCount` texture slots
  // (each starts as white until filled by uploadTexture). Materials reference
  // texture slots by index.
  virtual void beginScene(const std::vector<DrawMaterialCPU>& materials,
                          int textureCount) = 0;
  // Append one mesh (uploaded immediately). Rendered from the next frame on.
  virtual void appendMesh(const DrawMeshCPU& mesh) = 0;
  // Fill texture slot `slot`; materials referencing it switch from white to it.
  virtual void uploadTexture(int slot, const DrawTextureCPU& tex) = 0;
  virtual void uploadSkinningFrame(const SkinningFrameCPU& /*skin*/) {}
  // Replace mesh `meshIndex`'s vertex buffer in place (same vertex count) — used
  // for per-frame GPU blendshape morph (positions/normals re-derived on the CPU
  // from the rest pose, then GPU-skinned). No-op if unsupported or size differs.
  virtual void updateMeshVertices(int /*meshIndex*/,
                                  const std::vector<DrawVertex>& /*verts*/) {}

  // Convenience: upload an entire scene in one call (used by the headless /
  // synchronous path so screenshots are deterministic).
  bool uploadScene(const DrawScene& scene, std::string* /*err*/) {
    beginScene(scene.materials, static_cast<int>(scene.textures.size()));
    for (size_t i = 0; i < scene.textures.size(); ++i) {
      uploadTexture(static_cast<int>(i), scene.textures[i]);
    }
    for (const auto& m : scene.meshes) appendMesh(m);
    return true;
  }

  // Resize the offscreen viewport target (dock content size). Clamped to >= 1.
  virtual void resizeViewport(int width, int height) = 0;

  // Per-frame backend ImGui new-frame (ImGui_ImplOpenGL3/Vulkan_NewFrame).
  virtual void newFrame() = 0;

  // Render the 3D scene with `params`. GL renders to its FBO immediately; Vulkan
  // records the parameters and renders during present().
  virtual void renderFrame(const RenderFrameParams& params) = 0;

  // Texture handle of the offscreen color target for ImGui::Image.
  virtual ViewportTexHandle viewportTexture() const = 0;

  // Composite: draw the current ImGui draw data to the window and present/swap.
  // Must be called after ImGui::Render().
  virtual void present() = 0;

  // Read back the offscreen 3D viewport as top-down RGBA8 (headless QA).
  // Returns false if unsupported.
  virtual bool captureViewport(std::vector<uint8_t>* rgba, int* w, int* h) {
    (void)rgba;
    (void)w;
    (void)h;
    return false;
  }

  // Ask the next present() to grab the composited window (back buffer) so it can
  // be retrieved with captureWindow() afterwards. No-op if unsupported.
  virtual void requestWindowCapture() {}

  // Return the window grabbed by the most recent requestWindowCapture()+present(),
  // as top-down RGBA8. Returns false if unsupported / nothing captured.
  virtual bool captureWindow(std::vector<uint8_t>* rgba, int* w, int* h) {
    (void)rgba;
    (void)w;
    (void)h;
    return false;
  }

  virtual const RendererCaps& caps() const = 0;

  // --- Ray tracing (Vulkan only) ---
  // True when the device supports the RT extensions and a ray-tracing technique
  // is available. The GL backend always returns false.
  virtual bool rayTracingAvailable() const { return false; }
  // Whether the ray-tracing technique is currently active.
  virtual bool rayTracingActive() const { return false; }
  // Switch the active technique between rasterization (false) and ray tracing
  // (true). No-op / ignored when ray tracing is unavailable. Both techniques
  // consume the same uploaded scene, so toggling needs no reload.
  virtual void setRayTracing(bool /*enable*/) {}

  // Tear down ImGui backend + device resources.
  virtual void shutdown() = 0;
};

std::unique_ptr<Renderer> CreateGLRenderer();
#if defined(HAVE_VULKAN)
std::unique_ptr<Renderer> CreateVulkanRenderer();
#endif

}  // namespace tusdview
