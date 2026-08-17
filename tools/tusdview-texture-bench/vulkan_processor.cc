// SPDX-License-Identifier: Apache-2.0
#include "vulkan_processor.hh"

#include "volk.h"
#include "resize_comp.spv.h"
#include "resize_hdr_comp.spv.h"
#include "compress_comp.spv.h"
#include "bc6h_comp.spv.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <vector>

namespace tusdview_texture_bench {
namespace {

using Clock = std::chrono::steady_clock;

}  // namespace

VulkanProcessor::VulkanProcessor(bool allowSoftware, const std::string& selector) {
  std::string ignored;
  if (volkInitialize() != VK_SUCCESS) return;
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "tusdview texture benchmark";
  app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app.pEngineName = "tinyusdz";
  app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app.apiVersion = VK_API_VERSION_1_2;
  VkInstanceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &app;
  if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) return;
  volkLoadInstance(instance_);
  if (!pickDevice(allowSoftware, selector, &ignored)) return;
  if (!createResources(&ignored)) return;
}

VulkanProcessor::~VulkanProcessor() {
  if (device_) vkDeviceWaitIdle(device_);
  if (device_) {
    if (resizePipeline_) vkDestroyPipeline(device_, resizePipeline_, nullptr);
    if (resizeHdrPipeline_) vkDestroyPipeline(device_, resizeHdrPipeline_, nullptr);
    if (compressPipeline_) vkDestroyPipeline(device_, compressPipeline_, nullptr);
    if (bc6hPipeline_) vkDestroyPipeline(device_, bc6hPipeline_, nullptr);
    if (resizeLayout_) vkDestroyPipelineLayout(device_, resizeLayout_, nullptr);
    if (resizeHdrLayout_) vkDestroyPipelineLayout(device_, resizeHdrLayout_, nullptr);
    if (compressLayout_) vkDestroyPipelineLayout(device_, compressLayout_, nullptr);
    if (bc6hLayout_) vkDestroyPipelineLayout(device_, bc6hLayout_, nullptr);
    if (resizeSetLayout_) vkDestroyDescriptorSetLayout(device_, resizeSetLayout_, nullptr);
    if (compressSetLayout_) vkDestroyDescriptorSetLayout(device_, compressSetLayout_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (queryPool_) vkDestroyQueryPool(device_, queryPool_, nullptr);
    if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
    destroyBuffer(&input_, &inputMemory_, &inputMapped_);
    destroyBuffer(&resizeOutput_, &resizeOutputMemory_, &resizeOutputMapped_);
    destroyBuffer(&compressedOutput_, &compressedOutputMemory_, &compressedOutputMapped_);
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_) vkDestroyInstance(instance_, nullptr);
}

uint32_t VulkanProcessor::memoryType(uint32_t bits, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties mp{};
  vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & properties) == properties)
      return i;
  }
  return std::numeric_limits<uint32_t>::max();
}

void VulkanProcessor::destroyBuffer(VkBuffer* buffer, VkDeviceMemory* memory, void** mapped) {
  if (*mapped && *memory) vkUnmapMemory(device_, *memory);
  if (*buffer) vkDestroyBuffer(device_, *buffer, nullptr);
  if (*memory) vkFreeMemory(device_, *memory, nullptr);
  *buffer = VK_NULL_HANDLE; *memory = VK_NULL_HANDLE; *mapped = nullptr;
}

