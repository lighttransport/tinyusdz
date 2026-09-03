// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "texture_gpu.hh"

namespace lusdview_texture_bench {

class HipProcessor final : public Processor {
 public:
  explicit HipProcessor(const std::string& selector);
  ~HipProcessor() override;
  const DeviceInfo& device() const override { return info_; }
  bool process(const TextureRequest& request, TextureResult* result,
               std::string* error) override;
  bool init(std::string* error);

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
  size_t inputCapacity_{0};
  size_t hdrInputCapacity_{0};
  size_t hdrResizedCapacity_{0};
  size_t resizedCapacity_{0};
  size_t compressedCapacity_{0};
  bool ldrChainActive_{false};
  uint32_t ldrChainWidth_{0};
  uint32_t ldrChainHeight_{0};
};

}  // namespace lusdview_texture_bench
