// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tinyusdz {
namespace tydra {
namespace next {

constexpr uint64_t GiB(uint64_t value) {
  return value > ((std::numeric_limits<uint64_t>::max)() >> 30)
             ? (std::numeric_limits<uint64_t>::max)()
             : value << 30;
}
constexpr uint64_t MiB(uint64_t value) {
  return value > ((std::numeric_limits<uint64_t>::max)() >> 20)
             ? (std::numeric_limits<uint64_t>::max)()
             : value << 20;
}

inline uint64_t Percent(uint64_t value, uint64_t percent) {
  if (percent == 0) return 0;
  // Split before multiplying so hostile capacity values cannot overflow the
  // budget formulas.
  const uint64_t whole = value / 100;
  const uint64_t remainder = value % 100;
  const uint64_t first =
      whole > (std::numeric_limits<uint64_t>::max)() / percent
          ? (std::numeric_limits<uint64_t>::max)()
          : whole * percent;
  const uint64_t second =
      remainder > (std::numeric_limits<uint64_t>::max)() / percent
          ? (std::numeric_limits<uint64_t>::max)()
          : (remainder * percent) / 100;
  return first > (std::numeric_limits<uint64_t>::max)() - second
             ? (std::numeric_limits<uint64_t>::max)()
             : first + second;
}

enum class LargeSceneQuality : uint8_t { Full, Adaptive, Proxy };

struct ResourceBudget {
  uint64_t host_capacity = 0;
  uint64_t host_limit = 0;
  uint64_t stage_limit = 0;
  uint64_t cpu_geometry_limit = 0;
  uint64_t io_cache_limit = 0;

  uint64_t vram_capacity = 0;
  uint64_t vram_limit = 0;
  uint64_t gpu_geometry_limit = 0;
  uint64_t gpu_texture_limit = 0;
  uint64_t upload_staging_limit = 0;
  uint64_t proxy_geometry_threshold = 0;

  LargeSceneQuality quality = LargeSceneQuality::Adaptive;
};

// Leaves two GiB to the display/system on 10 GiB-class devices. Devices with
// at least 12 GiB use half their capacity but never less than eight GiB.
inline uint64_t ComputeVramLimit(uint64_t capacity) {
  if (capacity == 0) return GiB(8);
  if (capacity >= GiB(12)) {
    return std::min(capacity, std::max(GiB(8), capacity / 2));
  }
  if (capacity > GiB(2)) return capacity - GiB(2);
  return capacity - capacity / 4;
}

inline ResourceBudget ComputeResourceBudget(uint64_t host_capacity,
                                            uint64_t vram_capacity) {
  ResourceBudget budget;
  budget.host_capacity = host_capacity;
  // The 32 GiB target retains two GiB for the OS, driver, and allocator
  // fragmentation. Smaller machines retain at least one GiB or one eighth.
  const uint64_t host_reserve =
      host_capacity >= GiB(16)
          ? GiB(2)
          : std::max(GiB(1), host_capacity / uint64_t{8});
  budget.host_limit = host_capacity > host_reserve
                          ? host_capacity - host_reserve
                          : host_capacity - host_capacity / 4;
  budget.stage_limit = Percent(budget.host_limit, 55);
  budget.cpu_geometry_limit = Percent(budget.host_limit, 25);
  budget.io_cache_limit = Percent(budget.host_limit, 10);

  budget.vram_capacity = vram_capacity;
  budget.vram_limit = ComputeVramLimit(vram_capacity);
  budget.upload_staging_limit =
      std::min(MiB(512), std::max(MiB(64), budget.vram_limit / 16));
  const uint64_t gpu_resident =
      budget.vram_limit > budget.upload_staging_limit
          ? budget.vram_limit - budget.upload_staging_limit
          : budget.vram_limit;
  budget.gpu_geometry_limit = Percent(gpu_resident, 65);
  budget.gpu_texture_limit = Percent(gpu_resident, 25);
  budget.proxy_geometry_threshold =
      std::min(MiB(256), std::max(MiB(16), budget.gpu_geometry_limit / 16));
  return budget;
}

// Decode-time texture limits derived from a resource budget. Applications hand
// these to tydra::next::TextureDecoder so a large scene's textures are capped
// while they are decoded, rather than after they are all resident.
struct TextureBudget {
  // Longest-edge cap in texels. 0 = keep source resolution.
  uint32_t max_edge = 0;
  // Best-effort cap on total decoded texture bytes. 0 = unbounded.
  uint64_t budget_bytes = 0;
};

// The edge cap keys off the quality tier rather than the device size: a 4K
// albedo is wasted on a proxy-quality overview no matter how much VRAM is free,
// and the byte budget already handles the device-size axis.
inline TextureBudget DeriveTextureBudget(const ResourceBudget& budget) {
  TextureBudget texture;
  texture.budget_bytes = budget.gpu_texture_limit;
  switch (budget.quality) {
    case LargeSceneQuality::Full:
      texture.max_edge = 0;
      break;
    case LargeSceneQuality::Proxy:
      texture.max_edge = 1024;
      break;
    case LargeSceneQuality::Adaptive:
    default:
      texture.max_edge = 2048;
      break;
  }
  return texture;
}

}  // namespace next
}  // namespace tydra
}  // namespace tinyusdz
