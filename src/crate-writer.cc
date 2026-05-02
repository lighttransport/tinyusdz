// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer Implementation
#include "crate-writer.hh"

#include <algorithm>
#include <cstring>
#include <set>
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

/// Build a ListOpHeader from any ListOp<T> (identical logic for all types)
template<typename T>
ListOpHeader BuildListOpHeader(const ListOp<T>& listop) {
  ListOpHeader header;
  header.bits = 0;
  if (listop.IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
  if (listop.HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
  if (listop.HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
  if (listop.HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
  if (listop.HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
  if (listop.HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
  if (listop.HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;
  return header;  // NRVO
}

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

  // Check for duplicate specs with same path
  // USD Crate format requires each path to appear only once
  for (const auto& existing_spec : spec_data_) {
    if (existing_spec.path.full_path_name() == path.full_path_name()) {
      return true;  // Silently skip duplicate (not an error)
    }
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

/// Helper to convert value::Value to CrateValue for TimeSamples serialization
/// Returns true if conversion succeeded
static bool ConvertValueToCrateValue(const value::Value& val, crate::CrateValue* out, std::string* err) {
  if (!out) {
    if (err) *err = "ConvertValueToCrateValue: output is null";
    return false;
  }

  uint32_t type_id = val.type_id();

  // Macro to reduce repetitive scalar/vector type dispatch
#define CONVERT_CRATE_VALUE(CppType) \
  if (auto* v = val.as<CppType>()) { \
    out->Set(*v); \
    return true; \
  } else

  // Scalar numeric types
  CONVERT_CRATE_VALUE(bool)
  CONVERT_CRATE_VALUE(int32_t)
  CONVERT_CRATE_VALUE(uint32_t)
  CONVERT_CRATE_VALUE(int64_t)
  CONVERT_CRATE_VALUE(uint64_t)
  CONVERT_CRATE_VALUE(value::half)
  CONVERT_CRATE_VALUE(float)
  CONVERT_CRATE_VALUE(double)
  // Vector types
  CONVERT_CRATE_VALUE(value::float2)
  CONVERT_CRATE_VALUE(value::float3)
  CONVERT_CRATE_VALUE(value::float4)
  CONVERT_CRATE_VALUE(value::double2)
  CONVERT_CRATE_VALUE(value::double3)
  CONVERT_CRATE_VALUE(value::double4)
  CONVERT_CRATE_VALUE(value::int2)
  CONVERT_CRATE_VALUE(value::int3)
  CONVERT_CRATE_VALUE(value::int4)
  // Array types - numeric scalars
  CONVERT_CRATE_VALUE(std::vector<bool>)
  CONVERT_CRATE_VALUE(std::vector<int32_t>)
  CONVERT_CRATE_VALUE(std::vector<uint32_t>)
  CONVERT_CRATE_VALUE(std::vector<int64_t>)
  CONVERT_CRATE_VALUE(std::vector<uint64_t>)
  CONVERT_CRATE_VALUE(std::vector<value::half>)
  CONVERT_CRATE_VALUE(std::vector<float>)
  CONVERT_CRATE_VALUE(std::vector<double>)
  // Vector arrays
  CONVERT_CRATE_VALUE(std::vector<value::float2>)
  CONVERT_CRATE_VALUE(std::vector<value::float3>)
  CONVERT_CRATE_VALUE(std::vector<value::float4>)
  CONVERT_CRATE_VALUE(std::vector<value::double2>)
  CONVERT_CRATE_VALUE(std::vector<value::double3>)
  CONVERT_CRATE_VALUE(std::vector<value::double4>)
  CONVERT_CRATE_VALUE(std::vector<value::int2>)
  CONVERT_CRATE_VALUE(std::vector<value::int3>)
  CONVERT_CRATE_VALUE(std::vector<value::int4>)
  // Token/String/AssetPath types
  CONVERT_CRATE_VALUE(value::token)
  CONVERT_CRATE_VALUE(std::string)
  CONVERT_CRATE_VALUE(value::AssetPath)
  // Token/String/AssetPath arrays
  CONVERT_CRATE_VALUE(std::vector<value::token>)
  CONVERT_CRATE_VALUE(std::vector<std::string>)
  CONVERT_CRATE_VALUE(std::vector<value::AssetPath>)
  // Half-vec / matrix / quaternion scalars
  CONVERT_CRATE_VALUE(value::half2)
  CONVERT_CRATE_VALUE(value::half3)
  CONVERT_CRATE_VALUE(value::half4)
  CONVERT_CRATE_VALUE(value::matrix2d)
  CONVERT_CRATE_VALUE(value::matrix3d)
  CONVERT_CRATE_VALUE(value::matrix4d)
  CONVERT_CRATE_VALUE(value::quath)
  CONVERT_CRATE_VALUE(value::quatf)
  CONVERT_CRATE_VALUE(value::quatd)
  // Half-vec / matrix / quaternion arrays
  CONVERT_CRATE_VALUE(std::vector<value::half2>)
  CONVERT_CRATE_VALUE(std::vector<value::half3>)
  CONVERT_CRATE_VALUE(std::vector<value::half4>)
  CONVERT_CRATE_VALUE(std::vector<value::matrix2d>)
  CONVERT_CRATE_VALUE(std::vector<value::matrix3d>)
  CONVERT_CRATE_VALUE(std::vector<value::matrix4d>)
  CONVERT_CRATE_VALUE(std::vector<value::quath>)
  CONVERT_CRATE_VALUE(std::vector<value::quatf>)
  CONVERT_CRATE_VALUE(std::vector<value::quatd>)

#undef CONVERT_CRATE_VALUE
  // fall through to unmatched type handling
  {}

  // Phase 5.7: Custom/Unregistered value types
  // For unknown types, attempt to encode as an unregistered value
  // This allows custom attributes with user-defined types to be stored
  const std::string& type_name = val.type_name();

  DCOUT("[ConvertValueToCrateValue] Unmatched type: type_name='" << type_name
        << "' type_id=" << type_id);

  if (!type_name.empty()) {
    // Try to encode as Dictionary (most flexible representation)
    if (auto* v = val.as<Dictionary>()) {
      out->Set(*v);
      DCOUT("[ConvertValueToCrateValue] Encoded custom/unregistered value as Dictionary: "
            << type_name);
      return true;
    }

    // Try to encode as generic string representation
    // This is a fallback for values that can be stringified
    DCOUT("[ConvertValueToCrateValue] Encoding custom type as Dictionary: "
          << type_name << " (type_id=" << type_id << ")");

    // For now, we store the type name in a dictionary as metadata
    Dictionary custom_dict;
    custom_dict["__type__"] = type_name;
    custom_dict["__note__"] = std::string("Custom unregistered value type - type information preserved in metadata");
    out->Set(custom_dict);
    return true;
  }

  // Truly unsupported type - no type name available
  if (err) {
    *err = "ConvertValueToCrateValue: Unsupported type_id " + std::to_string(type_id) +
           " (type_name: " + val.type_name() + ")";
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

  constexpr uint32_t kMaxPathTreeDepth = 512;

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

  return rep;
}

int64_t CrateWriter::WriteValueData(const crate::CrateValue& value,
                                    bool* is_compressed,
                                    std::string* err) {
  if (is_compressed) {
    (*is_compressed) = false;
  }

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
  // Scalar vector/matrix/quaternion macros
  // A. Indexable vectors (float/double/int 2/3/4)
#define WRITE_VEC_SCALAR(Type, N, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    for (size_t i = 0; i < N; ++i) { \
      if (!Write((*v)[i])) { \
        if (err) *err = "Failed to write " TypeName " component"; \
        return -1; \
      } \
    } \
  }
  // B. Half vector (needs .value suffix)
#define WRITE_HALFVEC_SCALAR(Type, N, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    for (size_t i = 0; i < N; ++i) { \
      if (!Write((*v)[i].value)) { \
        if (err) *err = "Failed to write " TypeName " component"; \
        return -1; \
      } \
    } \
  }
  // C. Matrices (binary-serializable struct, write as contiguous bytes)
#define WRITE_MATRIX_SCALAR(Type, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    if (!WriteBytes(v, sizeof(Type))) { \
      if (err) *err = "Failed to write " TypeName; \
      return -1; \
    } \
  }
  // D. Quaternions (real then imag[0..2], USD Crate format order)
#define WRITE_QUAT_SCALAR(Type, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    if (!Write(v->real) || !Write(v->imag[0]) || \
        !Write(v->imag[1]) || !Write(v->imag[2])) { \
      if (err) *err = "Failed to write " TypeName " components"; \
      return -1; \
    } \
  }
  // E. Half-precision quaternion (needs .value on each component)
#define WRITE_QUATH_SCALAR(Type, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    if (!Write(v->real.value) || !Write(v->imag[0].value) || \
        !Write(v->imag[1].value) || !Write(v->imag[2].value)) { \
      if (err) *err = "Failed to write " TypeName " components"; \
      return -1; \
    } \
  }

  WRITE_VEC_SCALAR(value::float2, 2, "Vec2f")
  WRITE_VEC_SCALAR(value::double2, 2, "Vec2d")
  WRITE_VEC_SCALAR(value::int2, 2, "Vec2i")
  WRITE_VEC_SCALAR(value::float3, 3, "Vec3f")
  WRITE_VEC_SCALAR(value::double3, 3, "Vec3d")
  WRITE_VEC_SCALAR(value::int3, 3, "Vec3i")
  WRITE_HALFVEC_SCALAR(value::half2, 2, "Vec2h")
  WRITE_HALFVEC_SCALAR(value::half3, 3, "Vec3h")
  WRITE_HALFVEC_SCALAR(value::half4, 4, "Vec4h")
  WRITE_VEC_SCALAR(value::float4, 4, "Vec4f")
  WRITE_VEC_SCALAR(value::double4, 4, "Vec4d")
  WRITE_VEC_SCALAR(value::int4, 4, "Vec4i")
  WRITE_MATRIX_SCALAR(value::matrix2d, "Matrix2d")
  WRITE_MATRIX_SCALAR(value::matrix3d, "Matrix3d")
  WRITE_MATRIX_SCALAR(value::matrix4d, "Matrix4d")
  WRITE_QUATH_SCALAR(value::quath, "Quath")
  WRITE_QUAT_SCALAR(value::quatf, "Quatf")
  WRITE_QUAT_SCALAR(value::quatd, "Quatd")

