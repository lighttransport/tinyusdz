#include "usdz-reader.hh"
#include <cstring>
#include <algorithm>
#include <limits>

#if defined(TINYUSDZ_NEXT_ENABLE_FILEIO)
#include "../io/file-util.hh"
#endif

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

namespace {

bool CheckedAdd(size_t a, size_t b, size_t* out) {
  if (a > (std::numeric_limits<size_t>::max)() - b) return false;
  *out = a + b;
  return true;
}

}  // namespace

bool USDZReader::OpenFile(const std::string& filename,
                          const USDZReadOptions& options) {
  error_.clear();
#if defined(TINYUSDZ_NEXT_ENABLE_FILEIO)
  // Read the whole archive into memory via next_io, then parse from the buffer.
  std::string err;
  if (!io::ReadWholeFile(
          &file_data_, &err, filename,
          options.max_archive_size ? options.max_archive_size : 0)) {
    error_ = err.empty() ? ("failed to open USDZ file: " + filename) : err;
    file_data_.clear();
    return false;
  }
  return Open(file_data_.data(), file_data_.size(), options);
#else
  (void)filename;
  (void)options;
  error_ =
      "File I/O is disabled in this build (memory-only next-core). "
      "Use USDZReader::Open(data, size) with an in-memory buffer.";
  return false;
#endif
}

bool USDZReader::Open(const uint8_t* data, size_t size,
                      const USDZReadOptions& options) {
  error_.clear();
  data_ = data;
  data_size_ = size;
  entries_.clear();

  if (!data || size < 4) {
    error_ = "empty or truncated USDZ archive";
    return false;
  }
  if (options.max_archive_size > 0 && size > options.max_archive_size) {
    error_ = "USDZ archive exceeds maximum memory limit";
    return false;
  }

  size_t pos = 0;
  while (pos + sizeof(LocalFileHeader) <= size) {
    LocalFileHeader hdr;
    std::memcpy(&hdr, data + pos, sizeof(hdr));

    if (hdr.signature != kLocalFileHeaderSig) break;

    size_t name_pos = 0;
    size_t extra_pos = 0;
    size_t data_pos = 0;
    size_t next_pos = 0;
    if (!CheckedAdd(pos, sizeof(hdr), &name_pos) ||
        !CheckedAdd(name_pos, static_cast<size_t>(hdr.filename_length), &extra_pos) ||
        !CheckedAdd(extra_pos, static_cast<size_t>(hdr.extra_field_length), &data_pos) ||
        !CheckedAdd(data_pos, static_cast<size_t>(hdr.compressed_size), &next_pos) ||
        next_pos > size) {
      error_ = "truncated USDZ local file header";
      break;
    }

    // Check for 0xA serial number in extra field (64-byte alignment marker)
    // USDZ requires stored (uncompressed) entries
    if (hdr.compression != 0) {
      // Skip compressed entries (only stored entries supported)
      pos = next_pos;
      continue;
    }
    if (hdr.uncompressed_size != hdr.compressed_size) {
      error_ = "invalid stored USDZ entry size";
      return false;
    }
    if (options.max_entry_size > 0 &&
        static_cast<size_t>(hdr.uncompressed_size) > options.max_entry_size) {
      error_ = "USDZ entry exceeds maximum memory limit";
      return false;
    }

    std::string name(reinterpret_cast<const char*>(data + name_pos),
                     hdr.filename_length);

    Entry entry;
    entry.name = name;
    entry.offset = data_pos;
    entry.size = static_cast<size_t>(hdr.uncompressed_size);
    entries_.push_back(entry);

    pos = next_pos;
  }

  if (entries_.empty()) {
    if (error_.empty()) error_ = "no stored entries found in USDZ archive";
    return false;
  }
  error_.clear();
  return true;
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
