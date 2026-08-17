// SPDX-License-Identifier: Apache-2.0
#include "texture_gpu.hh"

#include "vulkan_processor.hh"
#if defined(TUSDVIEW_TEXTURE_HAVE_HIP)
#include "hip_processor.hh"
#endif
#if defined(TUSDVIEW_TEXTURE_HAVE_CUDA)
#include "cuda_processor.hh"
#endif

#include <algorithm>

namespace tusdview_texture_bench {

const char* BackendName(Backend backend) {
  switch (backend) {
    case Backend::Vulkan: return "vulkan";
    case Backend::CUDA: return "cuda";
    case Backend::HIP: return "hip";
  }
  return "unknown";
}

const char* FormatName(CompressionFormat format) {
  switch (format) {
    case CompressionFormat::BC1: return "bc1";
    case CompressionFormat::BC3: return "bc3";
    case CompressionFormat::BC5: return "bc5";
    case CompressionFormat::BC6H: return "bc6h";
    case CompressionFormat::BC7: return "bc7";
    case CompressionFormat::ASTC: return "astc";
  }
  return "unknown";
}

size_t CompressedSize(CompressionFormat format, uint32_t width, uint32_t height) {
  const size_t blocks = static_cast<size_t>((width + 3u) / 4u) *
                        static_cast<size_t>((height + 3u) / 4u);
  return blocks * (format == CompressionFormat::BC1 ? 8u : 16u);
}

std::unique_ptr<Processor> CreateProcessor(Backend backend, bool allowSoftware,
                                            const std::string& deviceSelector,
                                            std::string* error) {
  if (backend == Backend::Vulkan) {
    auto processor = std::make_unique<VulkanProcessor>(allowSoftware, deviceSelector);
    if (processor->device().backend.empty()) {
      if (error) *error = processor->device().driver.empty()
                              ? "Vulkan initialization failed"
                              : processor->device().driver;
      return nullptr;
    }
    return processor;
  }
  if (backend == Backend::HIP) {
#if defined(TUSDVIEW_TEXTURE_HAVE_HIP)
    auto processor = std::make_unique<HipProcessor>(deviceSelector);
    if (!processor->init(error)) return nullptr;
    return processor;
#else
    if (error) *error = "HIP compiler/runtime is not available in this build";
    return nullptr;
#endif
  }
  if (backend == Backend::CUDA) {
#if defined(TUSDVIEW_TEXTURE_HAVE_CUDA)
    auto processor = std::make_unique<CudaProcessor>(deviceSelector);
    if (!processor->init(error)) return nullptr;
    return processor;
#else
    if (error) *error = "CUDA compiler/runtime is not available in this build";
    return nullptr;
#endif
  }
  if (error) {
    *error = std::string(BackendName(backend)) +
             " backend is reserved for the CUDA/HIP adapter and is not built yet";
  }
  return nullptr;
}

}  // namespace tusdview_texture_bench
