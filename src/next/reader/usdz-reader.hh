#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace tinyusdz {
namespace next {

/// Minimal USDZ/ZIP reader for the next library.
/// Handles store-only ZIP archives with 64-byte alignment.
class USDZReader {
public:
  USDZReader() = default;

  /// Open and parse a USDZ file.
  /// Returns false if the file is not a valid USDZ archive.
  bool Open(const uint8_t* data, size_t size);
  bool OpenFile(const std::string& filename);

  /// Get the number of entries in the archive.
  size_t NumEntries() const { return entries_.size(); }

  /// Get entry name at index.
  const std::string& EntryName(size_t index) const;

  /// Get entry data at index (returns pointer to data, valid until Open is called again).
  const uint8_t* EntryData(size_t index) const;
  size_t EntrySize(size_t index) const;

  /// Find a .usdc or .usda entry (the first one found).
  /// Returns -1 if not found.
  int FindUSDCFile() const;
  int FindUSDAFile() const;

private:
  struct Entry {
    std::string name;
    size_t offset;  // offset of file data in the buffer
    size_t size;    // uncompressed size
  };
  std::vector<Entry> entries_;
  std::vector<uint8_t> file_data_;  // if reading from file
  const uint8_t* data_ = nullptr;
  size_t data_size_ = 0;
};

} // namespace next
} // namespace tinyusdz
