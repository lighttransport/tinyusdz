// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "texture_gpu.hh"

namespace tusdview_texture_bench {

class CudaProcessor final : public Processor {
 public:
  explicit CudaProcessor(const std::string& selector);
  ~CudaProcessor() override;
  const DeviceInfo& device() const override { return info_; }
  bool process(const TextureRequest&, TextureResult*, std::string*) override;
  bool init(std::string*);

 private:
  DeviceInfo info_;
  std::string selector_;
  int device_{-1};
  void* stream_{nullptr};
  void* input_{nullptr};
  void* hdrInput_{nullptr};
  void* hdrResized_{nullptr};
  void* resized_{nullptr};
  void* compressed_{nullptr};
  size_t inputCapacity_{0}, hdrInputCapacity_{0}, hdrResizedCapacity_{0}, resizedCapacity_{0}, compressedCapacity_{0};
  bool ldrChainActive_{false};
  uint32_t ldrChainWidth_{0};
  uint32_t ldrChainHeight_{0};
};

}  // namespace tusdview_texture_bench
