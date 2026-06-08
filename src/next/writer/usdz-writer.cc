#include "usdz-writer.hh"
#include "../writer/usdc-writer.hh"

#include <array>
#include <cstring>
#include <fstream>

namespace tinyusdz {
namespace next {

namespace {

// ============================================================
// CRC32 implementation (no external deps)
// ============================================================

// Thread-safe one-time init via a function-local static (C++11 magic statics),
// so concurrent USDZ writes don't race on a hand-rolled init flag.
static const std::array<uint32_t, 256>& CRC32Table() {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int j = 0; j < 8; j++) {
        if (c & 1)
          c = 0xEDB88320u ^ (c >> 1);
        else
          c = c >> 1;
      }
      t[i] = c;
    }
    return t;
  }();
  return table;
}

static uint32_t ComputeCRC32(const uint8_t* data, size_t len) {
  const std::array<uint32_t, 256>& crc32_table = CRC32Table();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

// ============================================================
// ZIP constants
// ============================================================

constexpr size_t kUSDZAlignment = 64;
constexpr size_t kZipLocalHeaderSize = 30;

// ============================================================
// ZIP write helpers
// ============================================================

struct CentralDirEntry {
  std::string name;
  uint32_t crc;
  uint32_t size;
  size_t local_header_offset;
};

static bool WriteLocalFileHeader(std::vector<uint8_t>& buf,
                                  const std::string& name,
                                  const uint8_t* data, size_t data_size,
                                  CentralDirEntry* out_entry,
                                  std::string* err) {
  size_t header_size = kZipLocalHeaderSize + name.size();
  size_t padding = 0;
  size_t remainder = (buf.size() + header_size) % kUSDZAlignment;
  if (remainder != 0) {
    padding = kUSDZAlignment - remainder;
  }

  size_t local_header_offset = buf.size();

  uint32_t crc = 0;
  if (data && data_size > 0) {
    crc = ComputeCRC32(data, data_size);
  }

  // Local file header
  uint8_t header[kZipLocalHeaderSize];
  std::memset(header, 0, sizeof(header));

  // Signature: PK\003\004
  header[0] = 0x50; header[1] = 0x4b; header[2] = 0x03; header[3] = 0x04;
  // Version needed: 2.0
  header[4] = 20; header[5] = 0;
  // Compression: 0 (stored/uncompressed)
  // CRC-32
  std::memcpy(&header[14], &crc, 4);
  // Compressed size == uncompressed size (stored)
  uint32_t sz32 = static_cast<uint32_t>(data_size);
  std::memcpy(&header[18], &sz32, 4);
  std::memcpy(&header[22], &sz32, 4);
  // Filename length
  uint16_t name_len = static_cast<uint16_t>(name.size());
  std::memcpy(&header[26], &name_len, 2);
  // Extra field length (for alignment padding)
  uint16_t extra_len = static_cast<uint16_t>(padding);
  std::memcpy(&header[28], &extra_len, 2);

  buf.insert(buf.end(), header, header + kZipLocalHeaderSize);
  buf.insert(buf.end(), name.begin(), name.end());
  if (padding > 0) {
    buf.insert(buf.end(), padding, 0);
  }

  // Verify alignment
  if ((buf.size() % kUSDZAlignment) != 0) {
    if (err) *err += "Internal error: USDZ alignment failed\n";
    return false;
  }

  // Write file data
  if (data && data_size > 0) {
    buf.insert(buf.end(), data, data + data_size);
  }

  if (out_entry) {
    out_entry->name = name;
    out_entry->crc = crc;
    out_entry->size = sz32;
    out_entry->local_header_offset = local_header_offset;
  }

  return true;
}

static void WriteCentralDirectory(
    std::vector<uint8_t>& buf,
    const std::vector<CentralDirEntry>& entries) {
  size_t cd_offset = buf.size();

  for (const auto& entry : entries) {
    uint32_t local_offset = static_cast<uint32_t>(entry.local_header_offset);

    uint8_t cdr[46];
    std::memset(cdr, 0, sizeof(cdr));

    // Signature: PK\001\002
    cdr[0] = 0x50; cdr[1] = 0x4b; cdr[2] = 0x01; cdr[3] = 0x02;
    // Version made by: 2.0
    cdr[4] = 20; cdr[5] = 0;
    // Version needed: 2.0
    cdr[6] = 20; cdr[7] = 0;
    // CRC-32
    std::memcpy(&cdr[16], &entry.crc, 4);
    // Compressed size
    std::memcpy(&cdr[20], &entry.size, 4);
    // Uncompressed size
    std::memcpy(&cdr[24], &entry.size, 4);
    // Filename length
    uint16_t name_len = static_cast<uint16_t>(entry.name.size());
    std::memcpy(&cdr[28], &name_len, 2);
    // Relative offset of local header
    std::memcpy(&cdr[42], &local_offset, 4);

    buf.insert(buf.end(), cdr, cdr + 46);
    buf.insert(buf.end(), entry.name.begin(), entry.name.end());
  }

  size_t cd_size = buf.size() - cd_offset;

  // End of central directory record
  uint8_t eocd[22];
  std::memset(eocd, 0, sizeof(eocd));
  // Signature: PK\005\006
  eocd[0] = 0x50; eocd[1] = 0x4b; eocd[2] = 0x05; eocd[3] = 0x06;
  // Number of entries on this disk
  uint16_t num_entries = static_cast<uint16_t>(entries.size());
  std::memcpy(&eocd[8], &num_entries, 2);
  // Total number of entries
  std::memcpy(&eocd[10], &num_entries, 2);
  // Size of central directory
  uint32_t cd_size32 = static_cast<uint32_t>(cd_size);
  std::memcpy(&eocd[12], &cd_size32, 4);
  // Offset of central directory
  uint32_t cd_offset32 = static_cast<uint32_t>(cd_offset);
  std::memcpy(&eocd[16], &cd_offset32, 4);

  buf.insert(buf.end(), eocd, eocd + 22);
}

} // namespace

