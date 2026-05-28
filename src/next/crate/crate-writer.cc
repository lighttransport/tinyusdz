// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate Writer Implementation
// Writes spec-compliant USDC with: tokens, strings, paths, fields,
// fieldsets, specs, VALUE data section, time samples, metadata.

#include "crate-writer.hh"
#include "../layer/property-index.hh"
#include "../types/type-id.hh"
#include <fstream>
#include <algorithm>
#include <cstring>

// LZ4 for compression
#include "lz4/lz4.h"

namespace tinyusdz {
namespace next {

namespace {

// ============================================================
// Stream writer helper
// ============================================================
class StreamWriter {
public:
  StreamWriter(std::vector<uint8_t>& buffer) : buffer_(buffer) {}

  size_t position() const { return buffer_.size(); }

  void write_bytes(const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), ptr, ptr + size);
  }

  void write_u8(uint8_t v) { buffer_.push_back(v); }
  void write_i8(int8_t v) { buffer_.push_back(static_cast<uint8_t>(v)); }

  void write_u16(uint16_t v) {
    buffer_.push_back(static_cast<uint8_t>(v));
    buffer_.push_back(static_cast<uint8_t>(v >> 8));
  }

  void write_u32(uint32_t v) {
    buffer_.push_back(static_cast<uint8_t>(v));
    buffer_.push_back(static_cast<uint8_t>(v >> 8));
    buffer_.push_back(static_cast<uint8_t>(v >> 16));
    buffer_.push_back(static_cast<uint8_t>(v >> 24));
  }

  void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }

  void write_u64(uint64_t v) {
    write_u32(static_cast<uint32_t>(v));
    write_u32(static_cast<uint32_t>(v >> 32));
  }

  void write_i64(int64_t v) { write_u64(static_cast<uint64_t>(v)); }

  void write_float(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(float));
    write_u32(bits);
  }

  void write_double(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(double));
    write_u64(bits);
  }

  void write_string(const std::string& s) {
    write_u64(s.size());
    write_bytes(s.data(), s.size());
  }

  // Align to boundary
  size_t align(size_t alignment) {
    size_t pos = buffer_.size();
    size_t aligned = (pos + alignment - 1) & ~(alignment - 1);
    while (buffer_.size() < aligned) {
      buffer_.push_back(0);
    }
    return aligned;
  }

  // Patch a value at a specific position
  void patch_u64(size_t offset, uint64_t v) {
    buffer_[offset + 0] = static_cast<uint8_t>(v);
    buffer_[offset + 1] = static_cast<uint8_t>(v >> 8);
    buffer_[offset + 2] = static_cast<uint8_t>(v >> 16);
    buffer_[offset + 3] = static_cast<uint8_t>(v >> 24);
    buffer_[offset + 4] = static_cast<uint8_t>(v >> 32);
    buffer_[offset + 5] = static_cast<uint8_t>(v >> 40);
    buffer_[offset + 6] = static_cast<uint8_t>(v >> 48);
    buffer_[offset + 7] = static_cast<uint8_t>(v >> 56);
  }

private:
  std::vector<uint8_t>& buffer_;
};

// Convert TypeId to CrateTypeId
CrateTypeId ToCrateTypeId(TypeId type_id) {
  switch (type_id) {
    case TypeId::Bool:    return CrateTypeId::Bool;
    case TypeId::Int:     return CrateTypeId::Int;
    case TypeId::UInt:    return CrateTypeId::UInt;
    case TypeId::Int64:   return CrateTypeId::Int64;
    case TypeId::UInt64:  return CrateTypeId::UInt64;
    case TypeId::Half:    return CrateTypeId::Half;
    case TypeId::Float:   return CrateTypeId::Float;
    case TypeId::Double:  return CrateTypeId::Double;
    case TypeId::String:  return CrateTypeId::String;
    case TypeId::Token:   return CrateTypeId::Token;
    case TypeId::AssetPath: return CrateTypeId::AssetPath;
    case TypeId::Int2:    return CrateTypeId::Vec2i;
    case TypeId::Int3:    return CrateTypeId::Vec3i;
    case TypeId::Int4:    return CrateTypeId::Vec4i;
    case TypeId::Float2:  return CrateTypeId::Vec2f;
    case TypeId::Float3:
    case TypeId::Point3f:
    case TypeId::Vector3f:
    case TypeId::Normal3f:
    case TypeId::Color3f:
      return CrateTypeId::Vec3f;
    case TypeId::Float4:
    case TypeId::Color4f:
      return CrateTypeId::Vec4f;
    case TypeId::Double2: return CrateTypeId::Vec2d;
    case TypeId::Double3:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d:
      return CrateTypeId::Vec3d;
    case TypeId::Double4: return CrateTypeId::Vec4d;
    case TypeId::Quatf:   return CrateTypeId::Quatf;
    case TypeId::Quatd:   return CrateTypeId::Quatd;
    case TypeId::Matrix2d: return CrateTypeId::Matrix2d;
    case TypeId::Matrix3d: return CrateTypeId::Matrix3d;
    case TypeId::Matrix4d: return CrateTypeId::Matrix4d;
    case TypeId::Matrix2f: return CrateTypeId::Matrix2d;
    case TypeId::Matrix3f: return CrateTypeId::Matrix3d;
    case TypeId::Matrix4f: return CrateTypeId::Matrix4d;
    default:
      return CrateTypeId::Invalid;
  }
}

/// Compute the amount of VALUE section data needed for a given CrateTypeId
size_t CrateValueSize(CrateTypeId type, bool is_array) {
  if (is_array) return 0; // arrays stored with count prefix in data section
  switch (type) {
    case CrateTypeId::Bool: return 1;
    case CrateTypeId::Int:
    case CrateTypeId::UInt:
    case CrateTypeId::Float: return 4;
    case CrateTypeId::Half: return 2;
    case CrateTypeId::Int64:
    case CrateTypeId::UInt64:
    case CrateTypeId::Double:
    case CrateTypeId::TimeCode: return 8;
    case CrateTypeId::Vec2f:
    case CrateTypeId::Vec2d:
    case CrateTypeId::Vec2h:
    case CrateTypeId::Vec2i: return 8;
    case CrateTypeId::Vec3f:
    case CrateTypeId::Vec3h:
    case CrateTypeId::Vec3i: return 12;
    case CrateTypeId::Vec3d: return 24;
    case CrateTypeId::Vec4f:
    case CrateTypeId::Vec4h:
    case CrateTypeId::Vec4i:
    case CrateTypeId::Quatf: return 16;
    case CrateTypeId::Vec4d:
    case CrateTypeId::Quatd: return 32;
    case CrateTypeId::Matrix2d: return 32;
    case CrateTypeId::Matrix3d: return 72;
    case CrateTypeId::Matrix4d: return 128;
    default: return 0;
  }
}

} // anonymous namespace

// ============================================================
// CrateWriter Implementation
// ============================================================

class CrateWriter::Impl {
public:
  explicit Impl(const CrateWriteOptions& options)
      : options_(options), writer_(buffer_) {
    // Initialize buffer
  }

