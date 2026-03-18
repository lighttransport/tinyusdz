// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer Implementation
#include "../include/crate-writer.hh"

#include <algorithm>
#include <cstring>
#include <sstream>

// XXH3 hash (header-only mode, namespaced to avoid collision with zstd's copy)
#define XXH_INLINE_ALL
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include "../../../src/external/xxhash.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Phase 4: Compression support
#include "../../../src/lz4-compression.hh"
#include "../../../src/integerCoding.h"
// Direct LZ4 API for OpenUSD compatibility
#include "../../../src/lz4/lz4.h"

// Namespace alias to avoid collision between tinyusdz::crate and ::crate (path library)
namespace pathlib = ::crate;

namespace tinyusdz {
namespace experimental {

namespace {

// Magic identifier for USDC files
constexpr char kMagicIdent[] = "PXR-USDC";

// Section names
constexpr char kTokensSection[] = "TOKENS";
constexpr char kStringsSection[] = "STRINGS";
constexpr char kFieldsSection[] = "FIELDS";
constexpr char kFieldSetsSection[] = "FIELDSETS";
constexpr char kPathsSection[] = "PATHS";
constexpr char kSpecsSection[] = "SPECS";

} // anonymous namespace

// ============================================================================
// Constructor / Destructor
// ============================================================================

CrateWriter::CrateWriter(const std::string& filepath)
    : filepath_(filepath) {
}

CrateWriter::~CrateWriter() {
  Close();
}

// ============================================================================
// NanAwareHash implementation
// ============================================================================

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wused-but-marked-unused"
#endif
size_t CrateWriter::NanAwareHash::hash_buffer(const void *data,
                                               size_t byte_count,
                                               size_t element_size,
                                               bool is_float) {
  if (!is_float) {
    // Non-float: hash raw bytes directly with XXH3
    return static_cast<size_t>(XXH_INLINE_XXH3_64bits(data, byte_count));
  }

  // Float/double: canonicalize +0/-0 into a temp buffer, then XXH3
  // (We copy to avoid mutating the caller's data.)
  std::vector<uint8_t> canon(byte_count);
  std::memcpy(canon.data(), data, byte_count);

  if (element_size == sizeof(float)) {
    size_t count = byte_count / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
      float v;
      std::memcpy(&v, canon.data() + i * sizeof(float), sizeof(float));
      if (v == 0.0f) {
        uint32_t zero = 0;
        std::memcpy(canon.data() + i * sizeof(float), &zero, sizeof(float));
      }
    }
  } else { // sizeof(double)
    size_t count = byte_count / sizeof(double);
    for (size_t i = 0; i < count; ++i) {
      double v;
      std::memcpy(&v, canon.data() + i * sizeof(double), sizeof(double));
      if (v == 0.0) {
        uint64_t zero = 0;
        std::memcpy(canon.data() + i * sizeof(double), &zero, sizeof(double));
      }
    }
  }

  return static_cast<size_t>(XXH_INLINE_XXH3_64bits(canon.data(), byte_count));
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

bool CrateWriter::NanAwareHash::buffers_equal(const void *a, const void *b,
                                               size_t byte_count,
                                               size_t element_size,
                                               bool is_float) {
  const auto *pa = static_cast<const uint8_t *>(a);
  const auto *pb = static_cast<const uint8_t *>(b);

  if (is_float && element_size == sizeof(float)) {
    size_t count = byte_count / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
      float va, vb;
      std::memcpy(&va, pa + i * sizeof(float), sizeof(float));
      std::memcpy(&vb, pb + i * sizeof(float), sizeof(float));
      // Canonicalize: +0/-0 both become bits=0, all else compare by bits
      uint32_t ba = 0, bb = 0;
      if (va != 0.0f) { std::memcpy(&ba, &va, sizeof(float)); }
      if (vb != 0.0f) { std::memcpy(&bb, &vb, sizeof(float)); }
      if (ba != bb) return false;
    }
    return true;
  } else if (is_float && element_size == sizeof(double)) {
    size_t count = byte_count / sizeof(double);
    for (size_t i = 0; i < count; ++i) {
      double va, vb;
      std::memcpy(&va, pa + i * sizeof(double), sizeof(double));
      std::memcpy(&vb, pb + i * sizeof(double), sizeof(double));
      uint64_t ba = 0, bb = 0;
      if (va != 0.0) { std::memcpy(&ba, &va, sizeof(double)); }
      if (vb != 0.0) { std::memcpy(&bb, &vb, sizeof(double)); }
      if (ba != bb) return false;
    }
    return true;
  }

  return std::memcmp(a, b, byte_count) == 0;
}

// ============================================================================
// Public API
// ============================================================================

bool CrateWriter::Open(std::string* err) {
  if (is_open_) {
    if (err) *err = "File already open";
    return false;
  }

  // Open file for binary write
  file_.open(filepath_, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!file_.is_open()) {
    if (err) *err = "Failed to open file: " + filepath_;
    return false;
  }

  is_open_ = true;

  // Reserve space for bootstrap header (we'll write it at the end)
  // Bootstrap is 72 bytes: 8 (ident) + 8 (version) + 8 (toc_offset) + 48 (reserved)
  char zeros[72] = {0};
  if (!WriteBytes(zeros, sizeof(zeros))) {
    if (err) *err = "Failed to write bootstrap placeholder";
    Close();
    return false;
  }

  value_data_start_offset_ = Tell();
  value_data_end_offset_ = value_data_start_offset_;

  return true;
}

bool CrateWriter::AddSpec(const Path& path,
                           SpecType spec_type,
                           const crate::FieldValuePairVector& fields,
                           std::string* err) {
  if (!is_open_) {
    if (err) *err = "File not open";
    return false;
  }

  if (is_finalized_) {
    if (err) *err = "File already finalized";
    return false;
  }

  // Create spec data
  SpecData spec_data;
  spec_data.path = path;
  spec_data.spec_type = spec_type;  // Store the spec type
  spec_data.fields = fields;

  // We'll fill in the actual crate::Spec later during Finalize
  // For now, just accumulate the data
  spec_data_.push_back(spec_data);

  // Pre-register the path for deduplication
  GetOrCreatePath(path);

  // Pre-register tokens from field names
  for (const auto& field : fields) {
    GetOrCreateToken(field.first);
  }

  return true;
}

bool CrateWriter::Finalize(std::string* err) {
  if (!is_open_) {
    if (err) *err = "File not open";
    return false;
  }

  if (is_finalized_) {
    if (err) *err = "File already finalized";
    return false;
  }

  // ========================================================================
  // Step 1: Process all specs and build internal tables
  // ========================================================================

  // Phase 5: Sort specs for better compression
  // Sorting strategy:
  // 1. Prims before properties
  // 2. Within prims: alphabetically by path
  // 3. Within properties: group by parent prim, then alphabetically by property name
  std::sort(spec_data_.begin(), spec_data_.end(),
    [](const SpecData& a, const SpecData& b) {
      bool a_is_prim = a.path.is_prim_path();
      bool b_is_prim = b.path.is_prim_path();

      // Prims before properties
      if (a_is_prim != b_is_prim) {
        return a_is_prim;  // true (prim) sorts before false (property)
      }

      // Both are prims or both are properties
      if (a_is_prim) {
        // Both prims - sort alphabetically by full path
        return a.path.prim_part() < b.path.prim_part();
      } else {
        // Both properties - first by parent prim, then by property name
        if (a.path.prim_part() != b.path.prim_part()) {
          return a.path.prim_part() < b.path.prim_part();
        }
        return a.path.prop_part() < b.path.prop_part();
      }
    });

  // Build field and fieldset tables
  for (auto& spec_data : spec_data_) {
    std::vector<crate::FieldIndex> field_indices;

    for (const auto& field_pair : spec_data.fields) {
      // Create field
      crate::Field field;
      field.token_index = GetOrCreateToken(field_pair.first);

      // Pack value
      field.value_rep = PackValue(field_pair.second, err);
      if (err && !err->empty()) {
        return false;
      }

      // Get or create field index
      crate::FieldIndex field_idx = GetOrCreateField(field);
      field_indices.push_back(field_idx);
    }

    // Get or create fieldset
    crate::FieldSetIndex fieldset_idx = GetOrCreateFieldSet(field_indices);

    // Now we can fill in the actual crate::Spec
    crate::PathIndex path_idx = GetOrCreatePath(spec_data.path);
    spec_data.spec.path_index = path_idx;
    spec_data.spec.fieldset_index = fieldset_idx;
    spec_data.spec.spec_type = spec_data.spec_type;  // Use the stored spec type
  }

  // ========================================================================
  // Step 2: Write all structural sections
  // ========================================================================

  // Note: value_data_end_offset_ is already updated by WriteValueData()
  // during PackValue() calls above. Do NOT reset it here.

  // ========================================================================
  // Prepare path tree and extract tokens (must happen before TOKENS section)
  // ========================================================================

  // Build the path tree now so we can extract tokens
  std::vector<pathlib::SimplePath> simple_paths_prep;

  // Check if root exists
  bool has_root_prep = false;
  for (const auto& path : paths_) {
    if (path.prim_part() == "/" && path.prop_part().empty()) {
      has_root_prep = true;
      break;
    }
  }

  if (!has_root_prep) {
    simple_paths_prep.emplace_back("/", "");
  }

  for (const auto& path : paths_) {
    simple_paths_prep.emplace_back(path.prim_part(), path.prop_part());
  }

  // Sort and encode
  pathlib::SortSimplePaths(simple_paths_prep);
  pathlib::CompressedPathTree tree_prep = pathlib::EncodePaths(simple_paths_prep);

  // Extract tokens from path tree and merge with existing tokens
  // The path tree has its own token table, but we need to merge it with
  // field name tokens that were already registered in AddSpec()
  const auto& reverse_tokens_prep = tree_prep.token_table.GetReverseTokens();

  // Build a map of path tree tokens (original index -> token string)
  std::map<int32_t, std::string> path_tree_tokens;
  for (const auto& pair : reverse_tokens_prep) {
    if (pair.first >= 0) {
      path_tree_tokens[pair.first] = pair.second;
    }
  }

  // For each path tree token, check if it already exists in our token table
  // If it exists, we can reuse it. If not, append it.
  std::map<int32_t, uint32_t> path_tree_to_our_index;  // Maps path tree index -> our token index

  for (const auto& pair : path_tree_tokens) {
    int32_t path_tree_idx = pair.first;
    const std::string& token_str = pair.second;

    // Check if this token already exists in our token table
    auto it = token_to_index_.find(token_str);
    if (it != token_to_index_.end()) {
      // Token already exists, reuse it
      path_tree_to_our_index[path_tree_idx] = it->second.value;
    } else {
      // New token, append it
      uint32_t new_idx = static_cast<uint32_t>(tokens_.size());
      tokens_.push_back(token_str);
      token_to_index_[token_str] = crate::TokenIndex(new_idx);
      path_tree_to_our_index[path_tree_idx] = new_idx;
    }
  }

  // Store the mapping for later use when writing the path tree
  path_tree_token_remap_ = path_tree_to_our_index;

  // Seek to the end of value data section before writing structural sections
  // (WriteValueData() seeks back after writing, so file position is not at the end)
  if (!Seek(value_data_end_offset_)) {
    if (err) *err = "Failed to seek to end of value data section";
    return false;
  }

  // Write sections in order
  if (!WriteTokensSection(err)) return false;
  if (!WriteStringsSection(err)) return false;
  if (!WriteFieldsSection(err)) return false;
  if (!WriteFieldSetsSection(err)) return false;
  if (!WritePathsSection(err)) return false;
  if (!WriteSpecsSection(err)) return false;

  // ========================================================================
  // Step 3: Write Table of Contents
  // ========================================================================

  if (!WriteTableOfContents(err)) return false;

  // ========================================================================
  // Step 4: Write Bootstrap header
  // ========================================================================

  if (!WriteBootStrap(err)) return false;

  is_finalized_ = true;

  return true;
}

void CrateWriter::Close() {
  if (file_.is_open()) {
    file_.flush();  // Ensure all writes are flushed before closing
    file_.close();
  }
  is_open_ = false;
}

// ============================================================================
// TimeSamples Value Conversion (Phase 5)
// ============================================================================

/// Helper to convert value::Value to CrateValue for TimeSamples serialization
/// Returns true if conversion succeeded
bool ConvertValueToCrateValue(const value::Value& val, crate::CrateValue* out, std::string* err) {
  if (!out) {
    if (err) *err = "ConvertValueToCrateValue: output is null";
    return false;
  }

  uint32_t type_id = val.type_id();

  // Phase 5.1: Scalar numeric types
  if (auto* v = val.as<bool>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<int32_t>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<uint32_t>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<int64_t>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<uint64_t>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::half>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<float>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<double>()) {
    out->Set(*v);
    return true;
  }
  // Phase 5.2: Vector types
  else if (auto* v = val.as<value::float2>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::float3>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::float4>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::double2>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::double3>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::double4>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::int2>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::int3>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::int4>()) {
    out->Set(*v);
    return true;
  }
  // Phase 5.3: Array types - numeric scalars
  else if (auto* v = val.as<std::vector<bool>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<int32_t>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<uint32_t>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<int64_t>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<uint64_t>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::half>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<float>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<double>>()) {
    out->Set(*v);
    return true;
  }
  // Phase 5.4: Vector arrays
  else if (auto* v = val.as<std::vector<value::float2>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::float3>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::float4>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::double2>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::double3>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::double4>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::int2>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::int3>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::int4>>()) {
    out->Set(*v);
    return true;
  }
  // Phase 5.5: Token/String/AssetPath types
  else if (auto* v = val.as<value::token>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::string>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<value::AssetPath>()) {
    out->Set(*v);
    return true;
  }
  // Phase 5.6: Token/String/AssetPath arrays
  else if (auto* v = val.as<std::vector<value::token>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<std::string>>()) {
    out->Set(*v);
    return true;
  }
  else if (auto* v = val.as<std::vector<value::AssetPath>>()) {
    out->Set(*v);
    return true;
  }

  // Unsupported type
  if (err) {
    *err = "ConvertValueToCrateValue: Unsupported type_id " + std::to_string(type_id);
  }
  return false;
}

// ============================================================================
// Array Deduplication (Phase 5)
// ============================================================================

/// Helper to serialize array to bytes for deduplication
template<typename T>
std::vector<char> SerializeArrayToBytes(const std::vector<T>& arr) {
  std::vector<char> bytes;
  size_t total_size = sizeof(T) * arr.size();
  bytes.resize(total_size);
  std::memcpy(bytes.data(), arr.data(), total_size);
  return bytes;
}

// ============================================================================
// Compression (Phase 4)
// ============================================================================

bool CrateWriter::CompressData(const char* input, size_t inputSize,
                                std::vector<char>* compressed, std::string* err) {
  if (!compressed) {
    if (err) *err = "CompressData: compressed output buffer is null";
    return false;
  }

  // IMPORTANT: USD crate format uses OpenUSD's TfFastCompression format:
  // - 1 byte: chunk count (0 for single chunk, N for multiple chunks)
  // - If chunk count == 0: raw LZ4 compressed data
  // - If chunk count > 0: for each chunk: int32_t size + LZ4 compressed data
  //
  // For simplicity, we always use single-chunk mode (chunk count = 0)
  // See: pxr/base/tf/fastCompression.cpp in OpenUSD

  // Get maximum compressed size
  int maxCompressedSize = LZ4_compressBound(static_cast<int>(inputSize));
  if (maxCompressedSize <= 0) {
    if (err) *err = "Input size too large for LZ4 compression: " + std::to_string(inputSize);
    return false;
  }

  // Allocate buffer: 1 byte for chunk count + compressed data
  compressed->resize(1 + static_cast<size_t>(maxCompressedSize));

  // Write chunk count byte (0 = single chunk)
  (*compressed)[0] = 0;

  // Compress with LZ4 (compatible with OpenUSD TfFastCompression)
  int compressedSize = LZ4_compress_default(
      input,
      compressed->data() + 1,  // Skip the chunk count byte
      static_cast<int>(inputSize),
      maxCompressedSize);

  std::cerr << "DEBUG CompressData: inputSize=" << inputSize
            << ", compressedSize=" << compressedSize
            << ", maxCompressedSize=" << maxCompressedSize << std::endl;

  if (compressedSize <= 0) {
    if (err) *err = "LZ4 compression failed with error code: " + std::to_string(compressedSize);
    return false;
  }

  // Resize to actual size: 1 byte chunk count + compressed data
  compressed->resize(1 + static_cast<size_t>(compressedSize));

  // DEBUG: Print first few bytes of compressed data
  std::cerr << "DEBUG CompressData: First 16 bytes after chunk byte: ";
  for (int i = 1; i < std::min(17, (int)compressed->size()); ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", (unsigned char)(*compressed)[i]);
    std::cerr << buf;
  }
  std::cerr << std::endl;

  return true;
}

// ============================================================================
// Section Writing
// ============================================================================

bool CrateWriter::WriteTokensSection(std::string* err) {
  int64_t section_start = Tell();

  // Debug: print token count
  std::cerr << "DEBUG: WriteTokensSection called with " << tokens_.size() << " tokens" << std::endl;
  for (size_t i = 0; i < std::min(tokens_.size(), size_t(10)); ++i) {
    std::cerr << "  Token[" << i << "]: " << tokens_[i] << std::endl;
  }

  // Write token count
  uint64_t token_count = static_cast<uint64_t>(tokens_.size());
  int64_t token_count_offset = Tell();
  std::cerr << "DEBUG: Writing token_count = " << token_count << " at offset " << token_count_offset << std::endl;

  // DEBUG: Print bytes being written
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&token_count);
  std::cerr << "DEBUG: Bytes to write: ";
  for (size_t i = 0; i < sizeof(token_count); ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", bytes[i]);
    std::cerr << buf;
  }
  std::cerr << std::endl;