#undef WRITE_VEC_SCALAR
#undef WRITE_HALFVEC_SCALAR
#undef WRITE_MATRIX_SCALAR
#undef WRITE_QUAT_SCALAR
#undef WRITE_QUATH_SCALAR
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
  // Integer arrays — compressed via WriteCompressedArray32/64 helpers
  else if (auto* int_array = value.as<std::vector<int32_t>>()) {
    uint64_t count = int_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write int array count"; return -1; }
    // int32 data is bit-compatible with uint32 for compression
    if (WriteCompressedArray32(reinterpret_cast<const uint32_t*>(int_array->data()),
                               count, "int", is_compressed, err) < 0) {
      return -1;
    }
  }
  else if (auto* uint_array = value.as<std::vector<uint32_t>>()) {
    uint64_t count = uint_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write uint array count"; return -1; }
    if (WriteCompressedArray32(uint_array->data(), count, "uint",
                               is_compressed, err) < 0) {
      return -1;
    }
  }
  else if (auto* int64_array = value.as<std::vector<int64_t>>()) {
    uint64_t count = int64_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write int64 array count"; return -1; }
    if (WriteCompressedArray64(reinterpret_cast<const uint64_t*>(int64_array->data()),
                               count, "int64", is_compressed, err) < 0) {
      return -1;
    }
  }
  else if (auto* uint64_array = value.as<std::vector<uint64_t>>()) {
    uint64_t count = uint64_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write uint64 array count"; return -1; }
    if (WriteCompressedArray64(uint64_array->data(), count, "uint64",
                               is_compressed, err) < 0) {
      return -1;
    }
  }
  // Half array — convert to uint32 for compression
  else if (auto* half_array = value.as<std::vector<value::half>>()) {
    uint64_t count = half_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write half array count"; return -1; }
    for (const auto& val : *half_array) {
      if (!Write(val.value)) {
        if (err) *err = "Failed to write half array element";
        return -1;
      }
    }
  }
  // Float arrays use a tagged compression format in the reader.
  // Until the writer emits that exact format, keep them uncompressed.
  else if (auto* float_array = value.as<std::vector<float>>()) {
    uint64_t count = float_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write float array count"; return -1; }
    if (!WriteBytes(float_array->data(), float_array->size() * sizeof(float))) {
      if (err) *err = "Failed to write float array data";
      return -1;
    }
  }
  // Double arrays use a tagged compression format in the reader.
  // Until the writer emits that exact format, keep them uncompressed.
  else if (auto* double_array = value.as<std::vector<double>>()) {
    uint64_t count = double_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write double array count"; return -1; }
    if (!WriteBytes(double_array->data(), double_array->size() * sizeof(double))) {
      if (err) *err = "Failed to write double array data";
      return -1;
    }
  }
  // Vector/Quaternion array macros
#define WRITE_VEC_ARRAY(ElemType, N, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& elem : *arr) { \
      for (size_t i = 0; i < N; ++i) { \
        if (!Write(elem[i])) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
      } \
    } \
  }
#define WRITE_QUAT_ARRAY(ElemType, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& q : *arr) { \
      bool qok = Write(q.real) && Write(q.imag[0]) && Write(q.imag[1]) && Write(q.imag[2]); \
      if (!qok) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
    } \
  }
