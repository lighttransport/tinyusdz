// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer Implementation
#include "crate-writer.hh"

#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>
#include <unordered_map>

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
#include "external/xxhash.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// Phase 4: Compression support
#include "lz4-compression.hh"
#include "integerCoding.h"
// Direct LZ4 API for OpenUSD compatibility
#include "lz4/lz4.h"

#include "safe-arithmetic.hh"

// Namespace alias to avoid collision between tinyusdz::crate and ::crate (path library)
namespace pathlib = ::crate;

// Disable specific clang warnings for this file
// - shadow: if-else chains reuse variable names intentionally
// - sign-conversion: safe narrowing in serialization code
// - old-style-cast: debug print formatting
// - exceptions: comparator functions may throw in debug builds
// - unused-parameter: some functions have consistent API signatures
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wexceptions"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

namespace tinyusdz {
namespace experimental {

// Out-of-line virtual destructors to anchor vtables in this TU.
IOutputStream::~IOutputStream() = default;
MemoryOutputStream::~MemoryOutputStream() = default;

namespace {

// Magic identifier for USDC files
constexpr char kMagicIdent[] = "PXR-USDC";

/// FileOutputStream — writes to disk via std::fstream
class FileOutputStream : public IOutputStream {
public:
  explicit FileOutputStream(const std::string& filepath) : filepath_(filepath) {}
  bool Open(std::string* err) override {
    file_.open(filepath_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
      if (err) *err = "Failed to open file: " + filepath_;
      return false;
    }
    return true;
  }
  void Close() override { if (file_.is_open()) { file_.flush(); file_.close(); } }
  bool IsOpen() const override { return file_.is_open(); }
  int64_t Tell() override { return static_cast<int64_t>(file_.tellp()); }
  bool Seek(int64_t pos) override { file_.seekp(pos, std::ios::beg); return file_.good(); }
  bool Write(const void* data, size_t size) override {
    file_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return file_.good();
  }
  bool Flush() override { file_.flush(); return file_.good(); }
private:
  std::string filepath_;
  std::fstream file_;
};

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
    : filepath_(filepath),
      stream_(std::unique_ptr<IOutputStream>(new FileOutputStream(filepath))) {
}

CrateWriter::CrateWriter(std::unique_ptr<IOutputStream> stream)
    : stream_(std::move(stream)) {
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
  // Guard the copy: an empty float/double array reaches here with
  // byte_count==0 (and possibly-null data/canon pointers); memcpy(null,null,0)
  // is UB-by-the-letter / flagged by UBSan's nonnull check. XXH3 of 0 bytes
  // below is already safe.
  if (byte_count) std::memcpy(canon.data(), data, byte_count);

  if (element_size == sizeof(float)) {
    size_t count = byte_count / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
      float v;
      size_t offset;
      if (!safe::mul(i, sizeof(float), &offset)) {
        return 0;  // Error - return 0 hash
      }
      std::memcpy(&v, canon.data() + offset, sizeof(float));
      if (v == 0.0f) {
        uint32_t zero = 0;
        std::memcpy(canon.data() + offset, &zero, sizeof(float));
      }
    }
  } else { // sizeof(double)
    size_t count = byte_count / sizeof(double);
    for (size_t i = 0; i < count; ++i) {
      double v;
      size_t offset;
      if (!safe::mul(i, sizeof(double), &offset)) {
        return 0;  // Error - return 0 hash
      }
      std::memcpy(&v, canon.data() + offset, sizeof(double));
      if (v == 0.0) {
        uint64_t zero = 0;
        std::memcpy(canon.data() + offset, &zero, sizeof(double));
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
      size_t offset;
      if (!safe::mul(i, sizeof(float), &offset)) {
        return false;  // Overflow - buffers aren't equal
      }
      std::memcpy(&va, pa + offset, sizeof(float));
      std::memcpy(&vb, pb + offset, sizeof(float));
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
      size_t offset;
      if (!safe::mul(i, sizeof(double), &offset)) {
        return false;  // Overflow - buffers aren't equal
      }
      std::memcpy(&va, pa + offset, sizeof(double));
      std::memcpy(&vb, pb + offset, sizeof(double));
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

  // Open the output stream
  if (!stream_) {
    if (err) *err = "No output stream configured";
    return false;
  }
  if (!stream_->Open(err)) {
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

  // Reserve token index 0 with a sentinel that cannot be a valid path
  // element. OpenUSD does the same (crateFile.cpp line 2594-2601, github
  // issue #811). The compressed path format uses negative token indices
  // for property path elements; -0 == 0 would otherwise make a property
  // at index 0 indistinguishable from a prim element, so ";-)" must sit
  // at index 0 before any caller has a chance to register a real token.
  // Previously this was deferred to Finalize(), but by then AddSpec had
  // already pushed field-name tokens into tokens_ at index 0.
  GetOrCreateToken(";-)");

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

  // Check for duplicate specs with same path (USD Crate requires each path to
  // appear only once). O(1) via spec_path_set_ — a prior implementation did an
  // O(n^2) linear scan of spec_data_, each step allocating two full_path_name()
  // strings, which dominated write time for spec-dense scenes.
  if (spec_path_set_.count(path)) {
    return true;  // Silently skip duplicate (not an error)
  }

  // Create spec data
  SpecData spec_data;
  spec_data.path = path;
  spec_data.spec_type = spec_type;  // Store the spec type
  spec_data.fields = fields;

  // Estimate memory usage for this spec
  // Path + field names + approximate field value sizes
  int64_t estimated_memory = path.full_path_name().size();  // Path string
  for (const auto& field : fields) {
    estimated_memory += field.first.size();  // Field name
    estimated_memory += 64;  // Approximate field value overhead
  }

  // Check memory limit
  if (WouldExceedMemoryLimit(estimated_memory)) {
    if (err) {
      *err = "Adding spec would exceed memory limit of " +
             std::to_string(options_.max_memory_bytes / (1024*1024)) + " MB. " +
             "Current usage: " + std::to_string(memory_used_estimate_ / (1024*1024)) + " MB";
    }
    return false;
  }

  // We'll fill in the actual crate::Spec later during Finalize
  // For now, just accumulate the data
  spec_data_.push_back(spec_data);
  spec_path_set_.emplace(path, 1u);
  memory_used_estimate_ += estimated_memory;

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

  // Reserve token index 0 with a sentinel that can't be a valid path element.
  // OpenUSD does the same (crateFile.cpp line 2594-2601, github issue #811).
  // The compressed path format uses negative token indices for property path
  // elements.  Because -0 == 0, a property at index 0 would be misread as a
  // prim element.  Inserting ";-)" here before any other token ensures no real
  // element gets index 0.
  if (tokens_.empty()) {
    GetOrCreateToken(";-)");
  }

  // Phase 5: Sort specs for better compression and correct hierarchy
  // CRITICAL: Sort specs using the same USD path comparison algorithm
  // that will be used in WritePathsSection (SortSimplePaths).
  // This ensures path indices assigned here match the path tree encoding.
  //
  // PseudoRoot ("/") MUST be first (required by USD spec)
  std::sort(spec_data_.begin(), spec_data_.end(),
    [](const SpecData& a, const SpecData& b) {
      // Use the same path comparison as pathlib::SortSimplePaths
      pathlib::SimplePath a_path(a.path.prim_part(), a.path.prop_part());
      pathlib::SimplePath b_path(b.path.prim_part(), b.path.prop_part());
      return pathlib::ComparePaths(a_path, b_path) < 0;
    });

  // Verify that the first spec is PseudoRoot (required by USD spec)
  if (!spec_data_.empty()) {
    const auto& first_spec = spec_data_[0];
    bool is_pseudoroot = (first_spec.spec_type == SpecType::PseudoRoot ||
                          (first_spec.path.prim_part() == "/" && first_spec.path.prop_part().empty()));

    if (!is_pseudoroot) {
      if (err) {
        *err = "First spec must be PseudoRoot (path '/'), but got: " +
               first_spec.path.prim_part() +
               (first_spec.path.prop_part().empty() ? "" : "." + first_spec.path.prop_part());
      }
      return false;
    }
  }

  // CRITICAL: Rebuild path deduplication table to match sorted order
  // Path indices must correspond to the sorted spec order
  path_to_index_.clear();
  paths_.clear();
  for (const auto& spec_data : spec_data_) {
    if (path_to_index_.find(spec_data.path) == path_to_index_.end()) {
      crate::PathIndex idx;
      idx.value = static_cast<uint32_t>(paths_.size());
      path_to_index_[spec_data.path] = idx;
      paths_.push_back(spec_data.path);
    }
  }

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

      // USD metadata fields `primChildren` and `properties` store a list of
      // child/property names. On the wire, pxrusd expects these as the
      // dedicated `TokenVector` type (CrateDataTypeId 41), not as a
      // `Token[]` array (CrateDataTypeId 11 with IsArray). The serialized
      // bytes are identical — uint64 count followed by uint32 token
      // indices — so we just retag the ValueRep after PackValue emitted
      // it as Token[]. Without this, pxrusd loads the layer but silently
      // drops every prim because its primChildren field fails type
      // validation, and we ship USDC that downstream DCCs can't read.
      const std::string& fname = field_pair.first;
      if ((fname == "primChildren" || fname == "properties") &&
          field_pair.second.as<std::vector<value::token>>() &&
          field.value_rep.IsArray() &&
          field.value_rep.GetType() ==
              static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN)) {
        uint64_t data = field.value_rep.GetData();
        data &= ~crate::ValueRep::IsArrayBit_;
        field.value_rep = crate::ValueRep(data);
        field.value_rep.SetType(static_cast<int32_t>(
            crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_VECTOR));
      }

      // Get or create field index
      crate::FieldIndex field_idx = GetOrCreateField(field);
      field_indices.push_back(field_idx);
    }

    // Get or create fieldset
    crate::FieldSetIndex fieldset_idx = GetOrCreateFieldSet(field_indices);

    // Temporarily assign path_index (will be updated after sorting)
    crate::PathIndex path_idx = GetOrCreatePath(spec_data.path);
    spec_data.spec.path_index = path_idx;
    spec_data.spec.fieldset_index = fieldset_idx;
    spec_data.spec.spec_type = spec_data.spec_type;  // Use the stored spec type
  }

  // ========================================================================
  // Path-index remap after field packing
  // ========================================================================
  // During field packing, ListOp<Reference> / ListOp<Payload> /
  // ListOp<Path> / connection-target / pathvector value data wrote raw
  // PathIndex bytes referencing positions in paths_ at the time of the
  // write. Historically we re-sorted paths_ here for tree-encoding ordering
  // and remapped only the spec path_indexes — that left those embedded
  // value-data indices pointing at the wrong paths after the sort, so e.g.
  // `references = @./a.usda@</A>` came back as `</x>` (whatever happened
  // to land at the original index after re-sorting).
  //
  // Fix: skip the re-sort. WritePathsSection sorts paths internally for
  // tree encoding and stores `encoded_path_indices[i] = preassigned`, so
  // the on-disk tree still maps correctly to the original PathIndex values
  // — and value-data path indices stay valid.

  // ========================================================================
  // Step 2: Write all structural sections
  // ========================================================================

  // Note: value_data_end_offset_ is already updated by WriteValueData()
  // during PackValue() calls above. Do NOT reset it here.

  // ========================================================================
  // Pre-register path element tokens before writing TOKENS section.
  // WritePathsSection uses GetOrCreateToken during tree building.
  // WritePathsSection rewrites the root path's element name ("/") to the
  // empty string before tokenizing, so we must ensure "" is in the pool;
  // otherwise the root row references a token that only gets appended
  // after the TOKENS section has already been serialized — which pxrusd
  // rejects with "Corrupt path element token index in crate file".
  GetOrCreateToken("");
  for (const auto& path : paths_) {
    std::string elem = path.element_name();
    if (!elem.empty() && elem != "/") {
      GetOrCreateToken(elem);
    }
    if (!path.prop_part().empty()) {
      GetOrCreateToken(path.prop_part());
    }
  }

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
  if (stream_ && stream_->IsOpen()) {
    stream_->Flush();
    stream_->Close();
  }
  is_open_ = false;
}

// ============================================================================
// TimeSamples Value Conversion (Phase 5)
// ============================================================================


// ============================================================================
// Array Deduplication (Phase 5)
// ============================================================================

/// Helper to serialize array to bytes for deduplication
template<typename T>
std::vector<char> SerializeArrayToBytes(const std::vector<T>& arr) {
  std::vector<char> bytes;
  size_t total_size;
  if (!safe::mul(arr.size(), sizeof(T), &total_size)) {
    return {};  // overflow
  }
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

  if (compressedSize <= 0) {
    if (err) *err = "LZ4 compression failed with error code: " + std::to_string(compressedSize);
    return false;
  }

  // Resize to actual size: 1 byte chunk count + compressed data
  compressed->resize(1 + static_cast<size_t>(compressedSize));

  return true;
}

// ============================================================================
// Section Writing
// ============================================================================

bool CrateWriter::WriteTokensSection(std::string* err) {
  int64_t section_start = Tell();

  // Write token count
  uint64_t token_count = static_cast<uint64_t>(tokens_.size());

  // Write directly as bytes instead of using Write() template
  if (!stream_->Write(reinterpret_cast<const char*>(&token_count), sizeof(token_count))) {
    if (err) *err = "Failed to write token count bytes";
    return false;
  }
  stream_->Flush();

  // Build token blob (null-terminated strings)
  std::ostringstream blob;
  for (const auto& token : tokens_) {
    blob << token;
    blob.put('\0');
  }

  std::string token_blob = blob.str();

  // Phase 4: Compress the blob if compression is enabled
  std::vector<char> compressed_blob;
  if (!CompressData(token_blob.data(), token_blob.size(), &compressed_blob, err)) {
    if (err) *err = "Failed to compress token blob: " + *err;
    return false;
  }


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
  size_t reps_data_size;
  if (!safe::mul(value_reps.size(), sizeof(uint64_t), &reps_data_size)) {
    if (err) *err = "Integer overflow: value_reps.size() * sizeof(uint64_t)";
    return false;
  }

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


  // Flatten all fieldsets into a single array with terminators
  std::vector<uint32_t> fieldset_vals;
  for (size_t i = 0; i < fieldsets_.size(); ++i) {
    const auto& fieldset = fieldsets_[i];
    for (const auto& field_idx : fieldset) {
      fieldset_vals.push_back(field_idx.value);
    }
    // Write terminator (default FieldIndex() has value ~0u)
    fieldset_vals.push_back(~0u);
  }


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

  // Build sorted paths with pre-assigned PathIndex values.
  // This matches OpenUSD's approach: each path has a pre-assigned index into
  // the _paths vector. The tree encoding references these indices directly.
  std::vector<std::pair<Path, crate::PathIndex>> sorted_paths;
  for (const auto& kv : path_to_index_) {
    const Path& path = kv.first;
    // Skip empty/invalid paths
    if (path.prim_part().empty() && path.prop_part().empty()) continue;
    sorted_paths.emplace_back(path, kv.second);
  }

  // Sort using Path::operator< (lexicographic USD path comparison)
  std::sort(sorted_paths.begin(), sorted_paths.end(),
    [](const std::pair<Path, crate::PathIndex>& a,
       const std::pair<Path, crate::PathIndex>& b) {
      return a.first < b.first;
    });

  size_t num_encoded_paths = sorted_paths.size();
  if (num_encoded_paths == 0) {
    if (err) *err = "No paths to encode";
    return false;
  }

  // Build the three compressed arrays directly from sorted paths
  // (matches OpenUSD's _BuildCompressedPathDataRecursive)
  std::vector<uint32_t> encoded_path_indices(num_encoded_paths);
  std::vector<int32_t> element_token_indices(num_encoded_paths);
  std::vector<int32_t> jump_indices(num_encoded_paths);

  // Fill with invalid sentinel
  for (auto& idx : encoded_path_indices) idx = crate::PathIndex().value;

  // Recursive tree builder (matches OpenUSD's algorithm)
  // Build path tree recursively with depth guard.
  // Matches OpenUSD's _BuildCompressedPathDataRecursive algorithm.
  auto getNextSubtree = [&](uint32_t sidx, uint32_t eidx) -> uint32_t {
    if (sidx >= eidx) return eidx;
    for (uint32_t i = sidx; i < eidx; i++) {
      if (!sorted_paths[i].first.has_prefix(sorted_paths[sidx].first))
        return i;
    }
    return eidx;
  };

  // Stack-overflow backstop for the recursive builder. ConvertPrimIterative is
  // the authoritative depth gate (rejects prim nesting > kMaxPrimNestingDepth
  // with a clear message before this pass runs); allow a small margin here for
  // the extra path level that prim-property / root paths add, so this backstop
  // is never the first thing a too-deep stage hits.
  constexpr uint32_t kMaxPathTreeDepth = kMaxPrimNestingDepth + 8;

  std::function<bool(uint32_t&, uint32_t, uint32_t, uint32_t, uint32_t&)>
  buildPathTree = [&](uint32_t& currentIdx, uint32_t startIdx, uint32_t endIdx,
                      uint32_t depth, uint32_t& nextIdxOut) -> bool {
    if (depth > kMaxPathTreeDepth) {
      if (err) *err = "Path tree too deep (>" + std::to_string(kMaxPathTreeDepth) + " levels)";
      return false;
    }
    if (currentIdx >= num_encoded_paths || startIdx > endIdx) return false;

    for (uint32_t pIdx = startIdx, nextIdx = pIdx; pIdx < endIdx; pIdx = nextIdx) {
      uint32_t nextSubtreeIdx = getNextSubtree(pIdx, endIdx);
      nextIdx = pIdx + 1;

      bool has_child = false;
      bool has_sibling = false;

      if (nextIdx != nextSubtreeIdx && nextIdx < num_encoded_paths) {
        if (sorted_paths[pIdx].first.is_root_path()) {
          has_child = true;
        } else if (sorted_paths[nextIdx].first.get_parent_path().full_path_name() ==
                   sorted_paths[pIdx].first.full_path_name()) {
          has_child = true;
        }
      }

      if (nextSubtreeIdx != endIdx && nextSubtreeIdx < num_encoded_paths) {
        if (!sorted_paths[pIdx].first.is_root_path() &&
            sorted_paths[nextSubtreeIdx].first.get_parent_path().full_path_name() ==
            sorted_paths[pIdx].first.get_parent_path().full_path_name()) {
          has_sibling = true;
        }
      }

      const auto& p = sorted_paths[pIdx];
      bool is_prop = p.first.is_prim_property_path();
      std::string elem = is_prop ? p.first.prop_part() : p.first.element_name();
      if (elem == "/") elem.clear();

      uint32_t thisIdx = currentIdx++;
      encoded_path_indices[thisIdx] = p.second.value;
      element_token_indices[thisIdx] =
          static_cast<int32_t>(GetOrCreateToken(elem).value);
      if (is_prop) {
        element_token_indices[thisIdx] = -element_token_indices[thisIdx];
      }

      if (has_child) {
        uint32_t childNextOut = 0;
        if (!buildPathTree(currentIdx, nextIdx, endIdx, depth + 1, childNextOut))
          return false;
        nextIdx = childNextOut;
      }

      if (has_sibling && has_child) {
        jump_indices[thisIdx] = static_cast<int32_t>(currentIdx - thisIdx);
      } else if (has_sibling) {
        jump_indices[thisIdx] = 0;
      } else if (has_child) {
        jump_indices[thisIdx] = -1;
      } else {
        jump_indices[thisIdx] = -2;
      }

      if (!has_sibling) {
        nextIdxOut = nextIdx;
        return true;
      }
    }

    nextIdxOut = endIdx;
    return true;
  };

  {
    uint32_t currentIdx = 0;
    uint32_t nextIdx = 0;
    if (!buildPathTree(currentIdx, 0, static_cast<uint32_t>(num_encoded_paths), 0, nextIdx)) {
      if (err) *err = "Failed to build path indices from sorted paths";
      return false;
    }
  }

  // Verify all indices were filled
  for (size_t i = 0; i < encoded_path_indices.size(); i++) {
    if (encoded_path_indices[i] == crate::PathIndex().value) {
      // Dump sorted paths for debugging
      std::string dbg = "path index " + std::to_string(i) + " not filled. Sorted paths:\n";
      for (size_t j = 0; j < sorted_paths.size(); j++) {
        dbg += "  [" + std::to_string(j) + "] " + sorted_paths[j].first.full_path_name()
             + " idx=" + std::to_string(sorted_paths[j].second.value)
             + (j == i ? " <-- UNFILLED" : "") + "\n";
      }
      if (err) *err = "Internal error: " + dbg;
      return false;
    }
  }

  // Write PATHS section:
  // 1. uint64_t numPaths (total paths — reader allocates _paths of this size)
  uint64_t num_paths = static_cast<uint64_t>(paths_.size());
  if (!Write(num_paths)) {
    if (err) *err = "Failed to write numPaths";
    return false;
  }

  // 2. uint64_t numEncodedPaths (may be <= numPaths; excludes empty/inactive)
  uint64_t num_enc = static_cast<uint64_t>(num_encoded_paths);
  if (!Write(num_enc)) {
    if (err) *err = "Failed to write numEncodedPaths";
    return false;
  }

  // 3. Compressed pathIndexes
  {
    size_t buf_size = Usd_IntegerCompression::GetCompressedBufferSize(num_encoded_paths);
    std::vector<char> comp(buf_size);
    std::string cerr;
    size_t csz = Usd_IntegerCompression::CompressToBuffer(
        encoded_path_indices.data(), num_encoded_paths, comp.data(), &cerr);
    if (csz == 0) {
      if (err) *err = "Compress pathIndexes failed: " + cerr;
      return false;
    }
    uint64_t sz = static_cast<uint64_t>(csz);
    if (!Write(sz) || !WriteBytes(comp.data(), csz)) {
      if (err) *err = "Failed to write pathIndexes";
      return false;
    }
  }

  // 4. Compressed elementTokenIndexes
  {
    size_t buf_size = Usd_IntegerCompression::GetCompressedBufferSize(num_encoded_paths);
    std::vector<char> comp(buf_size);
    std::string cerr;
    size_t csz = Usd_IntegerCompression::CompressToBuffer(
        element_token_indices.data(), num_encoded_paths, comp.data(), &cerr);
    if (csz == 0) {
      if (err) *err = "Compress elementTokenIndexes failed: " + cerr;
      return false;
    }
    uint64_t sz = static_cast<uint64_t>(csz);
    if (!Write(sz) || !WriteBytes(comp.data(), csz)) {
      if (err) *err = "Failed to write elementTokenIndexes";
      return false;
    }
  }

  // 5. Compressed jumps
  {
    size_t buf_size = Usd_IntegerCompression::GetCompressedBufferSize(num_encoded_paths);
    std::vector<char> comp(buf_size);
    std::string cerr;
    size_t csz = Usd_IntegerCompression::CompressToBuffer(
        jump_indices.data(), num_encoded_paths, comp.data(), &cerr);
    if (csz == 0) {
      if (err) *err = "Compress jumps failed: " + cerr;
      return false;
    }
    uint64_t sz = static_cast<uint64_t>(csz);
    if (!Write(sz) || !WriteBytes(comp.data(), csz)) {
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

  // Separate pathIndexes, fieldSetIndexes, specTypes
  std::vector<uint32_t> path_indexes;
  std::vector<uint32_t> fieldset_indexes;
  std::vector<uint32_t> spec_types;

  path_indexes.reserve(num_specs);
  fieldset_indexes.reserve(num_specs);
  spec_types.reserve(num_specs);

  for (size_t i = 0; i < spec_data_.size(); ++i) {
    const auto& spec_data = spec_data_[i];
    path_indexes.push_back(spec_data.spec.path_index.value);

    // Convert fieldset number to offset in flat array
    uint32_t fieldset_number = spec_data.spec.fieldset_index.value;
    uint32_t fieldset_offset = fieldset_number_to_offset[fieldset_number];
    fieldset_indexes.push_back(fieldset_offset);

    spec_types.push_back(static_cast<uint32_t>(spec_data.spec.spec_type));
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

  // Write section count
  uint64_t section_count = static_cast<uint64_t>(toc_.sections.size());
  if (!Write(section_count)) {
    if (err) *err = "Failed to write section count";
    return false;
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


  // IMPORTANT: Flush before seeking to beginning
  // We need to flush all buffered writes before seeking backwards
  stream_->Flush();

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

  stream_->Flush();

  return true;
}

bool CrateWriter::WriteBootStrap(std::string* /* err */) {
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

  bool dedup_candidate = false;
  std::vector<char> dedup_bytes;
  size_t dedup_element_size = 1;
  bool dedup_is_float = false;
  uint32_t dedup_wire_tag = 0;
  size_t dedup_hash = 0;

  if (options_.enable_deduplication &&
      ComputeValueDedupDescriptor(value, &dedup_bytes, &dedup_element_size,
                                  &dedup_is_float, &dedup_wire_tag)) {
    dedup_hash = NanAwareHash::combine(
        NanAwareHash::hash_buffer(dedup_bytes.data(), dedup_bytes.size(),
                                  dedup_element_size, dedup_is_float),
        dedup_wire_tag);
    if (LookupDeduplicatedValue(dedup_bytes, dedup_element_size,
                                dedup_is_float, dedup_wire_tag, &rep)) {
      return rep;
    }
    dedup_candidate = true;
  }

  // Value cannot be inlined, write to value data section
  bool is_compressed = false;
  int64_t offset = WriteValueData(value, &is_compressed, err);
  if (offset < 0 || (err && !err->empty())) {
    rep = crate::ValueRep();
    return rep;
  }

  // Create ValueRep with offset and proper type
  // Determine the type for out-of-line values

  // Macro to reduce repetitive scalar type dispatch
#define PACK_SCALAR_TYPE(CppType, CrateTypeId) \
  if (value.as<CppType>()) { \
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CrateTypeId)); \
  } else

  // Macro to reduce repetitive array type dispatch
#define PACK_ARRAY_TYPE(ElemType, CrateTypeId) \
  if (value.as<std::vector<ElemType>>()) { \
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CrateTypeId)); \
    rep.SetIsArray(); \
  } else

  // Scalar types
  PACK_SCALAR_TYPE(double, CRATE_DATA_TYPE_DOUBLE)
  PACK_SCALAR_TYPE(int64_t, CRATE_DATA_TYPE_INT64)
  PACK_SCALAR_TYPE(uint64_t, CRATE_DATA_TYPE_UINT64)
  PACK_SCALAR_TYPE(value::float2, CRATE_DATA_TYPE_VEC2F)
  PACK_SCALAR_TYPE(value::double2, CRATE_DATA_TYPE_VEC2D)
  PACK_SCALAR_TYPE(value::int2, CRATE_DATA_TYPE_VEC2I)
  PACK_SCALAR_TYPE(value::float3, CRATE_DATA_TYPE_VEC3F)
  PACK_SCALAR_TYPE(value::double3, CRATE_DATA_TYPE_VEC3D)
  PACK_SCALAR_TYPE(value::int3, CRATE_DATA_TYPE_VEC3I)
  PACK_SCALAR_TYPE(value::half2, CRATE_DATA_TYPE_VEC2H)
  PACK_SCALAR_TYPE(value::half3, CRATE_DATA_TYPE_VEC3H)
  PACK_SCALAR_TYPE(value::half4, CRATE_DATA_TYPE_VEC4H)
  PACK_SCALAR_TYPE(value::float4, CRATE_DATA_TYPE_VEC4F)
  PACK_SCALAR_TYPE(value::double4, CRATE_DATA_TYPE_VEC4D)
  PACK_SCALAR_TYPE(value::int4, CRATE_DATA_TYPE_VEC4I)
  PACK_SCALAR_TYPE(value::matrix2d, CRATE_DATA_TYPE_MATRIX2D)
  PACK_SCALAR_TYPE(value::matrix3d, CRATE_DATA_TYPE_MATRIX3D)
  PACK_SCALAR_TYPE(value::matrix4d, CRATE_DATA_TYPE_MATRIX4D)
  PACK_SCALAR_TYPE(value::quath, CRATE_DATA_TYPE_QUATH)
  PACK_SCALAR_TYPE(value::quatf, CRATE_DATA_TYPE_QUATF)
  PACK_SCALAR_TYPE(value::quatd, CRATE_DATA_TYPE_QUATD)
  // Array types - element type ID + IsArray flag (bit 63)
  PACK_ARRAY_TYPE(bool, CRATE_DATA_TYPE_BOOL)
  PACK_ARRAY_TYPE(uint8_t, CRATE_DATA_TYPE_UCHAR)
  PACK_ARRAY_TYPE(int32_t, CRATE_DATA_TYPE_INT)
  PACK_ARRAY_TYPE(uint32_t, CRATE_DATA_TYPE_UINT)
  PACK_ARRAY_TYPE(int64_t, CRATE_DATA_TYPE_INT64)
  PACK_ARRAY_TYPE(uint64_t, CRATE_DATA_TYPE_UINT64)
  PACK_ARRAY_TYPE(value::half, CRATE_DATA_TYPE_HALF)
  PACK_ARRAY_TYPE(float, CRATE_DATA_TYPE_FLOAT)
  PACK_ARRAY_TYPE(double, CRATE_DATA_TYPE_DOUBLE)
  PACK_ARRAY_TYPE(value::float2, CRATE_DATA_TYPE_VEC2F)
  PACK_ARRAY_TYPE(value::float3, CRATE_DATA_TYPE_VEC3F)
  PACK_ARRAY_TYPE(value::float4, CRATE_DATA_TYPE_VEC4F)
  PACK_ARRAY_TYPE(value::half2, CRATE_DATA_TYPE_VEC2H)
  PACK_ARRAY_TYPE(value::half3, CRATE_DATA_TYPE_VEC3H)
  PACK_ARRAY_TYPE(value::half4, CRATE_DATA_TYPE_VEC4H)
  PACK_ARRAY_TYPE(value::double2, CRATE_DATA_TYPE_VEC2D)
  PACK_ARRAY_TYPE(value::double3, CRATE_DATA_TYPE_VEC3D)
  PACK_ARRAY_TYPE(value::double4, CRATE_DATA_TYPE_VEC4D)
  PACK_ARRAY_TYPE(value::int2, CRATE_DATA_TYPE_VEC2I)
  PACK_ARRAY_TYPE(value::int3, CRATE_DATA_TYPE_VEC3I)
  PACK_ARRAY_TYPE(value::int4, CRATE_DATA_TYPE_VEC4I)
  PACK_ARRAY_TYPE(value::matrix2d, CRATE_DATA_TYPE_MATRIX2D)
  PACK_ARRAY_TYPE(value::matrix3d, CRATE_DATA_TYPE_MATRIX3D)
  PACK_ARRAY_TYPE(value::matrix4d, CRATE_DATA_TYPE_MATRIX4D)
  PACK_ARRAY_TYPE(value::quath, CRATE_DATA_TYPE_QUATH)
  PACK_ARRAY_TYPE(value::quatf, CRATE_DATA_TYPE_QUATF)
  PACK_ARRAY_TYPE(value::quatd, CRATE_DATA_TYPE_QUATD)
  PACK_ARRAY_TYPE(value::AssetPath, CRATE_DATA_TYPE_ASSET_PATH)
  PACK_ARRAY_TYPE(std::string, CRATE_DATA_TYPE_STRING)
  PACK_ARRAY_TYPE(value::token, CRATE_DATA_TYPE_TOKEN)

#undef PACK_SCALAR_TYPE
#undef PACK_ARRAY_TYPE

  // PathVector is a special type (type code 40) that doesn't use the array flag
  if (value.as<std::vector<Path>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_VECTOR));
  }
  // Dictionary type
  else if (value.as<value::dict>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY));
  }
  // Phase 2: CustomDataType (serializes like dictionary)
  else if (value.as<CustomDataType>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY));
  }
  // Phase 2: ListOp types
  else if (value.as<ListOp<value::token>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN_LIST_OP));
  } else if (value.as<ListOp<std::string>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING_LIST_OP));
  } else if (value.as<ListOp<Path>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_PATH_LIST_OP));
  } else if (value.as<ListOp<int32_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT_LIST_OP));
  } else if (value.as<ListOp<uint32_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT_LIST_OP));
  } else if (value.as<ListOp<int64_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64_LIST_OP));
  } else if (value.as<ListOp<uint64_t>>()) {
    rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64_LIST_OP));
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
  if (is_compressed) {
    rep.SetIsCompressed();
  }

  if (dedup_candidate) {
    RetainDeduplicatedValue(dedup_hash, std::move(dedup_bytes),
                            dedup_element_size, dedup_is_float,
                            dedup_wire_tag, rep);
  }

  return rep;
}