  CrateWriteResult Write(const Layer& layer) {
    result_ = CrateWriteResult();
    buffer_.clear();

    // Clear all accumulated table state from previous writes
    tokens_.clear();
    token_to_index_.clear();
    string_indices_.clear();
    string_to_index_.clear();
    paths_.clear();
    path_to_index_.clear();
    fields_.clear();
    fieldsets_.clear();
    specs_.clear();
    value_data_.clear();
    value_offsets_.clear();
    // writer_ already references buffer_; just clear buffer

    // Reserve space for bootstrap header
    buffer_.resize(kCrateBootstrapSize, 0);

    // Build tables from layer
    BuildTables(layer);

    // Build fields, fieldsets, and specs from layer
    BuildFieldsAndSpecs(layer);

    // Pre-register path element tokens so they're included in TOKENS section.
    // WritePathsSection calls InternToken() for path elements, but that happens
    // AFTER WriteTokensSection. Pre-registering ensures they're in the output.
    for (const auto& path : paths_) {
      if (path.empty() || path == "/") {
        InternToken("");  // Token 0 = empty string for root path marker
      } else {
        // Split path into components and register each
        size_t start = 1;
        while (start < path.size()) {
          size_t end = path.find('/', start);
          if (end == std::string::npos) end = path.size();
          std::string comp = path.substr(start, end - start);
          if (!comp.empty()) InternToken(comp);
          start = end + 1;
        }
      }
    }

    // Write all sections and record offsets
    std::vector<CrateSection> sections;

    // VALUE DATA section must come FIRST (before structural sections), NOT in TOC.
    // pxrUSD stores VALUE data between bootstrap and structural sections.
    // The bootstrap TOC offset points past the VALUE data to the TOC itself.
    // Bootstrap is 64 bytes, so reserve 64 bytes before VALUE section.
    // WriteBootstrap at the end patches these first 64 bytes.
    buffer_.resize(64, 0);
    value_start_offset_ = 64;
    WriteValueSection();
    int64_t value_data_size = static_cast<int64_t>(buffer_.size()) - 64;
    (void)value_data_size;

    // TOKENS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "TOKENS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size());
      WriteTokensSection();
      sec.size = static_cast<int64_t>(buffer_.size()) - sec.start;
      sections.push_back(sec);
    }

    // STRINGS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "STRINGS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size());
      WriteStringsSection();
      sec.size = static_cast<int64_t>(buffer_.size()) - sec.start;
      sections.push_back(sec);
    }

    // FIELDS section (written before VALUE so we know the offsets)
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "FIELDS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size());
      WriteFieldsSection();
      sec.size = static_cast<int64_t>(buffer_.size()) - sec.start;
      sections.push_back(sec);
    }

    // FIELDSETS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "FIELDSETS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size());
      WriteFieldsetsSection();
      sec.size = static_cast<int64_t>(buffer_.size()) - sec.start;
      sections.push_back(sec);
    }

    // PATHS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "PATHS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size());
      WritePathsSection();
      sec.size = static_cast<int64_t>(buffer_.size()) - sec.start;
      sections.push_back(sec);
    }

    // SPECS section
    {
      CrateSection sec;
      std::memset(sec.name, 0, sizeof(sec.name));
      std::strncpy(sec.name, "SPECS", sizeof(sec.name) - 1);
      sec.start = static_cast<int64_t>(buffer_.size());
      WriteSpecsSection();
      sec.size = static_cast<int64_t>(buffer_.size()) - sec.start;
      sections.push_back(sec);
    }

    // Write TOC at the end (VALUE section is NOT included in TOC)
    int64_t toc_offset = static_cast<int64_t>(buffer_.size());
    WriteTOC(sections);

    // Write bootstrap header at the beginning
    WriteBootstrap(toc_offset);

    result_.success = true;
    result_.bytes_written = buffer_.size();
    result_.token_count = tokens_.size();
    result_.string_count = string_indices_.size();
    result_.path_count = paths_.size();
    result_.spec_count = specs_.size();
    result_.field_count = fields_.size();

    return result_;
  }

  const std::vector<uint8_t>& buffer() const { return buffer_; }

