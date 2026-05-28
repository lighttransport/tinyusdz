#pragma once

#include "../stage/stage.hh"
#include <string>
#include <vector>
#include <cstdint>

namespace tinyusdz {
namespace next {

/// Result of a USDZ write operation.
struct USDZWriteResult {
  bool success = false;
  std::string error;
  size_t bytes_written = 0;
};

/// Write a Stage to a USDZ file.
/// The stage is first converted to USDC, then packed into a ZIP archive.
USDZWriteResult WriteUSDZToFile(const std::string& filename, const Stage& stage);

/// Write a Stage to a USDZ buffer.
/// The stage is first converted to USDC, then packed into a ZIP archive.
USDZWriteResult WriteUSDZToMemory(std::vector<uint8_t>& buffer, const Stage& stage);

/// Write USDC data directly as a USDZ archive (skips Stage serialization).
USDZWriteResult WriteUSDZFromUSDCToFile(const std::string& filename,
                                         const uint8_t* usdc_data, size_t usdc_size);
USDZWriteResult WriteUSDZFromUSDCToMemory(std::vector<uint8_t>& buffer,
                                           const uint8_t* usdc_data, size_t usdc_size);

} // namespace next
} // namespace tinyusdz
