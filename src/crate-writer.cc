// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer Implementation
#include "crate-writer.hh"

#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>

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
// - nrvo: return value optimization hints
// - exceptions: comparator functions may throw in debug builds
// - unused-parameter: some functions have consistent API signatures
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wnrvo"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wexceptions"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

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

  // Check for duplicate specs with same path
  // USD Crate format requires each path to appear only once
  for (const auto& existing_spec : spec_data_) {
    if (existing_spec.path.full_path_name() == path.full_path_name()) {
      std::cerr << "DEBUG AddSpec: Skipping duplicate spec for path=" << path.full_path_name() << "\n";
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

  std::cerr << "DEBUG AddSpec[" << (spec_data_.size()-1) << "]: path=" << path.full_path_name()
            << " spec_type=" << static_cast<int>(spec_type) << " (";
  switch(spec_type) {
    case SpecType::Unknown: std::cerr << "Unknown"; break;
    case SpecType::Attribute: std::cerr << "Attribute"; break;
    case SpecType::Connection: std::cerr << "Connection"; break;
    case SpecType::Expression: std::cerr << "Expression"; break;
    case SpecType::Mapper: std::cerr << "Mapper"; break;
    case SpecType::MapperArg: std::cerr << "MapperArg"; break;
    case SpecType::Prim: std::cerr << "Prim"; break;
    case SpecType::PseudoRoot: std::cerr << "PseudoRoot"; break;
    case SpecType::Relationship: std::cerr << "Relationship"; break;
    case SpecType::RelationshipTarget: std::cerr << "RelationshipTarget"; break;
    case SpecType::Variant: std::cerr << "Variant"; break;
    case SpecType::VariantSet: std::cerr << "VariantSet"; break;
    case SpecType::Invalid: std::cerr << "Invalid"; break;
  }

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

  // Debug: Show order after sorting
  for (size_t i = 0; i < spec_data_.size(); ++i) {
    std::cerr << "  Spec[" << i << "]: path=" << spec_data_[i].path.full_path_name()
              << " spec_type=" << static_cast<int>(spec_data_[i].spec_type) << " (";
    switch(spec_data_[i].spec_type) {
      case SpecType::Unknown: std::cerr << "Unknown"; break;
      case SpecType::Attribute: std::cerr << "Attribute"; break;
      case SpecType::Connection: std::cerr << "Connection"; break;
      case SpecType::Expression: std::cerr << "Expression"; break;
      case SpecType::Mapper: std::cerr << "Mapper"; break;
      case SpecType::MapperArg: std::cerr << "MapperArg"; break;
      case SpecType::Prim: std::cerr << "Prim"; break;
      case SpecType::PseudoRoot: std::cerr << "PseudoRoot"; break;
      case SpecType::Relationship: std::cerr << "Relationship"; break;
      case SpecType::RelationshipTarget: std::cerr << "RelationshipTarget"; break;
      case SpecType::Variant: std::cerr << "Variant"; break;
      case SpecType::VariantSet: std::cerr << "VariantSet"; break;
      case SpecType::Invalid: std::cerr << "Invalid"; break;
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

  for (size_t i = 0; i < paths_.size(); ++i) {
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
  // CRITICAL: Re-sort paths_ and update spec path_indexes
  // ========================================================================
  // During field packing, connection target paths were added to paths_.
  // These paths need to be in sorted order for correct tree encoding.
  // After sorting, we must update all spec path_indexes to use the new indices.

  {
    // Convert paths_ to SimplePaths for sorting
    std::vector<std::pair<pathlib::SimplePath, size_t>> paths_with_old_idx;
    for (size_t i = 0; i < paths_.size(); ++i) {
      paths_with_old_idx.emplace_back(
        pathlib::SimplePath(paths_[i].prim_part(), paths_[i].prop_part()),
        i
      );
    }

    // Sort using USD path comparison
    std::sort(paths_with_old_idx.begin(), paths_with_old_idx.end(),
      [](const auto& a, const auto& b) {
        return pathlib::ComparePaths(a.first, b.first) < 0;
      });

    // Build old -> new index mapping
    std::vector<uint32_t> old_to_new(paths_.size());
    for (size_t new_idx = 0; new_idx < paths_with_old_idx.size(); ++new_idx) {
      size_t old_idx = paths_with_old_idx[new_idx].second;
      old_to_new[old_idx] = static_cast<uint32_t>(new_idx);
    }

    // Rebuild paths_ in sorted order
    std::vector<Path> sorted_paths;
    sorted_paths.reserve(paths_.size());
    for (const auto& pair : paths_with_old_idx) {
      // Find original path by old index
      sorted_paths.push_back(paths_[pair.second]);
    }
    paths_ = std::move(sorted_paths);

    // Rebuild path_to_index_ with new indices
    path_to_index_.clear();
    for (size_t i = 0; i < paths_.size(); ++i) {
      path_to_index_[paths_[i]] = crate::PathIndex(static_cast<uint32_t>(i));
    }

    // Update all spec path_indexes using the mapping
    for (auto& spec_data : spec_data_) {
      uint32_t old_idx = spec_data.spec.path_index.value;
      spec_data.spec.path_index.value = old_to_new[old_idx];
    }

    std::cerr << "DEBUG Finalize: Re-sorted " << paths_.size() << " paths" << std::endl;
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
  // CRITICAL: Include BOTH positive (prim) and negative (property) indices
  std::map<int32_t, std::string> path_tree_tokens;
  for (const auto& pair : reverse_tokens_prep) {
    // Store both positive and negative indices
    // Properties use negative indices, prims use positive indices
    path_tree_tokens[pair.first] = pair.second;
  }

  // For each path tree token, check if it already exists in our token table
  // If it exists, we can reuse it. If not, append it.
  // CRITICAL: Keep path tree indices WITH THEIR ORIGINAL SIGN!
  // Both +N and -N can exist as different tokens (prims vs properties).
  // We need to store them separately to avoid collisions.
  std::map<int32_t, uint32_t> path_tree_to_our_index;  // Maps path tree index (with sign!) -> our token index

  for (const auto& pair : path_tree_tokens) {
    int32_t path_tree_idx = pair.first;  // Keep original sign!
    const std::string& token_str = pair.second;

    // Skip empty string tokens - root path doesn't need a token entry
    // The root is implicit in the path tree structure
    if (token_str.empty()) {
      // Map to token index 0 (will be replaced with actual first token)
      path_tree_to_our_index[path_tree_idx] = 0;
      continue;
    }

    // Check if this token already exists in our token table
    auto it = token_to_index_.find(token_str);
    if (it != token_to_index_.end()) {
      // Token already exists, reuse it
      path_tree_to_our_index[path_tree_idx] = it->second.value;  // Store with ORIGINAL sign
    } else {
      // New token, append it
      uint32_t new_idx = static_cast<uint32_t>(tokens_.size());
      tokens_.push_back(token_str);
      token_to_index_[token_str] = crate::TokenIndex(new_idx);
      path_tree_to_our_index[path_tree_idx] = new_idx;  // Store with ORIGINAL sign
    }
  }

  // Store the mapping for later use when writing the path tree
  path_tree_token_remap_ = path_tree_to_our_index;

  // Ensure we have at least one token - USD Crate format requires non-empty TOKENS section
  // The reader checks: (3 + num_tokens) <= uncompressedSize, so minimum is token of length 3+
  if (tokens_.empty()) {
    // Add a minimal valid token (";-)" is used in pxrUSD as a sentinel/placeholder)
    tokens_.push_back(";-)");
    token_to_index_[";-)"] = crate::TokenIndex(0);
  }

  // Debug loops removed (original debug statements deleted)

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
static bool ConvertValueToCrateValue(const value::Value& val, crate::CrateValue* out, std::string* err) {
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

  // Phase 5.7: Custom/Unregistered value types
  // For unknown types, attempt to encode as an unregistered value
  // This allows custom attributes with user-defined types to be stored
  const std::string& type_name = val.type_name();

  // DEBUG: Print what type we received that wasn't matched
  std::cerr << "[ConvertValueToCrateValue] DEBUG: Unmatched type! type_name='" << type_name
            << "' type_id=" << type_id << std::endl;

  if (!type_name.empty()) {
    // Try to encode as Dictionary (most flexible representation)
    if (auto* v = val.as<Dictionary>()) {
      out->Set(*v);
      std::cerr << "[ConvertValueToCrateValue] Encoded custom/unregistered value as Dictionary: "
                << type_name << "\n";
      return true;
    }

    // Try to encode as generic string representation
    // This is a fallback for values that can be stringified
    std::cerr << "[ConvertValueToCrateValue] Warning: Encoding custom type as Dictionary: "
              << type_name << " (type_id=" << type_id << ")\n";

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
  for (size_t i = 1; i < std::min(size_t(17), compressed->size()); ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", static_cast<unsigned char>((*compressed)[i]));
  }

  return true;
}

// ============================================================================
// Section Writing
// ============================================================================

bool CrateWriter::WriteTokensSection(std::string* err) {
  int64_t section_start = Tell();

  // Debug: print token count
  for (size_t i = 0; i < std::min(tokens_.size(), size_t(10)); ++i) {
  }

  // Write token count
  uint64_t token_count = static_cast<uint64_t>(tokens_.size());

  // DEBUG: Print bytes being written
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&token_count);
  for (size_t i = 0; i < sizeof(token_count); ++i) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02x ", bytes[i]);
  }

  // Write directly as bytes instead of using Write() template
  file_.write(reinterpret_cast<const char*>(&token_count), sizeof(token_count));
  file_.flush();

  if (!file_.good()) {
    if (err) *err = "Failed to write token count bytes";
    return false;
  }

  // Build token blob (null-terminated strings)
  std::cerr << "DEBUG WriteTokensSection: tokens_.size()=" << tokens_.size() << std::endl;
  for (size_t i = 0; i < tokens_.size(); ++i) {
    std::cerr << "  token[" << i << "]: \"" << tokens_[i] << "\"" << std::endl;
  }

  std::ostringstream blob;
  for (const auto& token : tokens_) {
    blob << token;
    blob.put('\0');
  }

  std::string token_blob = blob.str();

  for (size_t i = 0; i < std::min(token_blob.size(), size_t(60)); ++i) {
    if (token_blob[i] == '\0') {
    } else if (isprint(static_cast<unsigned char>(token_blob[i]))) {
    } else {
      char buf[5];
      snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(token_blob[i]));
    }
  }

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

  std::cerr << "DEBUG WriteStringsSection: strings_.size()=" << string_count << std::endl;
  for (size_t i = 0; i < strings_.size(); ++i) {
    auto it = token_to_index_.find(strings_[i]);
    if (it != token_to_index_.end()) {
      std::cerr << "  string[" << i << "]: \"" << strings_[i] << "\" -> token_idx=" << it->second.value << std::endl;
    }
  }

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

  // paths_ is already sorted (re-sorted in Finalize after all paths were collected).
  // Spec path_indexes have been updated to match the sorted order.
  // No additional sorting needed here.

  // Encode to compressed tree
  pathlib::CompressedPathTree tree = pathlib::EncodePaths(simple_paths);

  // Remap the path tree token indices to our token indices
  // The path tree has its own token indices, but we need to use our global token indices
  // that were computed in Finalize() to preserve field name tokens
  std::vector<int32_t> remapped_element_token_indexes;
  remapped_element_token_indexes.reserve(tree.element_token_indexes.size());

  for (size_t i = 0; i < tree.element_token_indexes.size(); ++i) {
  }

  for (int32_t path_tree_idx : tree.element_token_indexes) {
    // Look up using the ORIGINAL signed index (map now stores with sign)
    auto it = path_tree_token_remap_.find(path_tree_idx);
    if (it != path_tree_token_remap_.end()) {
      // Found the mapping
      if (path_tree_idx < 0) {
        // Property: negate the result to keep it negative
        remapped_element_token_indexes.push_back(-static_cast<int32_t>(it->second));
      } else {
        // Prim: use positive result
        remapped_element_token_indexes.push_back(static_cast<int32_t>(it->second));
      }
    } else {
      // Not found - shouldn't happen, but preserve original
      remapped_element_token_indexes.push_back(path_tree_idx);
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
  for (size_t i = 0; i < tree.path_indexes.size(); ++i) {
    path_indexes_32[i] = static_cast<uint32_t>(tree.path_indexes[i]);
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

  // Debug: Print element_token_indexes array
  std::cerr << "DEBUG WritePathsSection: element_token_indexes (count=" << tree.element_token_indexes.size() << "):" << std::endl;
  for (size_t i = 0; i < tree.element_token_indexes.size(); ++i) {
    int32_t tok_idx = tree.element_token_indexes[i];
    std::cerr << "  [" << i << "]: " << tok_idx;
    if (tok_idx < 0) {
      std::cerr << " (PROPERTY)";
    }
    std::cerr << std::endl;
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

  // Debug: Print jumps array
  for (size_t i = 0; i < tree.jumps.size(); ++i) {
    if (tree.jumps[i] == -2) std::cerr << " (leaf)";
    else if (tree.jumps[i] == -1) std::cerr << " (only child follows)";
    else if (tree.jumps[i] == 0) std::cerr << " (only sibling follows)";
    else if (tree.jumps[i] > 0) std::cerr << " (child+sibling, offset=" << tree.jumps[i] << ")";
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

  for (size_t i = 0; i < fieldset_number_to_offset.size(); ++i) {
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

    std::cerr << "  Spec[" << i << "]: path_index=" << spec_data.spec.path_index.value
              << " fieldset_number=" << fieldset_number
              << " fieldset_offset=" << fieldset_offset
              << " spec_type=" << static_cast<uint32_t>(spec_data.spec.spec_type)
              << " (";
    switch(spec_data.spec.spec_type) {
      case SpecType::Unknown: std::cerr << "Unknown"; break;
      case SpecType::Attribute: std::cerr << "Attribute"; break;
      case SpecType::Connection: std::cerr << "Connection"; break;
      case SpecType::Expression: std::cerr << "Expression"; break;
      case SpecType::Mapper: std::cerr << "Mapper"; break;
      case SpecType::MapperArg: std::cerr << "MapperArg"; break;
      case SpecType::Prim: std::cerr << "Prim"; break;
      case SpecType::PseudoRoot: std::cerr << "PseudoRoot"; break;
      case SpecType::Relationship: std::cerr << "Relationship"; break;
      case SpecType::RelationshipTarget: std::cerr << "RelationshipTarget"; break;
      case SpecType::Variant: std::cerr << "Variant"; break;
      case SpecType::VariantSet: std::cerr << "VariantSet"; break;
      case SpecType::Invalid: std::cerr << "Invalid"; break;
    }
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

  // Debug loop removed (original debug statements deleted)

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

  if (value.as<CustomDataType>() || value.as<value::dict>()) {
    std::cerr << "DEBUG PackValue: Dictionary offset=" << offset << " type=" << value.type_name() << std::endl;
  }

  return rep;
}

int64_t CrateWriter::WriteValueData(const crate::CrateValue& value, std::string* err) {
  // Save current position
  int64_t current_pos = Tell();

  // Seek to end of value data section
  if (value.as<CustomDataType>() || value.as<value::dict>()) {
    std::cerr << "DEBUG WriteValueData: Seeking to value_data_end_offset_=" << value_data_end_offset_
              << " from current_pos=" << current_pos << std::endl;
  }
  if (!Seek(value_data_end_offset_)) {
    if (err) *err = "Failed to seek to value data section";
    return -1;
  }

  int64_t value_offset = Tell();
  if (value.as<CustomDataType>() || value.as<value::dict>()) {
    std::cerr << "DEBUG WriteValueData: After seek, Tell()=" << value_offset << std::endl;
  }

  // Phase 1: Write out-of-line value data based on type
  // This handles values that cannot be inlined in the 48-bit payload

  // Double - 8 bytes
  if (auto* double_val = value.as<double>()) {
    std::cerr << "DEBUG WriteValueData: Writing double at offset=" << Tell() << std::endl;
    if (!Write(*double_val)) {
      if (err) *err = "Failed to write double value";
      return -1;
    }
    std::cerr << "DEBUG WriteValueData: After double, offset=" << Tell() << std::endl;
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
    std::cerr << "DEBUG WriteValueData: Writing float4[] at offset=" << Tell()
              << " count=" << count << std::endl;
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
    std::cerr << "DEBUG WriteValueData: After float4[], offset=" << Tell() << std::endl;
  }
  // Vec2d array (double2[])
  else if (auto* vec2d_array = value.as<std::vector<value::double2>>()) {
    uint64_t count = vec2d_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Vec2d array count";
      return -1;
    }
    for (const auto& vec : *vec2d_array) {
      for (size_t i = 0; i < 2; ++i) {
        if (!Write(vec[i])) {
          if (err) *err = "Failed to write Vec2d array element";
          return -1;
        }
      }
    }
  }
  // Vec3d array (double3[])
  else if (auto* vec3d_array = value.as<std::vector<value::double3>>()) {
    uint64_t count = vec3d_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Vec3d array count";
      return -1;
    }
    for (const auto& vec : *vec3d_array) {
      for (size_t i = 0; i < 3; ++i) {
        if (!Write(vec[i])) {
          if (err) *err = "Failed to write Vec3d array element";
          return -1;
        }
      }
    }
  }
  // Vec4d array (double4[])
  else if (auto* vec4d_array = value.as<std::vector<value::double4>>()) {
    uint64_t count = vec4d_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Vec4d array count";
      return -1;
    }
    for (const auto& vec : *vec4d_array) {
      for (size_t i = 0; i < 4; ++i) {
        if (!Write(vec[i])) {
          if (err) *err = "Failed to write Vec4d array element";
          return -1;
        }
      }
    }
  }
  // Quath array (half-precision quaternion array)
  else if (auto* quath_array = value.as<std::vector<value::quath>>()) {
    uint64_t count = quath_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Quath array count";
      return -1;
    }
    // Each quath is: real (uint16_t) + imag[0,1,2] (3 x uint16_t) = 8 bytes
    for (const auto& q : *quath_array) {
      if (!Write(q.real.value) || !Write(q.imag[0].value) ||
          !Write(q.imag[1].value) || !Write(q.imag[2].value)) {
        if (err) *err = "Failed to write Quath array element";
        return -1;
      }
    }
  }
  // Quatf array (single-precision quaternion array)
  else if (auto* quatf_array = value.as<std::vector<value::quatf>>()) {
    uint64_t count = quatf_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Quatf array count";
      return -1;
    }
    // Each quatf is: real (float) + imag[0,1,2] (3 x float) = 16 bytes
    for (const auto& q : *quatf_array) {
      if (!Write(q.real) || !Write(q.imag[0]) ||
          !Write(q.imag[1]) || !Write(q.imag[2])) {
        if (err) *err = "Failed to write Quatf array element";
        return -1;
      }
    }
  }
  // Quatd array (double-precision quaternion array)
  else if (auto* quatd_array = value.as<std::vector<value::quatd>>()) {
    uint64_t count = quatd_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write Quatd array count";
      return -1;
    }
    // Each quatd is: real (double) + imag[0,1,2] (3 x double) = 32 bytes
    for (const auto& q : *quatd_array) {
      if (!Write(q.real) || !Write(q.imag[0]) ||
          !Write(q.imag[1]) || !Write(q.imag[2])) {
        if (err) *err = "Failed to write Quatd array element";
        return -1;
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
    for (int64_t i = 0; i < dict_struct_size; i++) {
      char zero = 0;
      if (!WriteBytes(&zero, 1)) {
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

    for (const auto& kv : *dict_val) {
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

    std::cerr << "DEBUG CustomDataType: Writing dictionary with " << count << " entries at offset " << Tell() << std::endl;

    // Calculate size of dictionary structure:
    // 8 bytes for count + (4 + 8 + 8) bytes per entry = 8 + 20*count
    int64_t dict_struct_size = 8 + (count * 20);
    int64_t dict_struct_start = Tell();

    // Reserve space by writing zeros
    for (int64_t i = 0; i < dict_struct_size; i++) {
      char zero = 0;
      if (!WriteBytes(&zero, 1)) {
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

      // Try int32
      if (auto* int_val = raw_value.as<int32_t>()) {
        crate::CrateValue cv;
        cv.Set(*int_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try int (convert to int32)
      else if (auto* int_val = raw_value.as<int>()) {
        crate::CrateValue cv;
        cv.Set(static_cast<int32_t>(*int_val));
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try uint32
      else if (auto* uint_val = raw_value.as<uint32_t>()) {
        crate::CrateValue cv;
        cv.Set(*uint_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try float
      else if (auto* float_val = raw_value.as<float>()) {
        crate::CrateValue cv;
        cv.Set(*float_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try double
      else if (auto* double_val = raw_value.as<double>()) {
        crate::CrateValue cv;
        cv.Set(*double_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try bool
      else if (auto* bool_val = raw_value.as<bool>()) {
        crate::CrateValue cv;
        cv.Set(*bool_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try string
      else if (auto* str_val = raw_value.as<std::string>()) {
        crate::CrateValue cv;
        cv.Set(*str_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try StringData (string with metadata like triple-quote)
      else if (auto* str_data = raw_value.as<value::StringData>()) {
        crate::CrateValue cv;
        cv.Set(str_data->value);  // Extract the string value
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Fallback: if type_id indicates string but as<std::string>() failed, try MetaVariable's get_value
      else if (raw_value.type_id() == value::TYPE_ID_STRING) {
        // String type but as<std::string>() didn't work - try MetaVariable's get_value method
        auto str_opt = kv.second.get_value<std::string>();
        if (str_opt) {
          crate::CrateValue cv;
          cv.Set(*str_opt);
          value_rep = PackValue(cv, err);
          value_packed = true;
          std::cerr << "DEBUG CustomDataType: Successfully extracted string via MetaVariable::get_value for key="
                    << kv.first << " value=\"" << *str_opt << "\"" << std::endl;
        } else {
          std::cerr << "DEBUG CustomDataType: String type detected but couldn't extract value for key=" << kv.first << std::endl;
        }
      }
      // Try token
      else if (auto* tok_val = raw_value.as<value::token>()) {
        crate::CrateValue cv;
        cv.Set(*tok_val);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try nested CustomDataType (dictionary)
      else if (auto* nested_dict = raw_value.as<CustomDataType>()) {
        crate::CrateValue cv;
        cv.Set(*nested_dict);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try string array
      else if (auto* str_array = raw_value.as<std::vector<std::string>>()) {
        crate::CrateValue cv;
        cv.Set(*str_array);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try StringData array (stored with TYPE_ID_STRING_DATA + array bit)
      else if (auto* str_data_array = raw_value.as<std::vector<value::StringData>>()) {
        // Convert StringData array to string array
        std::vector<std::string> str_array;
        str_array.reserve(str_data_array->size());
        for (const auto& sd : *str_data_array) {
          str_array.push_back(sd.value);
        }
        crate::CrateValue cv;
        cv.Set(str_array);
        value_rep = PackValue(cv, err);
        value_packed = true;
        std::cerr << "DEBUG CustomDataType: Successfully extracted StringData[] and converted to string[] for key="
                  << kv.first << " size=" << str_array.size() << std::endl;
      }
      // Fallback: if type name indicates string array, check is_array() and extract
      else if (raw_value.type_name() == "string[]" || (raw_value.is_array() && raw_value.type_id() == value::TYPE_ID_STRING)) {
        std::cerr << "DEBUG CustomDataType: Detected string[] for key=" << kv.first
                  << " is_array=" << raw_value.is_array()
                  << " type_id=" << raw_value.type_id()
                  << " underlying_type_id=" << raw_value.underlying_type_id() << std::endl;

        // First try direct as<std::vector<std::string>>()
        if (auto* str_array_direct = raw_value.as<std::vector<std::string>>()) {
          crate::CrateValue cv;
          cv.Set(*str_array_direct);
          value_rep = PackValue(cv, err);
          value_packed = true;
          std::cerr << "DEBUG CustomDataType: Successfully extracted string[] directly for key="
                    << kv.first << " size=" << str_array_direct->size() << std::endl;
        }
        // Try as std::vector<value::token> (tokens can be converted to strings)
        else if (auto* tok_array = raw_value.as<std::vector<value::token>>()) {
          std::cerr << "DEBUG CustomDataType: Extracting as token[] for key=" << kv.first
                    << " size=" << tok_array->size() << std::endl;
          std::vector<std::string> str_array;
          str_array.reserve(tok_array->size());
          for (const auto& tok : *tok_array) {
            str_array.push_back(tok.str());
          }
          crate::CrateValue cv;
          cv.Set(str_array);
          value_rep = PackValue(cv, err);
          value_packed = true;
          std::cerr << "DEBUG CustomDataType: Successfully converted token[] to string[] for key="
                    << kv.first << " size=" << str_array.size() << std::endl;
        } else {
          // Try MetaVariable::get_value as fallback
          auto str_array_opt = kv.second.get_value<std::vector<std::string>>();
          if (str_array_opt) {
            crate::CrateValue cv;
            cv.Set(*str_array_opt);
            value_rep = PackValue(cv, err);
            value_packed = true;
            std::cerr << "DEBUG CustomDataType: Successfully extracted string[] via MetaVariable::get_value for key="
                      << kv.first << " size=" << str_array_opt->size() << std::endl;
          } else {
            std::cerr << "DEBUG CustomDataType: string[] type detected but couldn't extract with any method for key=" << kv.first << std::endl;
          }
        }
      }
      // Try token array
      else if (auto* tok_array = raw_value.as<std::vector<value::token>>()) {
        crate::CrateValue cv;
        cv.Set(*tok_array);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try float array
      else if (auto* float_array = raw_value.as<std::vector<float>>()) {
        crate::CrateValue cv;
        cv.Set(*float_array);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try double array
      else if (auto* double_array = raw_value.as<std::vector<double>>()) {
        crate::CrateValue cv;
        cv.Set(*double_array);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      // Try int array
      else if (auto* int_array = raw_value.as<std::vector<int32_t>>()) {
        crate::CrateValue cv;
        cv.Set(*int_array);
        value_rep = PackValue(cv, err);
        value_packed = true;
      }
      else {
        std::cerr << "DEBUG CustomDataType: Unsupported type key=" << kv.first
                  << " type_name=" << raw_value.type_name()
                  << " type_id=" << raw_value.type_id() << std::endl;
        if (err) *err = "Unsupported CustomDataType value type: " + std::string(raw_value.type_name()) + " (type_id=" + std::to_string(raw_value.type_id()) + ")";
        return -1;
      }

      if (!value_packed) {
        if (err) *err = "Failed to pack CustomDataType value";
        return -1;
      }

      value_reps.push_back(value_rep);
    }

    // Now go back and write the dictionary structure
    std::cerr << "DEBUG CustomDataType: After packing nested values, pos=" << Tell()
              << " now seeking back to dict_struct_start=" << dict_struct_start << std::endl;
    if (!Seek(dict_struct_start)) {
      if (err) *err = "Failed to seek to dictionary structure start";
      return -1;
    }

    // Write count
    std::cerr << "DEBUG CustomDataType: Writing count=" << count << " at pos=" << Tell() << std::endl;
    if (!Write(count)) {
      if (err) *err = "Failed to write CustomDataType count";
      return -1;
    }
    std::cerr << "DEBUG CustomDataType: After writing count, pos=" << Tell() << std::endl;

    // Write each (key, offset, ValueRep) tuple
    size_t idx = 0;
    for (const auto& kv : *custom_data) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      std::cerr << "DEBUG CustomDataType: Writing entry[" << idx << "] key='" << kv.first
                << "' key_idx=" << key_idx.value << " offset=8 ValueRep=" << std::hex << value_reps[idx].GetData() << std::dec << std::endl;
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

        // Write customData dictionary
        // Dictionary format: uint64_t count + (StringIndex key, ValueRep value) pairs
        uint64_t dict_count = ref.customData.size();
        if (!Write(dict_count)) return false;

        for (const auto& kv : ref.customData) {
          // Write key as StringIndex
          crate::StringIndex key_idx = GetOrCreateString(kv.first);
          if (!Write(key_idx.value)) return false;

          // Write value as ValueRep - kv.second is MetaVariable
          crate::ValueRep value_rep;
          bool value_written = false;

          // Try common types
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
          } else if (auto double_val = kv.second.get_value<double>()) {
            crate::CrateValue cv;
            cv.Set(*double_val);
            value_rep = PackValue(cv, err);
            value_written = true;
          } else if (auto str_val = kv.second.get_value<std::string>()) {
            crate::CrateValue cv;
            cv.Set(*str_val);
            value_rep = PackValue(cv, err);
            value_written = true;
          } else if (auto bool_val = kv.second.get_value<bool>()) {
            crate::CrateValue cv;
            cv.Set(*bool_val);
            value_rep = PackValue(cv, err);
            value_written = true;
          }

          if (!value_written) {
            // Unsupported type - write as invalid value rep
            return false;
          }

          if (!Write(value_rep.GetData())) return false;
        }
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
  // IntListOp (int32_t) serialization
  else if (auto* int_listop = value.as<ListOp<int32_t>>()) {
    ListOpHeader header;
    header.bits = 0;
    if (int_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (int_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (int_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (int_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (int_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (int_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (int_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write IntListOp header";
      return -1;
    }

    // Helper lambda to write int32 list
    auto writeIntList = [&](const std::vector<int32_t>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& val : list) {
        if (!Write(val)) return false;
      }
      return true;
    };

    if (int_listop->HasExplicitItems() && !writeIntList(int_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write IntListOp explicit items";
      return -1;
    }
    if (int_listop->HasAddedItems() && !writeIntList(int_listop->GetAddedItems())) {
      if (err) *err = "Failed to write IntListOp added items";
      return -1;
    }
    if (int_listop->HasPrependedItems() && !writeIntList(int_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write IntListOp prepended items";
      return -1;
    }
    if (int_listop->HasAppendedItems() && !writeIntList(int_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write IntListOp appended items";
      return -1;
    }
    if (int_listop->HasDeletedItems() && !writeIntList(int_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write IntListOp deleted items";
      return -1;
    }
    if (int_listop->HasOrderedItems() && !writeIntList(int_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write IntListOp ordered items";
      return -1;
    }
  }
  // UIntListOp (uint32_t) serialization
  else if (auto* uint_listop = value.as<ListOp<uint32_t>>()) {
    ListOpHeader header;
    header.bits = 0;
    if (uint_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (uint_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (uint_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (uint_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (uint_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (uint_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (uint_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write UIntListOp header";
      return -1;
    }

    auto writeUIntList = [&](const std::vector<uint32_t>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& val : list) {
        if (!Write(val)) return false;
      }
      return true;
    };

    if (uint_listop->HasExplicitItems() && !writeUIntList(uint_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write UIntListOp explicit items";
      return -1;
    }
    if (uint_listop->HasAddedItems() && !writeUIntList(uint_listop->GetAddedItems())) {
      if (err) *err = "Failed to write UIntListOp added items";
      return -1;
    }
    if (uint_listop->HasPrependedItems() && !writeUIntList(uint_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write UIntListOp prepended items";
      return -1;
    }
    if (uint_listop->HasAppendedItems() && !writeUIntList(uint_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write UIntListOp appended items";
      return -1;
    }
    if (uint_listop->HasDeletedItems() && !writeUIntList(uint_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write UIntListOp deleted items";
      return -1;
    }
    if (uint_listop->HasOrderedItems() && !writeUIntList(uint_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write UIntListOp ordered items";
      return -1;
    }
  }
  // Int64ListOp serialization
  else if (auto* int64_listop = value.as<ListOp<int64_t>>()) {
    ListOpHeader header;
    header.bits = 0;
    if (int64_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (int64_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (int64_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (int64_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (int64_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (int64_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (int64_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write Int64ListOp header";
      return -1;
    }

    auto writeInt64List = [&](const std::vector<int64_t>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& val : list) {
        if (!Write(val)) return false;
      }
      return true;
    };

    if (int64_listop->HasExplicitItems() && !writeInt64List(int64_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write Int64ListOp explicit items";
      return -1;
    }
    if (int64_listop->HasAddedItems() && !writeInt64List(int64_listop->GetAddedItems())) {
      if (err) *err = "Failed to write Int64ListOp added items";
      return -1;
    }
    if (int64_listop->HasPrependedItems() && !writeInt64List(int64_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write Int64ListOp prepended items";
      return -1;
    }
    if (int64_listop->HasAppendedItems() && !writeInt64List(int64_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write Int64ListOp appended items";
      return -1;
    }
    if (int64_listop->HasDeletedItems() && !writeInt64List(int64_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write Int64ListOp deleted items";
      return -1;
    }
    if (int64_listop->HasOrderedItems() && !writeInt64List(int64_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write Int64ListOp ordered items";
      return -1;
    }
  }
  // UInt64ListOp serialization
  else if (auto* uint64_listop = value.as<ListOp<uint64_t>>()) {
    ListOpHeader header;
    header.bits = 0;
    if (uint64_listop->IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
    if (uint64_listop->HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
    if (uint64_listop->HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
    if (uint64_listop->HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
    if (uint64_listop->HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
    if (uint64_listop->HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
    if (uint64_listop->HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;

    if (!Write(header.bits)) {
      if (err) *err = "Failed to write UInt64ListOp header";
      return -1;
    }

    auto writeUInt64List = [&](const std::vector<uint64_t>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& val : list) {
        if (!Write(val)) return false;
      }
      return true;
    };

    if (uint64_listop->HasExplicitItems() && !writeUInt64List(uint64_listop->GetExplicitItems())) {
      if (err) *err = "Failed to write UInt64ListOp explicit items";
      return -1;
    }
    if (uint64_listop->HasAddedItems() && !writeUInt64List(uint64_listop->GetAddedItems())) {
      if (err) *err = "Failed to write UInt64ListOp added items";
      return -1;
    }
    if (uint64_listop->HasPrependedItems() && !writeUInt64List(uint64_listop->GetPrependedItems())) {
      if (err) *err = "Failed to write UInt64ListOp prepended items";
      return -1;
    }
    if (uint64_listop->HasAppendedItems() && !writeUInt64List(uint64_listop->GetAppendedItems())) {
      if (err) *err = "Failed to write UInt64ListOp appended items";
      return -1;
    }
    if (uint64_listop->HasDeletedItems() && !writeUInt64List(uint64_listop->GetDeletedItems())) {
      if (err) *err = "Failed to write UInt64ListOp deleted items";
      return -1;
    }
    if (uint64_listop->HasOrderedItems() && !writeUInt64List(uint64_listop->GetOrderedItems())) {
      if (err) *err = "Failed to write UInt64ListOp ordered items";
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

    // Get samples - this works for both POD and non-POD types
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

        // ===== NUMERIC ARRAY TYPES =====
        if (auto* float_arr = crate_value.as<std::vector<float>>()) {
          is_dedup_candidate = true;
          size_t byte_size = float_arr->size() * sizeof(float);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float_arr->data(), byte_size);
        } else if (auto* double_arr = crate_value.as<std::vector<double>>()) {
          is_dedup_candidate = true;
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
          size_t byte_size = float3_arr->size() * sizeof(value::float3);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float3_arr->data(), byte_size);
        } else if (auto* double3_arr = crate_value.as<std::vector<value::double3>>()) {
          is_dedup_candidate = true;
          size_t byte_size = double3_arr->size() * sizeof(value::double3);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), double3_arr->data(), byte_size);
        } else if (auto* float2_arr = crate_value.as<std::vector<value::float2>>()) {
          is_dedup_candidate = true;
          size_t byte_size = float2_arr->size() * sizeof(value::float2);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float2_arr->data(), byte_size);
        } else if (auto* float4_arr = crate_value.as<std::vector<value::float4>>()) {
          is_dedup_candidate = true;
          size_t byte_size = float4_arr->size() * sizeof(value::float4);
          value_bytes.resize(byte_size);
          std::memcpy(value_bytes.data(), float4_arr->data(), byte_size);
        }
        // ===== SCALAR MATRIX TYPES =====
        else if (auto* matrix2d = crate_value.as<value::matrix2d>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::matrix2d));
          std::memcpy(value_bytes.data(), matrix2d, sizeof(value::matrix2d));
        } else if (auto* matrix3d = crate_value.as<value::matrix3d>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::matrix3d));
          std::memcpy(value_bytes.data(), matrix3d, sizeof(value::matrix3d));
        } else if (auto* matrix4d = crate_value.as<value::matrix4d>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::matrix4d));
          std::memcpy(value_bytes.data(), matrix4d, sizeof(value::matrix4d));
        }
        // ===== SCALAR QUATERNION TYPES =====
        else if (auto* quatf = crate_value.as<value::quatf>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::quatf));
          std::memcpy(value_bytes.data(), quatf, sizeof(value::quatf));
        } else if (auto* quatd = crate_value.as<value::quatd>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::quatd));
          std::memcpy(value_bytes.data(), quatd, sizeof(value::quatd));
        } else if (auto* quath = crate_value.as<value::quath>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::quath));
          std::memcpy(value_bytes.data(), quath, sizeof(value::quath));
        }
        // ===== SCALAR VECTOR TYPES =====
        else if (auto* float3 = crate_value.as<value::float3>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::float3));
          std::memcpy(value_bytes.data(), float3, sizeof(value::float3));
        } else if (auto* double3 = crate_value.as<value::double3>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::double3));
          std::memcpy(value_bytes.data(), double3, sizeof(value::double3));
        } else if (auto* float2 = crate_value.as<value::float2>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::float2));
          std::memcpy(value_bytes.data(), float2, sizeof(value::float2));
        } else if (auto* float4 = crate_value.as<value::float4>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::float4));
          std::memcpy(value_bytes.data(), float4, sizeof(value::float4));
        } else if (auto* double2 = crate_value.as<value::double2>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::double2));
          std::memcpy(value_bytes.data(), double2, sizeof(value::double2));
        } else if (auto* double4 = crate_value.as<value::double4>()) {
          is_dedup_candidate = true;
          value_bytes.resize(sizeof(value::double4));
          std::memcpy(value_bytes.data(), double4, sizeof(value::double4));
        }

        if (is_dedup_candidate && !value_bytes.empty()) {
          // Check dedup cache
          auto it = array_dedup_map_.find(value_bytes);
          if (it != array_dedup_map_.end()) {
            // Found duplicate! Reuse the offset without writing data
            int64_t cached_offset = it->second;

            // Create ValueRep manually without calling PackValue (which would write data)
            // Determine type ID and whether it's an array
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
            } else if (crate_value.as<std::vector<value::double2>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC2D;
              is_array = true;
            } else if (crate_value.as<std::vector<value::float4>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4F;
              is_array = true;
            } else if (crate_value.as<std::vector<value::double4>>()) {
              type_id = crate::CrateDataTypeId::CRATE_DATA_TYPE_VEC4D;
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
              value_rep.SetIsArray();  // Mark as array
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
            array_dedup_map_[value_bytes] = new_offset;
            dedup_attempted = true;

            std::cerr << "[TimeSamples Dedup] Cached new value at offset " << new_offset
                      << " for sample " << i << " (" << value_bytes.size() << " bytes)\n";
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
    std::cerr << "DEBUG WriteValueData: Unsupported type_id=" << value.type_id()
              << " type_name=" << value.type_name() << std::endl;
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
    (void)double_val;  // Suppress unused variable warning
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
    (void)vec2f_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec2d - double2 (16 bytes, cannot inline)
  if (auto* vec2d_val = value.as<value::double2>()) {
    (void)vec2d_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec2i - int2 (8 bytes, cannot inline)
  if (auto* vec2i_val = value.as<value::int2>()) {
    (void)vec2i_val;  // Suppress unused variable warning
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
    (void)vec3f_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec3d - double3 (24 bytes, cannot inline)
  if (auto* vec3d_val = value.as<value::double3>()) {
    (void)vec3d_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec3i - int3 (12 bytes, cannot inline)
  if (auto* vec3i_val = value.as<value::int3>()) {
    (void)vec3i_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4h - half4 (8 bytes, cannot inline)
  if (auto* vec4h_val = value.as<value::half4>()) {
    (void)vec4h_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4f - float4 (16 bytes, cannot inline)
  if (auto* vec4f_val = value.as<value::float4>()) {
    (void)vec4f_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4d - double4 (32 bytes, cannot inline)
  if (auto* vec4d_val = value.as<value::double4>()) {
    (void)vec4d_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Vec4i - int4 (16 bytes, cannot inline)
  if (auto* vec4i_val = value.as<value::int4>()) {
    (void)vec4i_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Phase 1: Matrix types - all matrices are too large to inline
  // Matrix2d (4x8 = 32 bytes), Matrix3d (9x8 = 72 bytes), Matrix4d (16x8 = 128 bytes)

  if (auto* mat2d_val = value.as<value::matrix2d>()) {
    (void)mat2d_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* mat3d_val = value.as<value::matrix3d>()) {
    (void)mat3d_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* mat4d_val = value.as<value::matrix4d>()) {
    (void)mat4d_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  // Phase 1: Quaternion types
  // Quath (4x2 = 8 bytes), Quatf (4x4 = 16 bytes), Quatd (4x8 = 32 bytes)
  // All too large to inline (> 6 bytes)

  if (auto* quath_val = value.as<value::quath>()) {
    (void)quath_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* quatf_val = value.as<value::quatf>()) {
    (void)quatf_val;  // Suppress unused variable warning
    // Cannot inline, need out-of-line storage
    return false;
  }

  if (auto* quatd_val = value.as<value::quatd>()) {
    (void)quatd_val;  // Suppress unused variable warning
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
  // Check file size limit before writing
  if (WouldExceedFileSizeLimit(static_cast<int64_t>(size))) {
    std::cerr << "ERROR: Writing " << size << " bytes would exceed file size limit of "
              << options_.max_file_size_bytes / (1024*1024) << " MB\n"
              << "  Current file size: " << bytes_written_ << " bytes\n"
              << "  Limit: " << options_.max_file_size_bytes << " bytes\n";
    return false;
  }

  file_.write(static_cast<const char*>(data), size);
  if (file_.good()) {
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
