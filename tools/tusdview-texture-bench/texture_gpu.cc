// SPDX-License-Identifier: Apache-2.0
#include "texture_gpu.hh"

#include "vulkan_processor.hh"
#if defined(TUSDVIEW_TEXTURE_HAVE_HIP) && !defined(TUSDVIEW_TEXTURE_HIP_DYNAMIC)
#include "hip_processor.hh"
#endif
#if defined(TUSDVIEW_TEXTURE_HAVE_CUDA)
#include "cuda_processor.hh"
#endif

#if defined(TUSDVIEW_TEXTURE_HAVE_HIP) && defined(TUSDVIEW_TEXTURE_HIP_DYNAMIC)
#include "hipew.h"
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
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

#if defined(TUSDVIEW_TEXTURE_HAVE_HIP) && defined(TUSDVIEW_TEXTURE_HIP_DYNAMIC)
namespace {
using HipCreateFn = Processor* (*)(const char*, std::string*);
using HipDestroyFn = void (*)(Processor*);

class DynamicHipProcessor final : public Processor {
 public:
  DynamicHipProcessor(void* library, Processor* impl, HipDestroyFn destroy)
      : library_(library), impl_(impl), destroy_(destroy) {}
  ~DynamicHipProcessor() override {
    if (destroy_ && impl_) destroy_(impl_);
#if !defined(_WIN32)
    if (library_) dlclose(library_);
#endif
  }
  const DeviceInfo& device() const override { return impl_->device(); }
  bool process(const TextureRequest& request, TextureResult* result,
               std::string* error) override {
    return impl_->process(request, result, error);
  }

 private:
  void* library_{nullptr};
  Processor* impl_{nullptr};
  HipDestroyFn destroy_{nullptr};
};

std::unique_ptr<Processor> LoadHipTexturePlugin(const std::string& selector,
                                                std::string* error) {
#if defined(_WIN32)
  if (error) *error = "dynamic HIP texture plugin is not implemented on Windows";
  (void)selector;
  return nullptr;
#else
  if (hipewInit(HIPEW_INIT_HIP) != HIPEW_SUCCESS) {
    if (error) *error = "hipew: HIP runtime is unavailable";
    return nullptr;
  }
#if defined(TUSDVIEW_TEXTURE_HIP_PLUGIN_PATH)
  const char* path = TUSDVIEW_TEXTURE_HIP_PLUGIN_PATH;
#else
  const char* path = "tusdview_texture_hip.so";
#endif
  void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    if (error) {
      const char* detail = dlerror();
      *error = std::string("HIP texture plugin load failed: ") +
               (detail ? detail : "unknown error");
    }
    return nullptr;
  }
  auto create = reinterpret_cast<HipCreateFn>(
      dlsym(library, "tusdview_texture_hip_create"));
  auto destroy = reinterpret_cast<HipDestroyFn>(
      dlsym(library, "tusdview_texture_hip_destroy"));
  if (!create || !destroy) {
    if (error) *error = "HIP texture plugin has an invalid ABI";
    dlclose(library);
    return nullptr;
  }
  Processor* impl = create(selector.c_str(), error);
  if (!impl) {
    dlclose(library);
    return nullptr;
  }
  return std::make_unique<DynamicHipProcessor>(library, impl, destroy);
#endif
}
}  // namespace
#endif

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
#if defined(TUSDVIEW_TEXTURE_HIP_DYNAMIC)
    return LoadHipTexturePlugin(deviceSelector, error);
#else
    auto processor = std::make_unique<HipProcessor>(deviceSelector);
    if (!processor->init(error)) return nullptr;
    return processor;
#endif
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