#define WRITE_QUATH_ARRAY(ElemType, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& q : *arr) { \
      bool qok = Write(q.real.value) && Write(q.imag[0].value) && Write(q.imag[1].value) && Write(q.imag[2].value); \
      if (!qok) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
    } \
  }

  WRITE_VEC_ARRAY(value::float2, 2, "Vec2f")
  WRITE_VEC_ARRAY(value::float3, 3, "Vec3f")
  WRITE_VEC_ARRAY(value::float4, 4, "Vec4f")
  WRITE_VEC_ARRAY(value::double2, 2, "Vec2d")
  WRITE_VEC_ARRAY(value::double3, 3, "Vec3d")
  WRITE_VEC_ARRAY(value::double4, 4, "Vec4d")
  WRITE_VEC_ARRAY(value::int2, 2, "Vec2i")
  WRITE_VEC_ARRAY(value::int3, 3, "Vec3i")
  WRITE_VEC_ARRAY(value::int4, 4, "Vec4i")
  WRITE_VEC_ARRAY(value::uint2, 2, "Vec2u")
  WRITE_VEC_ARRAY(value::uint3, 3, "Vec3u")
  WRITE_VEC_ARRAY(value::uint4, 4, "Vec4u")
  WRITE_QUATH_ARRAY(value::quath, "Quath")
  WRITE_QUAT_ARRAY(value::quatf, "Quatf")
  WRITE_QUAT_ARRAY(value::quatd, "Quatd")

  // Half-vector arrays — write raw uint16 per component
#define WRITE_HALFVEC_ARRAY(ElemType, N, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& elem : *arr) { \
      for (size_t i = 0; i < N; ++i) { \
        if (!Write(elem[i].value)) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
      } \
    } \
  }

  WRITE_HALFVEC_ARRAY(value::half2, 2, "Half2")
  WRITE_HALFVEC_ARRAY(value::half3, 3, "Half3")
  WRITE_HALFVEC_ARRAY(value::half4, 4, "Half4")

  // Matrix arrays — write raw doubles (sizeof(ElemType) doubles per element)
#define WRITE_MATRIX_ARRAY(ElemType, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    if (count > 0 && !WriteBytes(arr->data(), count * sizeof(ElemType))) { \
      if (err) *err = "Failed to write " TypeName " array data"; \
      return -1; \
    } \
  }

  WRITE_MATRIX_ARRAY(value::matrix2d, "Matrix2d")
  WRITE_MATRIX_ARRAY(value::matrix3d, "Matrix3d")
  WRITE_MATRIX_ARRAY(value::matrix4d, "Matrix4d")

