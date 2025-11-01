// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer Implementation
#include "../include/crate-writer.hh"

#include <algorithm>
#include <cstring>
#include <sstream>

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
// Public API
// ============================================================================

bool CrateWriter::Open(std::string* err) {
  if (is_open_) {
    if (err) *err = "File already open";
    return false;
  }

  // Open file for binary writing
  file_.open(filepath_, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!file_.is_open()) {
    if (err) *err = "Failed to open file: " + filepath_;
    return false;
  }

  is_open_ = true;

  // Reserve space for bootstrap header (we'll write it at the end)
  // Bootstrap is always 64 bytes at offset 0
  char zeros[64] = {0};
  if (!WriteBytes(zeros, 64)) {
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
    spec_data.spec.spec_type = SpecType::Prim; // TODO: detect proper type from context
  }

  // ========================================================================
  // Step 2: Write all structural sections
  // ========================================================================

  // Mark end of value data section
  value_data_end_offset_ = Tell();

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
    file_.close();
  }
  is_open_ = false;
}

// ============================================================================
// Section Writing
// ============================================================================

bool CrateWriter::WriteTokensSection(std::string* err) {
  int64_t section_start = Tell();

  // Write token count
  uint64_t token_count = static_cast<uint64_t>(tokens_.size());
  if (!Write(token_count)) {
    if (err) *err = "Failed to write token count";
    return false;
  }

  // Build token blob (null-terminated strings)
  std::ostringstream blob;
  for (const auto& token : tokens_) {
    blob << token;
    blob.put('\0');
  }

  std::string token_blob = blob.str();

  // TODO: Compress the blob if compression is enabled

  // Write blob size and data
  uint64_t blob_size = static_cast<uint64_t>(token_blob.size());
  if (!Write(blob_size)) {
    if (err) *err = "Failed to write token blob size";
    return false;
  }

  if (!WriteBytes(token_blob.data(), token_blob.size())) {
    if (err) *err = "Failed to write token blob";
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

  // Write field count
  uint64_t field_count = static_cast<uint64_t>(fields_.size());
  if (!Write(field_count)) {
    if (err) *err = "Failed to write field count";
    return false;
  }

  // Write fields
  // TODO: Compress if enabled
  for (const auto& field : fields_) {
    if (!Write(field.token_index)) {
      if (err) *err = "Failed to write field token index";
      return false;
    }
    if (!Write(field.value_rep)) {
      if (err) *err = "Failed to write field value rep";
      return false;
    }
  }

  int64_t section_end = Tell();

  crate::Section section(kFieldsSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WriteFieldSetsSection(std::string* err) {
  int64_t section_start = Tell();

  // Write fieldset count
  uint64_t fieldset_count = static_cast<uint64_t>(fieldsets_.size());
  if (!Write(fieldset_count)) {
    if (err) *err = "Failed to write fieldset count";
    return false;
  }

  // Write fieldsets as null-terminated lists of FieldIndex
  // TODO: Compress if enabled
  for (const auto& fieldset : fieldsets_) {
    for (const auto& field_idx : fieldset) {
      if (!Write(field_idx)) {
        if (err) *err = "Failed to write field index";
        return false;
      }
    }
    // Write terminator (index with value ~0u)
    crate::FieldIndex terminator;
    if (!Write(terminator)) {
      if (err) *err = "Failed to write fieldset terminator";
      return false;
    }
  }

  int64_t section_end = Tell();

  crate::Section section(kFieldSetsSection, section_start, section_end - section_start);
  toc_.sections.push_back(section);

  return true;
}

bool CrateWriter::WritePathsSection(std::string* err) {
  int64_t section_start = Tell();

  // Use the path sorting and encoding library
  // Convert TinyUSDZ Path to SimplePath for encoding
  std::vector<pathlib::SimplePath> simple_paths;
  for (const auto& path : paths_) {
    simple_paths.emplace_back(path.prim_part(), path.prop_part());
  }

  // Sort paths
  pathlib::SortSimplePaths(simple_paths);

  // Encode to compressed tree
  pathlib::CompressedPathTree tree = pathlib::EncodePaths(simple_paths);

  // Write the compressed tree
  // Format: count + three arrays

  uint64_t path_count = static_cast<uint64_t>(tree.size());
  if (!Write(path_count)) {
    if (err) *err = "Failed to write path count";
    return false;
  }

  // Write path_indexes array
  // TODO: Compress if enabled
  for (size_t i = 0; i < tree.size(); ++i) {
    if (!Write(tree.path_indexes[i])) {
      if (err) *err = "Failed to write path index";
      return false;
    }
  }

  // Write element_token_indexes array
  // TODO: Compress if enabled
  for (size_t i = 0; i < tree.size(); ++i) {
    if (!Write(tree.element_token_indexes[i])) {
      if (err) *err = "Failed to write element token index";
      return false;
    }
  }

  // Write jumps array
  // TODO: Compress if enabled
  for (size_t i = 0; i < tree.size(); ++i) {
    if (!Write(tree.jumps[i])) {
      if (err) *err = "Failed to write jump value";
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

  // Write spec count
  uint64_t spec_count = static_cast<uint64_t>(spec_data_.size());
  if (!Write(spec_count)) {
    if (err) *err = "Failed to write spec count";
    return false;
  }

  // Write specs
  // TODO: Sort specs by path for better compression
  // TODO: Compress if enabled
  for (const auto& spec_data : spec_data_) {
    if (!Write(spec_data.spec)) {
      if (err) *err = "Failed to write spec";
      return false;
    }
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

  // Seek back to write bootstrap
  BootStrap boot;
  memset(&boot, 0, sizeof(boot));

  memcpy(boot.ident, kMagicIdent, 8);
  boot.version[0] = options_.version_major;
  boot.version[1] = options_.version_minor;
  boot.version[2] = options_.version_patch;
  boot.toc_offset = saved_toc_offset;

  if (!Seek(0)) {
    if (err) *err = "Failed to seek to bootstrap";
    return false;
  }

  if (!WriteBytes(&boot, sizeof(boot))) {
    if (err) *err = "Failed to write bootstrap";
    return false;
  }

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
  } else {
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

  // Array types - Phase 1 will add basic array support next

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
