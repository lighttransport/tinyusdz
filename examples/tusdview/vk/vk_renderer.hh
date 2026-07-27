// SPDX-License-Identifier: Apache-2.0
// tusdview - Vulkan backend. Renders the scene into an offscreen VkImage which
// is shown by the GUI via ImGui::Image (ImGui_ImplVulkan_AddTexture). ImGui
// itself renders into the swapchain.
//
// Raster path: vertex/index buffers, material textures, and GPU skinning data
// descriptors. Optional ray-query path is built when Vulkan RT support exists.
#pragma once

// Vulkan entry points are resolved at runtime via volk (cuew-style): we never
// link the Vulkan loader. volk.h pulls in <vulkan/vulkan.h> with
// VK_NO_PROTOTYPES and declares the vk* symbols as function pointers that
// volkInitialize()/volkLoadInstance()/volkLoadDevice() populate.
#include "volk.h"

#include <cstdint>
#include <string>
#include <vector>

#include "gpu_scene.hh"
#include "raster_lighting.hh"
#include "renderer.hh"

namespace tusdview {

class VulkanRenderer final : public Renderer {
 public:
  VulkanRenderer() = default;
  ~VulkanRenderer() override;

  void setDevicePreference(const RendererDevicePreference& preference) override {
    devicePreference_ = preference;
  }
  bool init(GLFWwindow* window, std::string* err) override;
  void setHeadlessSize(int w, int h) override {
    if (w > 0) headlessW_ = w;
    if (h > 0) headlessH_ = h;
    // Headless never calls resizeViewport(), so seed the viewport extent here too:
    // the offscreen target IS the headless size, and consumers like the RT-LOD
    // focalPx (0.5 * vpH_ * proj_[5]) would otherwise see vpH_ == 0 -> focalPx 0
    // -> every instance projects to 0px and is size-culled.
    if (w > 0) vpW_ = w;
    if (h > 0) vpH_ = h;
  }
  bool resizeHeadless(int w, int h) override;
  bool initImGui(std::string* err) override;
  void beginScene(const std::vector<DrawMaterialCPU>& materials, int textureCount) override;
  void setLights(const std::vector<DrawLightCPU>& lights,
                 size_t meshCount) override;
  void appendMesh(const DrawMeshCPU& mesh) override;
  void appendPoints(const DrawPointsCPU& points) override;
  void appendCurves(const DrawCurvesCPU& curves) override;
  void appendVolume(const DrawVolumeCPU& vol) override;
  void uploadTexture(int slot, const DrawTextureCPU& tex) override;
  void uploadSkinningFrame(const SkinningFrameCPU& skin) override;
  void updateMeshVertices(int meshIndex,
                          const std::vector<DrawVertex>& verts) override;
  bool updateMeshSkinningGpu(int meshIndex, const float* mats, int jointCount,
                             int matrixBase, const float aabbMin[3],
                             const float aabbMax[3]) override;
  void updateMorphWeights(int meshIndex,
                          const std::vector<float>& coeffs) override;
  void updateInstanceVisibility(size_t meshIndex, const float* xforms,
                                const float* colors, const float* opacities,
                                uint32_t count) override;
  // Raster LOD box proxies (optimization B): the VK raster path collapses distant
  // instances to a shared unit cube (box-fit per instance) instead of dropping them.
  bool supportsProxyDraw() const override { return true; }
  void updateProxyInstances(const float* xforms, const float* tints,
                            uint32_t count) override;
  void updateMeshWorld(int meshIndex, const float world[16]) override;
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
  void viewportSize(int* w, int* h) const override {
    if (w) *w = vpW_;
    if (h) *h = vpH_;
  }
  bool uploadViewportImage(const uint8_t* rgba, int w, int h) override;
  bool captureViewport(std::vector<uint8_t>* rgba, int* w, int* h) override;
  bool captureWindow(std::vector<uint8_t>* rgba, int* w, int* h) override;  // headless composite
  const RendererCaps& caps() const override { return caps_; }
  bool rayTracingAvailable() const override { return rtSupported_; }
  bool rayTracingActive() const override { return rtActive_; }
  void setRayTracing(bool enable) override;
  void setLodCamera(const RtLodCamera& cam, bool reselect) override;
  void shutdown() override;

 private:
  // Keep the CPU from queueing a second frame ahead of input. The swapchain can
  // still own multiple images, but tusdview records/submits at most one frame at
  // a time for lower interactive latency.
  static constexpr int kFramesInFlight = 1;