bool VulkanProcessor::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags properties, VkBuffer* buffer,
                                   VkDeviceMemory* memory, void** mapped,
                                   std::string* error) {
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device_, &bi, nullptr, buffer) != VK_SUCCESS) {
    if (error) *error = "vkCreateBuffer failed";
    return false;
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device_, *buffer, &req);
  const uint32_t type = memoryType(req.memoryTypeBits, properties);
  if (type == std::numeric_limits<uint32_t>::max()) {
    if (error) *error = "no compatible Vulkan memory type";
    vkDestroyBuffer(device_, *buffer, nullptr); *buffer = VK_NULL_HANDLE;
    return false;
  }
  VkMemoryAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = type;
  if (vkAllocateMemory(device_, &ai, nullptr, memory) != VK_SUCCESS ||
      vkBindBufferMemory(device_, *buffer, *memory, 0) != VK_SUCCESS) {
    if (*memory) vkFreeMemory(device_, *memory, nullptr);
    vkDestroyBuffer(device_, *buffer, nullptr);
    *buffer = VK_NULL_HANDLE; *memory = VK_NULL_HANDLE;
    if (error) *error = "Vulkan buffer allocation/bind failed";
    return false;
  }
  if (mapped && vkMapMemory(device_, *memory, 0, size, 0, mapped) != VK_SUCCESS) {
    destroyBuffer(buffer, memory, mapped);
    if (error) *error = "Vulkan host mapping failed";
    return false;
  }
  return true;
}

bool VulkanProcessor::pickDevice(bool allowSoftware, const std::string& selector,
                                 std::string* error) {
  uint32_t count = 0;
  if (vkEnumeratePhysicalDevices(instance_, &count, nullptr) != VK_SUCCESS || count == 0) {
    if (error) *error = "no Vulkan physical devices";
    return false;
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());
  int bestScore = std::numeric_limits<int>::min();
  for (uint32_t index = 0; index < count; ++index) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(devices[index], &props);
    if (!selector.empty() && selector != std::to_string(index) &&
        std::string(props.deviceName).find(selector) == std::string::npos) continue;
    if (!allowSoftware && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &qcount, queues.data());
    uint32_t q = std::numeric_limits<uint32_t>::max();
    for (uint32_t qi = 0; qi < qcount; ++qi) {
      if (queues[qi].queueFlags & VK_QUEUE_COMPUTE_BIT) { q = qi; break; }
    }
    if (q == std::numeric_limits<uint32_t>::max()) continue;
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(devices[index], &features);
    int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 0;
    if (props.vendorID == 0x1002u) score += 200;
    if (features.textureCompressionBC) score += 50;
    if (score > bestScore) {
      bestScore = score; physical_ = devices[index]; queueFamily_ = q;
      info_.name = props.deviceName;
      info_.driver = std::to_string(VK_VERSION_MAJOR(props.driverVersion)) + "." +
                     std::to_string(VK_VERSION_MINOR(props.driverVersion));
      info_.hardware = props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU;
      info_.supportsBC1 = info_.supportsBC3 = info_.supportsBC5 = info_.supportsBC7 =
          features.textureCompressionBC == VK_TRUE;
      info_.supportsBC6H = true;
      info_.supportsASTC = true;
    }
  }
  if (!physical_) {
    if (error) *error = "no suitable Vulkan compute device";
    return false;
  }
  info_.backend = "vulkan";
  return true;
}

bool VulkanProcessor::createPipeline(const uint32_t* code, size_t words,
                                     VkPipeline* pipeline, VkPipelineLayout* layout,
                                     std::string* error) {
  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = words * sizeof(uint32_t);
  smi.pCode = code;
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_, &smi, nullptr, &module) != VK_SUCCESS) {
    if (error) *error = "vkCreateShaderModule failed"; return false;
  }
  VkPushConstantRange pc{};
  pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pc.offset = 0; pc.size = 24;
  VkPipelineLayoutCreateInfo lci{};
  lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  lci.setLayoutCount = 1;
  lci.pSetLayouts = (pipeline == &resizePipeline_ || pipeline == &resizeHdrPipeline_) ? &resizeSetLayout_ : &compressSetLayout_;
  lci.pushConstantRangeCount = 1; lci.pPushConstantRanges = &pc;
  if (vkCreatePipelineLayout(device_, &lci, nullptr, layout) != VK_SUCCESS) {
    vkDestroyShaderModule(device_, module, nullptr);
    if (error) *error = "vkCreatePipelineLayout failed"; return false;
  }
  VkComputePipelineCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module = module; ci.stage.pName = "main"; ci.layout = *layout;
  const VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, pipeline);
  vkDestroyShaderModule(device_, module, nullptr);
  if (result != VK_SUCCESS) {
    if (error) *error = "vkCreateComputePipelines failed"; return false;
  }
  return true;
}

