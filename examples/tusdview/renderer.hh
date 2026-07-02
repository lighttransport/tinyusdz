// SPDX-License-Identifier: Apache-2.0
// tusdview - abstract Renderer interface (the GL/Vulkan boundary).
//
// Both backends render the 3D scene into an *offscreen* color target and expose
// it as an opaque texture handle. The GUI shows that handle via ImGui::Image in
// the "Viewport" dock window, so the 3D view participates in docking. All
// backend-specific ImGui wiring (NewFrame/Init/RenderDrawData, swap/present)
// lives behind this interface so the app main-loop is backend-agnostic.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "gpu_scene.hh"
#include "rt_lod.hh"  // RtLodCamera (view-dependent RT LOD)

struct GLFWwindow;
struct ImDrawData;

namespace tusdview {

enum class Backend { GL, Vulkan };

// Shaded/Wireframe + debug AOVs. Normals = the shading normal used by the lit path;
// GeomNormal = the geometric face normal; Uv = texcoord set 0; Depth = camera
// distance normalized by RenderFrameParams::depthScale.
enum class RenderMode : int {
  Shaded = 0,
  Wireframe = 1,
  Normals = 2,       // shading normal
  MaterialId = 3,
  GeomNormal = 4,    // geometric face normal
  Uv = 5,            // texcoord set 0
  Depth = 6,         // camera distance / scene-bbox diagonal
  Albedo = 7,        // unlit base color (material baseColor x displayColor)
  Facing = 8,        // front (green) / back (red) by face orientation
  Roughness = 9,
  Metallic = 10,
  Emissive = 11,
  Opacity = 12,
  Position = 13,     // world position normalized to scene bbox
  Barycentric = 14,
  PrimId = 15,       // triangle id (hashed color)
  MeshId = 16,       // per-mesh/prim id (hashed color)
  Facing2 = 17,      // reserved
  Purpose = 18,      // default/render/proxy/guide
  MissingNormals = 19,  // meshes with no authored normals
  DoubleSided = 20,
  SkinWeights = 21,  // dominant joint (hashed) tinted by weight
  Tangent = 22,
  UvChecker = 23,
  AmbientOcclusion = 24,  // ray-traced (RT backends)
  Curvature = 25,         // screen-space normal variation (raster)
  InstanceId = 26,        // per-instance hashed color (instanced geom)
  BvhHeatmap = 27,        // BVH traversal cost (CUDA software tracer)
  SoftShadow = 28,        // ray-traced soft shadow / sky visibility (RT backends)
  Kind = 29,              // USD model kind (component/group/assembly/subcomponent)
  UdimTile = 30,          // UDIM tile id from UV set 0 (hashed color)
  Uv1 = 31,               // texcoord set 1 (multi-UV; raster)
  BlendInfluence = 32,    // per-vertex blendshape displacement magnitude (raster)
  TexelDensity = 33,      // UV-to-world area ratio (view-independent texel density)
  SourceFaceId = 34,      // original USD face id before triangulation (hashed)
};
enum class SkinningMode : int { Auto = 0, CPU = 1, GPU = 2 };

// Unlit, vertex-colored line vertex for debug helpers (grid, axes, bboxes).
// Drawn as GL_LINES / VK_PRIMITIVE_TOPOLOGY_LINE_LIST in world space.
struct HelperVertex {
  float pos[3];
  float col[3];
};

struct RendererCaps {
  const char* backend_name{""};
  std::string gpu_name;
  std::string api_info;
  bool usesZeroToOneDepth{false};  // Vulkan clip space Z in [0,1]; GL in [-1,1]
  bool flipViewportV{false};       // GL FBO textures are bottom-up
  bool supportsRayTracing{false};  // device has the RT extensions (Vulkan only)
  bool supportsGpuSkinning{false};
  bool supportsExtendedGpuSkinning{false};  // texture-backed >4 influences
};

struct RenderFrameParams {
  const float* view{nullptr};  // column-major 4x4 (light3d::Mat4 layout)
  const float* proj{nullptr};  // column-major 4x4 (GL: Z[-1,1]; VK: Z[0,1])
  float cameraPos[3]{0, 0, 0};
  RenderMode mode{RenderMode::Shaded};
  // Wireframe overlay state, cycled with the 'w' key (GL backend):
  //   0 = off (shaded fill only)
  //   1 = wireframe only (hidden-line: depth-only fill, then polygon edges)
  //   2 = wireframe + shading (shaded fill, then polygon edges on top)
  // Edges are the ORIGINAL polygon edges of the base (pre-tessellation) mesh --
  // triangulation diagonals are dropped via per-triangle source face ids.
  int wireMode{0};
  float clearColor[4]{0.12f, 0.12f, 0.13f, 1.0f};
  float depthScale{1.0f};  // Depth AOV: normalize camera distance by this (scene extent)
  float sceneMin[3]{0, 0, 0};     // Position AOV: scene bbox min
  float sceneExtent[3]{1, 1, 1};  // Position AOV: scene bbox size (max-min)
  int highlightMeshIndex{-1};  // draw a wireframe overlay on this mesh (-1 = none)
  // When set, the highlight overlay draws only these triangle vertex indices (a
  // selected GeomSubset of highlightMeshIndex) instead of the whole mesh.
  const uint32_t* highlightIndices{nullptr};
  int highlightIndexCount{0};
  // Selection highlight as world-space orange edge lines (2 verts/segment). The
  // GL backend uses the polygon-mode overlay above; the Vulkan backend, which has
  // no wireframe pass, draws these through its line pipeline instead.
  const HelperVertex* highlightLines{nullptr};
  int highlightLineVertexCount{0};

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
  // (GL + VK raster); the VK ray-tracing path traces the whole TLAS (it must
  // keep off-screen geometry for shadow/AO rays, so this frustum-culled raster
  // mask cannot drive it) EXCEPT for whole USD purposes, filtered below.
  const uint8_t* meshVisible{nullptr};
  int meshVisibleCount{0};

