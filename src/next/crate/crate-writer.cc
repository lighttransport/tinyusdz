// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate Writer Implementation

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

// Stream writer helper
class StreamWriter {
public:
  StreamWriter(std::vector<uint8_t>& buffer) : buffer_(buffer) {}

  size_t position() const { return buffer_.size(); }

  void write_bytes(const void* data, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    buffer_.insert(buffer_.end(), ptr, ptr + size);
  }

  void write_u8(uint8_t v) { buffer_.push_back(v); }

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

  void write_u64(uint64_t v) {
    write_u32(static_cast<uint32_t>(v));
    write_u32(static_cast<uint32_t>(v >> 32));
  }

  void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }
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
  void align(size_t alignment) {
    size_t pos = buffer_.size();
    size_t aligned = (pos + alignment - 1) & ~(alignment - 1);
    while (buffer_.size() < aligned) {
      buffer_.push_back(0);
    }
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

  void patch_i64(size_t offset, int64_t v) {
    patch_u64(offset, static_cast<uint64_t>(v));
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

    // Float matrices stored as double in Crate format
    case TypeId::Matrix2f: return CrateTypeId::Matrix2d;
    case TypeId::Matrix3f: return CrateTypeId::Matrix3d;
    case TypeId::Matrix4f: return CrateTypeId::Matrix4d;

    default:
      return CrateTypeId::Invalid;
  }
}

}  // anonymous namespace

// ============================================================
// CrateWriter Implementation
// ============================================================

class CrateWriter::Impl {
public:
  explicit Impl(const CrateWriteOptions& options) : options_(options) {}