#undef WRITE_VEC_ARRAY
#undef WRITE_QUAT_ARRAY
#undef WRITE_QUATH_ARRAY
#undef WRITE_HALFVEC_ARRAY
#undef WRITE_MATRIX_ARRAY
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
  // Asset array - reader expects StringIndex elements (uninlined/array path
  // in crate-reader-values.cc). Note inlined scalar AssetPath uses a
  // TokenIndex; arrays use StringIndex. See the asymmetric reader at
  // CRATE_DATA_TYPE_ASSET_PATH.
  else if (auto* asset_array = value.as<std::vector<value::AssetPath>>()) {
    uint64_t count = asset_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write asset[] count";
      return -1;
    }
    for (const auto& ap : *asset_array) {
      crate::StringIndex idx = GetOrCreateString(ap.GetAssetPath());
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write asset[] element index";
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
  // Dictionary format (recursive offset pattern as expected by TinyUSDZ reader):
  // uint64_t count + for each: (StringIndex key, int64_t offset, ValueRep)
  // The offset is relative to the position after reading the offset field.
  // offset=8 means ValueRep immediately follows the offset field.
  //
  // IMPORTANT: We must reserve space for the entire dictionary structure FIRST,
  // then call PackValue for each value. Otherwise, nested PackValue calls for
  // out-of-line values would write to value_data_end_offset_ which still points
  // to the start of the dictionary, corrupting the data.
  else if (auto* dict_val = value.as<value::dict>()) {
    uint64_t count = dict_val->size();

    // Calculate size of dictionary structure:
    // 8 bytes for count + (4 + 8 + 8) bytes per entry = 8 + 20*count
    int64_t dict_struct_size = 8 + (count * 20);
    int64_t dict_struct_start = Tell();

    // Reserve space by writing zeros
    {
      std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
      if (!WriteBytes(zeros.data(), zeros.size())) {
        if (err) *err = "Failed to reserve dictionary space";
        return -1;
      }
    }

    // Update value_data_end_offset_ NOW, before calling PackValue
    // This ensures nested writes go AFTER the dictionary structure
    value_data_end_offset_ = Tell();

    // Now pack all values first (this may write out-of-line data after dict structure)
    // IMPORTANT: PackValue calls will further update value_data_end_offset_,
    // so we need to capture the final end position before seeking back
    std::vector<crate::ValueRep> value_reps;
    value_reps.reserve(count);

    // Dict value packing: any_value_cast dispatch (value::dict uses any_value)
#define TRY_PACK_DICT(Type) \
      else if (auto* typed = value::any_value_cast<Type>(&kv.second)) { \
        crate::CrateValue cv; cv.Set(*typed); \
        value_rep = PackValue(cv, err); value_packed = true; }

    for (const auto& kv : *dict_val) {
      crate::ValueRep value_rep;
      bool value_packed = false;

      if (auto* v = value::any_value_cast<int32_t>(&kv.second)) {
        crate::CrateValue cv; cv.Set(*v);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      else if (auto* v = value::any_value_cast<int>(&kv.second)) {
        crate::CrateValue cv; cv.Set(static_cast<int32_t>(*v));
        value_rep = PackValue(cv, err); value_packed = true;
      }
      TRY_PACK_DICT(uint32_t)
      TRY_PACK_DICT(float)
      TRY_PACK_DICT(double)
      TRY_PACK_DICT(bool)
      TRY_PACK_DICT(std::string)
      TRY_PACK_DICT(value::token)
      else {
        if (err) *err = "Unsupported dictionary value type";
        return -1;
      }
#undef TRY_PACK_DICT

      if (!value_packed) {
        if (err) *err = "Failed to pack dictionary value";
        return -1;
      }

      value_reps.push_back(value_rep);
    }

    // Now go back and write the dictionary structure
    if (!Seek(dict_struct_start)) {
      if (err) *err = "Failed to seek to dictionary structure start";
      return -1;
    }

    // Write count
    if (!Write(count)) {
      if (err) *err = "Failed to write dictionary count";
      return -1;
    }

    // Write each (key, offset, ValueRep) tuple
    size_t idx = 0;
    for (const auto& kv : *dict_val) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write dictionary key index";
        return -1;
      }

      // Write offset = 8 (ValueRep immediately follows the offset)
      int64_t offset = 8;
      if (!Write(offset)) {
        if (err) *err = "Failed to write dictionary value offset";
        return -1;
      }

      // Write ValueRep
      if (!Write(value_reps[idx].GetData())) {
        if (err) *err = "Failed to write dictionary ValueRep";
        return -1;
      }

      ++idx;
    }

    // IMPORTANT: Seek to value_data_end_offset_, not saved_pos!
    // value_data_end_offset_ has been updated by all the PackValue calls to point to
    // the end of all written data. saved_pos was captured before we packed values.
    if (!Seek(value_data_end_offset_)) {
      if (err) *err = "Failed to seek to end of value data";
      return -1;
    }
  }
  // Phase 2: CustomDataType serialization (similar to dictionary but uses MetaVariable)
  // CustomDataType = std::map<std::string, MetaVariable> where MetaVariable wraps value::Value
  // Uses same offset pattern as dictionary: StringIndex key + int64_t offset, ValueRep
  //
  // IMPORTANT: We must reserve space for the entire dictionary structure FIRST,
  // then call PackValue for each value. Otherwise, nested PackValue calls for
  // out-of-line values would write to value_data_end_offset_ which still points
  // to the start of the dictionary, corrupting the data.
  else if (auto* custom_data = value.as<CustomDataType>()) {
    uint64_t count = custom_data->size();

    // Calculate size of dictionary structure:
    // 8 bytes for count + (4 + 8 + 8) bytes per entry = 8 + 20*count
    int64_t dict_struct_size = 8 + (count * 20);
    int64_t dict_struct_start = Tell();

    // Reserve space by writing zeros
    {
      std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
      if (!WriteBytes(zeros.data(), zeros.size())) {
        if (err) *err = "Failed to reserve CustomDataType space";
        return -1;
      }
    }

    // Update value_data_end_offset_ NOW, before calling PackValue
    // This ensures nested writes go AFTER the dictionary structure
    value_data_end_offset_ = Tell();

    // Now pack all values first (this may write out-of-line data after dict structure)
    // IMPORTANT: PackValue calls will further update value_data_end_offset_,
    // so we need to capture the final end position before seeking back
    std::vector<crate::ValueRep> value_reps;
    value_reps.reserve(count);

    for (const auto& kv : *custom_data) {
      // Get the raw value::Value from MetaVariable and pack it
      const value::Value& raw_value = kv.second.get_raw_value();
      crate::ValueRep value_rep;
      bool value_packed = false;

      // CustomDataType value packing: value::Value dispatch
#define TRY_PACK_AS(Type) \
      else if (auto* typed = raw_value.as<Type>()) { \
        crate::CrateValue cv; cv.Set(*typed); \
        value_rep = PackValue(cv, err); value_packed = true; }

      if (auto* v = raw_value.as<int32_t>()) {
        crate::CrateValue cv; cv.Set(*v);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      else if (auto* v = raw_value.as<int>()) {
        crate::CrateValue cv; cv.Set(static_cast<int32_t>(*v));
        value_rep = PackValue(cv, err); value_packed = true;
      }
      TRY_PACK_AS(uint32_t)
      TRY_PACK_AS(float)
      TRY_PACK_AS(double)
      TRY_PACK_AS(bool)
      TRY_PACK_AS(std::string)
      // StringData: extract string value from metadata wrapper
      else if (auto* sd = raw_value.as<value::StringData>()) {
        crate::CrateValue cv; cv.Set(sd->value);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      // Fallback: type_id says string but as<string>() failed
      else if (raw_value.type_id() == value::TYPE_ID_STRING) {
        auto str_opt = kv.second.get_value<std::string>();
        if (str_opt) {
          crate::CrateValue cv; cv.Set(*str_opt);
          value_rep = PackValue(cv, err); value_packed = true;
        }
      }
      TRY_PACK_AS(value::token)
      TRY_PACK_AS(CustomDataType)
      TRY_PACK_AS(std::vector<std::string>)
      // StringData array: convert to string array
      else if (auto* sda = raw_value.as<std::vector<value::StringData>>()) {
        std::vector<std::string> sa;
        sa.reserve(sda->size());
        for (const auto& sd : *sda) { sa.push_back(sd.value); }
        crate::CrateValue cv; cv.Set(sa);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      // Fallback: string array via type_name or is_array+TYPE_ID_STRING
      else if (raw_value.type_name() == "string[]" || (raw_value.is_array() && raw_value.type_id() == value::TYPE_ID_STRING)) {
        if (auto* sa = raw_value.as<std::vector<std::string>>()) {
          crate::CrateValue cv; cv.Set(*sa);
          value_rep = PackValue(cv, err); value_packed = true;
        } else if (auto* ta = raw_value.as<std::vector<value::token>>()) {
          std::vector<std::string> sa;
          sa.reserve(ta->size());
          for (const auto& t : *ta) { sa.push_back(t.str()); }
          crate::CrateValue cv; cv.Set(sa);
          value_rep = PackValue(cv, err); value_packed = true;
        } else {
          auto opt = kv.second.get_value<std::vector<std::string>>();
          if (opt) {
            crate::CrateValue cv; cv.Set(*opt);
            value_rep = PackValue(cv, err); value_packed = true;
          }
        }
      }
      TRY_PACK_AS(std::vector<value::token>)
      TRY_PACK_AS(std::vector<float>)
      TRY_PACK_AS(std::vector<double>)
      TRY_PACK_AS(std::vector<int32_t>)
      TRY_PACK_AS(std::vector<bool>)
      TRY_PACK_AS(std::vector<uint32_t>)
      TRY_PACK_AS(std::vector<int64_t>)
      TRY_PACK_AS(std::vector<uint64_t>)
      TRY_PACK_AS(std::vector<value::half>)
      // Vec scalars (used by clips dict: double2 active/times entries, etc.)
      TRY_PACK_AS(value::float2)
      TRY_PACK_AS(value::float3)
      TRY_PACK_AS(value::float4)
      TRY_PACK_AS(value::double2)
      TRY_PACK_AS(value::double3)
      TRY_PACK_AS(value::double4)
      TRY_PACK_AS(value::int2)
      TRY_PACK_AS(value::int3)
      TRY_PACK_AS(value::int4)
      // Vec arrays
      TRY_PACK_AS(std::vector<value::float2>)
      TRY_PACK_AS(std::vector<value::float3>)
      TRY_PACK_AS(std::vector<value::float4>)
      TRY_PACK_AS(std::vector<value::double2>)
      TRY_PACK_AS(std::vector<value::double3>)
      TRY_PACK_AS(std::vector<value::double4>)
      TRY_PACK_AS(std::vector<value::int2>)
      TRY_PACK_AS(std::vector<value::int3>)
      TRY_PACK_AS(std::vector<value::int4>)
      // Asset paths (clips: assetPaths, manifestAssetPath)
      TRY_PACK_AS(value::AssetPath)
      TRY_PACK_AS(std::vector<value::AssetPath>)
      else {
        if (err) *err = "Unsupported CustomDataType value type: " + std::string(raw_value.type_name()) + " (type_id=" + std::to_string(raw_value.type_id()) + ")";
        return -1;
      }
#undef TRY_PACK_AS

      if (!value_packed) {
        if (err) *err = "Failed to pack CustomDataType value";
        return -1;
      }

      value_reps.push_back(value_rep);
    }

    // Now go back and write the dictionary structure
    if (!Seek(dict_struct_start)) {
      if (err) *err = "Failed to seek to dictionary structure start";
      return -1;
    }

    // Write count
    if (!Write(count)) {
      if (err) *err = "Failed to write CustomDataType count";
      return -1;
    }
    // Write each (key, offset, ValueRep) tuple
    size_t idx = 0;
    for (const auto& kv : *custom_data) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write CustomDataType key index";
        return -1;
      }

      // Write offset = 8 (ValueRep immediately follows the offset)
      int64_t offset = 8;
      if (!Write(offset)) {
        if (err) *err = "Failed to write CustomDataType value offset";
        return -1;
      }

      // Write ValueRep
      if (!Write(value_reps[idx].GetData())) {
        if (err) *err = "Failed to write CustomDataType ValueRep";
        return -1;
      }

      ++idx;
    }

    // IMPORTANT: Seek to value_data_end_offset_, not saved_pos!
    // value_data_end_offset_ has been updated by all the PackValue calls to point to
    // the end of all written data.
    if (!Seek(value_data_end_offset_)) {
      if (err) *err = "Failed to seek back after CustomDataType";
      return -1;
    }
  }
  // ListOp serialization macros
  // Dispatches all 6 list categories (explicit, added, prepended, appended, deleted, ordered)
#define WRITE_LISTOP_ITEMS(listop_ptr, writeListFn, TypeName) \
    if (listop_ptr->HasExplicitItems() && !writeListFn(listop_ptr->GetExplicitItems())) { \
      if (err) { *err = "Failed to write " TypeName " explicit items"; } \
      return -1; } \
    if (listop_ptr->HasAddedItems() && !writeListFn(listop_ptr->GetAddedItems())) { \
      if (err) { *err = "Failed to write " TypeName " added items"; } \
      return -1; } \
    if (listop_ptr->HasPrependedItems() && !writeListFn(listop_ptr->GetPrependedItems())) { \
      if (err) { *err = "Failed to write " TypeName " prepended items"; } \
      return -1; } \
    if (listop_ptr->HasAppendedItems() && !writeListFn(listop_ptr->GetAppendedItems())) { \
      if (err) { *err = "Failed to write " TypeName " appended items"; } \
      return -1; } \
    if (listop_ptr->HasDeletedItems() && !writeListFn(listop_ptr->GetDeletedItems())) { \
      if (err) { *err = "Failed to write " TypeName " deleted items"; } \
      return -1; } \
    if (listop_ptr->HasOrderedItems() && !writeListFn(listop_ptr->GetOrderedItems())) { \
      if (err) { *err = "Failed to write " TypeName " ordered items"; } \
      return -1; }

  // TokenListOp
  else if (auto* token_listop = value.as<ListOp<value::token>>()) {
    auto header = BuildListOpHeader(*token_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write TokenListOp header"; return -1; }
    auto writeTokenList = [&](const std::vector<value::token>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& tok : list) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) return false;
      }
      return true;
    };
    WRITE_LISTOP_ITEMS(token_listop, writeTokenList, "TokenListOp")
  }
  // StringListOp
  else if (auto* string_listop = value.as<ListOp<std::string>>()) {
    auto header = BuildListOpHeader(*string_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write StringListOp header"; return -1; }
    auto writeStringList = [&](const std::vector<std::string>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& str : list) {
        crate::StringIndex idx = GetOrCreateString(str);
        if (!Write(idx.value)) return false;
      }
      return true;
    };
    WRITE_LISTOP_ITEMS(string_listop, writeStringList, "StringListOp")
  }
  // PathListOp
  else if (auto* path_listop = value.as<ListOp<Path>>()) {
    auto header = BuildListOpHeader(*path_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write PathListOp header"; return -1; }
    auto writePathList = [&](const std::vector<Path>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& path : list) {
        crate::PathIndex idx = GetOrCreatePath(path);
        if (!Write(idx.value)) return false;
      }
      return true;
    };
    WRITE_LISTOP_ITEMS(path_listop, writePathList, "PathListOp")
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

    // Write customData dictionary using the same reserve-pack-seek
    // pattern as top-level Dictionary serialization (see line ~1929).
    // PackValue may write out-of-line value data (doubles, int64 above
    // 2^47, large arrays) — we reserve dict frame first, then pack
    // (allowing nested writes to land after the frame), then come back
    // to fill in the (StringIndex key, int64 offset, ValueRep) tuples.
    {
      uint64_t dict_count = ref_val->customData.size();
      int64_t dict_struct_size = 8 + (int64_t)(dict_count * 20);
      int64_t dict_struct_start = Tell();

      // Reserve.
      {
        std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
        if (!WriteBytes(zeros.data(), zeros.size())) {
          if (err) *err = "Failed to reserve Reference customData space";
          return -1;
        }
      }
      value_data_end_offset_ = Tell();

      // Pack.
      std::vector<crate::ValueRep> value_reps;
      value_reps.reserve(dict_count);
      for (const auto& kv : ref_val->customData) {
        crate::ValueRep value_rep;
        bool packed = false;
        if (auto v = kv.second.get_value<int32_t>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<float>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<double>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<bool>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<std::string>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto sd = kv.second.get_value<value::StringData>()) {
          crate::CrateValue cv; cv.Set(sd->value);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<int64_t>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        if (!packed) {
          if (err) *err = "Unsupported Reference customData value type: "
                          + kv.second.type_name();
          return -1;
        }
        value_reps.push_back(value_rep);
      }

      // Fill in dict frame.
      if (!Seek(dict_struct_start)) {
        if (err) *err = "Failed to seek to Reference customData start";
        return -1;
      }
      if (!Write(dict_count)) {
        if (err) *err = "Failed to write Reference customData count";
        return -1;
      }
      size_t idx = 0;
      for (const auto& kv : ref_val->customData) {
        if (!Write(GetOrCreateString(kv.first).value)) {
          if (err) *err = "Failed to write Reference customData key";
          return -1;
        }
        const int64_t offset = 8;
        if (!Write(offset)) {
          if (err) *err = "Failed to write Reference customData offset";
          return -1;
        }
        if (!Write(value_reps[idx].GetData())) {
          if (err) *err = "Failed to write Reference customData value";
          return -1;
        }
        ++idx;
      }

      // Resume at end of out-of-line value data.
      if (!Seek(value_data_end_offset_)) {
        if (err) *err = "Failed to seek past Reference customData value data";
        return -1;
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
    auto header = BuildListOpHeader(*ref_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write ReferenceListOp header"; return -1; }

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

        // Write customData dictionary using reserve-pack-seek pattern
        // (same as single-Reference + top-level Dictionary): tolerates
        // out-of-line PackValue writes for doubles, large int64,
        // arrays, etc.
        {
          uint64_t dict_count = ref.customData.size();
          int64_t dict_struct_size = 8 + (int64_t)(dict_count * 20);
          int64_t dict_struct_start = Tell();

          {
            std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
            if (!WriteBytes(zeros.data(), zeros.size())) return false;
          }
          value_data_end_offset_ = Tell();

          std::vector<crate::ValueRep> value_reps;
          value_reps.reserve(dict_count);
          for (const auto& kv : ref.customData) {
            crate::ValueRep value_rep;
            bool value_written = false;
            if (auto v = kv.second.get_value<int32_t>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<float>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<double>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<bool>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<std::string>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto sd = kv.second.get_value<value::StringData>()) {
              crate::CrateValue cv; cv.Set(sd->value);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<int64_t>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            if (!value_written) {
              if (err) *err = "Unsupported Reference customData value type: "
                              + kv.second.type_name();
              return false;
            }
            value_reps.push_back(value_rep);
          }

          if (!Seek(dict_struct_start)) return false;
          if (!Write(dict_count)) return false;
          size_t idx = 0;
          for (const auto& kv : ref.customData) {
            if (!Write(GetOrCreateString(kv.first).value)) return false;
            const int64_t offset = 8;
            if (!Write(offset)) return false;
            if (!Write(value_reps[idx].GetData())) return false;
            ++idx;
          }
          if (!Seek(value_data_end_offset_)) return false;
        }
      }
      return true;
    };

    WRITE_LISTOP_ITEMS(ref_listop, writeRefList, "ReferenceListOp")
  }
  // PayloadListOp serialization
  else if (auto* payload_listop = value.as<ListOp<Payload>>()) {
    auto header = BuildListOpHeader(*payload_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write PayloadListOp header"; return -1; }

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

    WRITE_LISTOP_ITEMS(payload_listop, writePayloadList, "PayloadListOp")
  }
  // Simple value ListOps (int32, uint32, int64, uint64) — items written directly
#define WRITE_VALUE_LISTOP(ItemType, TypeName) \
  else if (auto* listop_##TypeName = value.as<ListOp<ItemType>>()) { \
    auto header = BuildListOpHeader(*listop_##TypeName); \
    if (!Write(header.bits)) { if (err) *err = "Failed to write " #TypeName "ListOp header"; return -1; } \
    auto writeList = [&](const std::vector<ItemType>& list) -> bool { \
      uint64_t cnt = list.size(); \
      if (!Write(cnt)) return false; \
      for (const auto& val : list) { if (!Write(val)) return false; } \
      return true; \
    }; \
    WRITE_LISTOP_ITEMS(listop_##TypeName, writeList, #TypeName "ListOp") \
  }

  WRITE_VALUE_LISTOP(int32_t, Int)
  WRITE_VALUE_LISTOP(uint32_t, UInt)
  WRITE_VALUE_LISTOP(int64_t, Int64)
  WRITE_VALUE_LISTOP(uint64_t, UInt64)

#undef WRITE_VALUE_LISTOP
#undef WRITE_LISTOP_ITEMS
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
    // TimeSamples format (recursive value pattern):
    // 1. int64_t offset1 (indirection to times_rep)
    // 2. ValueRep times_rep (type=double[], payload=offset to times data)
    // 3. int64_t offset2 (indirection to values data)
    // 4. [in value data] times data: uint64_t count + double[]
    // 5. [at offset2] values data: uint64_t count + ValueRep[]

    uint64_t num_samples = static_cast<uint64_t>(timesamples_val->size());

    // === Step 1: Write initial indirection offset (points 8 bytes ahead to times_rep) ===
    int64_t indirection_offset = 8;
    if (!Write(indirection_offset)) {
      if (err) *err = "Failed to write TimeSamples indirection offset";
      return -1;
    }

    // === Step 2: Reserve space for times_rep ValueRep (will write later) ===
    int64_t times_rep_pos = Tell();  // Position 92 (value_offset + 8)
    uint64_t placeholder = 0;
    if (!Write(placeholder)) {  // Reserve 8 bytes for times_rep
      if (err) *err = "Failed to reserve space for times ValueRep";
      return -1;
    }
    // DON'T update value_data_end_offset_ yet - we'll do it after writing all in-line data

    // === Step 4: Write second indirection offset for values ===
    // The offset points 8 bytes ahead (right after this int64) to the values count
    int64_t values_indirection_offset = 8;

    if (!Write(values_indirection_offset)) {
      if (err) *err = "Failed to write values indirection offset";
      return -1;
    }

    // === Step 5: Write values count ===
    if (!Write(num_samples)) {
      if (err) *err = "Failed to write TimeSamples value count";
      return -1;
    }

    // Update value_data_end_offset_ NOW, before writing ValueReps
    // This ensures PackValue() writes out-of-line data AFTER our inline structure
    value_data_end_offset_ = Tell() + (num_samples * 8);  // Reserve space for ValueReps

    // Get samples - this works for both binary and generic value-backed types
    const auto& samples = timesamples_val->get_samples();

    if (samples.size() != num_samples) {
      if (err) *err = "TimeSamples: samples size mismatch (size=" + std::to_string(num_samples) +
                       ", get_samples=" + std::to_string(samples.size()) + ")";
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
        size_t dedup_element_size = 1;
        bool dedup_is_float = false;

        // Byte-packing macros with NaN-aware element type tracking
#define DEDUP_FLOAT_ARRAY(Type, ElemSize) \
        else if (auto* arr = crate_value.as<std::vector<Type>>()) { \
          is_dedup_candidate = true; \
          dedup_element_size = ElemSize; dedup_is_float = true; \
          size_t bsz = arr->size() * sizeof(Type); \
          value_bytes.resize(bsz); \
          std::memcpy(value_bytes.data(), arr->data(), bsz); }

#define DEDUP_BINARY_ARRAY(Type) \
        else if (auto* arr = crate_value.as<std::vector<Type>>()) { \
          is_dedup_candidate = true; \
          size_t bsz = arr->size() * sizeof(Type); \
          value_bytes.resize(bsz); \
          std::memcpy(value_bytes.data(), arr->data(), bsz); }

#define DEDUP_FLOAT_SCALAR(Type, ElemSize) \
        else if (auto* ptr = crate_value.as<Type>()) { \
          is_dedup_candidate = true; \
          dedup_element_size = ElemSize; dedup_is_float = true; \
          value_bytes.resize(sizeof(Type)); \
          std::memcpy(value_bytes.data(), ptr, sizeof(Type)); }

#define DEDUP_BINARY_SCALAR(Type) \
        else if (auto* ptr = crate_value.as<Type>()) { \
          is_dedup_candidate = true; \
          value_bytes.resize(sizeof(Type)); \
          std::memcpy(value_bytes.data(), ptr, sizeof(Type)); }

        if (auto* arr = crate_value.as<std::vector<float>>()) {
          is_dedup_candidate = true;
          dedup_element_size = sizeof(float); dedup_is_float = true;
          size_t bsz = arr->size() * sizeof(float);
          value_bytes.resize(bsz);
          std::memcpy(value_bytes.data(), arr->data(), bsz);
        }
        DEDUP_FLOAT_ARRAY(double, sizeof(double))
        DEDUP_BINARY_ARRAY(int32_t)
        DEDUP_BINARY_ARRAY(uint32_t)
        DEDUP_BINARY_ARRAY(int64_t)
        DEDUP_BINARY_ARRAY(uint64_t)
        // String/token arrays: variable-length serialization (not macroable)
        else if (auto* string_arr = crate_value.as<std::vector<std::string>>()) {
          is_dedup_candidate = true;
          size_t total_size = sizeof(uint64_t);
          for (const auto& str : *string_arr) { total_size += sizeof(uint64_t) + str.size(); }
          value_bytes.reserve(total_size);
          uint64_t count = string_arr->size();
          value_bytes.insert(value_bytes.end(), reinterpret_cast<const char*>(&count), reinterpret_cast<const char*>(&count) + sizeof(uint64_t));
          for (const auto& str : *string_arr) {
            uint64_t len = str.size();
            value_bytes.insert(value_bytes.end(), reinterpret_cast<const char*>(&len), reinterpret_cast<const char*>(&len) + sizeof(uint64_t));
            value_bytes.insert(value_bytes.end(), str.begin(), str.end());
          }
        } else if (auto* token_arr = crate_value.as<std::vector<value::token>>()) {
          is_dedup_candidate = true;
          size_t total_size = sizeof(uint64_t);
          for (const auto& tok : *token_arr) { total_size += sizeof(uint64_t) + tok.str().size(); }
          value_bytes.reserve(total_size);
          uint64_t count = token_arr->size();
          value_bytes.insert(value_bytes.end(), reinterpret_cast<const char*>(&count), reinterpret_cast<const char*>(&count) + sizeof(uint64_t));
          for (const auto& tok : *token_arr) {
            uint64_t len = tok.str().size();
            value_bytes.insert(value_bytes.end(), reinterpret_cast<const char*>(&len), reinterpret_cast<const char*>(&len) + sizeof(uint64_t));
            value_bytes.insert(value_bytes.end(), tok.str().begin(), tok.str().end());
          }
        }
        // Float vector arrays
        DEDUP_FLOAT_ARRAY(value::float3, sizeof(float))
        DEDUP_FLOAT_ARRAY(value::float2, sizeof(float))
        DEDUP_FLOAT_ARRAY(value::float4, sizeof(float))
        // Double vector arrays
        DEDUP_FLOAT_ARRAY(value::double3, sizeof(double))
        // Scalar types (matrix=double, quaternion, vector)
        DEDUP_FLOAT_SCALAR(value::matrix2d, sizeof(double))
        DEDUP_FLOAT_SCALAR(value::matrix3d, sizeof(double))
        DEDUP_FLOAT_SCALAR(value::matrix4d, sizeof(double))
        DEDUP_FLOAT_SCALAR(value::quatf, sizeof(float))
        DEDUP_FLOAT_SCALAR(value::quatd, sizeof(double))
        DEDUP_BINARY_SCALAR(value::quath)  // half is uint16_t, raw byte hash
        DEDUP_FLOAT_SCALAR(value::float3, sizeof(float))
        DEDUP_FLOAT_SCALAR(value::double3, sizeof(double))
        DEDUP_FLOAT_SCALAR(value::float2, sizeof(float))
        DEDUP_FLOAT_SCALAR(value::float4, sizeof(float))
        DEDUP_FLOAT_SCALAR(value::double2, sizeof(double))
        DEDUP_FLOAT_SCALAR(value::double4, sizeof(double))
#undef DEDUP_FLOAT_ARRAY
#undef DEDUP_BINARY_ARRAY
#undef DEDUP_FLOAT_SCALAR
#undef DEDUP_BINARY_SCALAR

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
            // Determine CrateDataTypeId for the cached ValueRep
            crate::CrateDataTypeId type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_INVALID;
            bool is_array = false;

#define DEDUP_TYPE_ARRAY(CppType, CrateType) \
            else if (crate_value.as<std::vector<CppType>>()) { \
              type_id = crate::CrateDataTypeId::CrateType; is_array = true; }

#define DEDUP_TYPE_SCALAR(CppType, CrateType) \
            else if (crate_value.as<CppType>()) { \
              type_id = crate::CrateDataTypeId::CrateType; is_array = false; }

            if (crate_value.as<std::vector<float>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT; is_array = true;
            }
            DEDUP_TYPE_ARRAY(double, CRATE_DATA_TYPE_DOUBLE)
            DEDUP_TYPE_ARRAY(int32_t, CRATE_DATA_TYPE_INT)
            DEDUP_TYPE_ARRAY(uint32_t, CRATE_DATA_TYPE_UINT)
            DEDUP_TYPE_ARRAY(int64_t, CRATE_DATA_TYPE_INT64)
            DEDUP_TYPE_ARRAY(uint64_t, CRATE_DATA_TYPE_UINT64)
            DEDUP_TYPE_ARRAY(std::string, CRATE_DATA_TYPE_STRING)
            DEDUP_TYPE_ARRAY(value::token, CRATE_DATA_TYPE_TOKEN)
            DEDUP_TYPE_ARRAY(value::float3, CRATE_DATA_TYPE_VEC3F)
            DEDUP_TYPE_ARRAY(value::double3, CRATE_DATA_TYPE_VEC3D)
            DEDUP_TYPE_ARRAY(value::float2, CRATE_DATA_TYPE_VEC2F)
            DEDUP_TYPE_ARRAY(value::double2, CRATE_DATA_TYPE_VEC2D)
            DEDUP_TYPE_ARRAY(value::float4, CRATE_DATA_TYPE_VEC4F)
            DEDUP_TYPE_ARRAY(value::double4, CRATE_DATA_TYPE_VEC4D)
            DEDUP_TYPE_SCALAR(value::matrix2d, CRATE_DATA_TYPE_MATRIX2D)
            DEDUP_TYPE_SCALAR(value::matrix3d, CRATE_DATA_TYPE_MATRIX3D)
            DEDUP_TYPE_SCALAR(value::matrix4d, CRATE_DATA_TYPE_MATRIX4D)
            DEDUP_TYPE_SCALAR(value::quatf, CRATE_DATA_TYPE_QUATF)
            DEDUP_TYPE_SCALAR(value::quatd, CRATE_DATA_TYPE_QUATD)
            DEDUP_TYPE_SCALAR(value::quath, CRATE_DATA_TYPE_QUATH)
            DEDUP_TYPE_SCALAR(value::float3, CRATE_DATA_TYPE_VEC3F)
            DEDUP_TYPE_SCALAR(value::double3, CRATE_DATA_TYPE_VEC3D)
            DEDUP_TYPE_SCALAR(value::float2, CRATE_DATA_TYPE_VEC2F)
            DEDUP_TYPE_SCALAR(value::float4, CRATE_DATA_TYPE_VEC4F)
            DEDUP_TYPE_SCALAR(value::double2, CRATE_DATA_TYPE_VEC2D)
            DEDUP_TYPE_SCALAR(value::double4, CRATE_DATA_TYPE_VEC4D)
#undef DEDUP_TYPE_ARRAY
#undef DEDUP_TYPE_SCALAR

            value_rep.SetType(static_cast<int32_t>(type_id));
            if (is_array) { value_rep.SetIsArray(); }
            value_rep.SetPayload(static_cast<uint64_t>(cached_offset));
            dedup_attempted = true;

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

    // === Step 6: Write times array data (count + doubles) ===
    // PackValue() seeks to value_data_end_offset_ to write each sample's
    // out-of-line bytes, then seeks back. After the ValueRep loop, Tell()
    // points just past the ValueRep[] block — but value_data_end_offset_
    // has advanced past the actual sample-value bytes. Writing times at
    // Tell() would clobber those bytes; jump to value_data_end_offset_
    // first so the file layout is ValueRep[] | values data | times data.
    if (!Seek(value_data_end_offset_)) {
      if (err) *err = "Failed to seek past value bytes before writing times";
      return -1;
    }
    int64_t times_data_start = Tell();

    // Write count
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

    // === Step 7: Go back and fill in times_rep ValueRep ===
    int64_t current_pos = Tell();

    crate::ValueRep times_rep;
    times_rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE));
    times_rep.SetIsArray();
    times_rep.SetPayload(static_cast<uint64_t>(times_data_start));

    if (!Seek(times_rep_pos)) {
      if (err) *err = "Failed to seek to write times ValueRep";
      return -1;
    }

    uint64_t times_rep_data = times_rep.GetData();
    if (!Write(times_rep_data)) {
      if (err) *err = "Failed to write times ValueRep";
      return -1;
    }

    // Seek back to end
    if (!Seek(current_pos)) {
      if (err) *err = "Failed to seek back after writing times ValueRep";
      return -1;
    }

    // Update value_data_end_offset_ to include all TimeSamples data
    value_data_end_offset_ = Tell();
  }
  // Integer ListOps are handled above (IntListOp, UIntListOp, Int64ListOp, UInt64ListOp)
  else {
    // Unsupported type for out-of-line storage
    if (err) *err = "Unsupported value type for out-of-line storage: " + std::string(value.type_name()) + " (type_id=" + std::to_string(value.type_id()) + ")";
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
    // Inlined AssetPath is stored as a TokenIndex per OpenUSD's crate
    // format (matches CrateReader at crate-reader-values.cc which reads
    // it via GetToken). Storing as StringIndex caused the reader to read
    // a token at the wrong index and surface garbage like ";-)".
    crate::TokenIndex idx = GetOrCreateToken(asset_val->GetAssetPath());
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

  // Double cannot be inlined (64 bits > 48 bit payload)
  if (value.as<double>()) { return false; }

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

  // Try to get as ValueBlock (None)
  if (value.as<value::ValueBlock>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK));
    rep->SetIsInlined();
    rep->SetPayload(0);  // ValueBlock has no payload data
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

  // Vec2h - half2 (4 bytes = sizeof(uint32_t)): "always inlined" in Pixar's
  // crate format — raw half bit patterns stored directly via memcpy, NOT int8.
  if (auto* vec2h_val = value.as<value::half2>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H));
    rep->SetIsInlined();
    uint32_t packed = 0;
    uint16_t data[2] = {(*vec2h_val)[0].value, (*vec2h_val)[1].value};
    memcpy(&packed, data, sizeof(data));
    rep->SetPayload(static_cast<uint64_t>(packed));
    return true;
  }

  // Types that cannot be inlined (too large for 48-bit payload)
#define CANNOT_INLINE(Type) \
  if (value.as<Type>()) { return false; }

  CANNOT_INLINE(value::float2)   // 8 bytes
  CANNOT_INLINE(value::double2)  // 16 bytes
  CANNOT_INLINE(value::int2)     // 8 bytes

  // Vec3h - half3: inline only if each component is exactly int8
  if (auto* vec3h_val = value.as<value::half3>()) {
    rep->SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H));
    float f[3];
    f[0] = value::half_to_float((*vec3h_val)[0]);
    f[1] = value::half_to_float((*vec3h_val)[1]);
    f[2] = value::half_to_float((*vec3h_val)[2]);
    bool can_inline = true;
    int8_t ivec[3];
    for (int i = 0; i < 3; ++i) {
      float roundtripped = static_cast<float>(static_cast<int8_t>(f[i]));
      if (f[i] < -128.0f || f[i] > 127.0f ||
          std::memcmp(&roundtripped, &f[i], sizeof(float)) != 0) {
        can_inline = false;
        break;
      }
      ivec[i] = static_cast<int8_t>(f[i]);
    }
    if (can_inline) {
      uint32_t packed = 0;
      memcpy(&packed, ivec, sizeof(ivec));
      rep->SetIsInlined();
      rep->SetPayload(static_cast<uint64_t>(packed));
      return true;
    }
    return false;  // can't inline, write out-of-line
  }

  CANNOT_INLINE(value::float3)   // 12 bytes
  CANNOT_INLINE(value::double3)  // 24 bytes
  CANNOT_INLINE(value::int3)     // 12 bytes
  CANNOT_INLINE(value::half4)    // 8 bytes
  CANNOT_INLINE(value::float4)   // 16 bytes
  CANNOT_INLINE(value::double4)  // 32 bytes
  CANNOT_INLINE(value::int4)     // 16 bytes
  CANNOT_INLINE(value::matrix2d) // 32 bytes
  CANNOT_INLINE(value::matrix3d) // 72 bytes
  CANNOT_INLINE(value::matrix4d) // 128 bytes
  CANNOT_INLINE(value::quath)    // 8 bytes
  CANNOT_INLINE(value::quatf)    // 16 bytes
  CANNOT_INLINE(value::quatd)    // 32 bytes

#undef CANNOT_INLINE

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

  // Phase 1: Empty arrays are inlined with payload=0
  // Non-empty arrays require out-of-line storage with size prefix + data
  //
  // IMPORTANT: The Crate reader expects empty arrays to have payload=0.
  // When the reader sees payload=0 for an array type, it creates an empty array
  // without seeking to any offset.

  // Helper macro to check if array is empty and inline it
#define TRY_INLINE_EMPTY_ARRAY(VecType, CrateTypeId) \
  if (auto* arr = value.as<VecType>()) { \
    if (arr->empty()) { \
      rep->SetType(static_cast<int32_t>(CrateTypeId)); \
      rep->SetIsArray(); \
      rep->SetPayload(0); /* Empty array marker */ \
      return true; \
    } \
    return false; /* Non-empty array needs out-of-line storage */ \
  }

  // Check all array types - inline if empty, otherwise out-of-line
  TRY_INLINE_EMPTY_ARRAY(std::vector<bool>, crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL)
  TRY_INLINE_EMPTY_ARRAY(std::vector<uint8_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_UCHAR)
  TRY_INLINE_EMPTY_ARRAY(std::vector<int32_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_INT)
  TRY_INLINE_EMPTY_ARRAY(std::vector<uint32_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT)
  TRY_INLINE_EMPTY_ARRAY(std::vector<int64_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_INT64)
  TRY_INLINE_EMPTY_ARRAY(std::vector<uint64_t>, crate::CrateDataTypeId::CRATE_DATA_TYPE_UINT64)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half>, crate::CrateDataTypeId::CRATE_DATA_TYPE_HALF)
  TRY_INLINE_EMPTY_ARRAY(std::vector<float>, crate::CrateDataTypeId::CRATE_DATA_TYPE_FLOAT)
  TRY_INLINE_EMPTY_ARRAY(std::vector<double>, crate::CrateDataTypeId::CRATE_DATA_TYPE_DOUBLE)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2H)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3H)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::half4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4H)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::float2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2F)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::float3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3F)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::float4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::double2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::double3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::double4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::int2>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2I)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::int3>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC3I)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::int4>, crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4I)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::matrix2d>, crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::matrix3d>, crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::matrix4d>, crate::CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::quath>, crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATH)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::quatf>, crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATF)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::quatd>, crate::CrateDataTypeId::CRATE_DATA_TYPE_QUATD)
  TRY_INLINE_EMPTY_ARRAY(std::vector<std::string>, crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::token>, crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN)
  TRY_INLINE_EMPTY_ARRAY(std::vector<value::AssetPath>, crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH)

#undef TRY_INLINE_EMPTY_ARRAY

  // Cannot inline - need out-of-line storage
  return false;
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
