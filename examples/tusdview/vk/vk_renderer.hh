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
#include "renderer.hh"

namespace tusdview {

class VulkanRenderer final : public Renderer {
 public:
  VulkanRenderer() = default;
  ~VulkanRenderer() override;

  bool init(GLFWwindow* window, std::string* err) override;
  void setHeadlessSize(int w, int h) override {
    if (w > 0) headlessW_ = w;
    if (h > 0) headlessH_ = h;
  }
  bool initImGui(std::string* err) override;
  void beginScene(const std::vector<DrawMaterialCPU>& materials, int textureCount) override;
  void appendMesh(const DrawMeshCPU& mesh) override;
  void appendVolume(const DrawVolumeCPU& vol) override;
  void uploadTexture(int slot, const DrawTextureCPU& tex) override;
  void uploadSkinningFrame(const SkinningFrameCPU& skin) override;
  void updateMeshVertices(int meshIndex,
                          const std::vector<DrawVertex>& verts) override;
  void updateMorphWeights(int meshIndex,
                          const std::vector<float>& coeffs) override;
  void updateInstanceVisibility(size_t meshIndex, const float* xforms,
                                const float* colors, uint32_t count) override;
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
    // Optional displacement-baked vertex buffer for the ray-tracing path: the BLAS
    // + ray-query hit read this (via vboAddr) so RT shows displacement, while the
    // raster path keeps using `vbo` (shader displacement). Null when the mesh has
    // no displacement.
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
    // loc 8) + a static delta SSBO (set 7) + a tiny per-frame coefficient SSBO (set
    // 8, persistently mapped). hasMorph gates it; non-morph meshes bind dummy sets.
    VkBuffer morphOffsetVbo{VK_NULL_HANDLE};     // binding 6: uvec2 (offset,count)
    VkDeviceMemory morphOffsetVboMem{VK_NULL_HANDLE};
    VkBuffer morphDeltaBuf{VK_NULL_HANDLE};       // set 7 SSBO (vec4: chId,dx,dy,dz)
    VkDeviceMemory morphDeltaMem{VK_NULL_HANDLE};
    VkDescriptorSet morphDeltaDesc{VK_NULL_HANDLE};
    VkBuffer morphCoeffBuf{VK_NULL_HANDLE};       // set 8 SSBO (float/channel, dynamic)
    VkDeviceMemory morphCoeffMem{VK_NULL_HANDLE};
    VkDescriptorSet morphCoeffDesc{VK_NULL_HANDLE};
    void* morphCoeffMapped{nullptr};
    VkBuffer morphChanBuf{VK_NULL_HANDLE};         // set 9 SSBO (uint channelId/entry)
    VkDeviceMemory morphChanMem{VK_NULL_HANDLE};
    VkDescriptorSet morphChanDesc{VK_NULL_HANDLE};
    int morphChannelCount{0};
    bool hasMorph{false};
    VkBuffer ebo{VK_NULL_HANDLE};
    VkDeviceMemory eboMem{VK_NULL_HANDLE};
    std::vector<DrawSubmesh> submeshes;
    float world[16];
    // Ray tracing (built when RT is supported): a BLAS over this mesh's
    // triangles plus device addresses + counts for the shader.
    VkAccelerationStructureKHR blas{VK_NULL_HANDLE};
    VkBuffer blasBuf{VK_NULL_HANDLE};
    VkDeviceMemory blasMem{VK_NULL_HANDLE};
    VkDeviceAddress blasAddr{0};
    VkDeviceAddress vboAddr{0};
    VkDeviceAddress eboAddr{0};
    uint32_t vertexCount{0};
    uint32_t indexCount{0};
    int matId{-1};       // material id of the mesh's first submesh (RT shading)
    float normalMat[9];  // inverse-transpose of world 3x3 (object->world normals)
    bool skinned{false};
    bool extendedSkinned{false};
    // RT instancing + displayColor + authored-normal flag (mirrors the GL path).
    VkBuffer vtxColorBuf{VK_NULL_HANDLE};   // per-vertex displayColor (vec3[]), 0=none
    VkDeviceMemory vtxColorMem{VK_NULL_HANDLE};
    VkDeviceAddress vtxColorAddr{0};
    VkDeviceAddress uv1Addr{0};              // uv1 SSBO address (RT multi-UV AOV)
    VkDeviceAddress inflAddr{0};             // influence SSBO address (RT influence AOV)
    VkBuffer faceBuf{VK_NULL_HANDLE};        // per-triangle source face id (uint[])
    VkDeviceMemory faceMem{VK_NULL_HANDLE};
    VkDeviceAddress faceAddr{0};             // RT source-face-id AOV
    VkDeviceAddress jointAddr{0};            // RT skin-weights AOV (joint ids)
    VkDeviceAddress weightAddr{0};           // RT skin-weights AOV (weights)
    VkDescriptorSet faceDesc{VK_NULL_HANDLE}; // raster source-face-id (set 3); else dummy
    bool geometricNormal{false};            // no authored normals -> geometric face normal
    bool doubleSided{false};                // double-sided AOV flag
    int purposeId{0};                       // purpose AOV: 0=default/1=render/2=proxy/3=guide
    int kindId{0};                          // kind AOV: 0=none/1=component/2=group/3=assembly/4=subcomponent
    float flatColor[3]{0.8f, 0.8f, 0.8f};   // per-draw constant tint (instanced path)
    // Prototype object-space AABB (for RT view-dependent LOD: per-instance
    // projected size + the box-fit proxy transform). Captured from DrawMeshCPU.
    float protoAabbMin[3]{0, 0, 0};
    float protoAabbMax[3]{0, 0, 0};
    // GPU instancing: one TLAS instance per 3x4 o2w in instanceXforms (12 floats
    // each); instanceColors is 3 floats/instance (empty -> use flatColor). Held on
    // the CPU and consumed in rebuildTlas (the TLAS instance array is the GPU copy).
    std::vector<float> instanceXforms;
    std::vector<float> instanceColors;
    // Raster GPU instancing (flat --next path): per-instance 3x4 o2w (instVbo,
    // 48B/inst, host-visible so per-instance culling can re-map the visible subset)
    // + per-instance color (instColorBuf) + per-vertex prototype color
    // (instVtxColorBuf, white when absent). drawInstanceCount is the count actually
    // drawn (== instanceCount unless per-instance culling shrank it this frame).
    VkBuffer instVbo{VK_NULL_HANDLE};
    VkDeviceMemory instVboMem{VK_NULL_HANDLE};
    VkBuffer instColorBuf{VK_NULL_HANDLE};
    VkDeviceMemory instColorMem{VK_NULL_HANDLE};
    VkBuffer instVtxColorBuf{VK_NULL_HANDLE};
    VkDeviceMemory instVtxColorMem{VK_NULL_HANDLE};
    uint32_t instanceCount{0};
    uint32_t drawInstanceCount{0};
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
  bool createWhiteTexture(std::string* err);

