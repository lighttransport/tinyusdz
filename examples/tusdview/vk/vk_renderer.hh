// SPDX-License-Identifier: Apache-2.0
// tusdview - Vulkan backend. Renders the scene into an offscreen VkImage which
// is shown by the GUI via ImGui::Image (ImGui_ImplVulkan_AddTexture). ImGui
// itself renders into the swapchain.
//
// v1 scope: geometry + per-submesh base color via push constants (no descriptor
// sets for the 3D pass, no textures). Textures remain a GL-only feature for now.
#pragma once

#include <vulkan/vulkan.h>

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
  void uploadTexture(int slot, const DrawTextureCPU& tex) override;
  void resizeViewport(int width, int height) override;
  void newFrame() override;
  void renderFrame(const RenderFrameParams& params) override;
  ViewportTexHandle viewportTexture() const override;
  void present() override;
  bool captureViewport(std::vector<uint8_t>* rgba, int* w, int* h) override;
  bool captureWindow(std::vector<uint8_t>* rgba, int* w, int* h) override;  // headless composite
  const RendererCaps& caps() const override { return caps_; }
  bool rayTracingAvailable() const override { return rtSupported_; }
  bool rayTracingActive() const override { return rtActive_; }
  void setRayTracing(bool enable) override;
  void shutdown() override;

 private:
  static constexpr int kFramesInFlight = 2;

  struct VkMeshGPU {
    VkBuffer vbo{VK_NULL_HANDLE};
    VkDeviceMemory vboMem{VK_NULL_HANDLE};
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
  bool createOffscreenRenderPass(std::string* err);
  bool createPipeline(std::string* err);
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
  void rebuildTlas();                  // (re)build TLAS + MeshDesc/Material SSBOs
  void createRtImage();                // storage image sized to the viewport
  void traceRt(VkCommandBuffer cb);    // dispatch + copy into colorImg_

  void destroySwapchain();
  bool recreateSwapchain();
  void destroyOffscreen();
  void destroyScene();

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
  bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void* data,
                        VkBuffer* buf, VkDeviceMemory* mem, bool deviceAddress = false);
  bool createTextureImage(const light3d::Image& img, VkImage* outImg,
                          VkDeviceMemory* outMem, VkImageView* outView);
  VkDescriptorSet allocTexDescriptor(VkImageView view);
  VkCommandBuffer beginOneShot();
  void endOneShot(VkCommandBuffer cb);
  VkShaderModule createShader(const uint32_t* code, size_t bytes);

  GLFWwindow* window_{nullptr};
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

  // Unlit line pipeline for debug helpers (grid/axes/bbox). Per-frame host
  // buffers (grow on demand) so a frame never writes a buffer still in flight.
  VkPipelineLayout lineLayout_{VK_NULL_HANDLE};
  VkPipeline linePipeline_{VK_NULL_HANDLE};
  VkPipeline linePipelineNoDepth_{VK_NULL_HANDLE};  // X-ray overlay (skeleton)
  VkBuffer helperBuf_[kFramesInFlight]{};
  VkDeviceMemory helperMem_[kFramesInFlight]{};
  VkDeviceSize helperCap_[kFramesInFlight]{};
  std::vector<HelperVertex> helperCopy_;  // copied in renderFrame, drawn in present
  // Overlay (skeleton X-ray) per-frame line buffers + copy.
  VkBuffer overlayBuf_[kFramesInFlight]{};
  VkDeviceMemory overlayMem_[kFramesInFlight]{};
  VkDeviceSize overlayCap_[kFramesInFlight]{};
  std::vector<HelperVertex> overlayCopy_;
  std::vector<uint8_t> meshVisible_;  // per-mesh visibility mask (raster), copied in renderFrame

  // Textures (base color). One combined-image-sampler descriptor per texture,
  // plus a default 1x1 white texture for untextured submeshes.
  VkDescriptorSetLayout texSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool texPool_{VK_NULL_HANDLE};
  VkImage whiteImg_{VK_NULL_HANDLE};
  VkDeviceMemory whiteMem_{VK_NULL_HANDLE};
  VkImageView whiteView_{VK_NULL_HANDLE};
  VkDescriptorSet whiteDesc_{VK_NULL_HANDLE};
  std::vector<VkImage> texImgs_;
  std::vector<VkDeviceMemory> texMems_;
  std::vector<VkImageView> texViews_;
  std::vector<VkDescriptorSet> texDescs_;
  std::vector<int> matBaseTex_;  // per material: DrawScene texture index or -1

  // Commands & sync
  VkCommandPool commandPool_{VK_NULL_HANDLE};
  VkCommandBuffer cmd_[kFramesInFlight]{};
  VkSemaphore imageAvailable_[kFramesInFlight]{};
  VkSemaphore renderFinished_[kFramesInFlight]{};
  VkFence inFlight_[kFramesInFlight]{};
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

  VkImage rtImage_{VK_NULL_HANDLE};
  VkDeviceMemory rtImageMem_{VK_NULL_HANDLE};
  VkImageView rtImageView_{VK_NULL_HANDLE};

  VkDescriptorSetLayout rtSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool rtPool_{VK_NULL_HANDLE};
  VkDescriptorSet rtSet_{VK_NULL_HANDLE};
  VkPipelineLayout rtPipelineLayout_{VK_NULL_HANDLE};
  VkPipeline rtPipeline_{VK_NULL_HANDLE};
};

}  // namespace tusdview