  CrateWriteResult Write(const Layer& layer) {
    result_ = CrateWriteResult();
    buffer_.clear();

    // Reserve space for bootstrap header
    buffer_.resize(kCrateBootstrapSize, 0);

    // Build tables from layer
    BuildTokenTable(layer);
    BuildStringTable(layer);
    BuildPathTable(layer);
    BuildFieldsAndSpecs(layer);

    // Write sections
    std::vector<CrateSection> sections;

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

    // FIELDS section
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

    // Write TOC at the end
    int64_t toc_offset = static_cast<int64_t>(buffer_.size());
    WriteTOC(sections);

    // Now write bootstrap header at the beginning
    WriteBootstrap(toc_offset);

    result_.success = true;
    result_.bytes_written = buffer_.size();
    result_.token_count = tokens_.size();
    result_.string_count = strings_.size();
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

  // Token table (interned strings for property names, type names, etc.)
  std::vector<std::string> tokens_;
  std::unordered_map<std::string, uint32_t> token_to_index_;

  // String table (for string values)
  std::vector<std::string> strings_;
  std::unordered_map<std::string, uint32_t> string_to_index_;

  // Path table
  std::vector<std::string> paths_;
  std::unordered_map<std::string, uint32_t> path_to_index_;

  // Fields and fieldsets
  std::vector<CrateField> fields_;
  std::vector<std::vector<uint32_t>> fieldsets_;  // Each fieldset is a list of field indices

  // Specs
  std::vector<CrateSpec> specs_;

  // Intern a token
  uint32_t InternToken(const std::string& s) {
    auto it = token_to_index_.find(s);
    if (it != token_to_index_.end()) {
      return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(tokens_.size());
    tokens_.push_back(s);
    token_to_index_[s] = idx;
    return idx;
  }

  // Intern a string
  uint32_t InternString(const std::string& s) {
    auto it = string_to_index_.find(s);
    if (it != string_to_index_.end()) {
      return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(strings_.size());
    strings_.push_back(s);
    string_to_index_[s] = idx;
    return idx;
  }

  // Intern a path
  uint32_t InternPath(const std::string& path) {
    auto it = path_to_index_.find(path);
    if (it != path_to_index_.end()) {
      return it->second;
    }
    uint32_t idx = static_cast<uint32_t>(paths_.size());
    paths_.push_back(path);
    path_to_index_[path] = idx;
    return idx;
  }

  void BuildTokenTable(const Layer& layer) {
    // Pre-intern common tokens
    InternToken("");  // Empty token at index 0
    InternToken("typeName");
    InternToken("specifier");
    InternToken("default");
    InternToken("active");
    InternToken("hidden");

    // Intern type names from all prims
    for (const auto& prim : layer.prims()) {
      InternToken(prim.type_name());

      // Intern property names
      PropNameTable& name_table = GetPropNameTable();
      for (const auto& slot : prim.properties().slots()) {
        InternToken(name_table.get(slot.name_id));
      }
    }
  }

  void BuildStringTable(const Layer& layer) {
    // Intern layer metadata strings
    if (!layer.meta().doc.empty()) {
      InternString(layer.meta().doc);
    }
    if (!layer.meta().defaultPrim.empty()) {
      InternString(layer.meta().defaultPrim);
    }

    // Walk prims for string values
    for (const auto& prim : layer.prims()) {
      // Check property values for strings
      for (const auto& slot : prim.properties().slots()) {
        const Value* val = prim.property_value(slot.name_id);
        if (val) {
          TypeId type = val->type_id();
          if (type == TypeId::String || type == TypeId::AssetPath) {
            const std::string* s = val->as_string();
            if (s) InternString(*s);
          }
        }
      }
    }
  }

  void BuildPathTable(const Layer& layer) {
    // Intern root path
    InternPath("/");

    // Intern all prim paths
    for (const auto& prim : layer.prims()) {
      InternPath(prim.path().str());
    }
  }

  void BuildFieldsAndSpecs(const Layer& layer) {
    // Process each prim to create specs and their fieldsets
    for (size_t i = 0; i < layer.prims().size(); ++i) {
      const PrimSpec& prim = layer.prims()[i];

      // Build fieldset for this prim
      std::vector<uint32_t> fieldset;

      // Add typeName field
      if (!prim.type_name().empty()) {
        CrateField field;
        field.token_index.value = InternToken("typeName");
        field.value_rep = ValueRep::Make(CrateTypeId::Token,
                                         InternToken(prim.type_name()),
                                         false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(field);
      }

      // Add specifier field
      {
        CrateField field;
        field.token_index.value = InternToken("specifier");
        uint64_t spec_val = static_cast<uint64_t>(prim.specifier());
        field.value_rep = ValueRep::Make(CrateTypeId::Specifier, spec_val, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(field);
      }

      // Add property fields
      PropNameTable& name_table = GetPropNameTable();
      for (const auto& slot : prim.properties().slots()) {
        const std::string& prop_name = name_table.get(slot.name_id);
        const Value* val = prim.property_value(slot.name_id);
        if (!val) continue;

        CrateField field;
        field.token_index.value = InternToken(prop_name);
        field.value_rep = EncodeValue(*val);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(field);
      }

      // Record fieldset
      uint32_t fieldset_idx = static_cast<uint32_t>(fieldsets_.size());
      fieldsets_.push_back(std::move(fieldset));

      // Create spec
      CrateSpec spec;
      spec.path_index.value = InternPath(prim.path().str());
      spec.fieldset_index.value = fieldset_idx;
      spec.spec_type = SpecType::Prim;
      specs_.push_back(spec);
    }
  }

  ValueRep EncodeValue(const Value& val) {
    TypeId type_id = val.type_id();
    CrateTypeId crate_type = ToCrateTypeId(type_id);
    bool is_array = val.is_array();

    // For small scalar values, inline them
    if (!is_array && options_.inline_small_values) {
      uint64_t payload = 0;
      bool can_inline = false;

      switch (type_id) {
        case TypeId::Bool:
          if (const bool* v = val.as_bool()) {
            payload = *v ? 1 : 0;
            can_inline = true;
          }
          break;
        case TypeId::Int:
          if (const int32_t* v = val.as_int()) {
            payload = static_cast<uint64_t>(*v);
            can_inline = true;
          }
          break;
        case TypeId::UInt:
          if (const uint32_t* v = val.as_uint()) {
            payload = *v;
            can_inline = true;
          }
          break;
        case TypeId::Float:
          if (const float* v = val.as_float()) {
            uint32_t bits;
            std::memcpy(&bits, v, sizeof(float));
            payload = bits;
            can_inline = true;
          }
          break;
        case TypeId::Token:
          if (const std::string* s = val.as_token()) {
            payload = InternToken(*s);
            can_inline = true;
          }
          break;
        default:
          break;
      }

      if (can_inline) {
        return ValueRep::Make(crate_type, payload, is_array, true);
      }
    }

    // For non-inlined values, we need to write them to a data section
    // For now, return a placeholder that will need to be resolved
    return ValueRep::Make(crate_type, 0, is_array, false);
  }

  void WriteBootstrap(int64_t toc_offset) {
    // Write at position 0
    size_t pos = 0;

    // Magic (8 bytes)
    std::memcpy(buffer_.data() + pos, kCrateMagic, 8);
    pos += 8;

    // Version (3 bytes + 5 padding)
    buffer_[pos++] = options_.version_major;
    buffer_[pos++] = options_.version_minor;
    buffer_[pos++] = options_.version_patch;
    pos += 5;  // Padding

    // TOC offset (8 bytes)
    StreamWriter writer(buffer_);
    writer.patch_i64(pos, toc_offset);
    pos += 8;

    // Reserved (64 bytes)
    // Already zero-initialized
  }

  void WriteTokensSection() {
    StreamWriter writer(buffer_);

    // Number of tokens
    writer.write_u64(tokens_.size());

    // Write each token as length-prefixed string
    for (const auto& token : tokens_) {
      writer.write_u64(token.size());
      writer.write_bytes(token.data(), token.size());
    }
  }

  void WriteStringsSection() {
    StreamWriter writer(buffer_);

    // Number of strings
    writer.write_u64(strings_.size());

    // Compute offsets first
    std::vector<uint32_t> offsets;
    uint32_t current_offset = 0;
    for (const auto& s : strings_) {
      offsets.push_back(current_offset);
      current_offset += static_cast<uint32_t>(s.size()) + 1;  // +1 for null terminator
    }

    // Write index (offsets)
    for (uint32_t offset : offsets) {
      writer.write_u32(offset);
    }

    // Write string data
    for (const auto& s : strings_) {
      writer.write_bytes(s.data(), s.size());
      writer.write_u8(0);  // Null terminator
    }
  }

  void WriteFieldsSection() {
    StreamWriter writer(buffer_);

    // Number of fields
    writer.write_u64(fields_.size());

    // Write each field
    for (const auto& field : fields_) {
      writer.write_u32(field.token_index.value);
      writer.write_u64(field.value_rep.raw());
    }
  }

  void WriteFieldsetsSection() {
    StreamWriter writer(buffer_);

    // Number of fieldsets
    writer.write_u64(fieldsets_.size());

    // For each fieldset, write the field indices
    // Format: [count, field_idx0, field_idx1, ...]
    for (const auto& fieldset : fieldsets_) {
      writer.write_u64(fieldset.size());
      for (uint32_t field_idx : fieldset) {
        writer.write_u32(field_idx);
      }
    }
  }

  void WritePathsSection() {
    StreamWriter writer(buffer_);

    // Number of paths
    writer.write_u64(paths_.size());

    // Write path tokens
    // Paths are stored as token indices
    for (const auto& path : paths_) {
      // Get path components
      std::vector<std::string> components;
      if (!path.empty() && path != "/") {
        size_t start = 1;  // Skip leading /
        while (start < path.size()) {
          size_t end = path.find('/', start);
          if (end == std::string::npos) end = path.size();
          components.push_back(path.substr(start, end - start));
          start = end + 1;
        }
      }

      // Write number of components
      writer.write_u32(static_cast<uint32_t>(components.size()));

      // Write each component as token index
      for (const auto& comp : components) {
        writer.write_u32(InternToken(comp));
      }

      // Write jump offset (for sibling navigation, 0 for now)
      writer.write_i32(0);
    }
  }

  void WriteSpecsSection() {
    StreamWriter writer(buffer_);

    // Number of specs
    writer.write_u64(specs_.size());

    // Write each spec
    for (const auto& spec : specs_) {
      writer.write_u32(spec.path_index.value);
      writer.write_u32(spec.fieldset_index.value);
      writer.write_u32(static_cast<uint32_t>(spec.spec_type));
    }
  }

  void WriteTOC(const std::vector<CrateSection>& sections) {
    StreamWriter writer(buffer_);

    // Number of sections
    writer.write_u64(sections.size());

    // Write each section entry
    for (const auto& sec : sections) {
      writer.write_bytes(sec.name, 16);
      writer.write_i64(sec.start);
      writer.write_i64(sec.size);
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
  CrateWriteResult result;

  if (!filename) {
    result.error = "Null filename";
    return result;
  }

  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) {
    result.error = "Stage has no root layer";
    return result;
  }

  std::vector<uint8_t> buffer;
  result = WriteToMemory(buffer, stage);
  if (!result.success) {
    return result;
  }

  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) {
    result.success = false;
    result.error = "Failed to open file for writing: ";
    result.error += filename;
    return result;
  }

  ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  if (!ofs.good()) {
    result.success = false;
    result.error = "Failed to write to file";
    return result;
  }

  return result;
}

CrateWriteResult CrateWriter::WriteToFile(const std::string& filename, const Stage& stage) {
  return WriteToFile(filename.c_str(), stage);
}

CrateWriteResult CrateWriter::WriteToMemory(std::vector<uint8_t>& buffer, const Stage& stage) {
  const Layer* root_layer = stage.GetRootLayer();
  if (!root_layer) {
    CrateWriteResult result;
    result.error = "Stage has no root layer";
    return result;
  }

  CrateWriteResult result = impl_->Write(*root_layer);
  if (result.success) {
    buffer = impl_->buffer();
  }
  return result;
}

CrateWriteResult CrateWriter::WriteLayerToFile(const char* filename, const Layer& layer) {
  CrateWriteResult result;

  if (!filename) {
    result.error = "Null filename";
    return result;
  }

  std::vector<uint8_t> buffer;
  result = WriteLayerToMemory(buffer, layer);
  if (!result.success) {
    return result;
  }

  std::ofstream ofs(filename, std::ios::out | std::ios::binary);
  if (!ofs) {
    result.success = false;
    result.error = "Failed to open file for writing: ";
    result.error += filename;
    return result;
  }

  ofs.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  if (!ofs.good()) {
    result.success = false;
    result.error = "Failed to write to file";
    return result;
  }

  return result;
}

CrateWriteResult CrateWriter::WriteLayerToMemory(std::vector<uint8_t>& buffer, const Layer& layer) {
  CrateWriteResult result = impl_->Write(layer);
  if (result.success) {
    buffer = impl_->buffer();
  }
  return result;
}

}  // namespace next
}  // namespace tinyusdz