private:
  CrateWriteOptions options_;
  CrateWriteResult result_;
  std::vector<uint8_t> buffer_;
  StreamWriter writer_;

  // Table storage
  std::vector<std::string> tokens_;
  std::unordered_map<std::string, uint32_t> token_to_index_;

  // STRINGS: each entry is a token index
  std::vector<uint32_t> string_indices_;
  std::unordered_map<std::string, uint32_t> string_to_index_;

  // Paths
  std::vector<std::string> paths_;
  std::unordered_map<std::string, uint32_t> path_to_index_;

  // Fields and fieldsets
  std::vector<CrateField> fields_;
  std::vector<std::vector<uint32_t>> fieldsets_;

  // Specs
  std::vector<CrateSpec> specs_;

  // VALUE data section payloads
  struct DataBlock {
    TypeId type;
    std::vector<uint8_t> data;
  };
  std::vector<DataBlock> value_data_;
  uint64_t value_start_offset_ = 0; // absolute file offset of VALUE section start
  std::vector<uint64_t> value_offsets_; // offset in VALUE section for each data block

  // ============================================================
  // Table building
  // ============================================================

  uint32_t InternToken(const std::string& s) {
    auto it = token_to_index_.find(s);
    if (it != token_to_index_.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(tokens_.size());
    tokens_.push_back(s);
    token_to_index_[s] = idx;
    return idx;
  }

  uint32_t InternString(const std::string& s) {
    auto it = string_to_index_.find(s);
    if (it != string_to_index_.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(string_indices_.size());

    // String values in USDC: index into string_indices, which maps to tokens
    uint32_t token_idx = InternToken(s);
    string_indices_.push_back(token_idx);
    string_to_index_[s] = idx;
    return idx;
  }

  uint32_t InternPath(const std::string& path) {
    auto it = path_to_index_.find(path);
    if (it != path_to_index_.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(paths_.size());
    paths_.push_back(path);
    path_to_index_[path] = idx;
    return idx;
  }

  void BuildTables(const Layer& layer) {
    // Token 0 must be empty string (reserved for root path marker in PATHS)
    InternToken("");

    // Common field name tokens
    InternToken("typeName");
    InternToken("specifier");
    InternToken("default");
    InternToken("active");
    InternToken("hidden");
    InternToken("doc");
    InternToken("apiSchemas");
    InternToken("kind");
    InternToken("permission");
    InternToken("variability");
    InternToken("defaultPrim");
    InternToken("upAxis");
    InternToken("metersPerUnit");
    InternToken("timeCodesPerSecond");
    InternToken("startTimeCode");
    InternToken("endTimeCode");
    InternToken("references");
    InternToken("payload");
    InternToken("inherits");
    InternToken("specializes");
    InternToken("variantSets");
    InternToken("variants");
    InternToken("variantSelection");
    InternToken("customData");
    InternToken("comment");
    InternToken("subLayers");

    // Common type name tokens
    InternToken("Xform");
    InternToken("Scope");
    InternToken("Mesh");
    InternToken("Material");
    InternToken("Shader");
    InternToken("Camera");
    InternToken("Sphere");
    InternToken("Cube");
    InternToken("Cylinder");
    InternToken("Cone");
    InternToken("Capsule");
    InternToken("Skeleton");
    InternToken("SkelAnimation");
    InternToken("BlendShape");
    InternToken("SkelRoot");

    // Up/down axis tokens
    InternToken("Y");
    InternToken("Z");

    // Intern layer metadata
    if (!layer.meta().defaultPrim.empty()) {
      InternToken(layer.meta().defaultPrim);
    }
    if (!layer.meta().doc.empty()) {
      InternString(layer.meta().doc);
    }

    // Prim names, type names, property names
    for (const auto& prim : layer.prims()) {
      InternToken(prim.name());
      InternToken(prim.type_name());

      PropNameTable& name_table = GetPropNameTable();
      for (const auto& slot : prim.properties().slots()) {
        InternToken(name_table.get(slot.name_id));
      }

      // Metadata strings
      if (!prim.meta().doc.empty()) InternString(prim.meta().doc);
      for (const auto& schema : prim.meta().apiSchemas) {
        InternToken(schema);
      }
      for (const auto& ref : prim.meta().references) {
        InternString(ref);
      }
      for (const auto& inher : prim.meta().inherits) {
        InternString(inher);
      }
    }
  }

  // ============================================================
  // Value encoding
  // ============================================================

  /// Store data in VALUE section and return its offset.
  /// Returns the offset from the START of the VALUE section.
  uint64_t StoreValueData(const void* data, size_t size, TypeId type) {
    DataBlock block;
    block.type = type;
    block.data.resize(size);
    std::memcpy(block.data.data(), data, size);
    value_data_.push_back(std::move(block));
    return value_data_.size() - 1;
  }

  // Encode a Value into a ValueRep.
  // For inlinable scalars, payload contains the value directly.
  // For larger values, payload contains an index into value_data_.
  // Returns the ValueRep and records the field's VALUE section offset.
  ValueRep EncodeValue(const Value& val) {
    TypeId type_id = val.type_id();
    CrateTypeId crate_type = ToCrateTypeId(type_id);
    bool is_array = val.is_array();

    if (crate_type == CrateTypeId::Invalid) {
      return ValueRep::Make(CrateTypeId::Invalid, 0, is_array, false);
    }

    // Handle inline scalars
    if (!is_array && options_.inline_small_values) {
      uint64_t payload = 0;
      bool can_inline = false;

      switch (type_id) {
        case TypeId::Bool: {
          const bool* v = val.as_bool();
          if (v) { payload = *v ? 1 : 0; can_inline = true; }
          break;
        }
        case TypeId::Int: {
          const int32_t* v = val.as_int();
          if (v) { payload = static_cast<uint64_t>(*v); can_inline = true; }
          break;
        }
        case TypeId::UInt: {
          const uint32_t* v = val.as_uint();
          if (v) { payload = *v; can_inline = true; }
          break;
        }
        case TypeId::Float: {
          const float* v = val.as_float();
          if (v) { uint32_t bits; std::memcpy(&bits, v, 4); payload = bits; can_inline = true; }
          break;
        }
        case TypeId::Token: {
          const std::string* s = val.as_token();
          if (s) { payload = InternToken(*s); can_inline = true; }
          break;
        }
        default:
          break;
      }

      if (can_inline) {
        return ValueRep::Make(crate_type, payload, is_array, true);
      }
    }

    // String / AssetPath via string index (not inline)
    if (type_id == TypeId::String) {
      const std::string* s = val.as_string();
      if (s) {
        uint32_t str_idx = InternString(*s);
        return ValueRep::Make(crate_type, str_idx, false, true);
      }
    }
    if (type_id == TypeId::AssetPath) {
      const std::string* s = val.as_asset_path();
      if (s) {
        uint32_t tok_idx = InternToken(*s);
        return ValueRep::Make(CrateTypeId::Token, tok_idx, false, true);
      }
    }
    if (type_id == TypeId::Token) {
      const std::string* s = val.as_token();
      if (s) {
        uint32_t tok_idx = InternToken(*s);
        return ValueRep::Make(CrateTypeId::Token, tok_idx, false, true);
      }
    }

    // Double (always stored in VALUE section, not inlined)
    if (type_id == TypeId::Double) {
      const double* v = val.as_double();
      if (v) {
        uint64_t data_idx = StoreValueData(v, sizeof(double), type_id);
        return ValueRep::Make(crate_type, data_idx, false, false);
      }
      return ValueRep::Make(crate_type, 0, false, false);
    }

    // Non-inline scalars: store in data section
    if (!is_array) {
      size_t elem_size = CrateValueSize(crate_type, false);
      if (elem_size > 0 && elem_size <= 128) {
        uint8_t tmp[128];
        uint64_t data_idx = value_data_.size(); // placeholder
        switch (type_id) {
          case TypeId::Int64: {
            const int64_t* v = val.as_int64();
            if (v) { std::memcpy(tmp, v, 8); data_idx = StoreValueData(tmp, 8, type_id); }
            break;
          }
          case TypeId::UInt64: {
            const uint64_t* v = val.as_uint64();
            if (v) { std::memcpy(tmp, v, 8); data_idx = StoreValueData(tmp, 8, type_id); }
            break;
          }
          case TypeId::Float2: {
            const float* v = val.as_float2();
            if (v) { std::memcpy(tmp, v, 8); data_idx = StoreValueData(tmp, 8, type_id); }
            break;
          }
          case TypeId::Float3:
          case TypeId::Point3f:
          case TypeId::Vector3f:
          case TypeId::Normal3f:
          case TypeId::Color3f: {
            const float* v = val.as_float3();
            if (v) { std::memcpy(tmp, v, 12); data_idx = StoreValueData(tmp, 12, type_id); }
            break;
          }
          case TypeId::Float4:
          case TypeId::Color4f:
          case TypeId::Quatf: {
            const float* v = val.as_float4();
            if (v) { std::memcpy(tmp, v, 16); data_idx = StoreValueData(tmp, 16, type_id); }
            break;
          }
          case TypeId::Double2: {
            const double* v = val.as_double2();
            if (v) { std::memcpy(tmp, v, 16); data_idx = StoreValueData(tmp, 16, type_id); }
            break;
          }
          case TypeId::Double3:
          case TypeId::Point3d:
          case TypeId::Vector3d:
          case TypeId::Normal3d: {
            const double* v = val.as_double3();
            if (v) { std::memcpy(tmp, v, 24); data_idx = StoreValueData(tmp, 24, type_id); }
            break;
          }
          case TypeId::Double4:
          case TypeId::Quatd: {
            const double* v = val.as_double4();
            if (v) { std::memcpy(tmp, v, 32); data_idx = StoreValueData(tmp, 32, type_id); }
            break;
          }
          case TypeId::Matrix4d:
          case TypeId::Matrix4f: {
            const double* v = val.as_matrix4d();
            if (v) { std::memcpy(tmp, v, 128); data_idx = StoreValueData(tmp, 128, type_id); }
            break;
          }
          default:
            break;
        }
        return ValueRep::Make(crate_type, data_idx, false, false);
      }
      return ValueRep::Make(crate_type, 0, false, false);
    }

    // Array types: store count + elements in data section
    if (is_array) {
      uint64_t count = 0;
      bool data_compressed = false; // whether the array data was compressed
      size_t elem_size = CrateValueSize(crate_type, false);
      (void)elem_size;

      // Collect array data
      std::vector<uint8_t> arr_data;

      switch (type_id) {
        case TypeId::Float: {
          const std::vector<float>* arr = val.as_float_array();
          if (arr) {
            count = arr->size();
            size_t bytes = count * sizeof(float);
            arr_data.resize(8 + bytes);
            std::memcpy(arr_data.data(), &count, 8);
            std::memcpy(arr_data.data() + 8, arr->data(), bytes);
          }
          break;
        }
        case TypeId::Int: {
          const std::vector<int32_t>* arr = val.as_int_array();
          if (arr) {
            count = arr->size();
            if (options_.compress_arrays && count >= 16) {
              // Compressed integer array: [u64 count][u64 comp_size][LZ4+delta data]
              std::vector<uint32_t> tmp(count);
              for (size_t k = 0; k < count; ++k) tmp[k] = static_cast<uint32_t>((*arr)[k]);
              std::vector<uint8_t> delta = EncodeDeltaU32(tmp.data(), count);
              CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
              if (cr.success) {
                arr_data.resize(8 + 8 + cr.data.size());
                std::memcpy(arr_data.data(), &count, 8);
                uint64_t csz = static_cast<uint64_t>(cr.data.size());
                std::memcpy(arr_data.data() + 8, &csz, 8);
                std::memcpy(arr_data.data() + 16, cr.data.data(), cr.data.size());
                data_compressed = true;
              } else {
                // Fall back to uncompressed
                size_t bytes = count * sizeof(int32_t);
                arr_data.resize(8 + bytes);
                std::memcpy(arr_data.data(), &count, 8);
                std::memcpy(arr_data.data() + 8, arr->data(), bytes);
              }
            } else {
              size_t bytes = count * sizeof(int32_t);
              arr_data.resize(8 + bytes);
              std::memcpy(arr_data.data(), &count, 8);
              std::memcpy(arr_data.data() + 8, arr->data(), bytes);
            }
          }
          break;
        }
        case TypeId::Float3:
        case TypeId::Point3f:
        case TypeId::Vector3f:
        case TypeId::Normal3f:
        case TypeId::Color3f: {
          const std::vector<float>* arr = val.as_float_array();
          if (arr && arr->size() % 3 == 0) {
            count = arr->size() / 3;
            size_t bytes = arr->size() * sizeof(float);
            arr_data.resize(8 + bytes);
            std::memcpy(arr_data.data(), &count, 8);
            std::memcpy(arr_data.data() + 8, arr->data(), bytes);
          }
          break;
        }
        case TypeId::Double: {
          const std::vector<double>* arr = val.as_double_array();
          if (arr) {
            count = arr->size();
            size_t bytes = count * sizeof(double);
            arr_data.resize(8 + bytes);
            std::memcpy(arr_data.data(), &count, 8);
            std::memcpy(arr_data.data() + 8, arr->data(), bytes);
          } else {
            // Fallback: try reading as float array and convert
            const std::vector<float>* farr = val.as_float_array();
            if (farr) {
              count = farr->size();
              size_t bytes = count * sizeof(double);
              arr_data.resize(8 + bytes);
              std::memcpy(arr_data.data(), &count, 8);
              double* dptr = reinterpret_cast<double*>(arr_data.data() + 8);
              for (size_t i = 0; i < count; ++i) dptr[i] = static_cast<double>((*farr)[i]);
            }
          }
          break;
        }
        case TypeId::Int64: {
          const std::vector<int64_t>* arr = val.as_int64_array();
          if (arr) {
            count = arr->size();
            size_t bytes = count * sizeof(int64_t);
            arr_data.resize(8 + bytes);
            std::memcpy(arr_data.data(), &count, 8);
            std::memcpy(arr_data.data() + 8, arr->data(), bytes);
          }
          break;
        }
        case TypeId::UInt: {
          const std::vector<uint32_t>* arr = val.as_uint_array();
          if (arr) {
            count = arr->size();
            size_t bytes = count * sizeof(uint32_t);
            arr_data.resize(8 + bytes);
            std::memcpy(arr_data.data(), &count, 8);
            std::memcpy(arr_data.data() + 8, arr->data(), bytes);
          }
          break;
        }
        case TypeId::UInt64: {
          const std::vector<uint64_t>* arr = val.as_uint64_array();
          if (arr) {
            count = arr->size();
            size_t bytes = count * sizeof(uint64_t);
            arr_data.resize(8 + bytes);
            std::memcpy(arr_data.data(), &count, 8);
            std::memcpy(arr_data.data() + 8, arr->data(), bytes);
          }
          break;
        }
        case TypeId::Bool: {
          const std::vector<uint8_t>* arr = val.as_bool_array();
          if (arr) {
            count = arr->size();
            arr_data.resize(8 + count);
            std::memcpy(arr_data.data(), &count, 8);
            std::memcpy(arr_data.data() + 8, arr->data(), count);
          }
          break;
        }
        case TypeId::Token: {
          const std::vector<std::string>* arr = val.as_token_array();
          if (arr) {
            count = arr->size();
            // Store token indices
            arr_data.resize(8 + count * 4);
            std::memcpy(arr_data.data(), &count, 8);
            for (size_t k = 0; k < count; ++k) {
              uint32_t idx = InternToken((*arr)[k]);
              std::memcpy(arr_data.data() + 8 + k * 4, &idx, 4);
            }
          }
          break;
        }
        default:
          break;
      }

      if (!arr_data.empty()) {
        uint64_t idx = value_data_.size();
        DataBlock block;
        block.type = type_id;
        block.data = std::move(arr_data);
        value_data_.push_back(std::move(block));
        // Return non-inline value rep (index will be resolved to offset later)
        return ValueRep::Make(crate_type, idx, true, false, data_compressed);
      }
    }

    return ValueRep::Make(crate_type, 0, is_array, false);
  }

  // Build fields and specs from layer
  void BuildFieldsAndSpecs(const Layer& layer) {
    // 1. PseudoRoot spec (index 0)
    {
      std::vector<uint32_t> fieldset;

      // defaultPrim
      if (!layer.meta().defaultPrim.empty()) {
        CrateField f;
        f.token_index.value = InternToken("defaultPrim");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(layer.meta().defaultPrim),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // upAxis
      {
        CrateField f;
        f.token_index.value = InternToken("upAxis");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(layer.meta().upAxis),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // metersPerUnit
      {
        CrateField f;
        f.token_index.value = InternToken("metersPerUnit");
        double v = layer.meta().metersPerUnit;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // timeCodesPerSecond
      {
        CrateField f;
        f.token_index.value = InternToken("timeCodesPerSecond");
        double v = layer.meta().timeCodesPerSecond;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // startTimeCode
      {
        CrateField f;
        f.token_index.value = InternToken("startTimeCode");
        double v = layer.meta().startTimeCode;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::TimeCode, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // endTimeCode
      {
        CrateField f;
        f.token_index.value = InternToken("endTimeCode");
        double v = layer.meta().endTimeCode;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        f.value_rep = ValueRep::Make(CrateTypeId::TimeCode, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      uint32_t fs_idx = static_cast<uint32_t>(fieldsets_.size());
      fieldsets_.push_back(std::move(fieldset));

      CrateSpec spec;
      spec.path_index.value = InternPath("/");
      spec.fieldset_index.value = fs_idx;
      spec.spec_type = SpecType::PseudoRoot;
      specs_.push_back(spec);
    }

    // 2. Prim specs
    PropNameTable& name_table = GetPropNameTable();
    for (size_t i = 0; i < layer.prims().size(); ++i) {
      const PrimSpec& prim = layer.prims()[i];
      std::vector<uint32_t> fieldset;

      // typeName
      if (!prim.type_name().empty()) {
        CrateField f;
        f.token_index.value = InternToken("typeName");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(prim.type_name()),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // specifier
      {
        CrateField f;
        f.token_index.value = InternToken("specifier");
        uint64_t spec_val = static_cast<uint64_t>(prim.specifier());
        f.value_rep = ValueRep::Make(CrateTypeId::Specifier, spec_val, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // active
      if (!prim.meta().active) {
        CrateField f;
        f.token_index.value = InternToken("active");
        f.value_rep = ValueRep::Make(CrateTypeId::Bool, 0, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // hidden
      if (prim.meta().hidden) {
        CrateField f;
        f.token_index.value = InternToken("hidden");
        f.value_rep = ValueRep::Make(CrateTypeId::Bool, 1, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // doc
      if (!prim.meta().doc.empty()) {
        CrateField f;
        f.token_index.value = InternToken("doc");
        uint32_t str_idx = InternString(prim.meta().doc);
        f.value_rep = ValueRep::Make(CrateTypeId::String, str_idx, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // apiSchemas
      if (!prim.meta().apiSchemas.empty()) {
        // Write as array of tokens
        // Build data: uint64_t count + (uint32_t token_idx) * count
        size_t count = prim.meta().apiSchemas.size();
        std::vector<uint8_t> api_data(8 + count * 4);
        std::memcpy(api_data.data(), &count, 8);
        for (size_t i = 0; i < count; ++i) {
          uint32_t tok_idx = InternToken(prim.meta().apiSchemas[i]);
          std::memcpy(api_data.data() + 8 + i * 4, &tok_idx, 4);
        }
        uint64_t data_idx = value_data_.size();
        value_data_.push_back({TypeId::Token, std::move(api_data)});

        CrateField f;
        f.token_index.value = InternToken("apiSchemas");
        f.value_rep = ValueRep::Make(CrateTypeId::TokenListOp, data_idx, true, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // Property fields
      for (const auto& slot : prim.properties().slots()) {
        const std::string& prop_name = name_table.get(slot.name_id);
        const Value* val = prim.property_value(slot.name_id);
        if (!val) continue;

        CrateField f;
        f.token_index.value = InternToken(prop_name);
        f.value_rep = EncodeValue(*val);

        // Add variability flag if uniform
        if (slot.flags & PropSlot::kFlagUniform) {
          // Uniform flag stored as separate field in real USDC
          // For now just mark the value rep
        }

        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // Time samples: write as TimeSamples field (pxrUSD indirection format)
      // Layout:
      //   Header block (at offset pointed to by field's ValueRep):
      //     [i64 fwd1]              — forward skip past times_rep + values header
      //     [ValueRep times_rep]     — points to times array block
      //     [i64 fwd2]              — forward skip past values header
      //     [u64 num_values]
      //     [ValueRep values[N]]    — each points to a sample value block
      //   Times array block:
      //     [u64 count][double[count]]
      //   Sample value blocks: created by EncodeValue
      if (prim.has_any_time_samples()) {
        for (auto prop_id : prim.time_sampled_properties()) {
          auto* samples = prim.time_samples(prop_id);
          if (!samples || samples->empty()) continue;

          uint64_t num_samples = samples->size();

          // 1. Build times array block: [u64 count][double[count]]
          std::vector<uint8_t> times_data(8 + num_samples * 8);
          std::memcpy(times_data.data(), &num_samples, 8);
          size_t ti = 0;
          for (const auto& [time, val_offset] : *samples) {
            std::memcpy(times_data.data() + 8 + ti * 8, &time, 8);
            ti++;
          }
          uint64_t times_block_idx = value_data_.size();
          value_data_.push_back({TypeId::Double, std::move(times_data)});

          // 2. Build each sample value block via EncodeValue
          std::vector<uint64_t> sample_block_indices(num_samples);
          ti = 0;
          for (const auto& [time, val_offset] : *samples) {
            const Value* ts_val = prim.time_sample_value(val_offset);
            if (ts_val) {
              ValueRep val_rep = EncodeValue(*ts_val);
              sample_block_indices[ti] = val_rep.payload(); // block index
            } else {
              // ValueBlock — store a zero ValueBlock ValueRep
              std::vector<uint8_t> vb(8);
              uint64_t zero = 0;
              std::memcpy(vb.data(), &zero, 8);
              sample_block_indices[ti] = value_data_.size();
              value_data_.push_back({TypeId::Invalid, std::move(vb)});
            }
            ti++;
          }

          // 3. Build header block with placeholder payloads
          // [i64 fwd1=8][ValueRep times_rep][i64 fwd2=8][u64 count][ValueRep values[N]]
          size_t header_size = 8 + 8 + 8 + 8 + num_samples * 8;
          std::vector<uint8_t> header_data(header_size, 0);
          size_t hp = 0;

          // fwd1 = 8 (skip int64 to reach times_rep)
          int64_t fwd1 = 8;
          std::memcpy(header_data.data() + hp, &fwd1, 8); hp += 8;

          // times_rep placeholder — payload will be patched in WriteValueSection
          uint64_t times_rep_raw = ValueRep::Make(
              CrateTypeId::Double, times_block_idx, true, false).raw();
          std::memcpy(header_data.data() + hp, &times_rep_raw, 8); hp += 8;

          // fwd2 = 8 (skip int64 to reach num_values)
          int64_t fwd2 = 8;
          std::memcpy(header_data.data() + hp, &fwd2, 8); hp += 8;

          // num_values
          std::memcpy(header_data.data() + hp, &num_samples, 8); hp += 8;

          // Sample ValueReps (placeholders with block indices)
          for (size_t si = 0; si < num_samples; si++) {
            // Determine type — try to get it from the value data blocks
            CrateTypeId val_type = CrateTypeId::ValueBlock;
            if (sample_block_indices[si] < value_data_.size()) {
              val_type = ToCrateTypeId(value_data_[sample_block_indices[si]].type);
            }
            uint64_t vr = ValueRep::Make(val_type, sample_block_indices[si],
                                          false, false).raw();
            std::memcpy(header_data.data() + hp, &vr, 8); hp += 8;
          }

          // Store header block
          uint64_t header_idx = value_data_.size();
          value_data_.push_back({TypeId::Invalid, std::move(header_data)});

          // Add "timeSamples" field pointing to header block
          CrateField ts_field;
          ts_field.token_index.value = InternToken("timeSamples");
          ts_field.value_rep = ValueRep::Make(CrateTypeId::TimeSamples, header_idx, false, false);
          fieldset.push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(ts_field);
        }
      }

      // Metadata fields from reference/payload/etc strings
      // (stored as regular attributes for now, matching reader behavior)
      auto add_string_field = [&](const std::string& name,
                                  const std::string& value) {
        if (value.empty()) return;
        CrateField f;
        f.token_index.value = InternToken(name);
        uint32_t tok_idx = InternToken(value);
        f.value_rep = ValueRep::Make(CrateTypeId::Token, tok_idx, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };

      // Add metadata as flat fields (matching reader behavior)
      if (!prim.meta().references.empty()) {
        // Write as a single string for simplicity
        add_string_field("references", prim.meta().references[0]);
      }
      if (!prim.meta().payloads.empty()) {
        add_string_field("payload", prim.meta().payloads[0]);
      }
      if (!prim.meta().variantSelection.empty()) {
        add_string_field("variantSelection", prim.meta().variantSelection);
      }

      // Record fieldset
      uint32_t fs_idx = static_cast<uint32_t>(fieldsets_.size());
      fieldsets_.push_back(std::move(fieldset));

      // Create spec
      CrateSpec spec;
      spec.path_index.value = InternPath(prim.path().str());
      spec.fieldset_index.value = fs_idx;
      spec.spec_type = SpecType::Prim;
      specs_.push_back(spec);
    }

    // Add primChildren to pseudo-root (fieldset 0) from root prims
    {
      std::vector<uint32_t> root_child_tokens;
      for (const auto& prim : layer.prims()) {
        // Root prims have exactly one '/' in their path (e.g., "/World")
        const std::string& path_str = prim.path().str();
        if (!path_str.empty() && path_str != "/" &&
            path_str.find('/', 1) == std::string::npos) {
          root_child_tokens.push_back(InternToken(prim.name()));
        }
      }
      if (!root_child_tokens.empty()) {
        // Store as TokenListOp matching pxrUSD format:
        // [uint8 header_bits][if HasExplicitItems: u64 count + token_idx * count]
        constexpr uint8_t kIsExplicit = 1 << 0;
        constexpr uint8_t kHasExplicitItems = 1 << 1;
        constexpr uint8_t kNoOtherItems = 0;
        uint8_t header = kIsExplicit | kHasExplicitItems;
        size_t n = root_child_tokens.size();
        std::vector<uint8_t> raw(1 + 8 + n * 4);
        raw[0] = header;
        uint64_t cnt = n;
        std::memcpy(raw.data() + 1, &cnt, 8);
        for (size_t i = 0; i < n; ++i) {
          std::memcpy(raw.data() + 9 + i * 4, &root_child_tokens[i], 4);
        }
        uint64_t data_idx = value_data_.size();
        value_data_.push_back({TypeId::Token, std::move(raw)});

        CrateField pc_field;
        pc_field.token_index.value = InternToken("primChildren");
        pc_field.value_rep = ValueRep::Make(CrateTypeId::TokenListOp,
                                            data_idx, true, false);
        if (!fieldsets_.empty()) {
          fieldsets_[0].push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(pc_field);
        }
      }
    }
  }

  // ============================================================
  // Section writing
  // ============================================================

  void WriteBootstrap(int64_t toc_offset) {
    // Bootstrap is exactly 64 bytes: [ident(8)][version(8)][tocOffset(8)][reserved(40)]
    size_t pos = 0;
    std::memcpy(buffer_.data() + pos, kCrateMagic, 8); pos += 8;
    buffer_[pos++] = options_.version_major;
    buffer_[pos++] = options_.version_minor;
    buffer_[pos++] = options_.version_patch;
    buffer_[pos++] = 0; // pad
    buffer_[pos++] = 0; buffer_[pos++] = 0;
    buffer_[pos++] = 0; buffer_[pos++] = 0; // total 8 bytes for version
    writer_.patch_u64(pos, static_cast<uint64_t>(toc_offset)); pos += 8;
    // Reserved padding to 64 bytes
    while (pos < 64) buffer_[pos++] = 0;
  }

  void WriteTokensSection() {
    // Build uncompressed token data: concatenated null-terminated strings
    std::vector<uint8_t> raw;
    for (const auto& token : tokens_) {
      raw.insert(raw.end(), token.begin(), token.end());
      raw.push_back(0);  // null terminator
    }

    writer_.write_u64(tokens_.size());
    writer_.write_u64(raw.size());  // uncompressed_size

    if (raw.empty()) {
      writer_.write_u64(0);  // compressed_size = 0
      return;
    }

    CompressResult cr = CompressCrateBlob(raw.data(), raw.size());
    if (!cr.success) {
      // Fall back to storing uncompressed
      writer_.write_u64(raw.size());
      writer_.write_bytes(raw.data(), raw.size());
    } else {
      writer_.write_u64(cr.data.size());
      writer_.write_bytes(cr.data.data(), cr.data.size());
    }
  }

  void WriteStringsSection() {
    writer_.write_u64(string_indices_.size());
    for (uint32_t str_idx : string_indices_) {
      writer_.write_u32(str_idx);
    }
  }

  void WriteFieldsSection() {
    uint64_t num_fields = fields_.size();
    writer_.write_u64(num_fields);

    // Encode token indices using Usd_IntegerCompression (delta + LZ4)
    {
      std::vector<uint32_t> indices(num_fields);
      for (size_t i = 0; i < num_fields; i++) {
        indices[i] = fields_[i].token_index.value;
      }
      // Delta-encode
      std::vector<uint8_t> delta_encoded = EncodeDeltaU32(indices.data(), num_fields);
      // LZ4-compress (TfFastCompression format)
      CompressResult cr = CompressCrateBlob(delta_encoded.data(), delta_encoded.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        // Fallback: store raw delta-encoded data
        writer_.write_u64(delta_encoded.size());
        writer_.write_bytes(delta_encoded.data(), delta_encoded.size());
      }
    }

    // Write compressed value reps (TfFastCompression LZ4 format)
    std::vector<uint8_t> raw_reps(num_fields * 8);
    for (size_t i = 0; i < num_fields; i++) {
      uint64_t v = fields_[i].value_rep.raw();
      std::memcpy(raw_reps.data() + i * 8, &v, 8);
    }
    CompressResult cr_reps = CompressCrateBlob(raw_reps.data(), raw_reps.size());
    if (!cr_reps.success) {
      writer_.write_u64(raw_reps.size());
      writer_.write_bytes(raw_reps.data(), raw_reps.size());
    } else {
      writer_.write_u64(cr_reps.data.size());
      writer_.write_bytes(cr_reps.data.data(), cr_reps.data.size());
    }
  }

  void WriteFieldsetsSection() {
    // Build flat array: for each fieldset, write its field indices then a
    // 0xFFFFFFFF sentinel (matching pxrUSD and the old tinyusdz writer).
    std::vector<uint32_t> flat;
    for (const auto& fieldset : fieldsets_) {
      for (uint32_t field_idx : fieldset) {
        flat.push_back(field_idx);
      }
      flat.push_back(0xFFFFFFFF);  // sentinel
    }

    uint64_t flat_count = flat.size();
    writer_.write_u64(flat_count);
    // Use Usd_IntegerCompression (delta + LZ4)
    std::vector<uint8_t> delta_encoded = EncodeDeltaU32(flat.data(), static_cast<size_t>(flat_count));
    CompressResult cr = CompressCrateBlob(delta_encoded.data(), delta_encoded.size());
    if (cr.success) {
      writer_.write_u64(cr.data.size());
      writer_.write_bytes(cr.data.data(), cr.data.size());
    } else {
      writer_.write_u64(delta_encoded.size());
      writer_.write_bytes(delta_encoded.data(), delta_encoded.size());
    }
  }

  void WritePathsSection() {
    // Build sorted path index pairs, matching pxrUSD's approach.
    // Each path gets a sequential index.
    struct PathEntry {
      std::string full_path;
      uint32_t index;
    };
    std::vector<PathEntry> sorted;
    sorted.reserve(paths_.size());
    for (size_t i = 0; i < paths_.size(); i++) {
      sorted.push_back({paths_[i], static_cast<uint32_t>(i)});
    }
    // Sort lexicographically by full path
    std::sort(sorted.begin(), sorted.end(),
              [](const PathEntry& a, const PathEntry& b) {
                return a.full_path < b.full_path;
              });

    size_t num_paths = sorted.size();
    uint64_t num_encoded = static_cast<uint64_t>(num_paths);
    // Write total paths count AND encoded tree nodes count
    uint64_t total_paths = static_cast<uint64_t>(paths_.size());
    writer_.write_u64(total_paths);
    writer_.write_u64(num_encoded);

    if (num_paths == 0) {
      // No paths other than root, write empty arrays
      std::vector<uint8_t> empty_delta = EncodeDeltaU32(nullptr, 0);
      CompressResult empty_cr = CompressCrateBlob(empty_delta.data(), empty_delta.size());
      writer_.write_u64(empty_cr.data.size());
      writer_.write_bytes(empty_cr.data.data(), empty_cr.data.size());
      writer_.write_u64(empty_cr.data.size());
      writer_.write_bytes(empty_cr.data.data(), empty_cr.data.size());
      writer_.write_u64(empty_cr.data.size());
      writer_.write_bytes(empty_cr.data.data(), empty_cr.data.size());
      return;
    }

    // Build 3 arrays: path indices, element token indices, jump indices
    std::vector<uint32_t> path_indices(num_paths);     // path index for each node
    std::vector<int32_t> element_tokens(num_paths);    // token index (negated for property)
    std::vector<int32_t> jump_indices(num_paths, -2);  // -2 = leaf (no child, no sibling)

    // Fill with invalid sentinel
    for (auto& idx : path_indices) idx = 0xFFFFFFFF;

    // Recursive tree builder matching pxrUSD's _BuildCompressedPathDataRecursive
    constexpr uint32_t kMaxPathTreeDepth = 256;

    // Helper: check if parent is a prefix of child
    auto is_parent_of = [](const std::string& parent, const std::string& child) -> bool {
      if (parent.empty()) return false;
      if (parent.back() == '/') {
        return child.compare(0, parent.size(), parent) == 0;
      }
      return child.compare(0, parent.size(), parent) == 0 &&
             child.size() > parent.size() && child[parent.size()] == '/';
    };

    // Helper: get the last element of a path (or empty for root)
    auto get_element_name = [](const std::string& path) -> std::string {
      if (path.empty() || path == "/") return "";
      size_t pos = path.rfind('/');
      if (pos == std::string::npos) return path;
      if (pos == 0 && path.size() == 1) return "";
      return path.substr(pos + 1);
    };

    // Helper: get parent path of a path string
    auto get_parent_path = [](const std::string& path) -> std::string {
      if (path.empty() || path == "/") return "";
      size_t pos = path.rfind('/');
      if (pos == std::string::npos) return "";
      if (pos == 0) return "/";
      return path.substr(0, pos);
    };

    // Helper: find end of next subtree (paths that share the current prefix)
    std::function<uint32_t(uint32_t, uint32_t)> get_next_subtree;
    get_next_subtree = [&](uint32_t sidx, uint32_t eidx) -> uint32_t {
      if (sidx >= eidx) return eidx;
      const std::string& base = sorted[sidx].full_path;
      for (uint32_t i = sidx; i < eidx; i++) {
        if (!is_parent_of(base, sorted[i].full_path) && sorted[i].full_path != base)
          return i;
      }
      return eidx;
    };

    uint32_t current_idx = 0;
    std::function<bool(uint32_t&, uint32_t, uint32_t, uint32_t, uint32_t&)> build_tree;
    build_tree = [&](uint32_t& cur_idx, uint32_t start_idx, uint32_t end_idx,
                     uint32_t depth, uint32_t& next_out) -> bool {
      if (depth > kMaxPathTreeDepth) return false;
      if (cur_idx >= num_paths || start_idx > end_idx) return false;

      for (uint32_t p = start_idx, next = p; p < end_idx; p = next) {
        uint32_t subtree_end = get_next_subtree(p, end_idx);
        next = p + 1;

        bool has_child = false;
        bool has_sibling = false;

        if (next != subtree_end && next < num_paths) {
          if (is_parent_of(sorted[p].full_path, sorted[next].full_path)) {
            has_child = true;
          }
        }

        // Root path ("/") has no siblings
        if (subtree_end != end_idx && subtree_end < num_paths
            && sorted[p].full_path != "/") {
          // Only a real sibling if both have the same parent
          std::string cur_parent = get_parent_path(sorted[p].full_path);
          std::string next_parent = get_parent_path(sorted[subtree_end].full_path);
          has_sibling = (cur_parent == next_parent && cur_parent != "");
        }

        uint32_t this_node = cur_idx++;
        path_indices[this_node] = sorted[p].index;

        std::string elem = get_element_name(sorted[p].full_path);
        // Root path uses token 0 (empty string) as a marker.
        // pxrUSD expects the root's element token to be the empty string.
        bool is_root = sorted[p].full_path == "/";
        int32_t tok;
        if (is_root) {
          tok = static_cast<int32_t>(InternToken(""));  // token 0 = empty
        } else {
          tok = static_cast<int32_t>(InternToken(elem));
        }
        element_tokens[this_node] = tok;

        if (has_child) {
          uint32_t child_next_out = 0;
          if (!build_tree(cur_idx, next, subtree_end, depth + 1, child_next_out))
            return false;
          next = child_next_out;
        }

        if (has_sibling && has_child) {
          jump_indices[this_node] = static_cast<int32_t>(cur_idx - this_node);
        } else if (has_sibling) {
          jump_indices[this_node] = 0;
        } else if (has_child) {
          jump_indices[this_node] = -1;
        } else {
          jump_indices[this_node] = -2;
        }
      }
      next_out = cur_idx;
      return true;
    };

    uint32_t next_out = 0;
    build_tree(current_idx, 0, static_cast<uint32_t>(num_paths), 0, next_out);

    // Write 3 compressed arrays using Usd_IntegerCompression (delta + LZ4)
    auto write_comp_u32 = [&](const uint32_t* vals, size_t count) {
      std::vector<uint8_t> delta = EncodeDeltaU32(vals, count);
      CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        writer_.write_u64(delta.size());
        writer_.write_bytes(delta.data(), delta.size());
      }
    };

    auto write_comp_i32 = [&](const int32_t* vals, size_t count) {
      std::vector<uint32_t> tmp(count);
      for (size_t i = 0; i < count; i++) {
        tmp[i] = static_cast<uint32_t>(static_cast<int32_t>(vals[i]));
      }
      std::vector<uint8_t> delta = EncodeDeltaU32(tmp.data(), count);
      CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        writer_.write_u64(delta.size());
        writer_.write_bytes(delta.data(), delta.size());
      }
    };

    write_comp_u32(path_indices.data(), num_paths);
    write_comp_i32(element_tokens.data(), num_paths);
    write_comp_i32(jump_indices.data(), num_paths);
  }

  void WriteSpecsSection() {
    uint64_t num_specs = specs_.size();
    writer_.write_u64(num_specs);

    // Convert fieldset numbers to flat array offsets (matching pxrUSD format).
    // Build offset mapping: for each fieldset, its offset in the flat array
    // is the sum of all prior fieldsets' sizes (including sentinels).
    std::vector<uint32_t> fieldset_number_to_offset;
    {
      uint32_t current_offset = 0;
      for (const auto& fs : fieldsets_) {
        fieldset_number_to_offset.push_back(current_offset);
        current_offset += static_cast<uint32_t>(fs.size() + 1); // +1 for sentinel
      }
    }

    // Write 3 compressed integer arrays (delta+LZ4 Usd_IntegerCompression)
    std::vector<uint32_t> path_indices(num_specs);
    std::vector<uint32_t> fieldset_indices(num_specs);
    std::vector<uint32_t> spec_types(num_specs);
    for (size_t i = 0; i < num_specs; i++) {
      path_indices[i] = specs_[i].path_index.value;
      // Convert fieldset number → flat array offset
      uint32_t fs_num = specs_[i].fieldset_index.value;
      fieldset_indices[i] = (fs_num < fieldset_number_to_offset.size())
                            ? fieldset_number_to_offset[fs_num]
                            : 0;
      spec_types[i] = static_cast<uint32_t>(specs_[i].spec_type);
    }

    auto write_comp = [&](const uint32_t* vals, size_t count) {
      std::vector<uint8_t> delta = EncodeDeltaU32(vals, count);
      CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
      if (cr.success) {
        writer_.write_u64(cr.data.size());
        writer_.write_bytes(cr.data.data(), cr.data.size());
      } else {
        writer_.write_u64(delta.size());
        writer_.write_bytes(delta.data(), delta.size());
      }
    };

    write_comp(path_indices.data(), num_specs);
    write_comp(fieldset_indices.data(), num_specs);
    write_comp(spec_types.data(), num_specs);
  }

  void _PatchTimeSamplesBlock(uint64_t data_idx) {
    // Indirection format header:
    // [i64 fwd1=8][ValueRep times_rep][i64 fwd2=8][u64 count][ValueRep values[N]]
    // times_rep.payload and values[i].payload are block indices to patch to abs offsets.
    if (data_idx >= value_data_.size()) return;
    DataBlock& block = value_data_[data_idx];
    if (block.data.size() < 32) return; // minimum: 2*i64 + 1*u64 + times_rep

    // Patch times_rep at offset 8
    uint64_t vrep_raw;
    std::memcpy(&vrep_raw, block.data.data() + 8, 8);
    {
      ValueRep vrep(vrep_raw);
      if (!vrep.is_inlined()) {
        uint64_t payload = vrep.payload();
        if (payload < value_offsets_.size()) {
          uint64_t abs_offset = value_start_offset_ + value_offsets_[static_cast<size_t>(payload)];
          vrep_raw = ValueRep::Make(vrep.type_id(), abs_offset,
                                     vrep.is_array(), false,
                                     vrep.is_compressed()).raw();
          std::memcpy(block.data.data() + 8, &vrep_raw, 8);
        }
      }
    }

    // Read num_values at offset 24
    uint64_t num_samples;
    std::memcpy(&num_samples, block.data.data() + 24, 8);
    if (num_samples == 0) return;

    // Patch each sample ValueRep starting at offset 32
    for (size_t i = 0; i < num_samples; i++) {
      size_t voff = 32 + i * 8;
      if (voff + 8 > block.data.size()) break;

      std::memcpy(&vrep_raw, block.data.data() + voff, 8);
      ValueRep vrep(vrep_raw);
      if (vrep.is_inlined()) continue;

      uint64_t payload = vrep.payload();
      if (payload < value_offsets_.size()) {
        uint64_t abs_offset = value_start_offset_ + value_offsets_[static_cast<size_t>(payload)];
        vrep_raw = ValueRep::Make(vrep.type_id(), abs_offset,
                                   vrep.is_array(), false,
                                   vrep.is_compressed()).raw();
        std::memcpy(block.data.data() + voff, &vrep_raw, 8);
      }
    }
  }

  void WriteValueSection() {
    // Late-bind: compute actual file offsets for all value data blocks
    // The VALUE section layout:
    // [Block0: data][Block1: data]...
    // Each block is: [raw bytes]
    // Field ValueReps with is_inlined==false contain the offset from the
    // START OF VALUE SECTION to the block's data.

    // First pass: compute offsets for all data blocks in value_data_ order.
    // Align each block to an 8-byte boundary for memory-mapped zero-copy reads.
    value_offsets_.resize(value_data_.size());
    uint64_t current_offset = 0;
    for (size_t i = 0; i < value_data_.size(); ++i) {
      // Align to 8 bytes before each block
      if (current_offset % 8 != 0) {
        current_offset += 8 - (current_offset % 8);
      }
      value_offsets_[i] = current_offset;
      current_offset += value_data_[i].data.size();
    }

    // Second pass: patch all field ValueReps with actual file offsets (absolute).
    for (auto& field : fields_) {
      if (field.value_rep.is_inlined()) continue;

      CrateTypeId type = field.value_rep.type_id();
      uint64_t data_idx = field.value_rep.payload();

      if (data_idx < value_data_.size() && data_idx < value_offsets_.size()) {
        uint64_t offset = value_start_offset_ + value_offsets_[data_idx];
        // Patch the ValueRep with the correct absolute offset
        field.value_rep = ValueRep::Make(type, offset,
                                         field.value_rep.is_array(), false,
                                         field.value_rep.is_compressed());
      }
      // Patch embedded ValueReps in TimeSamples data blocks
      if (type == CrateTypeId::TimeSamples) {
        _PatchTimeSamplesBlock(data_idx);
      }
    }

    // Third pass: actually write the data blocks, 8-byte aligned
    for (size_t i = 0; i < value_data_.size(); ++i) {
      writer_.align(8);
      writer_.write_bytes(value_data_[i].data.data(),
                          value_data_[i].data.size());
    }
  }

  void WriteTOC(const std::vector<CrateSection>& sections) {
    writer_.write_u64(sections.size());
    for (const auto& sec : sections) {
      writer_.write_bytes(sec.name, 16);
      writer_.write_i64(sec.start);
      writer_.write_i64(sec.size);
    }
  }
};

// ============================================================
// CrateWriter public methods
// ============================================================

CrateWriter::CrateWriter(const CrateWriteOptions& options)
    : impl_(new Impl(options)) {}

CrateWriter::~CrateWriter() = default;

CrateWriteResult CrateWriter::WriteToFile(const char* filename, const Stage& stage) {
  if (!filename) { CrateWriteResult r; r.error = "Null filename"; return r; }
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) { CrateWriteResult r; r.error = "Stage has no root layer"; return r; }
  std::vector<uint8_t> buffer;
  CrateWriteResult result = WriteToMemory(buffer, stage);
  if (!result.success) return result;
  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) { result.success = false; result.error = "Failed to open file"; return result; }
  ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  if (!ofs.good()) { result.success = false; result.error = "Failed to write"; return result; }
  return result;
}

CrateWriteResult CrateWriter::WriteToFile(const std::string& filename, const Stage& stage) {
  return WriteToFile(filename.c_str(), stage);
}

CrateWriteResult CrateWriter::WriteToMemory(std::vector<uint8_t>& buffer, const Stage& stage) {
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) { CrateWriteResult r; r.error = "Stage has no root layer"; return r; }
  CrateWriteResult result = impl_->Write(*root_layer);
  if (result.success) buffer = impl_->buffer();
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToFile(const char* filename, const Layer& layer) {
  if (!filename) { CrateWriteResult r; r.error = "Null filename"; return r; }
  std::vector<uint8_t> buffer;
  CrateWriteResult result = WriteLayerToMemory(buffer, layer);
  if (!result.success) return result;
  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) { result.success = false; result.error = "Failed to open file"; return result; }
  ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  if (!ofs.good()) { result.success = false; result.error = "Failed to write"; return result; }
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToMemory(std::vector<uint8_t>& buffer, const Layer& layer) {
  CrateWriteResult result = impl_->Write(layer);
  if (result.success) buffer = impl_->buffer();
  return result;
}

}  // namespace next
}  // namespace tinyusdz
