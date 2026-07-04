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

/// One pre-built archive entry (name + bytes) for WriteUSDZFromEntriesToMemory.
struct USDZEntry {
  std::string name;
  const uint8_t* data = nullptr;
  size_t size = 0;
};

/// Pack pre-built entries into a USDZ archive (64-byte aligned, stored). The
/// FIRST entry MUST be the default root layer. Used to repackage a converted
/// USDZ so its root is (re)serialized by the next crate writer (which applies
/// cross-spec value-block dedup) while texture entries pass through verbatim.
USDZWriteResult WriteUSDZFromEntriesToMemory(std::vector<uint8_t>& buffer,
                                             const std::vector<USDZEntry>& entries);

} // namespace next
} // namespace tinyusdz