bool VulkanProcessor::createResources(std::string* error) {
  float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queueFamily_; qci.queueCount = 1; qci.pQueuePriorities = &priority;
  VkDeviceCreateInfo dci{}; dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
  if (vkCreateDevice(physical_, &dci, nullptr, &device_) != VK_SUCCESS) {
    if (error) *error = "vkCreateDevice failed"; return false;
  }
  volkLoadDevice(device_); vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
  VkCommandPoolCreateInfo cpi{}; cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex = queueFamily_;
  if (vkCreateCommandPool(device_, &cpi, nullptr, &commandPool_) != VK_SUCCESS) return false;
  VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cai.commandPool = commandPool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(device_, &cai, &commandBuffer_) != VK_SUCCESS) return false;
  VkQueryPoolCreateInfo qpi{}; qpi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  qpi.queryType = VK_QUERY_TYPE_TIMESTAMP; qpi.queryCount = 4;
  if (vkCreateQueryPool(device_, &qpi, nullptr, &queryPool_) != VK_SUCCESS) return false;
  VkDescriptorSetLayoutBinding bindings[2]{};
  for (uint32_t i=0;i<2;++i) { bindings[i].binding=i; bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; }
  VkDescriptorSetLayoutCreateInfo sl{}; sl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sl.bindingCount=2; sl.pBindings=bindings;
  if (vkCreateDescriptorSetLayout(device_, &sl, nullptr, &resizeSetLayout_) != VK_SUCCESS ||
      vkCreateDescriptorSetLayout(device_, &sl, nullptr, &compressSetLayout_) != VK_SUCCESS) return false;
  VkDescriptorPoolSize ps{}; ps.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps.descriptorCount=4;
  VkDescriptorPoolCreateInfo dpi{}; dpi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets=2; dpi.poolSizeCount=1; dpi.pPoolSizes=&ps;
  if (vkCreateDescriptorPool(device_, &dpi, nullptr, &descriptorPool_) != VK_SUCCESS) return false;
  VkDescriptorSetLayout layouts[2] = {resizeSetLayout_, compressSetLayout_};
  VkDescriptorSetAllocateInfo dai{}; dai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dai.descriptorPool=descriptorPool_; dai.descriptorSetCount=2; dai.pSetLayouts=layouts;
  VkDescriptorSet sets[2]{};
  if (vkAllocateDescriptorSets(device_, &dai, sets) != VK_SUCCESS) return false;
  resizeSet_=sets[0]; compressSet_=sets[1];
  if (!createPipeline(resize_comp_spv, sizeof(resize_comp_spv)/sizeof(uint32_t), &resizePipeline_, &resizeLayout_, error)) return false;
  if (!createPipeline(resize_hdr_comp_spv, sizeof(resize_hdr_comp_spv)/sizeof(uint32_t), &resizeHdrPipeline_, &resizeHdrLayout_, error)) return false;
  if (!createPipeline(compress_comp_spv, sizeof(compress_comp_spv)/sizeof(uint32_t), &compressPipeline_, &compressLayout_, error)) return false;
  if (!createPipeline(bc6h_comp_spv, sizeof(bc6h_comp_spv)/sizeof(uint32_t), &bc6hPipeline_, &bc6hLayout_, error)) return false;
  VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(physical_, &props);
  timestampPeriodNs_ = props.limits.timestampPeriod;
  return true;
}

bool VulkanProcessor::submit(VkCommandBuffer cb, std::string* error) {
  if (vkEndCommandBuffer(cb) != VK_SUCCESS) { if (error) *error="vkEndCommandBuffer failed"; return false; }
  VkFenceCreateInfo fi{}; fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; VkFence fence=VK_NULL_HANDLE;
  if (vkCreateFence(device_, &fi, nullptr, &fence) != VK_SUCCESS) return false;
  VkSubmitInfo si{}; si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount=1; si.pCommandBuffers=&cb;
  VkResult r=vkQueueSubmit(queue_,1,&si,fence); if(r==VK_SUCCESS) r=vkWaitForFences(device_,1,&fence,VK_TRUE,UINT64_MAX);
  vkDestroyFence(device_,fence,nullptr); if(r!=VK_SUCCESS){if(error)*error="Vulkan queue submission failed";return false;} return true;
}

