#include "usdz-reader.hh"
#include <cstring>
#include <algorithm>
#include <fstream>

namespace tinyusdz {
namespace next {

// ZIP local file header signature
static const uint32_t kLocalFileHeaderSig = 0x04034b50;
static const uint32_t kCentralDirHeaderSig = 0x02014b50;
static const uint32_t kEOCDSig = 0x06054b50;

#pragma pack(push, 1)
struct LocalFileHeader {
  uint32_t signature;
  uint16_t version_needed;
  uint16_t flags;
  uint16_t compression;
  uint16_t mod_time;
  uint16_t mod_date;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t filename_length;
  uint16_t extra_field_length;
};
#pragma pack(pop)

bool USDZReader::OpenFile(const std::string& filename) {
  std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
  if (!ifs) return false;
  size_t sz = static_cast<size_t>(ifs.tellg());
  ifs.seekg(0);
  file_data_.resize(sz);
  ifs.read(reinterpret_cast<char*>(file_data_.data()), sz);
  ifs.close();
  return Open(file_data_.data(), sz);
}

bool USDZReader::Open(const uint8_t* data, size_t size) {
  data_ = data;
  data_size_ = size;
  entries_.clear();

  if (!data || size < 4) return false;

  size_t pos = 0;
  while (pos + sizeof(LocalFileHeader) <= size) {
    LocalFileHeader hdr;
    std::memcpy(&hdr, data + pos, sizeof(hdr));

    if (hdr.signature != kLocalFileHeaderSig) break;

    if (pos + sizeof(hdr) + hdr.filename_length + hdr.extra_field_length + hdr.compressed_size > size)
      break;

    // Check for 0xA serial number in extra field (64-byte alignment marker)
    // USDZ requires stored (uncompressed) entries
    if (hdr.compression != 0) {
      // Skip compressed entries (only stored entries supported)
      pos += sizeof(hdr) + hdr.filename_length + hdr.extra_field_length + hdr.compressed_size;
      continue;
    }

    std::string name(reinterpret_cast<const char*>(data + pos + sizeof(hdr)), hdr.filename_length);

    Entry entry;
    entry.name = name;
    entry.offset = pos + sizeof(hdr) + hdr.filename_length + hdr.extra_field_length;
    entry.size = static_cast<size_t>(hdr.uncompressed_size);
    entries_.push_back(entry);

    pos += sizeof(hdr) + hdr.filename_length + hdr.extra_field_length + hdr.compressed_size;
  }

  return !entries_.empty();
}

const std::string& USDZReader::EntryName(size_t index) const {
  return entries_[index].name;
}

const uint8_t* USDZReader::EntryData(size_t index) const {
  if (index >= entries_.size()) return nullptr;
  return data_ + entries_[index].offset;
}

size_t USDZReader::EntrySize(size_t index) const {
  if (index >= entries_.size()) return 0;
  return entries_[index].size;
}

int USDZReader::FindUSDCFile() const {
  for (size_t i = 0; i < entries_.size(); i++) {
    const auto& name = entries_[i].name;
    // USDZ entries often have a leading "/" or not
    size_t pos = name.rfind(".usdc");
    if (pos != std::string::npos && pos == name.size() - 5) return static_cast<int>(i);
  }
  return -1;
}

int USDZReader::FindUSDAFile() const {
  for (size_t i = 0; i < entries_.size(); i++) {
    const auto& name = entries_[i].name;
    size_t pos = name.rfind(".usda");
    if (pos != std::string::npos && pos == name.size() - 5) return static_cast<int>(i);
  }
  return -1;
}

} // namespace next
} // namespace tinyusdz