  // Visible USD purposes, bit i = PurposeId i (0 default, 1 render, 2 proxy,
  // 3 guide). The VK ray-tracing path leaves meshes of hidden purposes out of
  // the TLAS entirely (a purpose toggle triggers a rebuild). Without this,
  // Caldera's guide breadcrumb planes -- hidden in raster -- engulf the RT
  // camera ("--rt renders near-blank"). Default matches the GUI: guide hidden.
  uint32_t purposeVisibleMask{0xBu};

  // Surface displacement (UsdPreviewSurface inputs:displacement). When enabled, a
  // material's displacement (constant or height-map red channel) offsets the
  // surface along its normal by displacementScale in the raster vertex/tess shader
  // (coarse, no extra geometry). maxTessLevel > 1 enables GPU tessellation for
  // adaptive sub-triangle detail (0/1 = coarse per-vertex only). Geometric normals
  // are used on displaced surfaces so shading follows the deformed geometry.
  bool displacement{true};
  float displacementScale{1.0f};
  int maxTessLevel{1};
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

  // Resize the headless composite at runtime (recreate the offscreen swap images
  // + framebuffers). Returns false if unsupported / not headless. The caller must
  // also update ImGui's DisplaySize. No-op for backends that need a window.
  virtual bool resizeHeadless(int /*w*/, int /*h*/) { return false; }

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
  // Append one UsdVol volume (OpenVDB). Default: no-op (backend has no volume
  // support yet; GL implements raymarching, VK/CUDA are placeholders).
  virtual void appendVolume(const DrawVolumeCPU& /*vol*/) {}
  // Fill texture slot `slot`; materials referencing it switch from white to it.
  virtual void uploadTexture(int slot, const DrawTextureCPU& tex) = 0;
  virtual void uploadSkinningFrame(const SkinningFrameCPU& /*skin*/) {}
  // Per-instance frustum culling: replace mesh `meshIndex`'s drawn instance set
  // with `count` visible instances (xforms = 12 floats/instance, 3x4 o2w row-major;
  // colors = 3 floats/instance or null to keep the existing per-instance colors).
  // count == instanceCount restores the full set. No-op for non-instanced meshes
  // or backends that flatten instances. Called each frame the view changes.
  virtual void updateInstanceVisibility(size_t /*meshIndex*/,
                                        const float* /*xforms*/,
                                        const float* /*colors*/,
                                        uint32_t /*count*/) {}
  // Replace mesh `meshIndex`'s vertex buffer in place (same vertex count) — used
  // for per-frame GPU blendshape morph (positions/normals re-derived on the CPU
  // from the rest pose, then GPU-skinned). No-op if unsupported or size differs.
  virtual void updateMeshVertices(int /*meshIndex*/,
                                  const std::vector<DrawVertex>& /*verts*/) {}
  // Upload mesh `meshIndex`'s per-channel blendshape coefficients (one float per
  // morph channel) for GPU-side morphing in the raster vertex shader. Only the
  // tiny coefficient buffer updates per frame — no vertex re-upload, no GPU stall.
  // No-op on backends that bake morph into geometry (ray tracing) or lack morph.
  virtual void updateMorphWeights(int /*meshIndex*/,
                                  const std::vector<float>& /*coeffs*/) {}
  // Replace mesh `meshIndex`'s world transform (column-major float[16]) — used
  // for per-frame node/xform animation alongside GPU skinning. No-op if
  // unsupported.
  virtual void updateMeshWorld(int /*meshIndex*/, const float /*world*/[16]) {}
  // Replace an entire mesh (vertices + indices + submeshes) — used by adaptive
  // re-tessellation where the vertex/index count may change. The mesh at
  // `meshIndex` is deleted and re-created with the new data.
  virtual void replaceMesh(int /*meshIndex*/, const DrawMeshCPU& /*mesh*/) {}
  // Number of uploaded meshes. `updateMeshVertices`/`updateMeshWorld` index by
  // DrawScene mesh order, which only matches when this equals the DrawScene
  // mesh count; callers verify before per-index updates.
  virtual int meshCount() const { return 0; }