  // Write directly as bytes instead of using Write() template
  file_.write(reinterpret_cast<const char*>(&token_count), sizeof(token_count));
  file_.flush();

  if (!file_.good()) {
    if (err) *err = "Failed to write token count bytes";
    return false;
  }
  std::cerr << "DEBUG: After write, offset = " << Tell() << ", file.good() = " << file_.good() << std::endl;

  // Build token blob (null-terminated strings)
  std::ostringstream blob;
  for (const auto& token : tokens_) {
    blob << token;
    blob.put('\0');
  }

  std::string token_blob = blob.str();

  std::cerr << "DEBUG: Token blob size = " << token_blob.size() << " bytes" << std::endl;
  std::cerr << "DEBUG: First 60 bytes: ";
  for (size_t i = 0; i < std::min(token_blob.size(), size_t(60)); ++i) {
    if (token_blob[i] == '\0') {
      std::cerr << "\\0";
    } else if (isprint(token_blob[i])) {
      std::cerr << token_blob[i];
    } else {
      char buf[5];
      snprintf(buf, sizeof(buf), "\\x%02x", (unsigned char)token_blob[i]);
      std::cerr << buf;
    }
  }
  std::cerr << std::endl;

  // Phase 4: Compress the blob if compression is enabled
  std::vector<char> compressed_blob;
  if (!CompressData(token_blob.data(), token_blob.size(), &compressed_blob, err)) {
    if (err) *err = "Failed to compress token blob: " + *err;
    return false;
  }

  std::cerr << "DEBUG: Compressed blob size (with chunk byte) = " << compressed_blob.size() << " bytes" << std::endl;

  // Write in compressed format (version 0.4.0+):
  // - uncompressedSize (uint64_t)
  // - compressedSize (uint64_t)
  // - compressed data
  uint64_t uncompressed_size = static_cast<uint64_t>(token_blob.size());
  uint64_t compressed_size = static_cast<uint64_t>(compressed_blob.size());

  if (!Write(uncompressed_size)) {
    if (err) *err = "Failed to write token blob uncompressed size";
    return false;
  }

  if (!Write(compressed_size)) {
    if (err) *err = "Failed to write token blob compressed size";
    return false;
  }

  if (!WriteBytes(compressed_blob.data(), compressed_blob.size())) {
    if (err) *err = "Failed to write compressed token blob";
    return false;
  }

  int64_t section_end = Tell();

