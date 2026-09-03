// SPDX-License-Identifier: Apache-2.0
// Minimal OptiX driver loader. OptiX SDK types remain private to the .cc file.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace lusdview {

struct OptixAccelSizes {
  size_t outputBytes{0};
  size_t temporaryBytes{0};
};

struct OptixInstanceInput {
  float transform[12]{};
  uint32_t instanceId{0};
  uint32_t sbtOffset{0};
  uint64_t traversable{0};
};

class OptixRuntime {
 public:
  OptixRuntime();
  ~OptixRuntime();

  OptixRuntime(const OptixRuntime&) = delete;
  OptixRuntime& operator=(const OptixRuntime&) = delete;

  // Loads the driver-provided OptiX library and queries the ABI-versioned
  // function table. Idempotent after a successful load; never resets a GPU.
  bool load(std::string* err);
  // Creates an OptiX device context from an already-current CUDA CUcontext.
  // The CUDA context remains owned by the caller and must outlive this object.
  bool attachCudaContext(void* cudaContext, std::string* err);
  void detachCudaContext();
  void unload();

  bool loaded() const;
  bool attached() const;
  int abiVersion() const;
  const void* functionTable() const;
  void* deviceContext() const;
  bool createPreviewPipeline(const void* optixIr, size_t optixIrBytes,
                             std::string* err);
  void destroyPipeline();
  bool pipelineReady() const;
  bool packPreviewSbt(const std::vector<uint64_t>& triangleOffsets,
                      std::vector<uint8_t>* packed, uint32_t* missOffset,
                      uint32_t* hitOffset, uint32_t* hitStride,
                      std::string* err) const;
  bool launchPreview(uintptr_t cudaStream, uintptr_t launchParams,
                     size_t launchParamsBytes, uintptr_t sbtRecords,
                     uint32_t missOffset, uint32_t hitOffset,
                     uint32_t hitStride, uint32_t hitCount,
                     unsigned int width, unsigned int height,
                     std::string* err) const;

  // Triangle vertices are tightly packed float3 values in CUDA device memory.
  // Allocation stays with CudaRayTracer so OptiX and software CUDA share one
  // memory owner and failure cleanup path.
  bool triangleGasSizes(uintptr_t vertices, size_t triangleCount,
                        OptixAccelSizes* sizes, std::string* err,
                        bool allowUpdate = false) const;
  bool buildTriangleGas(uintptr_t cudaStream, uintptr_t vertices,
                        size_t triangleCount, uintptr_t temporary,
                        size_t temporaryBytes, uintptr_t output,
                        size_t outputBytes, uintptr_t compactedSizeOutput,
                        uint64_t* traversable,
                        std::string* err, bool allowUpdate = false) const;
  bool updateTriangleGas(uintptr_t cudaStream, uintptr_t vertices,
                         size_t triangleCount, uintptr_t temporary,
                         size_t temporaryBytes, uintptr_t output,
                         size_t outputBytes, uint64_t* traversable,
                         std::string* err) const;
  bool compactGas(uintptr_t cudaStream, uint64_t inputTraversable,
                  uintptr_t output, size_t outputBytes,
                  uint64_t* outputTraversable, std::string* err) const;
  bool packInstances(const std::vector<OptixInstanceInput>& instances,
                     std::vector<uint8_t>* packed, std::string* err) const;
  bool instanceAccelSizes(uintptr_t packedInstances, size_t instanceCount,
                          OptixAccelSizes* sizes, std::string* err) const;
  bool buildInstanceAccel(uintptr_t cudaStream, uintptr_t packedInstances,
                          size_t instanceCount, uintptr_t temporary,
                          size_t temporaryBytes, uintptr_t output,
                          size_t outputBytes, uint64_t* traversable,
                          std::string* err) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lusdview
