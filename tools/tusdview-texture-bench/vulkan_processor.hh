// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "texture_gpu.hh"

#include <vulkan/vulkan.h>

namespace tusdview_texture_bench {

class VulkanProcessor final : public Processor {
 public:
  VulkanProcessor(bool allowSoftware, const std::string& selector);
  ~VulkanProcessor() override;
  const DeviceInfo& device() const override { return info_; }
  bool process(const TextureRequest& request, TextureResult* result,
               std::string* error) override;
  bool init(std::string* error);

 private:
  bool pickDevice(bool allowSoftware, const std::string& selector,
                  std::string* error);
  bool createResources(std::string* error);
  bool createPipeline(const uint32_t* code, size_t words, VkPipeline* pipeline,
                      VkPipelineLayout* layout, std::string* error);
  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkBuffer* buffer,
                    VkDeviceMemory* memory, void** mapped, std::string* error);
  uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags properties) const;
  void destroyBuffer(VkBuffer* buffer, VkDeviceMemory* memory, void** mapped);
  bool runResize(const TextureRequest& request, TextureResult* result,
                 std::string* error);
  bool runCompress(const TextureRequest& request, TextureResult* result,
                   std::string* error);
  bool submit(VkCommandBuffer commandBuffer, std::string* error);

  DeviceInfo info_;
  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue queue_{VK_NULL_HANDLE};
  uint32_t queueFamily_{0};
  VkCommandPool commandPool_{VK_NULL_HANDLE};
  VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
  VkQueryPool queryPool_{VK_NULL_HANDLE};
  VkPipeline resizePipeline_{VK_NULL_HANDLE};
  VkPipeline resizeHdrPipeline_{VK_NULL_HANDLE};
  VkPipeline compressPipeline_{VK_NULL_HANDLE};
  VkPipeline bc6hPipeline_{VK_NULL_HANDLE};
  VkPipelineLayout resizeLayout_{VK_NULL_HANDLE};
  VkPipelineLayout resizeHdrLayout_{VK_NULL_HANDLE};
  VkPipelineLayout compressLayout_{VK_NULL_HANDLE};
  VkPipelineLayout bc6hLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout resizeSetLayout_{VK_NULL_HANDLE};
  VkDescriptorSetLayout compressSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
  VkDescriptorSet resizeSet_{VK_NULL_HANDLE};
  VkDescriptorSet compressSet_{VK_NULL_HANDLE};
  VkBuffer input_{VK_NULL_HANDLE};
  VkDeviceMemory inputMemory_{VK_NULL_HANDLE};
  VkBuffer resizeOutput_{VK_NULL_HANDLE};
  VkDeviceMemory resizeOutputMemory_{VK_NULL_HANDLE};
  VkBuffer compressedOutput_{VK_NULL_HANDLE};
  VkDeviceMemory compressedOutputMemory_{VK_NULL_HANDLE};
  VkBuffer params_{VK_NULL_HANDLE};
  VkDeviceMemory paramsMemory_{VK_NULL_HANDLE};
  VkBuffer coefficients_{VK_NULL_HANDLE};
  VkDeviceMemory coefficientsMemory_{VK_NULL_HANDLE};
  VkDeviceSize inputCapacity_{0};
  VkDeviceSize resizeCapacity_{0};
  VkDeviceSize compressedCapacity_{0};
  VkDeviceSize paramsCapacity_{0};
  VkDeviceSize coefficientsCapacity_{0};
  void* inputMapped_{nullptr};
  void* resizeOutputMapped_{nullptr};
  void* compressedOutputMapped_{nullptr};
  void* paramsMapped_{nullptr};
  void* coefficientsMapped_{nullptr};
  float timestampPeriodNs_{1.0f};
  bool ldrChainActive_{false};
  uint32_t ldrChainWidth_{0};
  uint32_t ldrChainHeight_{0};
};

}  // namespace tusdview_texture_bench