bool VulkanProcessor::process(const TextureRequest& request, TextureResult* result,
                              std::string* error) {
  const bool bc6h = request.format == CompressionFormat::BC6H;
  if (!device_ || (!bc6h && (!request.rgba || request.rgbaBytes < size_t(request.width)*request.height*4u)) ||
      (bc6h && (!request.rgbf || request.rgbfBytes < size_t(request.width)*request.height*3u*sizeof(float))) || !result) {
    if (error) *error = "Vulkan processor is unavailable or request is invalid"; return false;
  }
  const uint32_t dw = request.resize ? std::max(1u, request.dstWidth) : request.width;
  const uint32_t dh = request.resize ? std::max(1u, request.dstHeight) : request.height;
  const size_t inputBytes=bc6h ? size_t(request.width)*request.height*3u*sizeof(float) : size_t(request.width)*request.height*4u;
  const bool chainInput = !bc6h && ldrChainActive_ &&
                          ldrChainWidth_ == request.width && ldrChainHeight_ == request.height;
  if (!bc6h && !request.deviceMipChain && !chainInput) ldrChainActive_ = false;
  const size_t resizedBytes=bc6h ? size_t(dw)*dh*3u*sizeof(float) : size_t(dw)*dh*4u;
  const size_t compressedBytes=CompressedSize(request.format,dw,dh);
  auto ensure=[&](VkDeviceSize need,VkDeviceSize* cap,VkBuffer* b,VkDeviceMemory* m,void** map)->bool{
    if(*cap>=need)return true; destroyBuffer(b,m,map); *cap=need; return createBuffer(need,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,b,m,map,error);
  };
  if(!ensure(inputBytes,&inputCapacity_,&input_,&inputMemory_,&inputMapped_) ||
     !ensure(resizedBytes,&resizeCapacity_,&resizeOutput_,&resizeOutputMemory_,&resizeOutputMapped_) ||
     !ensure(compressedBytes,&compressedCapacity_,&compressedOutput_,&compressedOutputMemory_,&compressedOutputMapped_)) return false;
  if (chainInput) {
    std::swap(input_, resizeOutput_); std::swap(inputMemory_, resizeOutputMemory_);
    std::swap(inputMapped_, resizeOutputMapped_); std::swap(inputCapacity_, resizeCapacity_);
  } else {
    std::memcpy(inputMapped_,bc6h ? static_cast<const void*>(request.rgbf) : static_cast<const void*>(request.rgba),inputBytes);
  }
  const auto start=Clock::now();
  VkDescriptorBufferInfo in{input_,0,inputBytes}, out{resizeOutput_,0,resizedBytes}, comp{compressedOutput_,0,compressedBytes};
  VkWriteDescriptorSet writes[4]{}; for(int i=0;i<4;++i)writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet=resizeSet_;writes[0].dstBinding=0;writes[0].descriptorCount=1;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[0].pBufferInfo=&in;
  writes[1].dstSet=resizeSet_;writes[1].dstBinding=1;writes[1].descriptorCount=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&out;
  writes[2].dstSet=compressSet_;writes[2].dstBinding=0;writes[2].descriptorCount=1;writes[2].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[2].pBufferInfo=bc6h ? &out : &out;
  writes[3].dstSet=compressSet_;writes[3].dstBinding=1;writes[3].descriptorCount=1;writes[3].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[3].pBufferInfo=&comp;
  vkUpdateDescriptorSets(device_,4,writes,0,nullptr); vkResetCommandBuffer(commandBuffer_,0); vkResetQueryPool(device_,queryPool_,0,4);
  VkCommandBufferBeginInfo bi{};bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; if(vkBeginCommandBuffer(commandBuffer_,&bi)!=VK_SUCCESS)return false;
  vkCmdWriteTimestamp(commandBuffer_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queryPool_,0);
  if (bc6h) { uint32_t pc[4]={request.width,request.height,dw,dh}; vkCmdBindPipeline(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,resizeHdrPipeline_); vkCmdBindDescriptorSets(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,resizeHdrLayout_,0,1,&resizeSet_,0,nullptr); vkCmdPushConstants(commandBuffer_,resizeHdrLayout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),pc); vkCmdDispatch(commandBuffer_,(dw+15u)/16u,(dh+15u)/16u,1); }
  else if (request.resize){uint32_t pc[6]={request.width,request.height,dw,dh,request.filter==ResizeFilter::Mitchell?1u:0u,request.srgb?1u:0u};vkCmdBindPipeline(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,resizePipeline_);vkCmdBindDescriptorSets(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,resizeLayout_,0,1,&resizeSet_,0,nullptr);vkCmdPushConstants(commandBuffer_,resizeLayout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),pc);vkCmdDispatch(commandBuffer_,(dw+15u)/16u,(dh+15u)/16u,1);}else std::memcpy(resizeOutputMapped_,request.rgba,resizedBytes);
  vkCmdWriteTimestamp(commandBuffer_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,queryPool_,1);
  if(request.compress){
    if (bc6h) { VkBufferMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.buffer=resizeOutput_;barrier.size=resizedBytes;vkCmdPipelineBarrier(commandBuffer_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,1,&barrier,0,nullptr); uint32_t pc[4]={dw,dh,dw,dh}; vkCmdBindPipeline(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,bc6hPipeline_); vkCmdBindDescriptorSets(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,bc6hLayout_,0,1,&compressSet_,0,nullptr); vkCmdPushConstants(commandBuffer_,bc6hLayout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),pc); vkCmdDispatch(commandBuffer_,(dw+3u)/4u,(dh+3u)/4u,1); }
    else {VkBufferMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.buffer=resizeOutput_;barrier.size=resizedBytes;vkCmdPipelineBarrier(commandBuffer_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,1,&barrier,0,nullptr);uint32_t pc[3]={dw,dh,request.format==CompressionFormat::BC1?0u:request.format==CompressionFormat::BC3?1u:request.format==CompressionFormat::BC5?2u:request.format==CompressionFormat::BC7?3u:4u};vkCmdBindPipeline(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,compressPipeline_);vkCmdBindDescriptorSets(commandBuffer_,VK_PIPELINE_BIND_POINT_COMPUTE,compressLayout_,0,1,&compressSet_,0,nullptr);vkCmdPushConstants(commandBuffer_,compressLayout_,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(pc),pc);vkCmdDispatch(commandBuffer_,(dw+3u)/4u,(dh+3u)/4u,1);}
  }
  vkCmdWriteTimestamp(commandBuffer_,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,queryPool_,2);
  if(!submit(commandBuffer_,error))return false; uint64_t q[4]={};vkGetQueryPoolResults(device_,queryPool_,0,3,sizeof(q),q,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT);
  result->width=dw;result->height=dh;if (bc6h) result->resizedRGBF.assign(static_cast<float*>(resizeOutputMapped_),static_cast<float*>(resizeOutputMapped_)+size_t(dw)*dh*3u); else if (!request.deviceMipChain || request.downloadResized) result->resizedRGBA.assign(static_cast<uint8_t*>(resizeOutputMapped_),static_cast<uint8_t*>(resizeOutputMapped_)+resizedBytes);if (!bc6h) { ldrChainActive_=request.deviceMipChain; ldrChainWidth_=dw; ldrChainHeight_=dh; }if(request.compress)result->compressed.assign(static_cast<uint8_t*>(compressedOutputMapped_),static_cast<uint8_t*>(compressedOutputMapped_)+compressedBytes);
  result->timing.uploadMs=std::chrono::duration<double,std::milli>(start-Clock::now()).count()*-1.0;result->timing.totalMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();result->timing.resizeGpuMs=double(q[1]-q[0])*timestampPeriodNs_/1e6;result->timing.compressGpuMs=request.compress?double(q[2]-q[1])*timestampPeriodNs_/1e6:0.0;result->timing.downloadMs=0.0;return true;
}

}  // namespace tusdview_texture_bench