// ============================================================
// Public API
// ============================================================

USDZWriteResult WriteUSDZToMemory(std::vector<uint8_t>& buffer, const Stage& stage) {
  USDZWriteResult result;

  // First serialize stage to USDC
  std::vector<uint8_t> usdc_buffer;
  auto usdc_result = WriteUSDCToMemory(usdc_buffer, stage);
  if (!usdc_result.success) {
    result.error = usdc_result.error;
    return result;
  }

  // Then wrap in ZIP
  return WriteUSDZFromUSDCToMemory(buffer, usdc_buffer.data(), usdc_buffer.size());
}

USDZWriteResult WriteUSDZToFile(const std::string& filename, const Stage& stage) {
  std::vector<uint8_t> buf;
  auto result = WriteUSDZToMemory(buf, stage);
  if (!result.success) return result;

  std::ofstream ofs(filename, std::ios::binary);
  if (!ofs) {
    result.success = false;
    result.error = "Failed to open file for writing: " + filename;
    return result;
  }
  ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
  result.bytes_written = buf.size();
  result.success = true;
  return result;
}

USDZWriteResult WriteUSDZFromUSDCToMemory(std::vector<uint8_t>& buffer,
                                           const uint8_t* usdc_data,
                                           size_t usdc_size) {
  USDZWriteResult result;

  if (!usdc_data || usdc_size == 0) {
    result.error = "Empty USDC data";
    return result;
  }

  buffer.clear();

  // Write root layer
  std::string root_name = "root.usdc";
  CentralDirEntry entry;
  std::string err;
  if (!WriteLocalFileHeader(buffer, root_name, usdc_data, usdc_size, &entry, &err)) {
    result.error = err.empty() ? "Failed to write ZIP entry" : err;
    return result;
  }

  // Write central directory
  std::vector<CentralDirEntry> entries = {entry};
  WriteCentralDirectory(buffer, entries);

  result.bytes_written = buffer.size();
  result.success = true;
  return result;
}

USDZWriteResult WriteUSDZFromUSDCToFile(const std::string& filename,
                                         const uint8_t* usdc_data,
                                         size_t usdc_size) {
  std::vector<uint8_t> buf;
  auto result = WriteUSDZFromUSDCToMemory(buf, usdc_data, usdc_size);
  if (!result.success) return result;

  std::ofstream ofs(filename, std::ios::binary);
  if (!ofs) {
    result.success = false;
    result.error = "Failed to open file for writing: " + filename;
    return result;
  }
  ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
  result.bytes_written = buf.size();
  result.success = true;
  return result;
}

} // namespace next
} // namespace tinyusdz