  // Record section in TOC
  crate::Section section(kTokensSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WriteStringsSection(std::string* err) {
  int64_t section_start = Tell();

  // Strings section is just a vector of TokenIndex
  // Each string maps to a token index

  uint64_t string_count = static_cast<uint64_t>(strings_.size());
  if (!Write(string_count)) {
    if (err) *err = "Failed to write string count";
    return false;
  }

  for (const auto& str : strings_) {
    // Find the token index for this string
    auto it = token_to_index_.find(str);
    if (it == token_to_index_.end()) {
      if (err) *err = "String not found in token table: " + str;
      return false;
    }

    if (!Write(it->second)) {
      if (err) *err = "Failed to write string token index";
      return false;
    }
  }

  int64_t section_end = Tell();

  crate::Section section(kStringsSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WriteFieldsSection(std::string* err) {
  int64_t section_start = Tell();

  // IMPORTANT: Format for v0.4.0+ (we target v0.7.0+):
  // 1. uint64_t numFields
  // 2. uint64_t tokenIndicesCompressedSize
  // 3. Compressed token indices (using Usd_IntegerCompression)
  // 4. uint64_t repsSize
  // 5. Compressed value reps (using TfFastCompression)
  //
  // This differs from older versions which mixed token indices and value reps.
  // See: pxr/usd/sdf/crateFile.cpp _ReadFields() and _WriteFields()

  size_t num_fields = fields_.size();

  // Write field count
  uint64_t field_count = static_cast<uint64_t>(num_fields);
  if (!Write(field_count)) {
    if (err) *err = "Failed to write field count";
    return false;
  }

  // Separate token indices from value reps
  std::vector<uint32_t> token_indices;
  std::vector<uint64_t> value_reps;
  token_indices.reserve(num_fields);
  value_reps.reserve(num_fields);

  for (const auto& field : fields_) {
    token_indices.push_back(field.token_index.value);
    value_reps.push_back(field.value_rep.GetData());
  }

  // Compress token indices using Usd_IntegerCompression
  size_t token_indices_compressed_buffer_size =
      Usd_IntegerCompression::GetCompressedBufferSize(num_fields);
  std::vector<char> compressed_token_indices(token_indices_compressed_buffer_size);

  std::string compress_err;
  size_t token_indices_compressed_size = Usd_IntegerCompression::CompressToBuffer(
      token_indices.data(), num_fields,
      compressed_token_indices.data(), &compress_err);

  if (token_indices_compressed_size == 0) {
    if (err) *err = "Failed to compress token indices: " + compress_err;
    return false;
  }

  compressed_token_indices.resize(token_indices_compressed_size);

  // Write tokenIndicesCompressedSize
  uint64_t token_indices_size = static_cast<uint64_t>(token_indices_compressed_size);
  if (!Write(token_indices_size)) {
    if (err) *err = "Failed to write token indices compressed size";
    return false;
  }

  // Write compressed token indices
  if (!WriteBytes(compressed_token_indices.data(), token_indices_compressed_size)) {
    if (err) *err = "Failed to write compressed token indices";
    return false;
  }

  // Compress value reps using TfFastCompression (our CompressData)
  const char* reps_data = reinterpret_cast<const char*>(value_reps.data());
  size_t reps_data_size = value_reps.size() * sizeof(uint64_t);

  std::vector<char> compressed_reps;
  if (!CompressData(reps_data, reps_data_size, &compressed_reps, err)) {
    if (err) *err = "Failed to compress value reps: " + *err;
    return false;
  }

  // Write repsSize
  uint64_t reps_size = static_cast<uint64_t>(compressed_reps.size());
  if (!Write(reps_size)) {
    if (err) *err = "Failed to write value reps size";
    return false;
  }

  // Write compressed value reps
  if (!WriteBytes(compressed_reps.data(), compressed_reps.size())) {
    if (err) *err = "Failed to write compressed value reps";
    return false;
  }

  int64_t section_end = Tell();

  crate::Section section(kFieldsSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WriteFieldSetsSection(std::string* err) {
  int64_t section_start = Tell();

  // IMPORTANT: Format for v0.4.0+ (we target v0.7.0+):
  // 1. uint64_t numFieldSetVals (total number of FieldIndex values)
  // 2. uint64_t compressedSize (size of compressed data)
  // 3. Compressed field index array (using Usd_IntegerCompression)
  //
  // Fieldsets are stored as a flat array of FieldIndex values.
  // Each fieldset is terminated by a default-constructed FieldIndex.
  //
  // See: pxr/usd/sdf/crateFile.cpp _WriteFieldSets()

  std::cerr << "DEBUG: WriteFieldSetsSection with " << fieldsets_.size() << " fieldsets" << std::endl;

  // Flatten all fieldsets into a single array with terminators
  std::vector<uint32_t> fieldset_vals;
  for (size_t i = 0; i < fieldsets_.size(); ++i) {
    const auto& fieldset = fieldsets_[i];
    std::cerr << "  Fieldset[" << i << "]: size=" << fieldset.size() << " values: [";
    for (const auto& field_idx : fieldset) {
      std::cerr << field_idx.value << " ";
      fieldset_vals.push_back(field_idx.value);
    }
    // Write terminator (default FieldIndex() has value ~0u)
    fieldset_vals.push_back(~0u);
    std::cerr << "~0u]" << std::endl;
  }

  std::cerr << "DEBUG: Total fieldset_vals = " << fieldset_vals.size() << std::endl;

  size_t num_vals = fieldset_vals.size();

  // Write total number of field index values
  uint64_t val_count = static_cast<uint64_t>(num_vals);
  if (!Write(val_count)) {
    if (err) *err = "Failed to write fieldset value count";
    return false;
  }

  // Compress fieldset values using Usd_IntegerCompression
  size_t buffer_size = Usd_IntegerCompression::GetCompressedBufferSize(num_vals);
  std::vector<char> compressed(buffer_size);

  std::string compress_err;
  size_t compressed_size = Usd_IntegerCompression::CompressToBuffer(
      fieldset_vals.data(), num_vals, compressed.data(), &compress_err);

  if (compressed_size == 0) {
    if (err) *err = "Failed to compress fieldset values: " + compress_err;
    return false;
  }

  // Write compressed size
  uint64_t size = static_cast<uint64_t>(compressed_size);
  if (!Write(size)) {
    if (err) *err = "Failed to write compressed size";
    return false;
  }

  // Write compressed data
  if (!WriteBytes(compressed.data(), compressed_size)) {
    if (err) *err = "Failed to write compressed fieldset data";
    return false;
  }

  int64_t section_end = Tell();

  crate::Section section(kFieldSetsSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WritePathsSection(std::string* err) {
  int64_t section_start = Tell();

  // IMPORTANT: Format for v0.4.0+ (we target v0.7.0+):
  // 1. uint64_t numPaths
  // 2. For each of three arrays (pathIndexes, elementTokenIndexes, jumps):
  //    - uint64_t arrayCompressedSize
  //    - Compressed array data (using Usd_IntegerCompression)
  //
  // See: pxr/usd/sdf/crateFile.cpp _WriteCompressedPathData()

  // Use the path sorting and encoding library
  // Convert TinyUSDZ Path to SimplePath for encoding
  std::vector<pathlib::SimplePath> simple_paths;

  // CRITICAL: Always include the root "/" path first
  // OpenUSD requires root to be in the paths list
  bool has_root = false;
  for (const auto& path : paths_) {
    if (path.prim_part() == "/" && path.prop_part().empty()) {
      has_root = true;
      break;
    }
  }

  if (!has_root) {
    simple_paths.emplace_back("/", "");  // Add root path
  }

  for (const auto& path : paths_) {
    simple_paths.emplace_back(path.prim_part(), path.prop_part());
  }

  // Sort paths
  pathlib::SortSimplePaths(simple_paths);

  // Encode to compressed tree
  pathlib::CompressedPathTree tree = pathlib::EncodePaths(simple_paths);

  // Remap the path tree token indices to our token indices
  // The path tree has its own token indices, but we need to use our global token indices
  // that were computed in Finalize() to preserve field name tokens
  std::vector<int32_t> remapped_element_token_indexes;
  remapped_element_token_indexes.reserve(tree.element_token_indexes.size());

  for (int32_t path_tree_idx : tree.element_token_indexes) {
    if (path_tree_idx < 0) {
      // Negative indices for properties: negate, remap, then negate again
      int32_t abs_idx = -path_tree_idx;
      auto it = path_tree_token_remap_.find(abs_idx);
      if (it != path_tree_token_remap_.end()) {
        remapped_element_token_indexes.push_back(-static_cast<int32_t>(it->second));
      } else {
        // Shouldn't happen, but preserve original if not found
        remapped_element_token_indexes.push_back(path_tree_idx);
      }
    } else {
      // Positive indices for prim parts
      auto it = path_tree_token_remap_.find(path_tree_idx);
      if (it != path_tree_token_remap_.end()) {
        remapped_element_token_indexes.push_back(static_cast<int32_t>(it->second));
      } else {
        // Shouldn't happen, but preserve original if not found
        remapped_element_token_indexes.push_back(path_tree_idx);
      }
    }
  }

  // Replace the tree's token indices with our remapped ones
  tree.element_token_indexes = remapped_element_token_indexes;

  // CRITICAL: OpenUSD expects TWO uint64_t values:
  // 1. Total number of paths (for resizing _paths vector)
  // 2. Number of encoded paths in the tree
  // For the tree encoding, these values are identical.
  uint64_t path_count = static_cast<uint64_t>(tree.size());
  if (!Write(path_count)) {
    if (err) *err = "Failed to write total path count";
    return false;
  }

  // Write the same value again for numEncodedPaths
  if (!Write(path_count)) {
    if (err) *err = "Failed to write encoded path count";
    return false;
  }

  // Convert path_indexes from uint64_t to uint32_t
  // (USD paths typically won't exceed 2^32 entries)
  std::vector<uint32_t> path_indexes_32(tree.path_indexes.size());
  std::cerr << "DEBUG: Path tree has " << tree.path_indexes.size() << " path indices:\n";
  for (size_t i = 0; i < tree.path_indexes.size(); ++i) {
    path_indexes_32[i] = static_cast<uint32_t>(tree.path_indexes[i]);
    std::cerr << "  pathIndex[" << i << "] = " << path_indexes_32[i] << "\n";
  }

  // Compress and write pathIndexes array (uint32_t)
  {
    size_t buffer_size = Usd_IntegerCompression::GetCompressedBufferSize(path_indexes_32.size());
    std::vector<char> compressed(buffer_size);

    std::string compress_err;
    size_t compressed_size = Usd_IntegerCompression::CompressToBuffer(
        path_indexes_32.data(), path_indexes_32.size(), compressed.data(), &compress_err);

    if (compressed_size == 0) {
      if (err) *err = std::string("Failed to compress pathIndexes: ") + compress_err;
      return false;
    }

    uint64_t size = static_cast<uint64_t>(compressed_size);
    if (!Write(size) || !WriteBytes(compressed.data(), compressed_size)) {
      if (err) *err = "Failed to write pathIndexes";
      return false;
    }
  }

  // Compress and write elementTokenIndexes array (int32_t - can be negative)
  {
    size_t buffer_size = Usd_IntegerCompression::GetCompressedBufferSize(tree.element_token_indexes.size());
    std::vector<char> compressed(buffer_size);

    std::string compress_err;
    size_t compressed_size = Usd_IntegerCompression::CompressToBuffer(
        tree.element_token_indexes.data(), tree.element_token_indexes.size(),
        compressed.data(), &compress_err);

    if (compressed_size == 0) {
      if (err) *err = std::string("Failed to compress elementTokenIndexes: ") + compress_err;
      return false;
    }

    uint64_t size = static_cast<uint64_t>(compressed_size);
    if (!Write(size) || !WriteBytes(compressed.data(), compressed_size)) {
      if (err) *err = "Failed to write elementTokenIndexes";
      return false;
    }
  }

  // Compress and write jumps array (int32_t)
  {
    size_t buffer_size = Usd_IntegerCompression::GetCompressedBufferSize(tree.jumps.size());
    std::vector<char> compressed(buffer_size);

    std::string compress_err;
    size_t compressed_size = Usd_IntegerCompression::CompressToBuffer(
        tree.jumps.data(), tree.jumps.size(), compressed.data(), &compress_err);

    if (compressed_size == 0) {
      if (err) *err = std::string("Failed to compress jumps: ") + compress_err;
      return false;
    }

    uint64_t size = static_cast<uint64_t>(compressed_size);
    if (!Write(size) || !WriteBytes(compressed.data(), compressed_size)) {
      if (err) *err = "Failed to write jumps";
      return false;
    }
  }

  int64_t section_end = Tell();

  crate::Section section(kPathsSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WriteSpecsSection(std::string* err) {
  int64_t section_start = Tell();

  // IMPORTANT: Format for v0.4.0+ (we target v0.7.0+):
  // 1. uint64_t numSpecs
  // 2. pathIndexes (compressed with Usd_IntegerCompression):
  //    - uint64_t pathIndexesSize
  //    - Compressed pathIndexes array
  // 3. fieldSetIndexes (compressed with Usd_IntegerCompression):
  //    - uint64_t fieldSetIndexesSize
  //    - Compressed fieldSetIndexes array
  // 4. specTypes (compressed with Usd_IntegerCompression):
  //    - uint64_t specTypesSize
  //    - Compressed specTypes array
  //
  // See: pxr/usd/sdf/crateFile.cpp _WriteSpecs()

  size_t num_specs = spec_data_.size();

  // Write spec count
  uint64_t spec_count = static_cast<uint64_t>(num_specs);
  if (!Write(spec_count)) {
    if (err) *err = "Failed to write spec count";
    return false;
  }

  // IMPORTANT: Build mapping from fieldset number to offset in flat array
  // The reader expects fieldset_index to be the OFFSET in the flat fieldset array,
  // not the fieldset number. We need to convert from fieldset_number -> offset.
  std::vector<uint32_t> fieldset_number_to_offset;
  fieldset_number_to_offset.resize(fieldsets_.size());

  uint32_t current_offset = 0;
  for (size_t i = 0; i < fieldsets_.size(); ++i) {
    fieldset_number_to_offset[i] = current_offset;
    // Each fieldset takes (num_fields + 1) slots (fields + sentinel)
    current_offset += static_cast<uint32_t>(fieldsets_[i].size() + 1);
  }

  std::cerr << "DEBUG: Fieldset number to offset mapping:\n";
  for (size_t i = 0; i < fieldset_number_to_offset.size(); ++i) {
    std::cerr << "  Fieldset[" << i << "] -> offset " << fieldset_number_to_offset[i] << "\n";
  }

  // Separate pathIndexes, fieldSetIndexes, specTypes
  std::vector<uint32_t> path_indexes;
  std::vector<uint32_t> fieldset_indexes;
  std::vector<uint32_t> spec_types;

  path_indexes.reserve(num_specs);
  fieldset_indexes.reserve(num_specs);
  spec_types.reserve(num_specs);

  std::cerr << "DEBUG WriteSpecsSection: Processing " << num_specs << " specs:\n";
  for (size_t i = 0; i < spec_data_.size(); ++i) {
    const auto& spec_data = spec_data_[i];
    path_indexes.push_back(spec_data.spec.path_index.value);

    // Convert fieldset number to offset in flat array
    uint32_t fieldset_number = spec_data.spec.fieldset_index.value;
    uint32_t fieldset_offset = fieldset_number_to_offset[fieldset_number];
    fieldset_indexes.push_back(fieldset_offset);

    spec_types.push_back(static_cast<uint32_t>(spec_data.spec.spec_type));

    std::cerr << "  Spec[" << i << "]: path_index=" << spec_data.spec.path_index.value
              << " fieldset_number=" << fieldset_number
              << " fieldset_offset=" << fieldset_offset
              << " spec_type=" << static_cast<uint32_t>(spec_data.spec.spec_type)
              << " (";
    switch(spec_data.spec.spec_type) {
      case SpecType::PseudoRoot: std::cerr << "PseudoRoot"; break;
      case SpecType::Prim: std::cerr << "Prim"; break;
      case SpecType::Attribute: std::cerr << "Attribute"; break;
      default: std::cerr << "Other"; break;
    }
    std::cerr << ")\n";
  }

  // Helper to compress and write an integer array
  auto writeCompressedIntArray = [this, err](const std::vector<uint32_t>& data,
                                              const char* array_name) -> bool {
    size_t buffer_size = Usd_IntegerCompression::GetCompressedBufferSize(data.size());
    std::vector<char> compressed(buffer_size);

    std::string compress_err;
    size_t compressed_size = Usd_IntegerCompression::CompressToBuffer(
        data.data(), data.size(), compressed.data(), &compress_err);

    if (compressed_size == 0) {
      if (err) *err = std::string("Failed to compress ") + array_name + ": " + compress_err;
      return false;
    }

    // Write compressed size
    uint64_t size = static_cast<uint64_t>(compressed_size);
    if (!Write(size)) {
      if (err) *err = std::string("Failed to write ") + array_name + " size";
      return false;
    }

    // Write compressed data
    if (!WriteBytes(compressed.data(), compressed_size)) {
      if (err) *err = std::string("Failed to write compressed ") + array_name;
      return false;
    }

    return true;
  };

  // Compress and write pathIndexes
  if (!writeCompressedIntArray(path_indexes, "pathIndexes")) {
    return false;
  }

  // Compress and write fieldSetIndexes
  if (!writeCompressedIntArray(fieldset_indexes, "fieldSetIndexes")) {
    return false;
  }

  // Compress and write specTypes
  if (!writeCompressedIntArray(spec_types, "specTypes")) {
    return false;
  }

  int64_t section_end = Tell();

  crate::Section section(kSpecsSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WriteTableOfContents(std::string* err) {
  int64_t toc_offset = Tell();
  std::cerr << "DEBUG: WriteTableOfContents() at offset " << toc_offset << std::endl;

  // Write section count
  uint64_t section_count = static_cast<uint64_t>(toc_.sections.size());
  if (!Write(section_count)) {
    if (err) *err = "Failed to write section count";
    return false;
  }

  // Debug: print first few sections
  for (size_t i = 0; i < std::min(toc_.sections.size(), size_t(3)); ++i) {
    const auto& sec = toc_.sections[i];
    std::cerr << "  Section[" << i << "]: " << sec.name << " start=" << sec.start << " size=" << sec.size << std::endl;
  }

  // Write sections
  for (const auto& section : toc_.sections) {
    // Write section name (null-terminated, max 15 chars)
    char name_buf[crate::kSectionNameMaxLength + 1] = {0};
    strncpy(name_buf, section.name, crate::kSectionNameMaxLength);
    if (!WriteBytes(name_buf, sizeof(name_buf))) {
      if (err) *err = "Failed to write section name";
      return false;
    }

    // Write section start and size
    if (!Write(section.start)) {
      if (err) *err = "Failed to write section start";
      return false;
    }
    if (!Write(section.size)) {
      if (err) *err = "Failed to write section size";
      return false;
    }
  }

  // Store TOC offset for bootstrap
  // We need to save this before writing bootstrap
  int64_t saved_toc_offset = toc_offset;

  std::cerr << "DEBUG: Before bootstrap write, TOC offset = " << saved_toc_offset << std::endl;

  // IMPORTANT: Flush before seeking to beginning
  // We need to flush all buffered writes before seeking backwards
  file_.flush();

  // Seek to beginning to write bootstrap (no need to close/reopen)
  if (!Seek(0)) {
    if (err) *err = "Failed to seek to beginning for bootstrap write";
    return false;
  }

  // Build bootstrap header
  BootStrap boot;
  memset(&boot, 0, sizeof(boot));

  memcpy(boot.ident, kMagicIdent, 8);
  boot.version[0] = options_.version_major;
  boot.version[1] = options_.version_minor;
  boot.version[2] = options_.version_patch;
  boot.toc_offset = saved_toc_offset;

  // Write bootstrap header
  if (!WriteBytes(&boot, sizeof(boot))) {
    if (err) *err = "Failed to write bootstrap";
    return false;
  }

  file_.flush();
  std::cerr << "DEBUG: Bootstrap written successfully" << std::endl;

  return true;
}

bool CrateWriter::WriteBootStrap(std::string* err) {
  // Bootstrap is already written in WriteTableOfContents
  // This is just a placeholder for consistency
  return true;
}

// ============================================================================
// Value Encoding
// ============================================================================

crate::ValueRep CrateWriter::PackValue(const crate::CrateValue& value, std::string* err) {
  crate::ValueRep rep;

  // Try to inline the value
  if (TryInlineValue(value, &rep)) {
    return rep;
  }

  // Value cannot be inlined, write to value data section
  int64_t offset = WriteValueData(value, err);
  if (offset < 0 || (err && !err->empty())) {
    return crate::ValueRep();
  }

  // Create ValueRep with offset and proper type
  // Determine the type for out-of-line values

  if (value.as<double>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE));
  } else if (value.as<int64_t>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64));
  } else if (value.as<uint64_t>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64));
  } else if (value.as<value::float2>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F));
  } else if (value.as<value::double2>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D));
  } else if (value.as<value::int2>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I));
  } else if (value.as<value::float3>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F));
  } else if (value.as<value::double3>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D));
  } else if (value.as<value::int3>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I));
  } else if (value.as<value::half4>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H));
  } else if (value.as<value::float4>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F));
  } else if (value.as<value::double4>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D));
  } else if (value.as<value::int4>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I));
  } else if (value.as<value::matrix2d>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D));
  } else if (value.as<value::matrix3d>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D));
  } else if (value.as<value::matrix4d>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D));
  } else if (value.as<value::quath>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH));
  } else if (value.as<value::quatf>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF));
  } else if (value.as<value::quatd>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD));
  }
  // Phase 1: Array types - detect and set proper type
  // Note: Arrays use the element type ID + IsArray flag (bit 63), NOT a modified type code
  else if (value.as<std::vector<bool>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL));
    rep.SetIsArray();
  } else if (value.as<std::vector<uint8_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR));
    rep.SetIsArray();
  } else if (value.as<std::vector<int32_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT));
    rep.SetIsArray();
  } else if (value.as<std::vector<uint32_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT));
    rep.SetIsArray();
  } else if (value.as<std::vector<int64_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64));
    rep.SetIsArray();
  } else if (value.as<std::vector<uint64_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64));
    rep.SetIsArray();
  } else if (value.as<std::vector<value::half>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF));
    rep.SetIsArray();
  } else if (value.as<std::vector<float>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT));
    rep.SetIsArray();
  } else if (value.as<std::vector<double>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE));
    rep.SetIsArray();
  } else if (value.as<std::vector<value::float2>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F));
    rep.SetIsArray();
  } else if (value.as<std::vector<value::float3>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F));
    rep.SetIsArray();
  } else if (value.as<std::vector<value::float4>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F));
    rep.SetIsArray();
  } else if (value.as<std::vector<std::string>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING));
    rep.SetIsArray();
  } else if (value.as<std::vector<value::token>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN));
    rep.SetIsArray();
  } else if (value.as<std::vector<Path>>()) {
    // PathVector is a special type (type code 40) that doesn't use the array flag
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR));
  }
  // Phase 2: Dictionary type
  else if (value.as<value::dict>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY));
  }
  // Phase 2: ListOp types
  else if (value.as<ListOp<value::token>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP));
  } else if (value.as<ListOp<std::string>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP));
  } else if (value.as<ListOp<Path>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP));
  }
  // Phase 2: Reference and Payload types
  else if (value.as<Reference>()) {
    // Note: There's no single Reference type ID in crate format - References are typically in ReferenceListOp
    // But we'll handle it anyway for completeness
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID));  // Or use a custom type
  } else if (value.as<Payload>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD));
  } else if (value.as<ListOp<Reference>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_REFERENCE_LIST_OP));
  } else if (value.as<ListOp<Payload>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PAYLOAD_LIST_OP));
  }
  // Phase 2: VariantSelectionMap
  else if (value.as<VariantSelectionMap>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIANT_SELECTION_MAP));
  }
  // Phase 3: TimeSamples
  else if (value.as<value::TimeSamples>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES));
  }
  // Unknown/unsupported type
  else {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID));
  }

  rep.SetPayload(static_cast<uint64_t>(offset));

  return rep;
}