// ============================================================================
// Deduplication
// ============================================================================

crate::TokenIndex CrateWriter::GetOrCreateToken(const std::string& token) {
  return GetOrCreateImpl<std::string, crate::TokenIndex>(token, token_to_index_, tokens_);
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

  // IMPORTANT: Ensure all parent paths exist first
  // This is necessary for building a valid path tree where intermediate nodes
  // must have valid path indices. For example, adding "/Material/bora" should
  // also add "/Material" and "/" if they don't exist.
  std::string prim_part = path.prim_part();
  std::string prop_part = path.prop_part();

  // If this is a property path, first ensure the prim path (without property) exists
  if (!prop_part.empty() && !prim_part.empty()) {
    Path prim_only_path(prim_part, "");
    GetOrCreatePath(prim_only_path);
  }
  // Then ensure all parent prim paths exist
  else if (!prim_part.empty() && prim_part != "/") {
    // Build parent path by removing the last element
    size_t last_slash = prim_part.find_last_of('/');
    if (last_slash != std::string::npos && last_slash > 0) {
      std::string parent_prim = prim_part.substr(0, last_slash);
      Path parent_path(parent_prim, "");
      // Recursively ensure parent exists
      GetOrCreatePath(parent_path);
    } else if (last_slash == 0) {
      // Parent is root "/"
      Path root_path("/", "");
      GetOrCreatePath(root_path);
    }
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
  return GetOrCreateImpl<crate::Field, crate::FieldIndex>(field, field_to_index_, fields_);
}

crate::FieldSetIndex CrateWriter::GetOrCreateFieldSet(const std::vector<crate::FieldIndex>& fieldset) {
  return GetOrCreateImpl<std::vector<crate::FieldIndex>, crate::FieldSetIndex>(fieldset, fieldset_to_index_, fieldsets_);
}

// ============================================================================
// Compressed Array Helpers
// ============================================================================

int64_t CrateWriter::WriteCompressedArray32(
    const uint32_t* data, uint64_t count,
    const char* typeName, bool* is_compressed, std::string* err) {
  if (is_compressed) {
    (*is_compressed) = false;
  }

  if (count >= 16 && options_.enable_compression) {
    size_t compBufSize = Usd_IntegerCompression::GetCompressedBufferSize(count);
    std::vector<char> compressed(compBufSize);
    std::string compress_err;
    size_t compSize = Usd_IntegerCompression::CompressToBuffer(
        data, count, compressed.data(), &compress_err);
    if (compSize != 0 && compSize != static_cast<size_t>(~0)) {
      uint64_t cs = static_cast<uint64_t>(compSize);
      if (!Write(cs)) { if (err) { *err = "Failed to write compressed "; *err += typeName; *err += " array size"; } return -1; }
      if (!WriteBytes(compressed.data(), compSize)) { if (err) { *err = "Failed to write compressed "; *err += typeName; *err += " array data"; } return -1; }
      if (is_compressed) {
        (*is_compressed) = true;
      }
      return 0;
    }
  }
  // Fallback: write uncompressed
  for (uint64_t i = 0; i < count; ++i) {
    if (!Write(data[i])) { if (err) { *err = "Failed to write "; *err += typeName; *err += " array element"; } return -1; }
  }
  return 0;
}

int64_t CrateWriter::WriteCompressedArray64(
    const uint64_t* data, uint64_t count,
    const char* typeName, bool* is_compressed, std::string* err) {
  if (is_compressed) {
    (*is_compressed) = false;
  }

  if (count >= 16 && options_.enable_compression) {
    size_t compBufSize = Usd_IntegerCompression64::GetCompressedBufferSize(count);
    std::vector<char> compressed(compBufSize);
    std::string compress_err;
    size_t compSize = Usd_IntegerCompression64::CompressToBuffer(
        data, count, compressed.data(), &compress_err);
    if (compSize != 0 && compSize != static_cast<size_t>(~0)) {
      uint64_t cs = static_cast<uint64_t>(compSize);
      if (!Write(cs)) { if (err) { *err = "Failed to write compressed "; *err += typeName; *err += " array size"; } return -1; }
      if (!WriteBytes(compressed.data(), compSize)) { if (err) { *err = "Failed to write compressed "; *err += typeName; *err += " array data"; } return -1; }
      if (is_compressed) {
        (*is_compressed) = true;
      }
      return 0;
    }
  }
  // Fallback: write uncompressed
  for (uint64_t i = 0; i < count; ++i) {
    if (!Write(data[i])) { if (err) { *err = "Failed to write "; *err += typeName; *err += " array element"; } return -1; }
  }
  return 0;
}

namespace {
// Compress `count` uint32 values into `out` (just the compressed bytes; the
// caller writes the uint64 size prefix). Matches the payload the reader's
// ReadCompressedInts consumes after the size prefix. Returns false on failure.
static bool CompressUInt32ToBuffer(const uint32_t* data, uint64_t count,
                                   std::vector<char>* out) {
  const size_t bufSize =
      Usd_IntegerCompression::GetCompressedBufferSize(static_cast<size_t>(count));
  out->resize(bufSize);
  std::string cerr;
  const size_t n = Usd_IntegerCompression::CompressToBuffer(
      data, static_cast<size_t>(count), out->data(), &cerr);
  if (n == 0 || n == static_cast<size_t>(~0)) {
    return false;
  }
  out->resize(n);
  return true;
}
}  // namespace

int64_t CrateWriter::WriteCompressedFloatArray(const float* data, uint64_t count,
                                               bool* is_compressed,
                                               std::string* err) {
  if (is_compressed) {
    (*is_compressed) = false;
  }

  // Opt-in (default off): tagged float-array compression. Requires both the
  // dedicated flag and the general compression flag (the latter gates the
  // integer-stream compressor the 'i'/'t' payloads use).
  if (options_.enable_float_array_compression && options_.enable_compression &&
      count >= crate::kMinCompressedArraySize) {
    // Code 'i': every value is an integer exactly representable as int32 (the
    // reader reconstructs via float(int32)). Note this collapses -0.0f -> +0.0f,
    // matching OpenUSD's identical heuristic. The range guard before the cast
    // mirrors OpenUSD's isIntegral() and avoids UB for NaN/Inf/out-of-range
    // values. 2^31 is exactly representable in float; use it as the exclusive
    // upper bound (anything >= it cannot be a valid int32).
    {
      constexpr float kInt32Lo = -2147483648.0f;     // -2^31 == INT32_MIN
      constexpr float kInt32HiExcl = 2147483648.0f;  // 2^31, one past INT32_MAX
      std::vector<int32_t> ints(static_cast<size_t>(count));
      bool all_int = true;
      for (uint64_t i = 0; i < count; ++i) {
        const float v = data[i];
        if (!(v >= kInt32Lo && v < kInt32HiExcl)) { all_int = false; break; }
        const int32_t iv = static_cast<int32_t>(v);
        if (static_cast<float>(iv) != v) { all_int = false; break; }
        ints[static_cast<size_t>(i)] = iv;
      }
      std::vector<char> comp;
      if (all_int &&
          CompressUInt32ToBuffer(reinterpret_cast<const uint32_t*>(ints.data()),
                                 count, &comp)) {
        const char code = 'i';
        if (!WriteBytes(&code, 1)) { if (err) *err = "Failed to write float 'i' code"; return -1; }
        if (!Write(static_cast<uint64_t>(comp.size()))) { if (err) *err = "Failed to write float compressed-int size"; return -1; }
        if (!comp.empty() && !WriteBytes(comp.data(), comp.size())) { if (err) *err = "Failed to write float compressed ints"; return -1; }
        if (is_compressed) { (*is_compressed) = true; }
        return 0;
      }
    }
    // Code 't': few distinct values -> lookup table + compressed indices. Keyed
    // on the raw bit pattern so -0.0/NaN/Inf round-trip exactly here.
    {
      std::unordered_map<uint32_t, uint32_t> seen;
      std::vector<float> lut;
      std::vector<uint32_t> indexes(static_cast<size_t>(count));
      // Give up once the LUT would exceed min(count/4, 1024) distinct values —
      // the same profitability bound and 1024 ceiling OpenUSD uses. (We key on
      // the raw bit pattern rather than operator==, so -0.0/NaN round-trip
      // exactly instead of merging/exploding the table.)
      const size_t max_lut = (std::min)(static_cast<size_t>(count / 4),
                                        static_cast<size_t>(1024));
      bool lut_ok = true;
      for (uint64_t i = 0; i < count; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &data[i], sizeof(bits));
        auto it = seen.find(bits);
        if (it != seen.end()) {
          indexes[static_cast<size_t>(i)] = it->second;
          continue;
        }
        if (lut.size() == max_lut) { lut_ok = false; break; }
        const uint32_t idx = static_cast<uint32_t>(lut.size());
        seen.emplace(bits, idx);
        lut.push_back(data[i]);
        indexes[static_cast<size_t>(i)] = idx;
      }
      std::vector<char> comp;
      if (lut_ok && !lut.empty() &&
          CompressUInt32ToBuffer(indexes.data(), count, &comp)) {
        const char code = 't';
        size_t lut_bytes;
        if (!safe::mul(lut.size(), sizeof(float), &lut_bytes)) { if (err) *err = "Overflow: float LUT bytes"; return -1; }
        if (!WriteBytes(&code, 1)) { if (err) *err = "Failed to write float 't' code"; return -1; }
        if (!Write(static_cast<uint32_t>(lut.size()))) { if (err) *err = "Failed to write float LUT size"; return -1; }
        if (!WriteBytes(lut.data(), lut_bytes)) { if (err) *err = "Failed to write float LUT"; return -1; }
        if (!Write(static_cast<uint64_t>(comp.size()))) { if (err) *err = "Failed to write float index size"; return -1; }
        if (!comp.empty() && !WriteBytes(comp.data(), comp.size())) { if (err) *err = "Failed to write float indices"; return -1; }
        if (is_compressed) { (*is_compressed) = true; }
        return 0;
      }
    }
  }

  // Uncompressed fallback: raw little-endian floats (is_compressed stays false).
  size_t byte_count;
  if (!safe::mul(static_cast<size_t>(count), sizeof(float), &byte_count)) {
    if (err) *err = "Integer overflow: count * sizeof(float)";
    return -1;
  }
  if (count > 0 && !WriteBytes(data, byte_count)) {
    if (err) *err = "Failed to write float array data";
    return -1;
  }
  return 0;
}

