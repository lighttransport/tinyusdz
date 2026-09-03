// SPDX-License-Identifier: Apache-2.0
// Standalone GPU texture processing interface used by lusdview's benchmark.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lusdview_texture_bench {

enum class Backend { Vulkan, CUDA, HIP };
enum class ResizeFilter { Bilinear, Mitchell };
enum class CompressionFormat { BC1, BC3, BC5, BC6H, BC7, ASTC };

struct DeviceInfo {
  std::string backend;
  std::string name;
  std::string driver;
  bool hardware{false};
  bool supportsBC1{false};
  bool supportsBC3{false};
  bool supportsBC5{false};
  bool supportsBC7{false};
  bool supportsBC6H{false};
  bool supportsASTC{false};
};

struct TextureRequest {
  const uint8_t* rgba{nullptr};
  size_t rgbaBytes{0};
  const float* rgbf{nullptr};
  size_t rgbfBytes{0};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t dstWidth{0};
  uint32_t dstHeight{0};
  bool srgb{true};
  ResizeFilter filter{ResizeFilter::Mitchell};
  CompressionFormat format{CompressionFormat::BC7};
  bool resize{true};
  bool compress{true};
  bool downloadResized{false};
  // Keep intermediate mip levels on the backend device. The final level may
  // set this false to request the host copy used for validation.
  bool deviceMipChain{false};
};

struct Timing {
  double uploadMs{0.0};
  double resizeGpuMs{0.0};
  double compressGpuMs{0.0};
  double downloadMs{0.0};
  double totalMs{0.0};
};

struct TextureResult {
  uint32_t width{0};
  uint32_t height{0};
  std::vector<uint8_t> resizedRGBA;
  std::vector<float> resizedRGBF;
  std::vector<uint8_t> compressed;
  Timing timing;
};

class Processor {
 public:
  virtual ~Processor() = default;
  virtual const DeviceInfo& device() const = 0;
  virtual bool process(const TextureRequest& request, TextureResult* result,
                       std::string* error) = 0;
};

std::unique_ptr<Processor> CreateProcessor(Backend backend, bool allowSoftware,
                                            const std::string& deviceSelector,
                                            std::string* error);

const char* BackendName(Backend backend);
const char* FormatName(CompressionFormat format);
inline size_t CompressedSize(CompressionFormat format, uint32_t width,
                             uint32_t height) {
  const size_t blocks = static_cast<size_t>((width + 3u) / 4u) *
                        static_cast<size_t>((height + 3u) / 4u);
  return blocks * (format == CompressionFormat::BC1 ? 8u : 16u);
}

}  // namespace lusdview_texture_bench