int64_t CrateWriter::WriteValueData(const crate::CrateValue& value, std::string* err) {
  // Save current position
  int64_t current_pos = Tell();

  // Seek to end of value data section
  if (!Seek(value_data_end_offset_)) {
    if (err) *err = "Failed to seek to value data section";
    return -1;
  }

  int64_t value_offset = Tell();

  // Phase 1: Write out-of-line value data based on type
  // This handles values that cannot be inlined in the 48-bit payload

  // Double - 8 bytes
  if (auto* double_val = value.as<double>()) {
    if (!Write(*double_val)) {
      if (err) *err = "Failed to write double value";
      return -1;
    }
  }
  // Int64 - 8 bytes (when doesn't fit in 48 bits)
  else if (auto* int64_val = value.as<int64_t>()) {
    if (!Write(*int64_val)) {
      if (err) *err = "Failed to write int64 value";
      return -1;
    }
  }
  // UInt64 - 8 bytes (when doesn't fit in 48 bits)
  else if (auto* uint64_val = value.as<uint64_t>()) {
    if (!Write(*uint64_val)) {
      if (err) *err = "Failed to write uint64 value";
      return -1;
    }
  }
  // Vec2f - 2 x 4 = 8 bytes
  else if (auto* vec2f_val = value.as<value::float2>()) {
    for (size_t i = 0; i < 2; ++i) {
      if (!Write((*vec2f_val)[i])) {
        if (err) *err = "Failed to write Vec2f component";
        return -1;
      }
    }
  }
  // Vec2d - 2 x 8 = 16 bytes
  else if (auto* vec2d_val = value.as<value::double2>()) {
    for (size_t i = 0; i < 2; ++i) {
      if (!Write((*vec2d_val)[i])) {
        if (err) *err = "Failed to write Vec2d component";
        return -1;
      }
    }
  }
  // Vec2i - 2 x 4 = 8 bytes
  else if (auto* vec2i_val = value.as<value::int2>()) {
    for (size_t i = 0; i < 2; ++i) {
      if (!Write((*vec2i_val)[i])) {
        if (err) *err = "Failed to write Vec2i component";
        return -1;
      }
    }
  }
  // Vec3f - 3 x 4 = 12 bytes
  else if (auto* vec3f_val = value.as<value::float3>()) {
    for (size_t i = 0; i < 3; ++i) {
      if (!Write((*vec3f_val)[i])) {
        if (err) *err = "Failed to write Vec3f component";
        return -1;
      }
    }
  }
  // Vec3d - 3 x 8 = 24 bytes
  else if (auto* vec3d_val = value.as<value::double3>()) {
    for (size_t i = 0; i < 3; ++i) {
      if (!Write((*vec3d_val)[i])) {
        if (err) *err = "Failed to write Vec3d component";
        return -1;
      }
    }
  }
  // Vec3i - 3 x 4 = 12 bytes
  else if (auto* vec3i_val = value.as<value::int3>()) {
    for (size_t i = 0; i < 3; ++i) {
      if (!Write((*vec3i_val)[i])) {
        if (err) *err = "Failed to write Vec3i component";
        return -1;
      }
    }
  }
  // Vec4h - 4 x 2 = 8 bytes
  else if (auto* vec4h_val = value.as<value::half4>()) {
    for (size_t i = 0; i < 4; ++i) {
      if (!Write((*vec4h_val)[i].value)) {
        if (err) *err = "Failed to write Vec4h component";
        return -1;
      }
    }
  }
  // Vec4f - 4 x 4 = 16 bytes
  else if (auto* vec4f_val = value.as<value::float4>()) {
    for (size_t i = 0; i < 4; ++i) {
      if (!Write((*vec4f_val)[i])) {
        if (err) *err = "Failed to write Vec4f component";
        return -1;
      }
    }
  }
  // Vec4d - 4 x 8 = 32 bytes
  else if (auto* vec4d_val = value.as<value::double4>()) {
    for (size_t i = 0; i < 4; ++i) {
      if (!Write((*vec4d_val)[i])) {
        if (err) *err = "Failed to write Vec4d component";
        return -1;
      }
    }
  }
  // Vec4i - 4 x 4 = 16 bytes
  else if (auto* vec4i_val = value.as<value::int4>()) {
    for (size_t i = 0; i < 4; ++i) {
      if (!Write((*vec4i_val)[i])) {
        if (err) *err = "Failed to write Vec4i component";
        return -1;
      }
    }
  }
  // Matrix2d - 4 x 8 = 32 bytes
  else if (auto* mat2d_val = value.as<value::matrix2d>()) {
    // Write matrix elements in column-major order (USD convention)
    for (size_t i = 0; i < 4; ++i) {
      if (!Write(mat2d_val->m[i])) {
        if (err) *err = "Failed to write Matrix2d element";
        return -1;
      }
    }
  }
  // Matrix3d - 9 x 8 = 72 bytes
  else if (auto* mat3d_val = value.as<value::matrix3d>()) {
    // Write matrix elements in column-major order (USD convention)
    for (size_t i = 0; i < 9; ++i) {
      if (!Write(mat3d_val->m[i])) {
        if (err) *err = "Failed to write Matrix3d element";
        return -1;
      }
    }
  }
  // Matrix4d - 16 x 8 = 128 bytes
  else if (auto* mat4d_val = value.as<value::matrix4d>()) {
    // Write matrix elements in column-major order (USD convention)
    for (size_t i = 0; i < 16; ++i) {
      if (!Write(mat4d_val->m[i])) {
        if (err) *err = "Failed to write Matrix4d element";
        return -1;
      }
    }
  }
  // Quath - 4 x 2 = 8 bytes
  else if (auto* quath_val = value.as<value::quath>()) {
    // Write quaternion components: real, i, j, k
    if (!Write(quath_val->real.value) || !Write(quath_val->imag[0].value) ||
        !Write(quath_val->imag[1].value) || !Write(quath_val->imag[2].value)) {
      if (err) *err = "Failed to write Quath components";
      return -1;
    }
  }
  // Quatf - 4 x 4 = 16 bytes
  else if (auto* quatf_val = value.as<value::quatf>()) {
    // Write quaternion components: real, i, j, k
    if (!Write(quatf_val->real) || !Write(quatf_val->imag[0]) ||
        !Write(quatf_val->imag[1]) || !Write(quatf_val->imag[2])) {
      if (err) *err = "Failed to write Quatf components";
      return -1;
    }
  }
  // Quatd - 4 x 8 = 32 bytes
  else if (auto* quatd_val = value.as<value::quatd>()) {
    // Write quaternion components: real, i, j, k
    if (!Write(quatd_val->real) || !Write(quatd_val->imag[0]) ||
        !Write(quatd_val->imag[1]) || !Write(quatd_val->imag[2])) {
      if (err) *err = "Failed to write Quatd components";
      return -1;
    }
  }
  // Phase 1: Array serialization
  // Arrays are written as: uint64_t count + elements
  // For bool arrays, each bool is written as 1 byte

  // Bool array - special handling (each bool = 1 byte)
  else if (auto* bool_array = value.as<std::vector<bool>>()) {
    uint64_t count = bool_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write bool array count";
      return -1;
    }
    // Write each bool as a byte
    for (bool b : *bool_array) {
      uint8_t byte = b ? 1 : 0;
      if (!Write(byte)) {
        if (err) *err = "Failed to write bool array element";
        return -1;
      }
    }
  }
  // Byte array
  else if (auto* byte_array = value.as<std::vector<uint8_t>>()) {
    uint64_t count = byte_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write byte array count";
      return -1;
    }
    if (count > 0 && !WriteBytes(byte_array->data(), count * sizeof(uint8_t))) {
      if (err) *err = "Failed to write byte array data";
      return -1;
    }
  }
  // Int array
  else if (auto* int_array = value.as<std::vector<int32_t>>()) {
    uint64_t count = int_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write int array count";
      return -1;
    }

    // Phase 5: Integer array compression (if >= 16 elements)
    if (count >= 16 && options_.enable_compression) {
      // Compress using Usd_IntegerCompression
      size_t compressedBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(count);
      std::vector<char> compressed(compressedBufferSize);

      std::string compress_err;
      size_t compressedSize = Usd_IntegerCompression::CompressToBuffer(
          int_array->data(), count, compressed.data(), &compress_err);

      if (compressedSize == 0 || compressedSize == static_cast<size_t>(~0)) {
        // Compression failed - write uncompressed
        for (int32_t val : *int_array) {
          if (!Write(val)) {
            if (err) *err = "Failed to write int array element";
            return -1;
          }
        }
      } else {
        // Write compressed data
        // Format: compressed size + compressed data
        uint64_t comp_size = static_cast<uint64_t>(compressedSize);
        if (!Write(comp_size)) {
          if (err) *err = "Failed to write compressed int array size";
          return -1;
        }
        if (!WriteBytes(compressed.data(), compressedSize)) {
          if (err) *err = "Failed to write compressed int array data";
          return -1;
        }
      }
    } else {
      // Small array or compression disabled - write uncompressed
      for (int32_t val : *int_array) {
        if (!Write(val)) {
          if (err) *err = "Failed to write int array element";
          return -1;
        }
      }
    }
  }
  // UInt array
  else if (auto* uint_array = value.as<std::vector<uint32_t>>()) {
    uint64_t count = uint_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write uint array count";
      return -1;
    }

    // Phase 5: Integer array compression (if >= 16 elements)
    if (count >= 16 && options_.enable_compression) {
      size_t compressedBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(count);
      std::vector<char> compressed(compressedBufferSize);

      std::string compress_err;
      size_t compressedSize = Usd_IntegerCompression::CompressToBuffer(
          uint_array->data(), count, compressed.data(), &compress_err);

      if (compressedSize == 0 || compressedSize == static_cast<size_t>(~0)) {
        for (uint32_t val : *uint_array) {
          if (!Write(val)) {
            if (err) *err = "Failed to write uint array element";
            return -1;
          }
        }
      } else {
        uint64_t comp_size = static_cast<uint64_t>(compressedSize);
        if (!Write(comp_size)) {
          if (err) *err = "Failed to write compressed uint array size";
          return -1;
        }
        if (!WriteBytes(compressed.data(), compressedSize)) {
          if (err) *err = "Failed to write compressed uint array data";
          return -1;
        }
      }
    } else {
      for (uint32_t val : *uint_array) {
        if (!Write(val)) {
          if (err) *err = "Failed to write uint array element";
          return -1;
        }
      }
    }
  }
  // Int64 array
  else if (auto* int64_array = value.as<std::vector<int64_t>>()) {
    uint64_t count = int64_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write int64 array count";
      return -1;
    }

    // Phase 5: Integer array compression (if >= 16 elements)
    if (count >= 16 && options_.enable_compression) {
      size_t compressedBufferSize = Usd_IntegerCompression64::GetCompressedBufferSize(count);
      std::vector<char> compressed(compressedBufferSize);

      std::string compress_err;
      size_t compressedSize = Usd_IntegerCompression64::CompressToBuffer(
          int64_array->data(), count, compressed.data(), &compress_err);

      if (compressedSize == 0 || compressedSize == static_cast<size_t>(~0)) {
        for (int64_t val : *int64_array) {
          if (!Write(val)) {
            if (err) *err = "Failed to write int64 array element";
            return -1;
          }
        }
      } else {
        uint64_t comp_size = static_cast<uint64_t>(compressedSize);
        if (!Write(comp_size)) {
          if (err) *err = "Failed to write compressed int64 array size";
          return -1;
        }
        if (!WriteBytes(compressed.data(), compressedSize)) {
          if (err) *err = "Failed to write compressed int64 array data";
          return -1;
        }
      }
    } else {
      for (int64_t val : *int64_array) {
        if (!Write(val)) {
          if (err) *err = "Failed to write int64 array element";
          return -1;
        }
      }
    }
  }
  // UInt64 array
  else if (auto* uint64_array = value.as<std::vector<uint64_t>>()) {
    uint64_t count = uint64_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write uint64 array count";
      return -1;
    }

    // Phase 5: Integer array compression (if >= 16 elements)
    if (count >= 16 && options_.enable_compression) {
      size_t compressedBufferSize = Usd_IntegerCompression64::GetCompressedBufferSize(count);
      std::vector<char> compressed(compressedBufferSize);

      std::string compress_err;
      size_t compressedSize = Usd_IntegerCompression64::CompressToBuffer(
          uint64_array->data(), count, compressed.data(), &compress_err);

      if (compressedSize == 0 || compressedSize == static_cast<size_t>(~0)) {
        for (uint64_t val : *uint64_array) {
          if (!Write(val)) {
            if (err) *err = "Failed to write uint64 array element";
            return -1;
          }
        }
      } else {
        uint64_t comp_size = static_cast<uint64_t>(compressedSize);
        if (!Write(comp_size)) {
          if (err) *err = "Failed to write compressed uint64 array size";
          return -1;
        }
        if (!WriteBytes(compressed.data(), compressedSize)) {
          if (err) *err = "Failed to write compressed uint64 array data";
          return -1;
        }
      }
    } else {
      for (uint64_t val : *uint64_array) {
        if (!Write(val)) {
          if (err) *err = "Failed to write uint64 array element";
          return -1;
        }
      }
    }
  }
  // Half array
  else if (auto* half_array = value.as<std::vector<value::half>>()) {
    uint64_t count = half_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write half array count";
      return -1;
    }

    // Phase 5: Integer array compression for half (16-bit float treated as uint16_t)
    if (count >= 16 && options_.enable_compression) {
      // Convert half values to uint16_t for compression
      std::vector<uint32_t> uint_values;
      uint_values.reserve(count);
      for (const auto& val : *half_array) {
        uint_values.push_back(static_cast<uint32_t>(val.value));
      }

      // Compress using Usd_IntegerCompression
      size_t compressedBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(count);
      std::vector<char> compressed(compressedBufferSize);

      std::string compress_err;
      size_t compressedSize = Usd_IntegerCompression::CompressToBuffer(
          uint_values.data(), count, compressed.data(), &compress_err);

      if (compressedSize == 0 || compressedSize == static_cast<size_t>(~0)) {
        // Compression failed - write uncompressed
        for (const auto& val : *half_array) {
          if (!Write(val.value)) {
            if (err) *err = "Failed to write half array element";
            return -1;
          }
        }
      } else {
        // Write compressed data
        uint64_t comp_size = static_cast<uint64_t>(compressedSize);
        if (!Write(comp_size)) {
          if (err) *err = "Failed to write compressed half array size";
          return -1;
        }
        if (!WriteBytes(compressed.data(), compressedSize)) {
          if (err) *err = "Failed to write compressed half array data";
          return -1;
        }
      }
    } else {
      // Small array or compression disabled - write uncompressed
      for (const auto& val : *half_array) {
        if (!Write(val.value)) {
          if (err) *err = "Failed to write half array element";
          return -1;
        }
      }
    }
  }
  // Float array
  else if (auto* float_array = value.as<std::vector<float>>()) {
    uint64_t count = float_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write float array count";
      return -1;
    }

    // Phase 5: Integer array compression for float (reinterpret as uint32_t)
    if (count >= 16 && options_.enable_compression) {
      // Reinterpret float values as uint32_t for compression
      std::vector<uint32_t> uint_values;
      uint_values.reserve(count);
      for (float val : *float_array) {
        uint32_t uint_val;
        std::memcpy(&uint_val, &val, sizeof(uint32_t));
        uint_values.push_back(uint_val);
      }

      // Compress using Usd_IntegerCompression
      size_t compressedBufferSize = Usd_IntegerCompression::GetCompressedBufferSize(count);
      std::vector<char> compressed(compressedBufferSize);

      std::string compress_err;
      size_t compressedSize = Usd_IntegerCompression::CompressToBuffer(
          uint_values.data(), count, compressed.data(), &compress_err);

      if (compressedSize == 0 || compressedSize == static_cast<size_t>(~0)) {
        // Compression failed - write uncompressed
        for (float val : *float_array) {
          if (!Write(val)) {
            if (err) *err = "Failed to write float array element";
            return -1;
          }
        }
      } else {
        // Write compressed data
        uint64_t comp_size = static_cast<uint64_t>(compressedSize);
        if (!Write(comp_size)) {
          if (err) *err = "Failed to write compressed float array size";
          return -1;
        }
        if (!WriteBytes(compressed.data(), compressedSize)) {
          if (err) *err = "Failed to write compressed float array data";
          return -1;
        }
      }
    } else {
      // Small array or compression disabled - write uncompressed
      for (float val : *float_array) {
        if (!Write(val)) {
          if (err) *err = "Failed to write float array element";
          return -1;
        }
      }
    }
  }
  // Double array
  else if (auto* double_array = value.as<std::vector<double>>()) {
    uint64_t count = double_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write double array count";
      return -1;
    }

    // Phase 5: Integer array compression for double (reinterpret as uint64_t)
    if (count >= 16 && options_.enable_compression) {
      // Reinterpret double values as uint64_t for compression
      std::vector<uint64_t> uint_values;
      uint_values.reserve(count);
      for (double val : *double_array) {
        uint64_t uint_val;
        std::memcpy(&uint_val, &val, sizeof(uint64_t));
        uint_values.push_back(uint_val);
      }

      // Compress using Usd_IntegerCompression64
      size_t compressedBufferSize = Usd_IntegerCompression64::GetCompressedBufferSize(count);
      std::vector<char> compressed(compressedBufferSize);

      std::string compress_err;
      size_t compressedSize = Usd_IntegerCompression64::CompressToBuffer(
          uint_values.data(), count, compressed.data(), &compress_err);

      if (compressedSize == 0 || compressedSize == static_cast<size_t>(~0)) {
        // Compression failed - write uncompressed
        for (double val : *double_array) {
          if (!Write(val)) {
            if (err) *err = "Failed to write double array element";
            return -1;
          }
        }
      } else {
        // Write compressed data
        uint64_t comp_size = static_cast<uint64_t>(compressedSize);
        if (!Write(comp_size)) {
          if (err) *err = "Failed to write compressed double array size";
          return -1;
        }
        if (!WriteBytes(compressed.data(), compressedSize)) {
          if (err) *err = "Failed to write compressed double array data";
          return -1;
        }
      }
    } else {
      // Small array or compression disabled - write uncompressed
      for (double val : *double_array) {
        if (!Write(val)) {
          if (err) *err = "Failed to write double array element";
          return -1;
        }
      }
    }
  }
  // Vec2f array
  else if (auto* vec2f_array = value.as<std::vector<value::float2>>()) {
    uint64_t count = vec2f_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Vec2f array count";
      return -1;
    }
    for (const auto& vec : *vec2f_array) {
      for (size_t i = 0; i < 2; ++i) {
        if (!Write(vec[i])) {
          if (err) *err = "Failed to write Vec2f array element";
          return -1;
        }
      }
    }
  }
  // Vec3f array
  else if (auto* vec3f_array = value.as<std::vector<value::float3>>()) {
    uint64_t count = vec3f_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Vec3f array count";
      return -1;
    }
    for (const auto& vec : *vec3f_array) {
      for (size_t i = 0; i < 3; ++i) {
        if (!Write(vec[i])) {
          if (err) *err = "Failed to write Vec3f array element";
          return -1;
        }
      }
    }
  }
  // Vec4f array
  else if (auto* vec4f_array = value.as<std::vector<value::float4>>()) {
    uint64_t count = vec4f_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Vec4f array count";
      return -1;
    }
    for (const auto& vec : *vec4f_array) {
      for (size_t i = 0; i < 4; ++i) {
        if (!Write(vec[i])) {
          if (err) *err = "Failed to write Vec4f array element";
          return -1;
        }
      }
    }
  }
  // String array - special handling (strings are stored as indices)
  else if (auto* string_array = value.as<std::vector<std::string>>()) {
    uint64_t count = string_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write string array count";
      return -1;
    }
    for (const auto& str : *string_array) {
      crate::StringIndex idx = GetOrCreateString(str);
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write string array element index";
        return -1;
      }
    }
  }
  // Token array - special handling (tokens are stored as indices)
  else if (auto* token_array = value.as<std::vector<value::token>>()) {
    uint64_t count = token_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write token array count";
      return -1;
    }
    for (const auto& tok : *token_array) {
      crate::TokenIndex idx = GetOrCreateToken(tok.str());
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write token array element index";
        return -1;
      }
    }
  }
  // Path array - special handling (paths are stored as indices)
  else if (auto* path_array = value.as<std::vector<Path>>()) {
    uint64_t count = path_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write path array count";
      return -1;
    }
    for (const auto& path : *path_array) {
      crate::PathIndex idx = GetOrCreatePath(path);
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write path array element index";
        return -1;
      }
    }
  }
  // Phase 2: Dictionary serialization
  // Dictionary format (OpenUSD simple WriteMap): uint64_t count + for each: (StringIndex key, ValueRep value)
  // This matches OpenUSD's WriteMap template (crateFile.cpp:1410-1416)
  // Note: TinyUSDZ's ReadCustomData (crate-reader.cc:2027-2094) expects recursive offset format,
  // but we're testing if it can also handle the simple format
  else if (auto* dict_val = value.as<value::dict>()) {
    uint64_t count = dict_val->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write dictionary count";
      return -1;
    }

    // Write each (key, value) pair directly
    for (const auto& kv : *dict_val) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write dictionary key index";
        return -1;
      }

      // Pack value to ValueRep
      crate::ValueRep value_rep;
      bool value_packed = false;

      // Try int32
      if (auto* int_val = linb::any_cast<int32_t>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(*int_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try int (convert to int32)
      else if (auto* int_val = linb::any_cast<int>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(static_cast<int32_t>(*int_val));
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try uint32
      else if (auto* uint_val = linb::any_cast<uint32_t>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(*uint_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try float
      else if (auto* float_val = linb::any_cast<float>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(*float_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try double
      else if (auto* double_val = linb::any_cast<double>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(*double_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try bool
      else if (auto* bool_val = linb::any_cast<bool>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(*bool_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try string
      else if (auto* str_val = linb::any_cast<std::string>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(*str_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try token
      else if (auto* tok_val = linb::any_cast<value::token>(&kv.second)) {
        crate::CrateValue cv;
        cv.Set(*tok_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      else {
        if (err) *err = "Unsupported dictionary value type";
        return -1;
      }

      if (!value_packed) {
        if (err) *err = "Failed to pack dictionary value";
        return -1;
      }

      // Write ValueRep directly (no offset indirection)
      if (!Write(value_rep.GetData())) {
        if (err) *err = "Failed to write dictionary ValueRep";
        return -1;
      }
    }
  }
  // Phase 2: TokenListOp serialization
  // ListOp format: ListOpHeader(uint8) + lists (each with uint64 count + elements)
  else if (auto* token_listop = value.as<ListOp<value::token>>()) {
    // Write ListOpHeader
    ListOpHeader header;
    header.bits = 0;
    if (token_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (token_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (token_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (token_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (token_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (token_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (token_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write TokenListOp header";
      return -1;
    }

    // Write explicit items if present
    if (token_listop->HasExplicitItems()) {
      uint64_t count = token_listop->GetExplicitItems().size();
      if (!Write(count)) {
        if (err) *err = "Failed to write TokenListOp explicit count";
        return -1;
      }
      for (const auto& tok : token_listop->GetExplicitItems()) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) {
          if (err) *err = "Failed to write TokenListOp explicit item";
          return -1;
        }
      }
    }

    // Write added items if present
    if (token_listop->HasAddedItems()) {
      uint64_t count = token_listop->GetAddedItems().size();
      if (!Write(count)) {
        if (err) *err = "Failed to write TokenListOp added count";
        return -1;
      }
      for (const auto& tok : token_listop->GetAddedItems()) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) {
          if (err) *err = "Failed to write TokenListOp added item";
          return -1;
        }
      }
    }

    // Write prepended items if present
    if (token_listop->HasPrependedItems()) {
      uint64_t count = token_listop->GetPrependedItems().size();
      if (!Write(count)) {
        if (err) *err = "Failed to write TokenListOp prepended count";
        return -1;
      }
      for (const auto& tok : token_listop->GetPrependedItems()) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) {
          if (err) *err = "Failed to write TokenListOp prepended item";
          return -1;
        }
      }
    }

    // Write appended items if present
    if (token_listop->HasAppendedItems()) {
      uint64_t count = token_listop->GetAppendedItems().size();
      if (!Write(count)) {
        if (err) *err = "Failed to write TokenListOp appended count";
        return -1;
      }
      for (const auto& tok : token_listop->GetAppendedItems()) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) {
          if (err) *err = "Failed to write TokenListOp appended item";
          return -1;
        }
      }
    }

    // Write deleted items if present
    if (token_listop->HasDeletedItems()) {
      uint64_t count = token_listop->GetDeletedItems().size();
      if (!Write(count)) {
        if (err) *err = "Failed to write TokenListOp deleted count";
        return -1;
      }
      for (const auto& tok : token_listop->GetDeletedItems()) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) {
          if (err) *err = "Failed to write TokenListOp deleted item";
          return -1;
        }
      }
    }

    // Write ordered items if present
    if (token_listop->HasOrderedItems()) {
      uint64_t count = token_listop->GetOrderedItems().size();
      if (!Write(count)) {
        if (err) *err = "Failed to write TokenListOp ordered count";
        return -1;
      }
      for (const auto& tok : token_listop->GetOrderedItems()) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) {
          if (err) *err = "Failed to write TokenListOp ordered item";
          return -1;
        }
      }
    }
  }
  // StringListOp serialization
  else if (auto* string_listop = value.as<ListOp<std::string>>()) {
    // Similar to TokenListOp but with StringIndex
    ListOpHeader header;
    header.bits = 0;
    if (string_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (string_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (string_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (string_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (string_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (string_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (string_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write StringListOp header";
      return -1;
    }

    // Helper lambda to write a string list
    auto writeStringList = [&](const std::vector<std::string>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& str : list) {
        crate::StringIndex idx = GetOrCreateString(str);
        if (!Write(idx.value)) return false;
      }
      return true;
    };

    if (string_listop->HasExplicitItems() && !writeStringList(string_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write StringListOp explicit items";
      return -1;
    }
    if (string_listop->HasAddedItems() && !writeStringList(string_listop->GetAddedItems())) {
      if (err) *err = "Failed to write StringListOp added items";
      return -1;
    }
    if (string_listop->HasPrependedItems() && !writeStringList(string_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write StringListOp prepended items";
      return -1;
    }
    if (string_listop->HasAppendedItems() && !writeStringList(string_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write StringListOp appended items";
      return -1;
    }
    if (string_listop->HasDeletedItems() && !writeStringList(string_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write StringListOp deleted items";
      return -1;
    }
    if (string_listop->HasOrderedItems() && !writeStringList(string_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write StringListOp ordered items";
      return -1;
    }
  }
  // PathListOp serialization
  else if (auto* path_listop = value.as<ListOp<Path>>()) {
    // Similar to StringListOp but with PathIndex
    ListOpHeader header;
    header.bits = 0;
    if (path_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (path_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (path_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (path_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (path_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (path_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (path_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write PathListOp header";
      return -1;
    }

    // Helper lambda to write a path list
    auto writePathList = [&](const std::vector<Path>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& path : list) {
        crate::PathIndex idx = GetOrCreatePath(path);
        if (!Write(idx.value)) return false;
      }
      return true;
    };

    if (path_listop->HasExplicitItems() && !writePathList(path_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write PathListOp explicit items";
      return -1;
    }
    if (path_listop->HasAddedItems() && !writePathList(path_listop->GetAddedItems())) {
      if (err) *err = "Failed to write PathListOp added items";
      return -1;
    }
    if (path_listop->HasPrependedItems() && !writePathList(path_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write PathListOp prepended items";
      return -1;
    }
    if (path_listop->HasAppendedItems() && !writePathList(path_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write PathListOp appended items";
      return -1;
    }
    if (path_listop->HasDeletedItems() && !writePathList(path_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write PathListOp deleted items";
      return -1;
    }
    if (path_listop->HasOrderedItems() && !writePathList(path_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write PathListOp ordered items";
      return -1;
    }
  }
  // Reference serialization
  else if (auto* ref_val = value.as<Reference>()) {
    // Reference format: StringIndex (asset_path) + PathIndex (prim_path) + LayerOffset (2 doubles) + Dictionary (customData)

    // Write asset path as StringIndex
    crate::StringIndex asset_idx = GetOrCreateString(ref_val->asset_path.GetAssetPath());
    if (!Write(asset_idx.value)) {
      if (err) *err = "Failed to write Reference asset_path";
      return -1;
    }

    // Write prim path as PathIndex
    crate::PathIndex prim_idx = GetOrCreatePath(ref_val->prim_path);
    if (!Write(prim_idx.value)) {
      if (err) *err = "Failed to write Reference prim_path";
      return -1;
    }

    // Write LayerOffset (2 doubles)
    if (!Write(ref_val->layerOffset._offset)) {
      if (err) *err = "Failed to write Reference LayerOffset offset";
      return -1;
    }
    if (!Write(ref_val->layerOffset._scale)) {
      if (err) *err = "Failed to write Reference LayerOffset scale";
      return -1;
    }

    // Write customData dictionary
    // Dictionary format: uint64_t count + (StringIndex key, ValueRep value) pairs
    uint64_t dict_count = ref_val->customData.size();
    if (!Write(dict_count)) {
      if (err) *err = "Failed to write Reference customData count";
      return -1;
    }

    for (const auto& kv : ref_val->customData) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write Reference customData key";
        return -1;
      }

      // Write value as ValueRep
      // kv.second is MetaVariable, need to get value from it
      crate::ValueRep value_rep;
      bool value_written = false;

      // Try common types using MetaVariable::get_value<T>()
      if (auto int_val = kv.second.get_value<int32_t>()) {
        crate::CrateValue cv;
        cv.Set(*int_val);
        value_rep = PackValue(cv, err);
        value_written = true;
      } else if (auto float_val = kv.second.get_value<float>()) {
        crate::CrateValue cv;
        cv.Set(*float_val);
        value_rep = PackValue(cv, err);
        value_written = true;
      } else if (auto str_val = kv.second.get_value<std::string>()) {
        crate::CrateValue cv;
        cv.Set(*str_val);
        value_rep = PackValue(cv, err);
        value_written = true;
      } else {
        if (err) *err = "Unsupported Reference customData value type";
        return -1;
      }

      if (value_written) {
        if (!Write(value_rep.GetData())) {
          if (err) *err = "Failed to write Reference customData value";
          return -1;
        }
      }
    }
  }
  // Payload serialization
  else if (auto* payload_val = value.as<Payload>()) {
    // Payload format: StringIndex (asset_path) + PathIndex (prim_path) + LayerOffset (2 doubles)

    // Write asset path as StringIndex
    crate::StringIndex asset_idx = GetOrCreateString(payload_val->asset_path.GetAssetPath());
    if (!Write(asset_idx.value)) {
      if (err) *err = "Failed to write Payload asset_path";
      return -1;
    }

    // Write prim path as PathIndex
    crate::PathIndex prim_idx = GetOrCreatePath(payload_val->prim_path);
    if (!Write(prim_idx.value)) {
      if (err) *err = "Failed to write Payload prim_path";
      return -1;
    }

    // Write LayerOffset (2 doubles)
    if (!Write(payload_val->layerOffset._offset)) {
      if (err) *err = "Failed to write Payload LayerOffset offset";
      return -1;
    }
    if (!Write(payload_val->layerOffset._scale)) {
      if (err) *err = "Failed to write Payload LayerOffset scale";
      return -1;
    }
  }
  // ReferenceListOp serialization
  else if (auto* ref_listop = value.as<ListOp<Reference>>()) {
    // Write ListOpHeader
    ListOpHeader header;
    header.bits = 0;
    if (ref_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (ref_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (ref_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (ref_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (ref_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (ref_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (ref_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write ReferenceListOp header";
      return -1;
    }

    // Helper lambda to write a Reference list (inline implementation for now)
    auto writeRefList = [&](const std::vector<Reference>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& ref : list) {
        // Write each Reference inline (same format as single Reference above)
        crate::StringIndex asset_idx = GetOrCreateString(ref.asset_path.GetAssetPath());
        if (!Write(asset_idx.value)) return false;

        crate::PathIndex prim_idx = GetOrCreatePath(ref.prim_path);
        if (!Write(prim_idx.value)) return false;

        if (!Write(ref.layerOffset._offset)) return false;
        if (!Write(ref.layerOffset._scale)) return false;

        // customData dictionary (simplified - just write count 0 for now)
        uint64_t dict_count = 0;  // TODO: Handle customData properly
        if (!Write(dict_count)) return false;
      }
      return true;
    };

    if (ref_listop->HasExplicitItems() && !writeRefList(ref_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write ReferenceListOp explicit items";
      return -1;
    }
    if (ref_listop->HasAddedItems() && !writeRefList(ref_listop->GetAddedItems())) {
      if (err) *err = "Failed to write ReferenceListOp added items";
      return -1;
    }
    if (ref_listop->HasPrependedItems() && !writeRefList(ref_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write ReferenceListOp prepended items";
      return -1;
    }
    if (ref_listop->HasAppendedItems() && !writeRefList(ref_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write ReferenceListOp appended items";
      return -1;
    }
    if (ref_listop->HasDeletedItems() && !writeRefList(ref_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write ReferenceListOp deleted items";
      return -1;
    }
    if (ref_listop->HasOrderedItems() && !writeRefList(ref_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write ReferenceListOp ordered items";
      return -1;
    }
  }
  // PayloadListOp serialization
  else if (auto* payload_listop = value.as<ListOp<Payload>>()) {
    // Write ListOpHeader
    ListOpHeader header;
    header.bits = 0;
    if (payload_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (payload_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (payload_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (payload_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (payload_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (payload_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (payload_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write PayloadListOp header";
      return -1;
    }

    // Helper lambda to write a Payload list
    auto writePayloadList = [&](const std::vector<Payload>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& payload : list) {
        crate::StringIndex asset_idx = GetOrCreateString(payload.asset_path.GetAssetPath());
        if (!Write(asset_idx.value)) return false;

        crate::PathIndex prim_idx = GetOrCreatePath(payload.prim_path);
        if (!Write(prim_idx.value)) return false;

        if (!Write(payload.layerOffset._offset)) return false;
        if (!Write(payload.layerOffset._scale)) return false;
      }
      return true;
    };

    if (payload_listop->HasExplicitItems() && !writePayloadList(payload_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write PayloadListOp explicit items";
      return -1;
    }
    if (payload_listop->HasAddedItems() && !writePayloadList(payload_listop->GetAddedItems())) {
      if (err) *err = "Failed to write PayloadListOp added items";
      return -1;
    }
    if (payload_listop->HasPrependedItems() && !writePayloadList(payload_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write PayloadListOp prepended items";
      return -1;
    }
    if (payload_listop->HasAppendedItems() && !writePayloadList(payload_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write PayloadListOp appended items";
      return -1;
    }
    if (payload_listop->HasDeletedItems() && !writePayloadList(payload_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write PayloadListOp deleted items";
      return -1;
    }
    if (payload_listop->HasOrderedItems() && !writePayloadList(payload_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write PayloadListOp ordered items";
      return -1;
    }
  }
  // VariantSelectionMap serialization
  else if (auto* variant_map = value.as<VariantSelectionMap>()) {
    // VariantSelectionMap format: uint64_t count + (StringIndex key, StringIndex value) pairs

    uint64_t count = variant_map->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write VariantSelectionMap count";
      return -1;
    }

    for (const auto& kv : *variant_map) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write VariantSelectionMap key";
        return -1;
      }

      // Write value as StringIndex
      crate::StringIndex val_idx = GetOrCreateString(kv.second);
      if (!Write(val_idx.value)) {
        if (err) *err = "Failed to write VariantSelectionMap value";
        return -1;
      }
    }
  }
  // Phase 5: TimeSamples with value serialization
  else if (auto* timesamples_val = value.as<value::TimeSamples>()) {
    // TimeSamples format:
    // 1. Time array: uint64_t count + double times[count]
    // 2. Value array: uint64_t count + ValueRep values[count]

    uint64_t num_samples = static_cast<uint64_t>(timesamples_val->size());

    // Write time array count
    if (!Write(num_samples)) {
      if (err) *err = "Failed to write TimeSamples time count";
      return -1;
    }

    // Write times
    for (size_t i = 0; i < num_samples; ++i) {
      auto time_opt = timesamples_val->get_time(i);
      if (!time_opt) {
        if (err) *err = "Failed to get time from TimeSamples at index " + std::to_string(i);
        return -1;
      }
      if (!Write(time_opt.value())) {
        if (err) *err = "Failed to write time value at index " + std::to_string(i);
        return -1;
      }
    }

    // Write value array count
    if (!Write(num_samples)) {
      if (err) *err = "Failed to write TimeSamples value count";
      return -1;
    }

    // Get samples - this works for both POD and non-POD types
    const auto& samples = timesamples_val->get_samples();

    if (samples.size() != num_samples) {
      if (err) *err = "TimeSamples: samples size mismatch";
      return -1;
    }

    // Write ValueRep for each value
    for (size_t i = 0; i < num_samples; ++i) {
      const auto& sample = samples[i];

      // Check if this is a blocked value (ValueBlock/None)
      if (sample.blocked) {
        // Write ValueBlock ValueRep
        crate::ValueRep rep;
        rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK));
        rep.SetPayload(0);
        uint64_t rep_data = rep.GetData();
        if (!Write(rep_data)) {
          if (err) *err = "Failed to write ValueBlock ValueRep";
          return -1;
        }
        continue;
      }

      // Convert value::Value to CrateValue
      crate::CrateValue crate_value;
      if (!ConvertValueToCrateValue(sample.value, &crate_value, err)) {
        if (err) *err = "Failed to convert TimeSamples value at index " + std::to_string(i) + ": " + *err;
        return -1;
      }

      // DEDUPLICATION: Check if this value can be deduplicated
      crate::ValueRep value_rep;
      bool dedup_attempted = false;

      if (options_.enable_deduplication) {
        bool is_dedup_candidate = false;
        std::vector<char> value_bytes;
        size_t dedup_element_size = 1;  // element stride for NaN-aware hash
        bool dedup_is_float = false;    // true for float/double element types

        // ===== NUMERIC ARRAY TYPES =====
        if (auto* float_arr = crate_value.as<std::vector<float>>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          size_t byte_size = float_arr->size() * sizeof(float);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float_arr->data(), byte_size);
        } else if (auto* double_arr = crate_value.as<std::vector<double>>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          size_t byte_size = double_arr->size() * sizeof(double);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), double_arr->data(), byte_size);
        } else if (auto* int_arr = crate_value.as<std::vector<int32_t>>()) {
          is_dedup_candidate = true;
          size_t byte_size = int_arr->size() * sizeof(int32_t);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), int_arr->data(), byte_size);
        } else if (auto* uint_arr = crate_value.as<std::vector<uint32_t>>()) {
          is_dedup_candidate = true;
          size_t byte_size = uint_arr->size() * sizeof(uint32_t);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), uint_arr->data(), byte_size);
        } else if (auto* int64_arr = crate_value.as<std::vector<int64_t>>()) {
          is_dedup_candidate = true;
          size_t byte_size = int64_arr->size() * sizeof(int64_t);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), int64_arr->data(), byte_size);
        } else if (auto* uint64_arr = crate_value.as<std::vector<uint64_t>>()) {
          is_dedup_candidate = true;
          size_t byte_size = uint64_arr->size() * sizeof(uint64_t);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), uint64_arr->data(), byte_size);
        }
        // ===== STRING/TOKEN ARRAY TYPES =====
        else if (auto* string_arr = crate_value.as<std::vector<std::string>>()) {
          is_dedup_candidate = true;
          // Serialize strings: [count][len1][str1][len2][str2]...
          size_t total_size = sizeof(uint64_t); // array count
          for (const auto& str : *string_arr) {
            total_size += sizeof(uint64_t) + str.size(); // length + data
          }
          value_bytes.reserve(total_size);

          // Write count
          uint64_t count = string_arr->size();
          value_bytes.insert(value_bytes.end(),
                            reinterpret_cast<const char*>(&count),
                            reinterpret_cast<const char*>(&count) + sizeof(uint64_t));

          // Write each string with length prefix
          for (const auto& str : *string_arr) {
            uint64_t len = str.size();
            value_bytes.insert(value_bytes.end(),
                              reinterpret_cast<const char*>(&len),
                              reinterpret_cast<const char*>(&len) + sizeof(uint64_t));
            value_bytes.insert(value_bytes.end(), str.begin(), str.end());
          }
        } else if (auto* token_arr = crate_value.as<std::vector<value::token>>()) {
          is_dedup_candidate = true;
          // Serialize tokens: [count][len1][str1][len2][str2]...
          size_t total_size = sizeof(uint64_t);
          for (const auto& tok : *token_arr) {
            total_size += sizeof(uint64_t) + tok.str().size();
          }
          value_bytes.reserve(total_size);

          uint64_t count = token_arr->size();
          value_bytes.insert(value_bytes.end(),
                            reinterpret_cast<const char*>(&count),
                            reinterpret_cast<const char*>(&count) + sizeof(uint64_t));

          for (const auto& tok : *token_arr) {
            uint64_t len = tok.str().size();
            value_bytes.insert(value_bytes.end(),
                              reinterpret_cast<const char*>(&len),
                              reinterpret_cast<const char*>(&len) + sizeof(uint64_t));
            value_bytes.insert(value_bytes.end(), tok.str().begin(), tok.str().end());
          }
        }
        // ===== VECTOR ARRAY TYPES =====
        else if (auto* float3_arr = crate_value.as<std::vector<value::float3>>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          size_t byte_size = float3_arr->size() * sizeof(value::float3);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float3_arr->data(), byte_size);
        } else if (auto* double3_arr = crate_value.as<std::vector<value::double3>>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          size_t byte_size = double3_arr->size() * sizeof(value::double3);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), double3_arr->data(), byte_size);
        } else if (auto* float2_arr = crate_value.as<std::vector<value::float2>>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          size_t byte_size = float2_arr->size() * sizeof(value::float2);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float2_arr->data(), byte_size);
        } else if (auto* float4_arr = crate_value.as<std::vector<value::float4>>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          size_t byte_size = float4_arr->size() * sizeof(value::float4);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float4_arr->data(), byte_size);
        }
        // ===== SCALAR MATRIX TYPES =====
        else if (auto* matrix2d = crate_value.as<value::matrix2d>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          value_bytes.resize(sizeof(value::matrix2d));
          std::memcpy(value_bytes.data(), matrix2d, sizeof(value::matrix2d));
        } else if (auto* matrix3d = crate_value.as<value::matrix3d>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          value_bytes.resize(sizeof(value::matrix3d));
          std::memcpy(value_bytes.data(), matrix3d, sizeof(value::matrix3d));
        } else if (auto* matrix4d = crate_value.as<value::matrix4d>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          value_bytes.resize(sizeof(value::matrix4d));
          std::memcpy(value_bytes.data(), matrix4d, sizeof(value::matrix4d));
        }
        // ===== SCALAR QUATERNION TYPES =====
        else if (auto* quatf = crate_value.as<value::quatf>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          value_bytes.resize(sizeof(value::quatf));
          std::memcpy(value_bytes.data(), quatf, sizeof(value::quatf));
        } else if (auto* quatd = crate_value.as<value::quatd>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          value_bytes.resize(sizeof(value::quatd));
          std::memcpy(value_bytes.data(), quatd, sizeof(value::quatd));
        } else if (auto* quath = crate_value.as<value::quath>()) {
          is_dedup_candidate = true;
          // half is stored as uint16_t, not IEEE float — use raw byte hash
          value_bytes.resize(sizeof(value::quath));
          std::memcpy(value_bytes.data(), quath, sizeof(value::quath));
        }
        // ===== SCALAR VECTOR TYPES =====
        else if (auto* float3 = crate_value.as<value::float3>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          value_bytes.resize(sizeof(value::float3));
          std::memcpy(value_bytes.data(), float3, sizeof(value::float3));
        } else if (auto* double3 = crate_value.as<value::double3>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          value_bytes.resize(sizeof(value::double3));
          std::memcpy(value_bytes.data(), double3, sizeof(value::double3));
        } else if (auto* float2 = crate_value.as<value::float2>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          value_bytes.resize(sizeof(value::float2));
          std::memcpy(value_bytes.data(), float2, sizeof(value::float2));
        } else if (auto* float4 = crate_value.as<value::float4>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          value_bytes.resize(sizeof(value::float4));
          std::memcpy(value_bytes.data(), float4, sizeof(value::float4));
        } else if (auto* double2 = crate_value.as<value::double2>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          value_bytes.resize(sizeof(value::double2));
          std::memcpy(value_bytes.data(), double2, sizeof(value::double2));
        } else if (auto* double4 = crate_value.as<value::double4>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(double); dedup_is_float = true;
          value_bytes.resize(sizeof(value::double4));
          std::memcpy(value_bytes.data(), double4, sizeof(value::double4));
        }

        if (is_dedup_candidate && !value_bytes.empty()) {
          // NaN-aware hash lookup
          size_t h = NanAwareHash::hash_buffer(
              value_bytes.data(), value_bytes.size(),
              dedup_element_size, dedup_is_float);

          int64_t cached_offset = -1;
          auto range = value_dedup_map_.equal_range(h);
          for (auto it = range.first; it != range.second; ++it) {
            const auto &entry = it->second;
            if (entry.bytes.size() == value_bytes.size() &&
                entry.element_size == dedup_element_size &&
                entry.is_float == dedup_is_float &&
                NanAwareHash::buffers_equal(
                    entry.bytes.data(), value_bytes.data(),
                    value_bytes.size(), dedup_element_size, dedup_is_float)) {
              cached_offset = entry.offset;
              break;
            }
          }

          if (cached_offset >= 0) {
            // Found duplicate! Reuse the offset without writing data
            crate::CrateDataTypeId type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID;
            bool is_array = false;

            // Numeric arrays
            if (crate_value.as<std::vector<float>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT;
              is_array = true;
            } else if (crate_value.as<std::vector<double>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE;
              is_array = true;
            } else if (crate_value.as<std::vector<int32_t>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_INT;
              is_array = true;
            } else if (crate_value.as<std::vector<uint32_t>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT;
              is_array = true;
            } else if (crate_value.as<std::vector<int64_t>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64;
              is_array = true;
            } else if (crate_value.as<std::vector<uint64_t>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64;
              is_array = true;
            }
            // String/token arrays
            else if (crate_value.as<std::vector<std::string>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING;
              is_array = true;
            } else if (crate_value.as<std::vector<value::token>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN;
              is_array = true;
            }
            // Vector arrays
            else if (crate_value.as<std::vector<value::float3>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F;
              is_array = true;
            } else if (crate_value.as<std::vector<value::double3>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D;
              is_array = true;
            } else if (crate_value.as<std::vector<value::float2>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F;
              is_array = true;
            } else if (crate_value.as<std::vector<value::float4>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F;
              is_array = true;
            }
            // Scalar matrix types
            else if (crate_value.as<value::matrix2d>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D;
              is_array = false;
            } else if (crate_value.as<value::matrix3d>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D;
              is_array = false;
            } else if (crate_value.as<value::matrix4d>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D;
              is_array = false;
            }
            // Scalar quaternion types
            else if (crate_value.as<value::quatf>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF;
              is_array = false;
            } else if (crate_value.as<value::quatd>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD;
              is_array = false;
            } else if (crate_value.as<value::quath>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH;
              is_array = false;
            }
            // Scalar vector types
            else if (crate_value.as<value::float3>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F;
              is_array = false;
            } else if (crate_value.as<value::double3>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D;
              is_array = false;
            } else if (crate_value.as<value::float2>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F;
              is_array = false;
            } else if (crate_value.as<value::float4>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F;
              is_array = false;
            } else if (crate_value.as<value::double2>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D;
              is_array = false;
            } else if (crate_value.as<value::double4>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D;
              is_array = false;
            }

            value_rep.SetType(static_cast<int32_t>(type_id));
            if (is_array) {
              value_rep.SetIsArray();
            }
            value_rep.SetPayload(static_cast<uint64_t>(cached_offset));
            dedup_attempted = true;

            std::cerr << "[TimeSamples Dedup] Reused value at offset " << cached_offset
                      << " for sample " << i << " (" << value_bytes.size() << " bytes saved)\n";
          } else {
            // New value - pack normally and cache the offset
            value_rep = PackValue(crate_value, err);
            if (err && !err->empty()) {
              return -1;
            }
            int64_t new_offset = static_cast<int64_t>(value_rep.GetPayload());
            value_dedup_map_.emplace(h, ValueDedupEntry{
                std::move(value_bytes), dedup_element_size,
                dedup_is_float, new_offset});
            dedup_attempted = true;

            std::cerr << "[TimeSamples Dedup] Cached new value at offset " << new_offset
                      << " for sample " << i << "\n";
          }
        }
      }

      // If deduplication wasn't attempted, pack normally
      if (!dedup_attempted) {
        value_rep = PackValue(crate_value, err);
        if (err && !err->empty()) {
          return -1;
        }
      }

      // Write the ValueRep
      uint64_t rep_data = value_rep.GetData();
      if (!Write(rep_data)) {
        if (err) *err = "Failed to write TimeSamples ValueRep at index " + std::to_string(i);
        return -1;
      }
    }
  }
  // TODO: Add IntListOp, UIntListOp, Int64ListOp, UInt64ListOp, etc.
  else {
    // Unsupported type for out-of-line storage
    if (err) *err = "Unsupported value type for out-of-line storage";
    return -1;
  }

  // Update value data end offset
  value_data_end_offset_ = Tell();

  // Seek back to where we were
  if (!Seek(current_pos)) {
    if (err) *err = "Failed to seek back after writing value";
    return -1;
  }

  return value_offset;
}

bool CrateWriter::TryInlineValue(const crate::CrateValue& value, crate::ValueRep* rep) {
  // Phase 1: String/Token/AssetPath values
  // Strings and tokens are always inlined as indices in USDC format

  // Try to get as token
  if (auto* token_val = value.as<value::token>()) {
    crate::TokenIndex idx = GetOrCreateToken(token_val->str());
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Try to get as string
  if (auto* str_val = value.as<std::string>()) {
    crate::StringIndex idx = GetOrCreateString(*str_val);
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Try to get as AssetPath
  if (auto* asset_val = value.as<value::AssetPath>()) {
    // AssetPath is stored as a string index
    crate::StringIndex idx = GetOrCreateString(asset_val->GetAssetPath());
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(idx.value));
    return true;
  }

  // Basic scalar types

  // Try to get as int32
  if (auto* int_val = value.as<int32_t>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*int_val));
    return true;
  }

  // Try to get as uint32
  if (auto* uint_val = value.as<uint32_t>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*uint_val));
    return true;
  }

  // Try to get as Specifier enum
  if (auto* spec_val = value.as<Specifier>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_SPECIFIER));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*spec_val));
    return true;
  }

  // Try to get as Permission enum
  if (auto* perm_val = value.as<Permission>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PERMISSION));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*perm_val));
    return true;
  }

  // Try to get as Variability enum
  if (auto* var_val = value.as<Variability>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VARIABILITY));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*var_val));
    return true;
  }

  // Try to get as int64
  if (auto* int64_val = value.as<int64_t>()) {
    // int64 cannot be inlined if value doesn't fit in 48 bits
    // (48 bits is the payload size in ValueRep)
    if (*int64_val >= -(1LL << 47) && *int64_val < (1LL << 47)) {
      rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64));
      rep->SetIsInlined();
      rep->SetPayload(static_cast<uint64_t>(*int64_val));
      return true;
    }
    // Falls through to out-of-line storage
  }

  // Try to get as uint64
  if (auto* uint64_val = value.as<uint64_t>()) {
    // uint64 can only be inlined if value fits in 48 bits
    if (*uint64_val < (1ULL << 48)) {
      rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64));
      rep->SetIsInlined();
      rep->SetPayload(*uint64_val);
      return true;
    }
    // Falls through to out-of-line storage
  }

  // Try to get as float
  if (auto* float_val = value.as<float>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT));
    rep->SetIsInlined();
    // Copy float bits to uint32, then to uint64
    uint32_t float_bits;
    memcpy(&float_bits, float_val, sizeof(float));
    rep->SetPayload(static_cast<uint64_t>(float_bits));
    return true;
  }

  // Try to get as double
  if (auto* double_val = value.as<double>()) {
    // Double cannot be inlined (64 bits > 48 bit payload)
    // Falls through to out-of-line storage
    return false;
  }

  // Try to get as half
  if (auto* half_val = value.as<value::half>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF));
    rep->SetIsInlined();
    // half is 16 bits, fits easily in payload
    uint16_t half_bits = half_val->value;
    rep->SetPayload(static_cast<uint64_t>(half_bits));
    return true;
  }

  // Try to get as bool
  if (auto* bool_val = value.as<bool>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL));
    rep->SetIsInlined();
    rep->SetPayload(*bool_val ? 1 : 0);
    return true;
  }

  // Try to get as uchar
  if (auto* uchar_val = value.as<uint8_t>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR));
    rep->SetIsInlined();
    rep->SetPayload(static_cast<uint64_t>(*uchar_val));
    return true;
  }

  // Phase 1: Vector types
  // Vectors can be inlined if they fit in 48 bits (6 bytes)
  // Vec2h (4 bytes), Vec2f/Vec2i (8 bytes) cannot be inlined
  // Vec3/Vec4 cannot be inlined (12+ bytes)

  // Vec2h - half2 (4 bytes, can inline)
  if (auto* vec2h_val = value.as<value::half2>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H));
    rep->SetIsInlined();
    // Pack two 16-bit halfs into 32 bits
    uint32_t packed = (uint32_t((*vec2h_val)[0].value) << 16) | uint32_t((*vec2h_val)[1].value);
    rep->SetPayload(static_cast<uint64_t>(packed));
    return true;
  }

  // Vec2f - float2 (8 bytes, cannot inline)
  if (auto* vec2f_val = value.as<value::float2>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec2d - double2 (16 bytes, cannot inline)
  if (auto* vec2d_val = value.as<value::double2>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec2i - int2 (8 bytes, cannot inline)
  if (auto* vec2i_val = value.as<value::int2>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec3h - half3 (6 bytes, can inline!)
  if (auto* vec3h_val = value.as<value::half3>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H));
    rep->SetIsInlined();
    // Pack three 16-bit halfs into 48 bits
    uint64_t packed = (uint64_t((*vec3h_val)[0].value) << 32) |
                      (uint64_t((*vec3h_val)[1].value) << 16) |
                       uint64_t((*vec3h_val)[2].value);
    rep->SetPayload(packed);
    return true;
  }

  // Vec3f - float3 (12 bytes, cannot inline)
  if (auto* vec3f_val = value.as<value::float3>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec3d - double3 (24 bytes, cannot inline)
  if (auto* vec3d_val = value.as<value::double3>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec3i - int3 (12 bytes, cannot inline)
  if (auto* vec3i_val = value.as<value::int3>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4h - half4 (8 bytes, cannot inline)
  if (auto* vec4h_val = value.as<value::half4>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4f - float4 (16 bytes, cannot inline)
  if (auto* vec4f_val = value.as<value::float4>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4d - double4 (32 bytes, cannot inline)
  if (auto* vec4d_val = value.as<value::double4>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4i - int4 (16 bytes, cannot inline)
  if (auto* vec4i_val = value.as<value::int4>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Phase 1: Matrix types - all matrices are too large to inline
  // Matrix2d (4x8 = 32 bytes), Matrix3d (9x8 = 72 bytes), Matrix4d (16x8 = 128 bytes)

  if (auto* mat2d_val = value.as<value::matrix2d>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* mat3d_val = value.as<value::matrix3d>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* mat4d_val = value.as<value::matrix4d>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Phase 1: Quaternion types
  // Quath (4x2 = 8 bytes), Quatf (4x4 = 16 bytes), Quatd (4x8 = 32 bytes)
  // All too large to inline (> 6 bytes)

  if (auto* quath_val = value.as<value::quath>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* quatf_val = value.as<value::quatf>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* quatd_val = value.as<value::quatd>()) {
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Phase 2: Dictionary, ListOps, Reference, and Payload are NEVER inlined - always out-of-line storage
  if (value.as<value::dict>()) {
    return false;
  }

  if (value.as<ListOp<value::token>>() ||
      value.as<ListOp<std::string>>() ||
      value.as<ListOp<Path>>() ||
      value.as<ListOp<Reference>>() ||
      value.as<ListOp<Payload>>()) {
    return false;
  }

  if (value.as<Reference>() || value.as<Payload>() || value.as<VariantSelectionMap>()) {
    return false;
  }

  // Phase 1: Arrays are NEVER inlined - always out-of-line storage
  // Arrays require size prefix + data

  // Check for all array types (std::vector<T>)
  // We detect arrays by trying all known array types
  if (value.as<std::vector<bool>>() ||
      value.as<std::vector<uint8_t>>() ||
      value.as<std::vector<int32_t>>() ||
      value.as<std::vector<uint32_t>>() ||
      value.as<std::vector<int64_t>>() ||
      value.as<std::vector<uint64_t>>() ||
      value.as<std::vector<value::half>>() ||
      value.as<std::vector<float>>() ||
      value.as<std::vector<double>>() ||
      value.as<std::vector<value::half2>>() ||
      value.as<std::vector<value::half3>>() ||
      value.as<std::vector<value::half4>>() ||
      value.as<std::vector<value::float2>>() ||
      value.as<std::vector<value::float3>>() ||
      value.as<std::vector<value::float4>>() ||
      value.as<std::vector<value::double2>>() ||
      value.as<std::vector<value::double3>>() ||
      value.as<std::vector<value::double4>>() ||
      value.as<std::vector<value::int2>>() ||
      value.as<std::vector<value::int3>>() ||
      value.as<std::vector<value::int4>>() ||
      value.as<std::vector<value::matrix2d>>() ||
      value.as<std::vector<value::matrix3d>>() ||
      value.as<std::vector<value::matrix4d>>() ||
      value.as<std::vector<value::quath>>() ||
      value.as<std::vector<value::quatf>>() ||
      value.as<std::vector<value::quatd>>() ||
      value.as<std::vector<std::string>>() ||
      value.as<std::vector<value::token>>() ||
      value.as<std::vector<value::AssetPath>>()) {
    // Arrays cannot be inlined - need out-of-line storage
    return false;
  }

  // Cannot inline - need out-of-line storage
  return false;
}

// ============================================================================
// Deduplication
// ============================================================================

crate::TokenIndex CrateWriter::GetOrCreateToken(const std::string& token) {
  auto it = token_to_index_.find(token);
  if (it != token_to_index_.end()) {
    return it->second;
  }

  // Create new token
  crate::TokenIndex idx(static_cast<uint32_t>(tokens_.size()));
  tokens_.push_back(token);
  token_to_index_[token] = idx;
  return idx;
}

crate::StringIndex CrateWriter::GetOrCreateString(const std::string& str) {
  auto it = string_to_index_.find(str);
  if (it != string_to_index_.end()) {
    return it->second;
  }

  // Strings map to tokens, so ensure the token exists
  GetOrCreateToken(str);

  // Create new string
  crate::StringIndex idx(static_cast<uint32_t>(strings_.size()));
  strings_.push_back(str);
  string_to_index_[str] = idx;
  return idx;
}

crate::PathIndex CrateWriter::GetOrCreatePath(const Path& path) {
  auto it = path_to_index_.find(path);
  if (it != path_to_index_.end()) {
    return it->second;
  }

  // Create new path
  crate::PathIndex idx(static_cast<uint32_t>(paths_.size()));
  paths_.push_back(path);
  path_to_index_[path] = idx;

  // Also register path tokens
  if (!path.prim_part().empty()) {
    // Split prim part into elements and register each
    std::string prim = path.prim_part();
    size_t pos = 0;
    while (pos < prim.size()) {
      size_t next = prim.find('/', pos + 1);
      if (next == std::string::npos) {
        next = prim.size();
      }
      std::string element = prim.substr(pos + 1, next - pos - 1);
      if (!element.empty()) {
        GetOrCreateToken(element);
      }
      pos = next;
    }
  }

  if (!path.prop_part().empty()) {
    GetOrCreateToken(path.prop_part());
  }

  return idx;
}

crate::FieldIndex CrateWriter::GetOrCreateField(const crate::Field& field) {
  auto it = field_to_index_.find(field);
  if (it != field_to_index_.end()) {
    return it->second;
  }

  // Create new field
  crate::FieldIndex idx(static_cast<uint32_t>(fields_.size()));
  fields_.push_back(field);
  field_to_index_[field] = idx;
  return idx;
}

crate::FieldSetIndex CrateWriter::GetOrCreateFieldSet(const std::vector<crate::FieldIndex>& fieldset) {
  auto it = fieldset_to_index_.find(fieldset);
  if (it != fieldset_to_index_.end()) {
    return it->second;
  }

  // Create new fieldset
  crate::FieldSetIndex idx(static_cast<uint32_t>(fieldsets_.size()));
  fieldsets_.push_back(fieldset);
  fieldset_to_index_[fieldset] = idx;
  return idx;
}

// ============================================================================
// I/O Utilities
// ============================================================================

int64_t CrateWriter::Tell() {
  return static_cast<int64_t>(file_.tellp());
}

bool CrateWriter::Seek(int64_t pos) {
  file_.seekp(pos, std::ios::beg);
  return file_.good();
}

bool CrateWriter::WriteBytes(const void* data, size_t size) {
  file_.write(static_cast<const char*>(data), size);
  return file_.good();
}

} // namespace experimental
} // namespace tinyusdz