  struct VkMeshGPU {
    VkBuffer vbo{VK_NULL_HANDLE};
    VkDeviceMemory vboMem{VK_NULL_HANDLE};
    // Optional ray-tracing vertex buffer: the BLAS + ray-query hit read this
    // (via vboAddr) for displacement-baked or dynamically-skinned RT geometry,
    // while the raster path keeps using `vbo` as rest geometry.
    VkBuffer vboDisp{VK_NULL_HANDLE};
    VkDeviceMemory vboDispMem{VK_NULL_HANDLE};
    VkBuffer jointVbo{VK_NULL_HANDLE};
    VkDeviceMemory jointVboMem{VK_NULL_HANDLE};
    VkBuffer weightVbo{VK_NULL_HANDLE};
    VkDeviceMemory weightVboMem{VK_NULL_HANDLE};
    VkBuffer influenceVbo{VK_NULL_HANDLE};
    VkDeviceMemory influenceVboMem{VK_NULL_HANDLE};
    VkBuffer uv1Vbo{VK_NULL_HANDLE};         // 2nd texcoord set (binding 4, vec2/vtx)
    VkDeviceMemory uv1VboMem{VK_NULL_HANDLE};
    VkBuffer morphInflVbo{VK_NULL_HANDLE};   // blendshape influence (binding 5, float/vtx)
    VkDeviceMemory morphInflVboMem{VK_NULL_HANDLE};
    VkBuffer influenceDataBuf{VK_NULL_HANDLE};
    VkDeviceMemory influenceDataMem{VK_NULL_HANDLE};
    VkDescriptorSet influenceDesc{VK_NULL_HANDLE};
    // GPU blendshape morph (raster): per-vertex (offset,count) attribute (binding 6,
    // loc 8) plus delta/coefficient/channel SSBOs in deform set 1. hasMorph gates
    // it; non-morph meshes bind dummy buffers.
    VkBuffer morphOffsetVbo{VK_NULL_HANDLE};     // binding 6: uvec2 (offset,count)
    VkDeviceMemory morphOffsetVboMem{VK_NULL_HANDLE};
    VkBuffer morphDeltaBuf{VK_NULL_HANDLE};       // set 1/binding 2 SSBO
    VkDeviceMemory morphDeltaMem{VK_NULL_HANDLE};
    VkDescriptorSet morphDeltaDesc{VK_NULL_HANDLE};
    VkBuffer morphCoeffBuf{VK_NULL_HANDLE};       // set 1/binding 3 SSBO
    VkDeviceMemory morphCoeffMem{VK_NULL_HANDLE};
    VkDescriptorSet morphCoeffDesc{VK_NULL_HANDLE};
    void* morphCoeffMapped{nullptr};
    VkBuffer morphChanBuf{VK_NULL_HANDLE};         // set 1/binding 4 SSBO
    VkDeviceMemory morphChanMem{VK_NULL_HANDLE};
    VkDescriptorSet morphChanDesc{VK_NULL_HANDLE};
    int morphChannelCount{0};
    bool hasMorph{false};
    VkBuffer ebo{VK_NULL_HANDLE};
    VkDeviceMemory eboMem{VK_NULL_HANDLE};
    std::vector<DrawSubmesh> submeshes;
    float world[16];
    float localCentroid[3]{0, 0, 0};  // mesh-space bbox center (translucency sort)
    // Ray tracing (built when RT is supported): a BLAS over this mesh's
    // triangles plus device addresses + counts for the shader.
    VkAccelerationStructureKHR blas{VK_NULL_HANDLE};
    VkBuffer blasBuf{VK_NULL_HANDLE};
    VkDeviceMemory blasMem{VK_NULL_HANDLE};
    VkDeviceAddress blasAddr{0};
    // Dynamic (per-frame deformed) BLAS: updateMeshVertices marks the mesh, its
    // BLAS is then built with ALLOW_UPDATE (and skips compaction -- a compacted
    // AS cannot be refit), and later poses REFIT it in place (MODE_UPDATE)
    // instead of destroy + full rebuild. The refit scratch is persistent: same
    // size every frame, so allocate once and keep it until the BLAS dies.
    bool blasDynamic{false};        // sticky: describes the mesh, not the AS
    bool blasRefitPending{false};   // vertices changed since last build/refit
    VkBuffer blasScratchBuf{VK_NULL_HANDLE};
    VkDeviceMemory blasScratchMem{VK_NULL_HANDLE};
    VkDeviceSize blasUpdateScratchSize{0};
    // Opt-in GPU compute skinning (skin.comp, TUSDVIEW_RT_GPU_SKIN=1):
    // persistently-mapped per-mesh SSBO of composed skinning matrices
    // (16 floats each), re-filled per frame by updateMeshSkinningGpu.
    VkBuffer skinMatBuf{VK_NULL_HANDLE};
    VkDeviceMemory skinMatMem{VK_NULL_HANDLE};
    VkDeviceAddress skinMatAddr{0};
    void* skinMatMapped{nullptr};
    uint32_t skinMatCapacity{0};  // capacity in matrices
    VkDeviceAddress vboAddr{0};
    VkDeviceAddress eboAddr{0};
    uint32_t vertexCount{0};
    uint32_t indexCount{0};
    int matId{-1};       // material id of the mesh's first submesh (RT shading)
    float normalMat[9];  // inverse-transpose of world 3x3 (object->world normals)
    bool skinned{false};
    bool extendedSkinned{false};
    // RT instancing + displayColor + authored-normal flag (mirrors the GL path).
    VkBuffer vtxColorBuf{VK_NULL_HANDLE};   // displayColor+displayOpacity (vec4[])
    VkDeviceMemory vtxColorMem{VK_NULL_HANDLE};
    VkDeviceAddress vtxColorAddr{0};
    // Retained descriptor for RT/compatibility; raster binds vtxColorBuf as an
    // ordinary vec4 vertex stream to avoid descriptor-dependent color loss.
    VkDescriptorSet vtxColorDesc{VK_NULL_HANDLE};
    VkDeviceAddress uv1Addr{0};              // uv1 SSBO address (RT multi-UV AOV)
    VkDeviceAddress inflAddr{0};             // influence SSBO address (RT influence AOV)
    VkBuffer faceBuf{VK_NULL_HANDLE};        // per-triangle source face id (uint[])
    VkDeviceMemory faceMem{VK_NULL_HANDLE};
    VkDeviceAddress faceAddr{0};             // RT source-face-id AOV
    // Optional per-triangle material ids for GeomSubset meshes. Single-material
    // meshes keep this null and use matId directly in the ray-query shader.
    VkBuffer triMatBuf{VK_NULL_HANDLE};
    VkDeviceMemory triMatMem{VK_NULL_HANDLE};
    VkDeviceAddress triMatAddr{0};
    VkDeviceAddress jointAddr{0};            // RT skin-weights AOV (joint ids)
    VkDeviceAddress weightAddr{0};           // RT skin-weights AOV (weights)
    // Optional compact primitive-range table for RT material lookup. Allocated
    // only for multi-material or distinct-back-material meshes.
    VkBuffer rtSubmeshBuf{VK_NULL_HANDLE};
    VkDeviceMemory rtSubmeshMem{VK_NULL_HANDLE};
    VkDeviceAddress rtSubmeshAddr{0};
    uint32_t rtSubmeshCount{0};
    VkDescriptorSet faceDesc{VK_NULL_HANDLE}; // raster source-face-id (set 3); else dummy
    // Consolidated raster set 1: bones, extended influences, morph buffers,
    // displayColor and source-face ids. Replaces seven separately-bound sets.
    VkDescriptorSet deformDesc{VK_NULL_HANDLE};
    bool geometricNormal{false};            // no authored normals -> geometric face normal
    bool doubleSided{false};                // double-sided AOV flag
    int purposeId{0};                       // purpose AOV: 0=default/1=render/2=proxy/3=guide
    int kindId{0};                          // kind AOV: 0=none/1=component/2=group/3=assembly/4=subcomponent
    float flatColor[3]{0.8f, 0.8f, 0.8f};   // per-draw constant tint (instanced path)
    float flatOpacity{1.0f};
    // Prototype object-space AABB (for RT view-dependent LOD: per-instance
    // projected size + the box-fit proxy transform). Captured from DrawMeshCPU.
    float protoAabbMin[3]{0, 0, 0};
    float protoAabbMax[3]{0, 0, 0};
    // GPU instancing: one TLAS instance per 3x4 o2w in instanceXforms (12 floats
    // each); instanceColors is 3 floats/instance (empty -> use flatColor), and
    // instanceOpacities is 1 float/instance (empty -> use flatOpacity). Held on
    // the CPU and consumed in rebuildTlas (the TLAS instance array is the GPU copy).
    std::vector<float> instanceXforms;
    std::vector<float> instanceColors;
    std::vector<float> instanceOpacities;
    bool hasTranslucentInstances{false};
    // Raster GPU instancing (flat --next path): per-instance 3x4 o2w (instVbo,
    // 48B/inst, host-visible so per-instance culling can re-map the visible subset)
    // + per-instance RGBA color/opacity (instColorBuf) + per-vertex prototype color
    // (instVtxColorBuf, white when absent). drawInstanceCount is the count actually
    // drawn (== instanceCount unless per-instance culling shrank it this frame).
    VkBuffer instVbo{VK_NULL_HANDLE};
    VkDeviceMemory instVboMem{VK_NULL_HANDLE};
    // Persistent host mapping when instVbo/instColorBuf are pool-suballocated (their
    // *Mem is VK_NULL_HANDLE then): per-instance culling writes the compacted subset
    // through these instead of vkMapMemory. Null for the legacy per-buffer path.
    void* instVboMapped{nullptr};
    void* instColorMapped{nullptr};
    VkBuffer instColorBuf{VK_NULL_HANDLE};
    VkDeviceMemory instColorMem{VK_NULL_HANDLE};
    VkBuffer instVtxColorBuf{VK_NULL_HANDLE};
    VkDeviceMemory instVtxColorMem{VK_NULL_HANDLE};
    uint32_t instanceCount{0};
    uint32_t drawInstanceCount{0};
    // Multi-draw-indirect (MDI) placement. When mdiEligible (device supports MDI
    // and this prototype is non-morph), the instance-rate buffers above point into
    // the renderer's ONE global instance buffer rather than a per-mesh allocation:
    // instVbo/instColorBuf are the shared handles and instVboMapped/instColorMapped
    // address this mesh's slice, so the existing cull writes land in the global
    // buffer unchanged. mdiInstFirst is the mesh's base instance index (the indirect
    // command's firstInstance); mdiVertBase/mdiIdxBase are its base into the shared
    // geometry/index buffers (vertexOffset/firstIndex). Morph prototypes stay on the
    // per-mesh path (MDI can't switch morph descriptor sets per draw).
    bool mdiEligible{false};
    uint32_t mdiInstFirst{0};
    uint32_t mdiVertBase{0};
    uint32_t mdiIdxBase{0};
    // Coarse per-prototype instance grid for RT LOD cell rejection (P5). Built
    // lazily on the first LOD-enabled rebuild; instance transforms are static.
    RtLodGrid lodGrid;
    bool lodGridTried{false};
  };