int64_t CrateWriter::WriteCompressedDoubleArray(const double* data, uint64_t count,
                                                bool* is_compressed,
                                                std::string* err) {
  if (is_compressed) {
    (*is_compressed) = false;
  }

  // Opt-in (default off); see WriteCompressedFloatArray for the gating rationale.
  if (options_.enable_float_array_compression && options_.enable_compression &&
      count >= crate::kMinCompressedArraySize) {
    // Code 'i': integers exactly representable as int32 (reader reconstructs via
    // double(int32)). int32 is always exact in double. The range guard mirrors
    // OpenUSD's isIntegral() and avoids UB for NaN/Inf/out-of-range values.
    {
      constexpr double kInt32Lo = -2147483648.0;     // -2^31 == INT32_MIN
      constexpr double kInt32HiExcl = 2147483648.0;  // 2^31, one past INT32_MAX
      std::vector<int32_t> ints(static_cast<size_t>(count));
      bool all_int = true;
      for (uint64_t i = 0; i < count; ++i) {
        const double v = data[i];
        if (!(v >= kInt32Lo && v < kInt32HiExcl)) { all_int = false; break; }
        const int32_t iv = static_cast<int32_t>(v);
        if (static_cast<double>(iv) != v) { all_int = false; break; }
        ints[static_cast<size_t>(i)] = iv;
      }
      std::vector<char> comp;
      if (all_int &&
          CompressUInt32ToBuffer(reinterpret_cast<const uint32_t*>(ints.data()),
                                 count, &comp)) {
        const char code = 'i';
        if (!WriteBytes(&code, 1)) { if (err) *err = "Failed to write double 'i' code"; return -1; }
        if (!Write(static_cast<uint64_t>(comp.size()))) { if (err) *err = "Failed to write double compressed-int size"; return -1; }
        if (!comp.empty() && !WriteBytes(comp.data(), comp.size())) { if (err) *err = "Failed to write double compressed ints"; return -1; }
        if (is_compressed) { (*is_compressed) = true; }
        return 0;
      }
    }
    // Code 't': lookup table + compressed indices. Keyed on the raw 64-bit
    // pattern so -0.0/NaN/Inf round-trip exactly.
    {
      std::unordered_map<uint64_t, uint32_t> seen;
      std::vector<double> lut;
      std::vector<uint32_t> indexes(static_cast<size_t>(count));
      // Same min(count/4, 1024) profitability bound as OpenUSD; keyed on the
      // raw 64-bit pattern so -0.0/NaN round-trip exactly.
      const size_t max_lut = (std::min)(static_cast<size_t>(count / 4),
                                        static_cast<size_t>(1024));
      bool lut_ok = true;
      for (uint64_t i = 0; i < count; ++i) {
        uint64_t bits;
        std::memcpy(&bits, &data[i], sizeof(bits));
        auto it = seen.find(bits);
        if (it != seen.end()) {
          indexes[static_cast<size_t>(i)] = it->second;
          continue;
        }
        if (lut.size() == max_lut) { lut_ok = false; break; }
        const uint32_t idx = static_cast<uint32_t>(lut.size());
        seen.emplace(bits, idx);
        lut.push_back(data[i]);
        indexes[static_cast<size_t>(i)] = idx;
      }
      std::vector<char> comp;
      if (lut_ok && !lut.empty() &&
          CompressUInt32ToBuffer(indexes.data(), count, &comp)) {
        const char code = 't';
        size_t lut_bytes;
        if (!safe::mul(lut.size(), sizeof(double), &lut_bytes)) { if (err) *err = "Overflow: double LUT bytes"; return -1; }
        if (!WriteBytes(&code, 1)) { if (err) *err = "Failed to write double 't' code"; return -1; }
        if (!Write(static_cast<uint32_t>(lut.size()))) { if (err) *err = "Failed to write double LUT size"; return -1; }
        if (!WriteBytes(lut.data(), lut_bytes)) { if (err) *err = "Failed to write double LUT"; return -1; }
        if (!Write(static_cast<uint64_t>(comp.size()))) { if (err) *err = "Failed to write double index size"; return -1; }
        if (!comp.empty() && !WriteBytes(comp.data(), comp.size())) { if (err) *err = "Failed to write double indices"; return -1; }
        if (is_compressed) { (*is_compressed) = true; }
        return 0;
      }
    }
  }

  // Uncompressed fallback: raw little-endian doubles (is_compressed stays false).
  size_t byte_count;
  if (!safe::mul(static_cast<size_t>(count), sizeof(double), &byte_count)) {
    if (err) *err = "Integer overflow: count * sizeof(double)";
    return -1;
  }
  if (count > 0 && !WriteBytes(data, byte_count)) {
    if (err) *err = "Failed to write double array data";
    return -1;
  }
  return 0;
}