  // Raster view-dependent LOD box proxies (optimization B). supportsProxyDraw()
  // gates whether the cull emits proxies at all; updateProxyInstances uploads the
  // shared per-frame set (`xforms`: 12 floats/proxy box-fit o2w; `tints`: 3
  // floats/proxy) drawn in one instanced call. Default: unsupported / no-op.
  virtual bool supportsProxyDraw() const { return false; }
  virtual void updateProxyInstances(const float* /*xforms*/, const float* /*tints*/,
                                    uint32_t /*count*/) {}

  // Convenience: upload an entire scene in one call (used by the headless /
  // synchronous path so screenshots are deterministic).
  bool uploadScene(const DrawScene& scene, std::string* /*err*/) {
    beginScene(scene.materials, static_cast<int>(scene.textures.size()));
    for (size_t i = 0; i < scene.textures.size(); ++i) {
      uploadTexture(static_cast<int>(i), scene.textures[i]);
    }
    static const bool timeit = std::getenv("TUSDVIEW_TIME_UPLOAD") != nullptr;
    if (timeit) {
      const auto t0 = std::chrono::steady_clock::now();
      auto last = t0;
      for (size_t i = 0; i < scene.meshes.size(); ++i) {
        appendMesh(scene.meshes[i]);
        if (((i + 1) % 5000) == 0 || i + 1 == scene.meshes.size()) {
          const auto now = std::chrono::steady_clock::now();
          const double tot =
              std::chrono::duration<double>(now - t0).count();
          const double dt =
              std::chrono::duration<double>(now - last).count();
          last = now;
          std::fprintf(stderr,
                       "[upload] meshes %zu/%zu  +%.2fs (total %.2fs)\n",
                       i + 1, scene.meshes.size(), dt, tot);
        }
      }
    } else {
      for (const auto& m : scene.meshes) appendMesh(m);
    }
    for (const auto& v : scene.volumes) appendVolume(v);
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

#if defined(TUSDVIEW_ENABLE_GL_THREAD)
  // --- Experimental threaded GL rendering (TUSDVIEW_ENABLE_GL_THREAD) ---
  // Threaded composite: like present() but renders the supplied (deep-copied) ImGui
  // draw data using the framebuffer size queried on the main thread, so it can run
  // on the render thread (glfwGetFramebufferSize is main-thread-only).
  virtual void presentThreaded(ImDrawData* /*drawData*/, int /*fbW*/, int /*fbH*/) {}
  // Threaded init split: the platform half (GLFW callbacks/input) runs on the main
  // thread; the backend half (GL/VK objects) on the render thread which owns the
  // context. Default = combined initImGui (single-thread path). `window` is passed
  // here (not just to init()) because init() runs later on the render thread, so the
  // backend must capture the window handle on the main thread for the GLFW backend.
  virtual bool initImGuiPlatform(GLFWwindow* /*window*/, std::string* /*err*/) { return true; }
  virtual bool initImGuiBackend(std::string* /*err*/) { return true; }
#endif

  // GPU video-memory usage in MB (used, total). Returns false if the backend
  // can't report it. Used by the Stats panel.
  virtual bool gpuMemoryMB(size_t* /*usedMB*/, size_t* /*totalMB*/) const {
    return false;
  }

  // Current offscreen viewport (color target) size in pixels. Default no-op.
  virtual void viewportSize(int* w, int* h) const {
    if (w) *w = 0;
    if (h) *h = 0;
  }

  // Upload an externally-traced RGBA8 image (R8G8B8A8_UNORM, w*h*4, top-down) into
  // the offscreen color target for display this frame. Used by the interactive
  // HIP/CUDA path, which traces on the GPU outside Vulkan: the next present()
  // composites this image instead of running the raster/RT 3D pass. Returns false
  // if unsupported. The flag is consumed by a single present().
  virtual bool uploadViewportImage(const uint8_t* /*rgba*/, int /*w*/, int /*h*/) {
    return false;
  }

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

  // View-dependent RT LOD: supply the camera snapshot used to classify instances
  // as Full / Proxy / Cull when the TLAS is (re)built. `reselect` requests a TLAS
  // rebuild now (call it when the camera has settled). focalPx is filled by the
  // backend from its own viewport height; the caller leaves it 0. No-op unless
  // the ray-tracing backend supports it.
  virtual void setLodCamera(const RtLodCamera& /*cam*/, bool /*reselect*/) {}

  // Tear down ImGui backend + device resources.
  virtual void shutdown() = 0;
};

std::unique_ptr<Renderer> CreateGLRenderer();
#if defined(HAVE_VULKAN)
std::unique_ptr<Renderer> CreateVulkanRenderer();
#endif

}  // namespace tusdview