  // --- Ray tracing (ray query) ---
  void detectRtSupport();              // sets rtSupported_ + loads RT entrypoints
  bool createRtResources(std::string* err);  // descriptor layout/pool + pipeline
  void destroyRt();
  VkDeviceAddress bufferDeviceAddress(VkBuffer buf) const;
  bool createDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer* buf, VkDeviceMemory* mem);  // device-local + addr
  void buildBlas(VkMeshGPU& m);
  void buildBoxBlas();                  // shared unit-cube BLAS for LOD box proxies
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
  bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                        VkBuffer* buf, VkDeviceMemory* mem, bool deviceAddress = false);
  bool createTextureImage(const light3d::Image& img, VkImage* outImg,
                          VkDeviceMemory* outMem, VkImageView* outView);
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
  VkRenderPass overlayLoadPass_{VK_NULL_HANDLE};  // draw overlays over the RT image
  VkImage colorImg_{VK_NULL_HANDLE};
  VkDeviceMemory colorMem_{VK_NULL_HANDLE};
  VkImageView colorView_{VK_NULL_HANDLE};
  VkImage depthImg_{VK_NULL_HANDLE};
  VkDeviceMemory depthMem_{VK_NULL_HANDLE};
  VkImageView depthView_{VK_NULL_HANDLE};
  VkFramebuffer offscreenFb_{VK_NULL_HANDLE};
  VkSampler sampler_{VK_NULL_HANDLE};
  VkDescriptorSet offscreenTexId_{VK_NULL_HANDLE};
  int vpW_{0}, vpH_{0};
  VkFormat colorFormat_{VK_FORMAT_R8G8B8A8_UNORM};
  VkFormat depthFormat_{VK_FORMAT_D32_SFLOAT};

  // Pipeline
  VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};
  // GPU tessellation displacement pipeline (shares pipelineLayout_; PATCH_LIST
  // topology + tesc/tese). Created only when the device supports the
  // tessellationShader feature; otherwise displaced meshes stay coarse.
  bool tessSupported_{false};
  VkPipeline tessPipeline_{VK_NULL_HANDLE};
  VkShaderStageFlags pushStages_{VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT};
  VkPipelineLayout instPipelineLayout_{VK_NULL_HANDLE};
  VkPipeline instPipeline_{VK_NULL_HANDLE};

  // Unlit line pipeline for debug helpers (grid/axes/bbox). Per-frame host
  // buffers (grow on demand) so a frame never writes a buffer still in flight.
  VkPipelineLayout lineLayout_{VK_NULL_HANDLE};
  VkPipeline linePipeline_{VK_NULL_HANDLE};
  VkPipeline linePipelineNoDepth_{VK_NULL_HANDLE};  // X-ray overlay (skeleton)

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

  // Textures (base color). One combined-image-sampler descriptor per texture,
  // plus a default 1x1 white texture for untextured submeshes.
  VkDescriptorSetLayout texSetLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout skinSetLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout influenceSetLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout faceSetLayout_{VK_NULL_HANDLE};  // set 3: source-face-id SSBO
  // Set 5: global displacement params UBO {scale, maxTessLevel}, read in the
  // vertex + tessellation stages so the UI's displacement-scale and max-tess
  // sliders are live on Vulkan (the push constants are full). One persistently
  // mapped host buffer, written each frame (volume-UBO convention).
  VkDescriptorSetLayout dispParamsSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool dispParamsPool_{VK_NULL_HANDLE};
  VkDescriptorSet dispParamsSet_{VK_NULL_HANDLE};
  VkBuffer dispParamsUbo_{VK_NULL_HANDLE};
  VkDeviceMemory dispParamsUboMem_{VK_NULL_HANDLE};
  void* dispParamsMapped_{nullptr};
  // Set 6: per-material displacement texture scale/bias (2 floats/material, indexed
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
  // source-face data (or on pool overflow), so set 3 is always present.
  VkBuffer dummyFaceBuf_{VK_NULL_HANDLE};
  VkDeviceMemory dummyFaceMem_{VK_NULL_HANDLE};
  VkDescriptorSet dummyFaceDesc_{VK_NULL_HANDLE};
  VkDescriptorSet allocFaceDescriptor(VkBuffer buffer, VkDeviceSize size);
  VkImage whiteImg_{VK_NULL_HANDLE};
  VkDeviceMemory whiteMem_{VK_NULL_HANDLE};
  VkImageView whiteView_{VK_NULL_HANDLE};
  VkDescriptorSet whiteDesc_{VK_NULL_HANDLE};
  // Black 1x1 (red=0) bound to set 4 when a submesh has no displacement (or
  // displacement is globally off): the vertex shader always samples set 4 and
  // displaces by red, so black = no displacement (no push-constant lane needed).
  VkImage blackImg_{VK_NULL_HANDLE};
  VkDeviceMemory blackMem_{VK_NULL_HANDLE};
  VkImageView blackView_{VK_NULL_HANDLE};
  VkDescriptorSet blackDesc_{VK_NULL_HANDLE};
  std::vector<VkImage> texImgs_;
  std::vector<VkDeviceMemory> texMems_;
  std::vector<VkImageView> texViews_;
  std::vector<VkDescriptorSet> texDescs_;
  std::vector<int> matBaseTex_;  // per material: DrawScene texture index or -1
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
  std::vector<float> matColor_;  // 4 floats (rgb+alpha) per material

  // Last frame parameters (copied; caller's pointers are transient)
  bool hasParams_{false};
  float view_[16];
  float proj_[16];
  float cameraPos_[3]{0, 0, 0};
  float clear_[4]{0.12f, 0.12f, 0.13f, 1.0f};

  // --- Ray tracing (ray query) state ---
  bool rtSupported_{false};   // device + shader available
  bool rtActive_{false};      // RT technique currently selected
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

  PFN_vkGetBufferDeviceAddressKHR pfnGetBufferDeviceAddress_{nullptr};
  PFN_vkGetAccelerationStructureBuildSizesKHR pfnGetASBuildSizes_{nullptr};
  PFN_vkCreateAccelerationStructureKHR pfnCreateAS_{nullptr};
  PFN_vkDestroyAccelerationStructureKHR pfnDestroyAS_{nullptr};
  PFN_vkCmdBuildAccelerationStructuresKHR pfnCmdBuildAS_{nullptr};
  PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetASDeviceAddress_{nullptr};

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
  VkDeviceSize rtMatCap_{0};
  VkBuffer instInfoBuf_{VK_NULL_HANDLE};    // per-TLAS-instance {meshId, tint} (binding 4)
  VkDeviceMemory instInfoMem_{VK_NULL_HANDLE};

  VkImage rtImage_{VK_NULL_HANDLE};
  VkDeviceMemory rtImageMem_{VK_NULL_HANDLE};
  VkImageView rtImageView_{VK_NULL_HANDLE};

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

  VkDescriptorSetLayout rtSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool rtPool_{VK_NULL_HANDLE};
  VkDescriptorSet rtSet_{VK_NULL_HANDLE};
  VkPipelineLayout rtPipelineLayout_{VK_NULL_HANDLE};
  VkPipeline rtPipeline_{VK_NULL_HANDLE};
};

}  // namespace tusdview