// ============================================================================
// I/O Utilities
// ============================================================================

int64_t CrateWriter::Tell() {
  return stream_->Tell();
}

bool CrateWriter::Seek(int64_t pos) {
  return stream_->Seek(pos);
}

bool CrateWriter::WriteBytes(const void* data, size_t size) {
  // Check file size limit before writing
  if (WouldExceedFileSizeLimit(static_cast<int64_t>(size))) {
    std::cerr << "ERROR: Writing " << size << " bytes would exceed file size limit of "
              << options_.max_file_size_bytes / (1024*1024) << " MB\n"
              << "  Current file size: " << bytes_written_ << " bytes\n"
              << "  Limit: " << options_.max_file_size_bytes << " bytes\n";
    return false;
  }

  if (stream_->Write(data, size)) {
    bytes_written_ += static_cast<int64_t>(size);
    return true;
  }
  return false;
}

// ============================================================================
// Validation Methods (Phase 5)
// ============================================================================

bool CrateWriter::ValidateStage(const Stage& stage, std::string* err) {
  // Reset validation state
  validation_prim_count_ = 0;
  validation_property_count_ = 0;
  validation_warnings_.clear();
  validation_warnings_count_ = 0;

  if (!options_.enable_validation) {
    return true;  // Validation disabled
  }

  // Check for empty stage
  if (stage.root_prims().empty()) {
    std::string warning = "WARNING: Stage has no root prims";
    if (err) *err = warning;
    validation_warnings_.push_back(warning);
    validation_warnings_count_++;
  }

  // Build a set of root prim names for defaultPrim validation
  std::set<std::string> root_prim_names;

  // Validate each root prim
  for (const auto& prim : stage.root_prims()) {
    validation_prim_count_++;

    // Check prim name is not empty
    if (prim.element_name().empty()) {
      std::string warning = "WARNING: Prim has empty name at index " +
                           std::to_string(validation_prim_count_);
      validation_warnings_.push_back(warning);
      validation_warnings_count_++;
    }

    // Collect prim name for defaultPrim validation
    if (!prim.element_name().empty()) {
      root_prim_names.insert(prim.element_name());
    }

    // Check for invalid path characters in prim name (/, ., and other invalid chars)
    const std::string& prim_name = prim.element_name();
    for (char c : prim_name) {
      if (c == '/' || c == '.' || c == ':' || c == '[' || c == ']' || c == '(' || c == ')') {
        std::string warning = "WARNING: Prim name contains invalid character '" +
                             std::string(1, c) + "' in name: " + prim_name;
        validation_warnings_.push_back(warning);
        validation_warnings_count_++;
        break;  // Only report once per prim
      }
    }
  }

  // Validate stage metadata
  const StageMetas& metas = stage.metas();

  // Validate defaultPrim if specified
  if (!metas.defaultPrim.str().empty()) {
    const std::string& default_prim_name = metas.defaultPrim.str();
    if (root_prim_names.find(default_prim_name) == root_prim_names.end()) {
      std::string warning = "WARNING: defaultPrim '" + default_prim_name +
                           "' does not refer to any root prim";
      validation_warnings_.push_back(warning);
      validation_warnings_count_++;
    }
  }

  // Validate time metadata consistency
  if (metas.startTimeCode.authored() && metas.endTimeCode.authored()) {
    double start = metas.startTimeCode.get_value();
    double end = metas.endTimeCode.get_value();
    if (start > end) {
      std::string warning = "WARNING: startTimeCode (" + std::to_string(start) +
                           ") is greater than endTimeCode (" + std::to_string(end) + ")";
      validation_warnings_.push_back(warning);
      validation_warnings_count_++;
    }
  }

  // Validate framesPerSecond and timeCodesPerSecond are positive
  if (metas.framesPerSecond.authored()) {
    double fps = metas.framesPerSecond.get_value();
    if (fps <= 0.0) {
      std::string warning = "WARNING: framesPerSecond must be positive, got " +
                           std::to_string(fps);
      validation_warnings_.push_back(warning);
      validation_warnings_count_++;
    }
  }

  if (metas.timeCodesPerSecond.authored()) {
    double tcps = metas.timeCodesPerSecond.get_value();
    if (tcps <= 0.0) {
      std::string warning = "WARNING: timeCodesPerSecond must be positive, got " +
                           std::to_string(tcps);
      validation_warnings_.push_back(warning);
      validation_warnings_count_++;
    }
  }

  // Validate metersPerUnit is positive if specified
  if (metas.metersPerUnit.authored()) {
    double mpu = metas.metersPerUnit.get_value();
    if (mpu <= 0.0) {
      std::string warning = "WARNING: metersPerUnit must be positive, got " +
                           std::to_string(mpu);
      validation_warnings_.push_back(warning);
      validation_warnings_count_++;
    }
  }

  return validation_warnings_count_ == 0 || !options_.enable_validation;
}