  // setup helpers
  bool createInstance(std::string* err);
  bool createSurface(std::string* err);
  bool pickPhysicalDevice(std::string* err);
  bool createDevice(std::string* err);
  bool createSwapchain(std::string* err);
  bool createSwapchainViews(std::string* err);
  // Windowless substitute for the swapchain: kFramesInFlight color images we own
  // (color-attachment + transfer-src) that the ImGui composite pass renders into
  // and captureWindow() reads back.
  bool createHeadlessSwapchain(std::string* err);
  bool createSwapchainRenderPass(std::string* err);
  bool createSwapchainFramebuffers(std::string* err);
  bool createCommands(std::string* err);
  bool createSync(std::string* err);
  // (Re)create the per-swapchain-image sync (renderFinished_ semaphores + clear
  // imagesInFlight_). Destroys any existing ones first; call after the swapchain (and
  // its image count) is (re)created. Requires the device to be idle.
  bool createPerImageSync(std::string* err);
  bool createOffscreenRenderPass(std::string* err);
  bool createOverlayLoadPass(std::string* err);  // LOAD pass for overlays over RT
  // Upload + draw one HelperVertex line set with the given pipeline.
  void drawLineSet(VkCommandBuffer cb, const std::vector<HelperVertex>& copy,
                   VkBuffer* buf, VkDeviceMemory* mem, VkDeviceSize* cap,
                   VkPipeline pipeline, const float vp[16]);
  bool createPipeline(std::string* err);
  void createTessPipeline();  // best-effort GPU-tessellation pipeline (non-fatal)
  bool createInstPipeline(std::string* err);
  bool createLinePipeline(std::string* err);
  bool createSampler(std::string* err);
  bool createDescriptorInfra(std::string* err);
  VkDescriptorSet allocDeformDescriptor(const VkMeshGPU& mesh);
  void refreshDeformDescriptors();
  void updateMaterialDescriptor(size_t materialId);
  void refreshMaterialDescriptors();
  bool createWhiteTexture(std::string* err);