bool CrateWriter::ValidateLayer(const Layer& layer, std::string* err) {
  // Reset validation state
  validation_prim_count_ = 0;
  validation_property_count_ = 0;
  validation_warnings_.clear();
  validation_warnings_count_ = 0;

  if (!options_.enable_validation) {
    return true;  // Validation disabled
  }


  // Note: Layer is forward declared in crate-writer.hh, so we do minimal validation
  // Actual validation should be done before passing to CrateWriter

  std::string msg = "Layer validation: structure check (detailed validation requires full Layer definition)";

  if (err) *err = msg;
  return true;
}

std::string CrateWriter::GetValidationSummary() const {
  std::string summary = "Validation Summary:\n";
  summary += "  Prims: " + std::to_string(validation_prim_count_) + "\n";
  summary += "  Properties: " + std::to_string(validation_property_count_) + "\n";
  summary += "  Warnings: " + std::to_string(validation_warnings_count_) + "\n";

  if (!validation_warnings_.empty()) {
    summary += "\nWarnings:\n";
    for (size_t i = 0; i < validation_warnings_.size() && i < 10; ++i) {
      summary += "  [" + std::to_string(i + 1) + "] " + validation_warnings_[i] + "\n";
    }
    if (validation_warnings_.size() > 10) {
      summary += "  ... and " + std::to_string(validation_warnings_.size() - 10) + " more warnings\n";
    }
  }

  return summary;
}

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