  // --- Ray tracing (ray query) ---
  void detectRtSupport();              // sets rtSupported_ + loads RT entrypoints
  bool createRtResources(std::string* err);  // descriptor layout/pool + pipeline
  void destroyRt();
  VkDeviceAddress bufferDeviceAddress(VkBuffer buf) const;
  bool createDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer* buf, VkDeviceMemory* mem);  // device-local + addr
  // Device-local buffer initialized from `data` via a one-shot staging copy (no
  // device address). For static geometry that the GPU reads every frame -- keeps
  // it in VRAM instead of host-visible memory fetched over PCIe per draw.
  bool createDeviceLocalBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               const void* data, VkBuffer* buf, VkDeviceMemory* mem);
  void destroyBlas(VkMeshGPU& m);
  bool meshHasAlphaMask(const VkMeshGPU& m) const;
  bool meshHasBoundMaterial(const VkMeshGPU& m) const;
  void buildBlas(VkMeshGPU& m);
  void refitBlas(VkMeshGPU& m);         // MODE_UPDATE in-place rebuild (dynamic BLAS)
  void buildBoxBlas();                  // shared unit-cube BLAS for LOD box proxies
  void initBoxProxyRaster();            // static box geometry for raster LOD proxies
  void drawBoxProxies(VkCommandBuffer cb);  // upload + instanced draw of box proxies
  void rebuildTlas();                  // (re)build TLAS + MeshDesc/Material SSBOs
  void createRtImage();                // storage image sized to the viewport
  void traceRt(VkCommandBuffer cb);    // dispatch + copy into colorImg_

  void destroySwapchain();
  bool recreateSwapchain();
  // Shared body of present()/presentThreaded(): records + submits the frame using
  // the supplied ImGui draw data. present() passes ImGui::GetDrawData(); the threaded
  // path passes the packet's deep-copied draw data (+ the main-thread framebuffer
  // size) so it can run on the render thread.
  void presentImpl(ImDrawData* drawData, int fbW, int fbH);
  // ImGui Vulkan render-backend init (device objects); shared by initImGui() and the
  // threaded initImGuiBackend(). Needs init()'s device/queue/render pass.
  bool initVulkanImGuiBackend(std::string* err);
  void destroyOffscreen();
  void destroyScene();

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
  // Create a host-visible buffer initialised with `data`. When `poolable` is true the
  // backing memory is sub-allocated from a few large shared blocks (set *mem =
  // VK_NULL_HANDLE) instead of its own vkAllocateMemory -- the big-assembled-scene win
  // (the per-buffer alloc count, and thus the driver's O(n^2) allocation cost,
  // collapses). Only pass poolable for write-once static buffers, or buffers re-written
  // through `mappedOut` (a persistent host pointer to the sub-allocation); never for
  // buffers whose *mem is later vkMapMemory'd/updated. Pool blocks are freed in
  // destroyScene() after every per-buffer vkDestroyBuffer.
  bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                        VkBuffer* buf, VkDeviceMemory* mem, bool deviceAddress = false,
                        bool poolable = false, void** mappedOut = nullptr);
  // One large persistently-mapped host-visible block the above sub-allocates from.
  struct HostMemBlock {
    VkDeviceMemory mem{VK_NULL_HANDLE};
    VkDeviceSize size{0};
    VkDeviceSize used{0};
    void* mapped{nullptr};
    uint32_t memoryTypeIndex{0};
    bool deviceAddress{false};
  };
  std::vector<HostMemBlock> hostBlocks_;
  // Sub-allocate `size` (honouring `align`) from a block matching memoryTypeIndex +
  // deviceAddress; grows a new block when none fits. Returns the block memory + offset
  // + persistent mapped address. Returns false on allocation failure.
  bool poolSubAlloc(VkDeviceSize size, VkDeviceSize align, uint32_t memoryTypeIndex,
                    bool deviceAddress, VkDeviceMemory* outMem, VkDeviceSize* outOffset,
                    void** outMapped);
  void freeHostPool();  // unmap + free every block (after buffers are destroyed)
  bool createTextureImage(const light3d::Image& img, VkImage* outImg,
                          VkDeviceMemory* outMem, VkImageView* outView,
                          const std::vector<light3d::Image>* mips = nullptr,
                          bool srgb = false);
  bool createCompressedTextureImage(const DrawCompressedImageCPU& img, bool srgb,
                                    VkImage* outImg, VkDeviceMemory* outMem,
                                    VkImageView* outView);
  bool createUdimTextureArrayImage(const DrawTextureCPU& tex, VkImage* outImg,
                                   VkDeviceMemory* outMem,
                                   VkImageView* outView);
  bool createUdimLookupImage(const DrawTextureCPU& tex, VkImage* outImg,
                             VkDeviceMemory* outMem, VkImageView* outView);
  bool createUdimLookupAtlas(int rows);
  bool updateUdimLookupAtlasRow(int row, const DrawTextureCPU& tex);
  // DomeLight split-sum IBL (sets 21-23; 1x1 black cube/2D fallbacks).
  // `levels` are face-major float RGB cube levels (DomeIblCPU layout).
  bool createIblCubeImage(const std::vector<std::vector<float>>& levels,
                          int faceSize, VkImage* outImg, VkDeviceMemory* outMem,
                          VkImageView* outView);
  bool createIblLutImage(const std::vector<float>& rg, int size, VkImage* outImg,
                         VkDeviceMemory* outMem, VkImageView* outView);
  void destroyIblImages();
  bool createRgba32fTextureImage(int width, int height, const float* data,
                                 VkImage* outImg, VkDeviceMemory* outMem,
                                 VkImageView* outView);
  bool uploadRgba32fTextureImage(VkImage image, int width, int height,
                                 const float* data);
  VkDescriptorSet allocTexDescriptor(VkImageView view);
  VkDescriptorSet allocSkinDescriptor(VkBuffer buffer, VkDeviceSize size);
  VkDescriptorSet allocInfluenceDescriptor(VkBuffer buffer, VkDeviceSize size);
  VkCommandBuffer beginOneShot();
  void endOneShot(VkCommandBuffer cb);
  VkShaderModule createShader(const uint32_t* code, size_t bytes);

  GLFWwindow* window_{nullptr};
  // Window framebuffer size carried in from the main thread by presentThreaded()
  // (glfwGetFramebufferSize is main-thread-only). When >0, recreateSwapchain()/the
  // createSwapchain currentExtent fallback use it instead of a GLFW query, so
  // swapchain recreation needs no GLFW call on the render thread. 0 on the
  // single-threaded path (then glfwGetFramebufferSize is used as before).
  int winFbW_{0}, winFbH_{0};
  bool headless_{false};       // windowless: no surface/swapchain, render offscreen
  int headlessW_{1280}, headlessH_{800};  // composite size when headless
  uint32_t lastSwapIndex_{0};  // last composite image written (for captureWindow)
  RendererCaps caps_{};
  bool imguiInited_{false};

  VkInstance instance_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkPhysicalDevice phys_{VK_NULL_HANDLE};
  RendererDevicePreference devicePreference_;
  VkDevice device_{VK_NULL_HANDLE};
  uint32_t queueFamily_{0};
  VkQueue queue_{VK_NULL_HANDLE};

  // Swapchain (for ImGui)
  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  VkFormat swapFormat_{VK_FORMAT_UNDEFINED};
  VkExtent2D swapExtent_{};
  std::vector<VkImage> swapImages_;
  std::vector<VkDeviceMemory> swapMem_;  // headless only: backing for swapImages_
  std::vector<VkImageView> swapViews_;
  std::vector<VkFramebuffer> swapFramebuffers_;
  VkRenderPass swapRenderPass_{VK_NULL_HANDLE};

  // Offscreen target (3D scene)
  VkRenderPass offscreenPass_{VK_NULL_HANDLE};
  VkRenderPass shadowPass_{VK_NULL_HANDLE};
  VkRenderPass overlayLoadPass_{VK_NULL_HANDLE};  // draw overlays over the RT image
  VkImage colorImg_{VK_NULL_HANDLE};
  VkDeviceMemory colorMem_{VK_NULL_HANDLE};
  VkImageView colorView_{VK_NULL_HANDLE};
  VkImage depthImg_{VK_NULL_HANDLE};
  VkDeviceMemory depthMem_{VK_NULL_HANDLE};
  VkImageView depthView_{VK_NULL_HANDLE};
  VkImage shadowColorImg_{VK_NULL_HANDLE}, shadowDepthImg_{VK_NULL_HANDLE};
  VkDeviceMemory shadowColorMem_{VK_NULL_HANDLE}, shadowDepthMem_{VK_NULL_HANDLE};
  VkImageView shadowColorView_{VK_NULL_HANDLE}, shadowDepthView_{VK_NULL_HANDLE};
  VkFramebuffer shadowFb_{VK_NULL_HANDLE};
  RasterShadowCamera shadowCamera_;
  VkImage pointShadowDepthImg_{VK_NULL_HANDLE};
  VkDeviceMemory pointShadowDepthMem_{VK_NULL_HANDLE};
  VkImageView pointShadowDepthView_{VK_NULL_HANDLE};
  std::array<VkImageView, 6> pointShadowFaceViews_{};
  std::array<VkFramebuffer, 6> pointShadowFbs_{};
  RasterPointShadowCameras pointShadowCameras_;
  VkFramebuffer offscreenFb_{VK_NULL_HANDLE};
  VkSampler sampler_{VK_NULL_HANDLE};
  // Material samplers are keyed by DrawTextureCPU wrapS/wrapT (4x4). Scene
  // helpers keep using `sampler_`; material descriptors select from this cache.
  std::array<VkSampler, 16> materialSamplers_{};
  VkDescriptorSet offscreenTexId_{VK_NULL_HANDLE};
  int vpW_{0}, vpH_{0};
  VkFormat colorFormat_{VK_FORMAT_R8G8B8A8_UNORM};
  VkFormat depthFormat_{VK_FORMAT_D32_SFLOAT};

  // Pipeline
  VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};
  VkPipeline shadowPipeline_{VK_NULL_HANDLE};
  // GPU tessellation displacement pipeline (shares pipelineLayout_; PATCH_LIST
  // topology + tesc/tese). Created only when the device supports the
  // tessellationShader feature; otherwise displaced meshes stay coarse.
  bool tessSupported_{false};
  // VK_EXT_extended_dynamic_state: per-draw cull mode, so single-sided meshes
  // back-face-cull like the GL backend. Optional; false = no culling (legacy).
  bool dynCullSupported_{false};
  PFN_vkCmdSetCullModeEXT vkCmdSetCullMode_{nullptr};
  // multiDrawIndirect + drawIndirectFirstInstance + shaderDrawParameters all
  // present -> the instanced raster pass can draw via vkCmdDrawIndexedIndirect
  // batches (per-draw meshId/flags come from a gl_DrawIDARB-indexed SSBO).
  bool mdiSupported_{false};
  // Translucent (Blend-material) variant of the main mesh pipeline: premultiplied
  // "over" blend + depth-write-off. Drawn back-to-front after the opaque pass.
  VkPipeline translucentPipeline_{VK_NULL_HANDLE};
  VkPipeline tessPipeline_{VK_NULL_HANDLE};
  VkShaderStageFlags pushStages_{VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT};
  VkPipelineLayout instPipelineLayout_{VK_NULL_HANDLE};
  VkPipeline instPipeline_{VK_NULL_HANDLE};
  VkPipeline instTranslucentPipeline_{VK_NULL_HANDLE};
  VkPipeline instShadowPipeline_{VK_NULL_HANDLE};

  // Unlit line pipeline for debug helpers (grid/axes/bbox). Per-frame host
  // buffers (grow on demand) so a frame never writes a buffer still in flight.
  VkPipelineLayout lineLayout_{VK_NULL_HANDLE};
  VkPipeline linePipeline_{VK_NULL_HANDLE};
  VkPipeline linePipelineNoDepth_{VK_NULL_HANDLE};  // X-ray overlay (skeleton)
  // Camera-facing native Points/Curves raster carrier.  The CPU expands the
  // carriers into quads each frame; this deliberately stays independent of the
  // mesh material descriptor sets.
  VkPipeline nonMeshPipeline_{VK_NULL_HANDLE};
  VkBuffer nonMeshBuf_[kFramesInFlight]{};
  VkDeviceMemory nonMeshMem_[kFramesInFlight]{};
  VkDeviceSize nonMeshCap_[kFramesInFlight]{};
  std::vector<HelperVertex> nonMeshCopy_;
  std::vector<DrawPointsCPU> nativePoints_;
  std::vector<DrawCurvesCPU> nativeCurves_;

  // --- UsdVol volume raymarch (proxy-box, 3D density texture) ---
  struct VkVolumeGPU {
    VkImage img{VK_NULL_HANDLE};
    VkDeviceMemory mem{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};
    VkBuffer ubo{VK_NULL_HANDLE};
    VkDeviceMemory uboMem{VK_NULL_HANDLE};
    void* uboMapped{nullptr};
    VkDescriptorSet set{VK_NULL_HANDLE};
    float model[16];
    float invModel[16];
    float bmin[3], bmax[3];
    float albedo[3], densityScale;
    float emission[3], background;
  };
  bool createVolumePipeline(std::string* err);
  // Record the volume raymarch draws (UBO update + proxy boxes) with `pipe`.
  void recordVolumePass(VkCommandBuffer cb, VkPipeline pipe);
  VkPipelineLayout volumeLayout_{VK_NULL_HANDLE};
  VkPipeline volumePipeline_{VK_NULL_HANDLE};
  VkPipeline volumePipelineNoDepth_{VK_NULL_HANDLE};  // RT overlay (no depth)
  VkDescriptorSetLayout volumeSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool volumePool_{VK_NULL_HANDLE};
  VkBuffer volumeCubeBuf_{VK_NULL_HANDLE};      // 36-vertex proxy cube
  VkDeviceMemory volumeCubeMem_{VK_NULL_HANDLE};
  std::vector<VkVolumeGPU> volumes_;
  VkBuffer helperBuf_[kFramesInFlight]{};
  VkDeviceMemory helperMem_[kFramesInFlight]{};
  VkDeviceSize helperCap_[kFramesInFlight]{};
  std::vector<HelperVertex> helperCopy_;  // copied in renderFrame, drawn in present
  // Overlay (skeleton X-ray) per-frame line buffers + copy.
  VkBuffer overlayBuf_[kFramesInFlight]{};
  VkDeviceMemory overlayMem_[kFramesInFlight]{};
  VkDeviceSize overlayCap_[kFramesInFlight]{};
  std::vector<HelperVertex> overlayCopy_;
  // Selection-highlight per-frame edge-line buffers + copy (VK wireframe overlay).
  VkBuffer highlightLineBuf_[kFramesInFlight]{};
  VkDeviceMemory highlightLineMem_[kFramesInFlight]{};
  VkDeviceSize highlightLineCap_[kFramesInFlight]{};
  std::vector<HelperVertex> highlightLineCopy_;
  std::vector<uint8_t> meshVisible_;  // per-mesh visibility mask (raster), copied in renderFrame
  std::vector<uint8_t> rtMeshVisible_;  // persistent user hide/isolate mask for TLAS

  // Textures (base color). One combined-image-sampler descriptor per texture,
  // plus a default 1x1 white texture for untextured submeshes.
  VkDescriptorSetLayout texSetLayout_{VK_NULL_HANDLE};
  // Consolidated raster layouts: set 0 holds all material/IBL samplers; set 1
  // holds all per-mesh deformation/AOV buffers. Together with frame + material
  // parameter sets this keeps both mesh pipelines at four bound sets.
  VkDescriptorSetLayout materialSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool materialPool_{VK_NULL_HANDLE};
  std::vector<VkDescriptorSet> materialSets_;
  VkDescriptorSetLayout deformSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool deformPool_{VK_NULL_HANDLE};
  VkDescriptorSet dummyDeformDesc_{VK_NULL_HANDLE};
  VkDescriptorSetLayout skinSetLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout influenceSetLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout faceSetLayout_{VK_NULL_HANDLE};
  // Raster set 2: global frame/displacement params UBO, read in the
  // vertex + tessellation stages so the UI's displacement-scale and max-tess
  // sliders are live on Vulkan (the push constants are full). One persistently
  // mapped host buffer, written each frame (volume-UBO convention).
  VkDescriptorSetLayout dispParamsSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool dispParamsPool_{VK_NULL_HANDLE};
  VkDescriptorSet dispParamsSet_{VK_NULL_HANDLE};
  VkBuffer dispParamsUbo_{VK_NULL_HANDLE};
  VkDeviceMemory dispParamsUboMem_{VK_NULL_HANDLE};
  void* dispParamsMapped_{nullptr};
  // Raster set 3: per-material texture scale/bias (indexed
  // by pc.matId in the vertex + tess-eval stages). Fixed-capacity host SSBO written
  // per scene; lets the VK viewer center height maps like GL/tusdrender. Push
  // constants are full, so this per-material data needs its own buffer.
  static constexpr uint32_t kMaxDispMaterials = 4096;
  VkDescriptorSetLayout dispMatSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool dispMatPool_{VK_NULL_HANDLE};
  VkDescriptorSet dispMatSet_{VK_NULL_HANDLE};
  VkBuffer dispMatSsbo_{VK_NULL_HANDLE};
  VkDeviceMemory dispMatSsboMem_{VK_NULL_HANDLE};
  void* dispMatMapped_{nullptr};
  // Set 3 of the instanced pipeline: per-draw metadata (meshId + flag bits), one
  // entry per mesh plus a trailing slot for the shared box proxy. The fragment
  // shader indexes it by (baseDraw + gl_DrawIDARB), so a multi-draw-indirect batch
  // needs no per-draw push. Contents are static per mesh -> (re)built by
  // ensureDrawMeta() only when the mesh count changes. Host-visible SSBO.
  VkDescriptorSetLayout drawMetaSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool drawMetaPool_{VK_NULL_HANDLE};
  VkDescriptorSet drawMetaSet_{VK_NULL_HANDLE};
  VkBuffer drawMetaBuf_{VK_NULL_HANDLE};
  VkDeviceMemory drawMetaBufMem_{VK_NULL_HANDLE};
  uint32_t drawMetaCount_{0};    // slots currently populated (meshes + 1 box slot)
  uint32_t drawMetaCap_{0};      // slots the buffer can hold
  uint32_t boxMetaSlot_{0};      // DrawMeta slot the box-proxy draw pushes as baseDraw
  uint32_t mdiMeshMetaBase_{0};  // per-mesh DrawMeta region base for the fallback loop
  void ensureDrawMeta();         // rebuild drawMetaBuf_ when meshes_ changes (non-MDI)
  // (Re)create drawMetaBuf_ to hold `meta` and repoint drawMetaSet_ at it. Shared by
  // ensureDrawMeta (per-mesh layout) and buildInstMdi (per-command layout).
  // Per-draw metadata (set 3), shared by mesh_inst.vert/.frag. `jointAddr` /
  // `weightAddr` are the device addresses of this mesh's per-vertex skin arrays
  // (0 = unskinned): the instanced vertex shader fetches them by gl_VertexIndex
  // rather than through vertex-input state, so the merged multi-draw path needs
  // no extra bindings -- its draws simply carry 0 (skinned prototypes are kept
  // out of MDI, whose gl_VertexIndex would index the MERGED buffer).
  struct DrawMetaCPU {
    int32_t ids[4];
    uint64_t jointAddr{0};
    uint64_t weightAddr{0};
  };
  static_assert(sizeof(int32_t) * 4 + sizeof(uint64_t) * 2 == 32, "DrawMeta 32B");
  void writeDrawMeta(const std::vector<DrawMetaCPU>& meta);

  // ---- Multi-draw-indirect instanced path (large-scene --next) ----
  // All MDI-eligible (non-morph) instanced prototypes are drawn as a handful of
  // vkCmdDrawIndexedIndirect batches over shared buffers instead of ~83k per-mesh
  // bind+draw calls. Geometry (positions/normals, per-vertex color, morph-zero,
  // indices) is concatenated once; per-instance o2w + RGBA color live in ONE global
  // buffer whose slices the meshes' instVboMapped point into (so cull fills it).
  // The indirect command list is patched each frame with per-mesh drawInstanceCount.
  VkBuffer mdiVbo_{VK_NULL_HANDLE};        VkDeviceMemory mdiVboMem_{VK_NULL_HANDLE};
  VkBuffer mdiVtxColBuf_{VK_NULL_HANDLE};  VkDeviceMemory mdiVtxColMem_{VK_NULL_HANDLE};
  VkBuffer mdiMorphBuf_{VK_NULL_HANDLE};   VkDeviceMemory mdiMorphMem_{VK_NULL_HANDLE};
  VkBuffer mdiEbo_{VK_NULL_HANDLE};        VkDeviceMemory mdiEboMem_{VK_NULL_HANDLE};
  VkBuffer mdiInstBuf_{VK_NULL_HANDLE};    VkDeviceMemory mdiInstMem_{VK_NULL_HANDLE};
  void* mdiInstMapped_{nullptr};
  VkBuffer mdiInstColBuf_{VK_NULL_HANDLE}; VkDeviceMemory mdiInstColMem_{VK_NULL_HANDLE};
  void* mdiInstColMapped_{nullptr};
  VkBuffer mdiIndirectBuf_{VK_NULL_HANDLE}; VkDeviceMemory mdiIndirectMem_{VK_NULL_HANDLE};
  void* mdiIndirectMapped_{nullptr};
  // Optional DEVICE-LOCAL mirror of the per-instance o2w + RGBA color buffers (island's
  // MDI frame time is instance-fetch bound: ~2 GB of host-visible transforms are
  // refetched over PCIe every frame). Cull writes still land in the host-visible
  // buffers; each rewritten mesh slice is queued here and vkCmdCopyBuffer'd into
  // the mirror at the top of the next frame, so a settled camera uploads nothing.
  // Enabled when the VK_EXT_memory_budget headroom check passes (the mirror costs
  // VRAM -- keep it OFF for tight-VRAM configs); TUSDVIEW_MDI_DEVLOCAL=0/1 forces.
  VkBuffer mdiInstDevBuf_{VK_NULL_HANDLE};    VkDeviceMemory mdiInstDevMem_{VK_NULL_HANDLE};
  VkBuffer mdiInstColDevBuf_{VK_NULL_HANDLE}; VkDeviceMemory mdiInstColDevMem_{VK_NULL_HANDLE};
  bool mdiInstDevLocal_{false};
  std::vector<VkBufferCopy> mdiInstPendingXf_, mdiInstPendingCol_;
  // One indirect command per (eligible mesh, submesh); meshIndex ties it back to the
  // prototype for the per-frame instanceCount patch + visibility gate.
  struct MdiCmd { uint32_t meshIndex; uint32_t reserved; VkDrawIndexedIndirectCommand cmd; };
  std::vector<MdiCmd> mdiCmds_;
  // CPU staging accumulated during appendMesh, uploaded + freed by buildInstMdi().
  std::vector<float> mdiInstXfStage_;      // 12 floats / instance (o2w rows)
  std::vector<float> mdiInstColStage_;     // 4 floats / instance (rgba)
  std::vector<float> mdiVtxStage_;         // DrawVertex floats (positions+normals+...)
  std::vector<float> mdiVtxColStage_;      // 3 floats / vertex (per-vertex proto color)
  std::vector<uint32_t> mdiMorphStage_;    // 2 uint / vertex (morph offset,count = 0)
  std::vector<uint32_t> mdiIdxStage_;      // concatenated local indices
  uint32_t mdiInstTotal_{0};               // running instance count during staging
  uint32_t mdiVertTotal_{0};               // running vertex count during staging
  uint32_t mdiDrawCount_{0};               // number of indirect commands
  bool mdiBuilt_{false};                   // buffers built for the current scene
  bool mdiActive_{false};                  // any eligible mesh -> use the indirect draw
  void buildInstMdi();                     // upload staging -> shared buffers (once)
  void patchMdiIndirect();                 // refresh per-frame instanceCount + meta
  void destroyMdiBuffers();                // free shared buffers (reload/shutdown)
  // Sets 7 & 8: per-mesh GPU blendshape morph (delta SSBO + per-frame coeff SSBO).
  // Both are "readonly SSBO, vertex stage, binding 0", so they share one layout.
  // Non-morph meshes bind shared 1-element dummy descriptors (the shader statically
  // references the SSBOs, so they must always be bound).
  VkDescriptorSetLayout morphSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool morphPool_{VK_NULL_HANDLE};
  VkBuffer dummyMorphBuf_{VK_NULL_HANDLE};
  VkDeviceMemory dummyMorphMem_{VK_NULL_HANDLE};
  VkDescriptorSet dummyMorphDesc_{VK_NULL_HANDLE};
  VkDescriptorSet allocMorphDescriptor(VkBuffer buffer, VkDeviceSize size);
  VkDescriptorPool texPool_{VK_NULL_HANDLE};
  VkDescriptorPool skinPool_{VK_NULL_HANDLE};
  VkDescriptorPool influencePool_{VK_NULL_HANDLE};
  VkDescriptorPool facePool_{VK_NULL_HANDLE};
  // Shared 1-element dummy face buffer + descriptor, bound when a mesh has no
  // source-face data (or on pool overflow).
  VkBuffer dummyFaceBuf_{VK_NULL_HANDLE};
  VkDeviceMemory dummyFaceMem_{VK_NULL_HANDLE};
  VkDescriptorSet dummyFaceDesc_{VK_NULL_HANDLE};
  VkDescriptorSet allocFaceDescriptor(VkBuffer buffer, VkDeviceSize size);
  // DomeLight split-sum IBL resources (scene-lifetime; setLights owns).
  VkImage iblIrrImg_{VK_NULL_HANDLE};
  VkDeviceMemory iblIrrMem_{VK_NULL_HANDLE};
  VkImageView iblIrrView_{VK_NULL_HANDLE};
  VkImage iblSpecImg_{VK_NULL_HANDLE};
  VkDeviceMemory iblSpecMem_{VK_NULL_HANDLE};
  VkImageView iblSpecView_{VK_NULL_HANDLE};
  VkImage iblLutImg_{VK_NULL_HANDLE};
  VkDeviceMemory iblLutMem_{VK_NULL_HANDLE};
  VkImageView iblLutView_{VK_NULL_HANDLE};
  // Full-resolution env cube (RT miss background; unclamped radiance).
  VkImage iblEnvImg_{VK_NULL_HANDLE};
  VkDeviceMemory iblEnvMem_{VK_NULL_HANDLE};
  VkImageView iblEnvView_{VK_NULL_HANDLE};
  VkDescriptorSet iblIrrDesc_{VK_NULL_HANDLE};
  VkDescriptorSet iblSpecDesc_{VK_NULL_HANDLE};
  VkDescriptorSet iblLutDesc_{VK_NULL_HANDLE};
  // 1x1 black cube fallback (init-lifetime, like whiteImg_).
  VkImage blackCubeImg_{VK_NULL_HANDLE};
  VkDeviceMemory blackCubeMem_{VK_NULL_HANDLE};
  VkImageView blackCubeView_{VK_NULL_HANDLE};
  VkDescriptorSet blackCubeDesc_{VK_NULL_HANDLE};
  bool iblActive_{false};
  int iblLods_{0};
  float iblColor_[3]{1.0f, 1.0f, 1.0f};
  float iblRotation_[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  VkImage whiteImg_{VK_NULL_HANDLE};
  VkDeviceMemory whiteMem_{VK_NULL_HANDLE};
  VkImageView whiteView_{VK_NULL_HANDLE};
  VkDescriptorSet whiteDesc_{VK_NULL_HANDLE};
  // Black 1x1 (red=0) bound to material binding 16 when a submesh has no
  // displacement. The vertex shader always samples it, so black means no
  // displacement (no push-constant lane needed).
  VkImage blackImg_{VK_NULL_HANDLE};
  VkDeviceMemory blackMem_{VK_NULL_HANDLE};
  VkImageView blackView_{VK_NULL_HANDLE};
  VkDescriptorSet blackDesc_{VK_NULL_HANDLE};
  VkImage dummyArrayImg_{VK_NULL_HANDLE};
  VkDeviceMemory dummyArrayMem_{VK_NULL_HANDLE};
  VkImageView dummyArrayView_{VK_NULL_HANDLE};
  VkDescriptorSet dummyArrayDesc_{VK_NULL_HANDLE};
  VkImage dummyLutImg_{VK_NULL_HANDLE};
  VkDeviceMemory dummyLutMem_{VK_NULL_HANDLE};
  VkImageView dummyLutView_{VK_NULL_HANDLE};
  VkDescriptorSet dummyLutDesc_{VK_NULL_HANDLE};
  std::vector<VkImage> texImgs_;
  std::vector<VkDeviceMemory> texMems_;
  std::vector<VkImageView> texViews_;
  std::vector<VkDescriptorSet> texDescs_;
  std::vector<VkDescriptorSet> texUdimArrayDescs_;
  std::vector<VkImageView> texSlotViews_;
  std::vector<VkImageView> texUdimArrayViews_;
  std::vector<uint8_t> texIsUdim_;
  VkImage udimLutAtlasImg_{VK_NULL_HANDLE};
  VkDeviceMemory udimLutAtlasMem_{VK_NULL_HANDLE};
  VkImageView udimLutAtlasView_{VK_NULL_HANDLE};
  VkDescriptorSet udimLutAtlasDesc_{VK_NULL_HANDLE};
  int udimLutAtlasRows_{0};
  std::vector<int> matBaseTex_;  // per material: DrawScene texture index or -1
  std::vector<int> matMetallicTex_;    // per material: DrawScene texture index or -1
  std::vector<int> matRoughnessTex_;
  std::vector<int> matNormalTex_;      // per material: DrawScene texture index or -1
  std::vector<int> matEmissiveTex_;    // per material: DrawScene texture index or -1
  std::vector<int> matOpacityTex_;     // scalar opacity texture index or -1
  std::vector<int> matOcclusionTex_;   // ambient-occlusion texture index or -1
  std::vector<int> matSpecularColorTex_;   // inputs:specularColor tex or -1
  std::vector<int> matCoatWeightTex_;      // coat weight (scalar) tex or -1
  std::vector<int> matCoatColorTex_;       // coat color (rgb) tex or -1
  std::vector<int> matCoatRoughnessTex_;   // coat roughness (scalar) tex or -1
  std::vector<int> matDispTex_;  // per material: displacement texture index or -1
  std::vector<float> matDispConst_;  // per material: constant displacement amount

  VkBuffer boneBuf_{VK_NULL_HANDLE};
  VkDeviceMemory boneMem_{VK_NULL_HANDLE};
  void* boneMapped_{nullptr};
  VkDescriptorSet boneDesc_{VK_NULL_HANDLE};
  VkDeviceSize boneBufSize_{0};

  // Commands & sync
  VkCommandPool commandPool_{VK_NULL_HANDLE};
  VkCommandBuffer cmd_[kFramesInFlight]{};
  VkSemaphore imageAvailable_[kFramesInFlight]{};  // acquire->render, per frame-in-flight
  VkFence inFlight_[kFramesInFlight]{};            // render-done fence, per frame-in-flight
  // Present-wait semaphore, ONE PER SWAPCHAIN IMAGE (not per frame): vkQueuePresentKHR
  // has no fence, so a present's wait semaphore must not be reused until that image is
  // acquired again -- reusing a per-frame semaphore across images corrupts the queue
  // (the threaded VK-RT black). imagesInFlight_ tracks which frame's fence last rendered
  // each swapchain image, so we wait before reusing the image. Both are sized to the
  // swapchain image count and (re)built with the swapchain.
  std::vector<VkSemaphore> renderFinished_;
  std::vector<VkFence> imagesInFlight_;
  uint32_t frame_{0};

  // Scene
  std::vector<VkMeshGPU> meshes_;
  std::vector<float> matColor_;    // 3 vec4 per material: preview subset
  std::vector<float> matLightRt_;  // 14 vec4 per material: LightRT/OpenPBR block
  std::vector<float> lightParams_;  // packed DrawLightCPU params
  std::vector<uint32_t> rtDirectLightMasks_;
  std::vector<uint32_t> rtShadowLightMasks_;
  RasterLightSet rasterLights_;
  // CPU copies retained for the backend-neutral RT texture-table build. Vulkan
  // raster uploads images immediately, while ray query consumes decoded RGBA8
  // texels and semantic material slots from storage buffers.
  std::vector<DrawMaterialCPU> rtMaterialsCpu_;
  std::vector<DrawTextureCPU> rtTexturesCpu_;

  // Last frame parameters (copied; caller's pointers are transient)
  bool hasParams_{false};
  float view_[16];
  float proj_[16];
  float cameraPos_[3]{0, 0, 0};
  float exposure_{0.0f};
  RtCameraLens cameraLens_;
  float lightDir_[3]{0.40160966f, 0.64257544f, 0.48193160f};
  float lightColor_[3]{1.0f, 1.0f, 1.0f};
  float clear_[4]{0.12f, 0.12f, 0.13f, 1.0f};

  // --- Ray tracing (ray query) state ---
  bool rtSupported_{false};   // device + shader available
  bool rtActive_{false};      // RT technique currently selected
  uint32_t rtMaxInstanceCount_{0};  // VK acceleration-structure limit
  int rtMode_{0};             // RenderMode (wireframe/matId/AOV) for RT + raster
  float depthScale_{1.0f};    // depth-AOV normalizer (scene extent)
  bool displacement_{true};   // apply UsdPreviewSurface displacement (coarse)
  float displacementScale_{1.0f};  // global displacement multiplier
  int maxTessLevel_{1};       // >1 selects the GPU-tessellation pipeline
  float sceneMin_[3]{0, 0, 0};      // position-AOV scene bbox
  float sceneExtent_[3]{1, 1, 1};
  bool tlasDirty_{true};      // TLAS / SSBOs need rebuild
  std::string techniqueLabel_{"Vulkan"};  // caps_.backend_name points here
  uint32_t scratchAlign_{256};
  // Visible USD purposes for the RT TLAS (bit i = PurposeId i; default: guide
  // hidden, matching the raster/GUI default). rebuildTlas skips hidden-purpose
  // meshes so e.g. Caldera's guide planes never enter (or pay for) the AS.
  uint32_t rtPurposeMask_{0xBu};

  // VK_EXT_memory_budget: live device-local VRAM usage/budget (0 = unsupported).
  bool memBudgetSupported_{false};
  bool memoryBudget(uint64_t* usedBytes, uint64_t* budgetBytes) const;

  PFN_vkGetBufferDeviceAddressKHR pfnGetBufferDeviceAddress_{nullptr};
  PFN_vkGetAccelerationStructureBuildSizesKHR pfnGetASBuildSizes_{nullptr};
  PFN_vkCreateAccelerationStructureKHR pfnCreateAS_{nullptr};
  PFN_vkDestroyAccelerationStructureKHR pfnDestroyAS_{nullptr};
  PFN_vkCmdBuildAccelerationStructuresKHR pfnCmdBuildAS_{nullptr};
  PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetASDeviceAddress_{nullptr};
  // BLAS compaction: build with ALLOW_COMPACTION, query the compacted size, then
  // copy into a right-sized AS. A BLAS is typically ~half its build-time size.
  PFN_vkCmdWriteAccelerationStructuresPropertiesKHR pfnCmdWriteASProps_{nullptr};
  PFN_vkCmdCopyAccelerationStructureKHR pfnCmdCopyAS_{nullptr};
  bool blasCompact_{true};  // TUSDVIEW_BLAS_COMPACT=0 opts out
  // Resident compacted BLAS bytes for this pose, and what they would have cost
  // uncompacted (reported in the [vk_rt] log; also what the test asserts on).
  uint64_t blasBytes_{0};
  uint64_t blasBytesUncompacted_{0};
  size_t blasUniqueBuilt_{0};  // prototypes actually built this pose (post-dedup)

  // Build the Full-LOD BLAS set as compacted waves (one build submit + one copy
  // submit per wave), instead of one build + one queue stall per prototype.
  void buildBlasWave(const std::vector<uint32_t>& meshIds);
  // Drop the BLAS of prototypes this pose does not render at Full. Without this
  // the BLAS set only ever grows as the camera visits new regions.
  void evictBlasNotIn(const std::vector<uint32_t>& keepMeshIds);

  VkAccelerationStructureKHR tlas_{VK_NULL_HANDLE};
  VkBuffer tlasBuf_{VK_NULL_HANDLE};
  VkDeviceMemory tlasMem_{VK_NULL_HANDLE};
  VkBuffer instBuf_{VK_NULL_HANDLE};       // VkAccelerationStructureInstanceKHR[]
  VkDeviceMemory instMem_{VK_NULL_HANDLE};
  VkDeviceSize instCap_{0};
  VkBuffer meshDescBuf_{VK_NULL_HANDLE};    // per-mesh {addrs, matId, normalMat}
  VkDeviceMemory meshDescMem_{VK_NULL_HANDLE};
  VkDeviceSize meshDescCap_{0};
  VkBuffer rtMatBuf_{VK_NULL_HANDLE};       // vec4 baseColor[]
  VkDeviceMemory rtMatMem_{VK_NULL_HANDLE};
  VkBuffer rtMatLightRtBuf_{VK_NULL_HANDLE};  // LightRT/OpenPBR material block
  VkDeviceMemory rtMatLightRtMem_{VK_NULL_HANDLE};
  VkBuffer rtLightBuf_{VK_NULL_HANDLE};        // packed DrawLightCPU params
  VkDeviceMemory rtLightMem_{VK_NULL_HANDLE};
  VkBuffer rtTexelBuf_{VK_NULL_HANDLE};         // packed RGBA8 texels (uint[])
  VkDeviceMemory rtTexelMem_{VK_NULL_HANDLE};
  VkBuffer rtTexDescBuf_{VK_NULL_HANDLE};       // HostTextureDesc[]
  VkDeviceMemory rtTexDescMem_{VK_NULL_HANDLE};
  VkBuffer rtMatTexBuf_{VK_NULL_HANDLE};        // six texture ids/material
  VkDeviceMemory rtMatTexMem_{VK_NULL_HANDLE};
  VkBuffer rtMatTexParamBuf_{VK_NULL_HANDLE};   // 72 floats/material
  VkDeviceMemory rtMatTexParamMem_{VK_NULL_HANDLE};
  VkDeviceSize rtMatCap_{0};
  VkBuffer instInfoBuf_{VK_NULL_HANDLE};    // per-TLAS-instance {meshId, tint} (binding 4)
  VkDeviceMemory instInfoMem_{VK_NULL_HANDLE};

  VkImage rtImage_{VK_NULL_HANDLE};
  VkDeviceMemory rtImageMem_{VK_NULL_HANDLE};
  VkImageView rtImageView_{VK_NULL_HANDLE};

  // Interactive HIP/CUDA path: an externally-traced image staged into colorImg_ by
  // uploadViewportImage(). When externalColorValid_ is set, the next presentImpl()
  // skips the raster/RT 3D pass (colorImg_ already holds the frame) and clears it.
  VkBuffer extStaging_{VK_NULL_HANDLE};
  VkDeviceMemory extStagingMem_{VK_NULL_HANDLE};
  VkDeviceSize extStagingCap_{0};
  bool externalColorValid_{false};

  // Progressive accumulation: an rgba32f radiance buffer the trace adds into each
  // frame while the view is static (sub-pixel jittered -> anti-aliased, and the
  // stochastic AOVs like AO / soft-shadow converge). Reset to sample 0 whenever
  // the camera, render mode, viewport, or geometry changes.
  VkImage accumImage_{VK_NULL_HANDLE};
  VkDeviceMemory accumImageMem_{VK_NULL_HANDLE};
  VkImageView accumImageView_{VK_NULL_HANDLE};
  uint32_t rtAccumFrame_{0};        // current accumulated sample index (0 = reset)
  float lastRtPV_[16]{};            // proj*view of the last traced frame
  int lastRtMode_{-1};              // render mode of the last traced frame
  uint64_t rtAccumGen_{0};          // bumped on geometry / viewport invalidation
  uint64_t lastRtAccumGen_{~0ull};  // generation of the last traced frame
  bool rtAccumEnabled_{true};       // master toggle for progressive accumulation

  // View-dependent LOD: camera snapshot used by rebuildTlas to classify instances.
  // Default (lodEnabled=false) reproduces the all-Full, no-cull TLAS exactly.
  RtLodCamera lodCam_;
  // Shared 12-triangle unit-cube BLAS instanced (box-fit per prototype AABB) for
  // every Proxy-LOD instance, so the long tail of distant prototypes needs no
  // full BLAS. Built once (buildBoxBlas). boxMesh_ holds only vbo/ebo/blas.
  VkMeshGPU boxMesh_;

  // Raster LOD box proxies (optimization B, VK raster path). Static unit-cube
  // geometry drawn with instPipeline_; per-instance box-fit o2w (binding 1) + rgba tint
  // (binding 2) are uploaded each frame from boxProxyXforms_/boxProxyTints_ (filled
  // by updateProxyInstances on the cull/upload path). bindings 3/4 carry constant
  // white per-vertex color + zero morph so the box reuses the prototype pipeline.
  VkBuffer boxRasterVbo_{VK_NULL_HANDLE};        // 8x DrawVertex (binding 0)
  VkDeviceMemory boxRasterVboMem_{VK_NULL_HANDLE};
  VkBuffer boxRasterEbo_{VK_NULL_HANDLE};        // 36 indices
  VkDeviceMemory boxRasterEboMem_{VK_NULL_HANDLE};
  VkBuffer boxRasterVtxColBuf_{VK_NULL_HANDLE};  // 8x vec3(1,1,1) (binding 3)
  VkDeviceMemory boxRasterVtxColMem_{VK_NULL_HANDLE};
  VkBuffer boxRasterMorphBuf_{VK_NULL_HANDLE};   // 8x uvec2(0,0) (binding 4)
  VkDeviceMemory boxRasterMorphMem_{VK_NULL_HANDLE};
  VkBuffer boxRasterInstBuf_{VK_NULL_HANDLE};    // per-instance o2w (binding 1)
  VkDeviceMemory boxRasterInstMem_{VK_NULL_HANDLE};
  VkDeviceSize boxRasterInstCap_{0};             // allocated bytes
  VkBuffer boxRasterTintBuf_{VK_NULL_HANDLE};    // per-instance rgba tint (binding 2)
  VkDeviceMemory boxRasterTintMem_{VK_NULL_HANDLE};
  VkDeviceSize boxRasterTintCap_{0};             // allocated bytes
  uint32_t boxRasterCount_{0};                   // proxies to draw this frame
  std::vector<float> boxProxyXforms_;            // 12 floats/proxy (CPU staging)
  std::vector<float> boxProxyTints_;             // 4 floats/proxy rgba (CPU staging)

  VkDescriptorSetLayout rtSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool rtPool_{VK_NULL_HANDLE};
  VkDescriptorSet rtSet_{VK_NULL_HANDLE};
  VkPipelineLayout rtPipelineLayout_{VK_NULL_HANDLE};
  VkPipeline rtPipeline_{VK_NULL_HANDLE};

  // Opt-in GPU compute skinning of the RT vertex stream (skin.comp):
  // descriptor-less (push constants carry buffer device addresses). Created
  // lazily on the first updateMeshSkinningGpu(); a failed creation latches
  // skinPipelineTried_ so the caller permanently falls back to CPU skinning.
  bool ensureSkinPipeline();
  VkPipelineLayout skinPipelineLayout_{VK_NULL_HANDLE};
  VkPipeline skinPipeline_{VK_NULL_HANDLE};
  bool skinPipelineTried_{false};
};

}  // namespace tusdview
