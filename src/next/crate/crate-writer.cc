// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate Writer Implementation
// Writes spec-compliant USDC with: tokens, strings, paths, fields,
// fieldsets, specs, VALUE data section, time samples, metadata.

#include "crate-writer.hh"
#include "crate-data-source.hh"
#include "lazy-array.hh"
#include "../layer/property-index.hh"
#include "../types/type-id.hh"
#include <unordered_map>
#include <unordered_set>
#include <map>
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
    arrays_passed_through_ = 0;
    arrays_reencoded_ = 0;
    // Reserve block index 0 (empty placeholder). The writer emits empty arrays
    // as a ValueRep with payload==0 / non-inlined (pxrUSD's empty-array marker);
    // reserving index 0 ensures no real block is ever referenced by payload 0,
    // so the offset-patch pass can safely leave payload-0 reps untouched.
    value_data_.push_back({TypeId::Invalid, std::vector<uint8_t>()});
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
        // Split into path-element tokens and register each. Property paths use
        // a '.' separator between the prim path and the property name
        // ("/a/b.prop"), so split on BOTH '/' and '.' — otherwise the trailing
        // "b.prop" would be interned as one bogus token (and "prop" would be
        // missing, only to be re-added later, after the TOKENS section).
        size_t start = 1;
        while (start < path.size()) {
          size_t end = path.size();
          size_t slash = path.find('/', start);
          size_t dot = path.find('.', start);
          if (slash != std::string::npos) end = std::min(end, slash);
          if (dot != std::string::npos) end = std::min(end, dot);
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

    // Reserve the output buffer up-front. The VALUE section (the data blocks
    // collected by BuildFieldsAndSpecs) dominates the output; without a reserve
    // std::vector growth would double its capacity repeatedly, transiently
    // needing ~2x the output size — expensive and a real risk near the wasm32
    // 2 GB ceiling. Over-estimating only wastes capacity; under-estimating just
    // falls back to normal growth, so this is always safe.
    {
      size_t est = 64;
      for (const auto& b : value_data_) est += b.size() + 8;  // +8 align slack
      est += est / 8 + (size_t(1) << 20);  // ~12% for structural sections + 1MB
      if (est > buffer_.capacity()) buffer_.reserve(est);
    }
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
    result_.arrays_passed_through = arrays_passed_through_;
    result_.arrays_reencoded = arrays_reencoded_;

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

  // VALUE data section payloads. A block is either inline (`data`) or a verbatim
  // pass-through reference into a retained source crate buffer (`src` pins the
  // buffer alive; bytes are copied to the output only in WriteValueSection, so
  // the large array payload is never staged in a second heap allocation).
  struct DataBlock {
    TypeId type;
    std::vector<uint8_t> data;
    std::shared_ptr<const CrateDataSource> src;  // null => inline `data`
    uint64_t src_offset = 0;
    uint64_t src_len = 0;
    bool is_ref() const { return static_cast<bool>(src); }
    size_t size() const { return is_ref() ? static_cast<size_t>(src_len) : data.size(); }
    const uint8_t* bytes() const {
      return is_ref() ? src->base() + src_offset : data.data();
    }
  };
  std::vector<DataBlock> value_data_;
  uint64_t value_start_offset_ = 0; // absolute file offset of VALUE section start
  std::vector<uint64_t> value_offsets_; // offset in VALUE section for each data block

  // Lazy-array write accounting.
  size_t arrays_passed_through_ = 0;
  size_t arrays_reencoded_ = 0;

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
    // Token 0: magic marker expected by pxrUSD's crate reader
    InternToken(";-)");  // software identifier (pxrUSD convention)
    // Token 1: empty string, reserved for root path marker in PATHS
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

  // Build an explicit PathListOp value_data_ block for relationship/connection
  // targets: [u8 header=IsExplicit|HasExplicitItems][u64 count][u32 path_idx...].
  // Each target path is interned into the path table. Returns the block index.
  uint64_t StorePathListOp(const std::vector<std::string>& targets) {
    std::vector<uint32_t> idxs;
    idxs.reserve(targets.size());
    for (const auto& t : targets) {
      if (t.empty()) continue;  // skip target-less marker
      idxs.push_back(InternPath(t));
    }
    const uint8_t header = 0x03;  // IsExplicit (0x01) | HasExplicitItems (0x02)
    uint64_t count = idxs.size();
    std::vector<uint8_t> blob(1 + 8 + count * 4);
    blob[0] = header;
    std::memcpy(blob.data() + 1, &count, 8);
    for (size_t i = 0; i < idxs.size(); ++i) {
      std::memcpy(blob.data() + 9 + i * 4, &idxs[i], 4);
    }
    DataBlock block;
    block.type = TypeId::Invalid;
    block.data = std::move(blob);
    value_data_.push_back(std::move(block));
    return value_data_.size() - 1;
  }

  // Build a VariantSelectionMap value_data_ block (CrateTypeId 45):
  // [u64 count][(u32 key_string_idx, u32 val_string_idx) * count]. Strings are
  // interned into the string table. Returns the block index.
  uint64_t StoreVariantSelectionMap(
      const std::vector<std::pair<std::string, std::string>>& sels) {
    uint64_t count = sels.size();
    std::vector<uint8_t> blob(8 + count * 8);
    std::memcpy(blob.data(), &count, 8);
    for (size_t i = 0; i < sels.size(); ++i) {
      uint32_t k = InternString(sels[i].first);
      uint32_t v = InternString(sels[i].second);
      std::memcpy(blob.data() + 8 + i * 8, &k, 4);
      std::memcpy(blob.data() + 8 + i * 8 + 4, &v, 4);
    }
    DataBlock block;
    block.type = TypeId::Invalid;
    block.data = std::move(blob);
    value_data_.push_back(std::move(block));
    return value_data_.size() - 1;
  }

  // Build a TokenVector field ([u64 count][u32 token_idx]*count) — used for
  // primChildren / variantSetChildren / variantChildren.
  CrateField MakeTokenVectorField(const std::string& field_name,
                                  const std::vector<std::string>& names) {
    size_t n = names.size();
    std::vector<uint8_t> raw(8 + n * 4);
    uint64_t cnt = n;
    std::memcpy(raw.data(), &cnt, 8);
    for (size_t i = 0; i < n; ++i) {
      uint32_t tok = InternToken(names[i]);
      std::memcpy(raw.data() + 8 + i * 4, &tok, 4);
    }
    uint64_t data_idx = value_data_.size();
    value_data_.push_back({TypeId::Token, std::move(raw)});
    CrateField f;
    f.token_index.value = InternToken(field_name);
    f.value_rep = ValueRep::Make(CrateTypeId::TokenVector, data_idx, false, false);
    return f;
  }

  // Derive the USD typeName token for an attribute. Prefer the type name the
  // reader recorded (exact role type, array suffix); otherwise fall back to the
  // value's runtime type, appending "[]" for arrays.
  std::string AttrTypeName(const PrimSpec& prim, const std::string& prop_name,
                           const Value* val, uint16_t slot_flags) {
    if (const std::string* tn = prim.property_type_name(prop_name)) {
      if (!tn->empty()) return *tn;
    }
    TypeId tid = val ? val->type_id()
                     : static_cast<TypeId>(TypeId::Invalid);
    const char* base = GetTypeName(tid);
    std::string name = base ? base : "token";
    const bool is_array = (slot_flags & PropSlot::kFlagArray) ||
                          (val && val->is_array());
    if (is_array) name += "[]";
    return name;
  }

  // Build the "timeSamples" CrateField (pxrUSD indirection format) for one
  // time-sampled property.
  CrateField BuildTimeSamplesField(const PrimSpec& prim, PropNameId prop_id) {
    auto* samples = prim.time_samples(prop_id);
    uint64_t num_samples = samples ? samples->size() : 0;

    // Times array block: [u64 count][double[count]]
    std::vector<uint8_t> times_data(8 + num_samples * 8);
    std::memcpy(times_data.data(), &num_samples, 8);
    size_t ti = 0;
    if (samples) {
      for (const auto& kv : *samples) {
        double t = kv.first;
        std::memcpy(times_data.data() + 8 + ti * 8, &t, 8);
        ti++;
      }
    }
    uint64_t times_block_idx = value_data_.size();
    value_data_.push_back({TypeId::Double, std::move(times_data)});

    std::vector<ValueRep> sample_reps(num_samples);
    ti = 0;
    if (samples) {
      for (const auto& kv : *samples) {
        const Value* ts_val = prim.time_sample_value(kv.second);
        sample_reps[ti] = ts_val
            ? EncodeValue(*ts_val)
            : ValueRep::Make(CrateTypeId::ValueBlock, 0, false, true);
        ti++;
      }
    }

    // Header block: [i64 fwd1][ValueRep times][i64 fwd2][u64 N][ValueRep vals[N]]
    size_t header_size = 8 + 8 + 8 + 8 + num_samples * 8;
    std::vector<uint8_t> header_data(header_size, 0);
    size_t hp = 0;
    int64_t fwd1 = 8;
    std::memcpy(header_data.data() + hp, &fwd1, 8); hp += 8;
    uint64_t times_rep_raw =
        ValueRep::Make(CrateTypeId::Double, times_block_idx, true, false).raw();
    std::memcpy(header_data.data() + hp, &times_rep_raw, 8); hp += 8;
    int64_t fwd2 = 8;
    std::memcpy(header_data.data() + hp, &fwd2, 8); hp += 8;
    std::memcpy(header_data.data() + hp, &num_samples, 8); hp += 8;
    for (size_t si = 0; si < num_samples; si++) {
      uint64_t vr = sample_reps[si].raw();
      std::memcpy(header_data.data() + hp, &vr, 8); hp += 8;
    }
    uint64_t header_idx = value_data_.size();
    value_data_.push_back({TypeId::Invalid, std::move(header_data)});

    CrateField ts_field;
    ts_field.token_index.value = InternToken("timeSamples");
    ts_field.value_rep =
        ValueRep::Make(CrateTypeId::TimeSamples, header_idx, false, false);
    return ts_field;
  }

  // Emit one separate Attribute/Relationship spec at `prop_path` carrying
  // `field_list`. Appends to fields_/fieldsets_/specs_.
  void EmitPropertySpec(const std::string& prop_path, SpecType st,
                        const std::vector<CrateField>& field_list) {
    std::vector<uint32_t> fs;
    fs.reserve(field_list.size());
    for (const auto& f : field_list) {
      fs.push_back(static_cast<uint32_t>(fields_.size()));
      fields_.push_back(f);
    }
    uint32_t fs_idx = static_cast<uint32_t>(fieldsets_.size());
    fieldsets_.push_back(std::move(fs));
    CrateSpec spec;
    spec.path_index.value = InternPath(prop_path);
    spec.fieldset_index.value = fs_idx;
    spec.spec_type = st;
    specs_.push_back(spec);
  }

  // Emit pxrUSD-style separate Attribute/Relationship specs for every property,
  // relationship, connection and time-sampled attribute of `prim`. Returns the
  // ordered list of child property names (for the prim's "properties" field).
  std::vector<std::string> EmitPrimProperties(const PrimSpec& prim) {
    std::vector<std::string> prop_names;
    PropNameTable& name_table = GetPropNameTable();
    const std::string base = prim.path().str();
    std::unordered_set<std::string> emitted;

    for (const auto& slot : prim.properties().slots()) {
      const std::string& prop_name = name_table.get(slot.name_id);
      const Value* val = prim.property_value(slot.name_id);
      const bool has_ts = prim.has_time_samples(slot.name_id);
      std::vector<CrateField> flds;

      CrateField tf;
      tf.token_index.value = InternToken("typeName");
      std::string tn = AttrTypeName(prim, prop_name, val, slot.flags);
      tf.value_rep =
          ValueRep::Make(CrateTypeId::Token, InternToken(tn), false, true);
      flds.push_back(tf);

      if (slot.flags & PropSlot::kFlagUniform) {
        CrateField vf;
        vf.token_index.value = InternToken("variability");
        vf.value_rep = ValueRep::Make(CrateTypeId::Variability, 1, false, true);
        flds.push_back(vf);
      }

      if (val) {
        CrateField df;
        df.token_index.value = InternToken("default");
        df.value_rep = EncodeValue(*val);
        flds.push_back(df);
      }

      if (slot.flags & PropSlot::kFlagConnection) {
        if (const std::vector<Path>* conns = prim.connection(prop_name)) {
          std::vector<std::string> targets;
          targets.reserve(conns->size());
          for (const auto& p : *conns)
            if (!p.str().empty()) targets.push_back(p.str());
          if (!targets.empty()) {
            uint64_t blk = StorePathListOp(targets);
            CrateField cf;
            cf.token_index.value = InternToken("connectionPaths");
            cf.value_rep =
                ValueRep::Make(CrateTypeId::PathListOp, blk, false, false);
            flds.push_back(cf);
          }
        }
      }

      if (has_ts) flds.push_back(BuildTimeSamplesField(prim, slot.name_id));

      EmitPropertySpec(base + "." + prop_name, SpecType::Attribute, flds);
      prop_names.push_back(prop_name);
      emitted.insert(prop_name);
    }

    // Time-sampled properties without a slot (added via add_time_sample directly).
    for (auto prop_id : prim.time_sampled_properties()) {
      const std::string& prop_name = name_table.get(prop_id);
      if (emitted.count(prop_name)) continue;
      std::vector<CrateField> flds;
      const Value* sv = nullptr;
      if (auto* s = prim.time_samples(prop_id))
        if (!s->empty()) sv = prim.time_sample_value((*s)[0].second);
      CrateField tf;
      tf.token_index.value = InternToken("typeName");
      std::string tn = AttrTypeName(prim, prop_name, sv, 0);
      tf.value_rep =
          ValueRep::Make(CrateTypeId::Token, InternToken(tn), false, true);
      flds.push_back(tf);
      flds.push_back(BuildTimeSamplesField(prim, prop_id));
      EmitPropertySpec(base + "." + prop_name, SpecType::Attribute, flds);
      prop_names.push_back(prop_name);
      emitted.insert(prop_name);
    }

    // Relationships (target paths via PathListOp; uniform per USD convention).
    for (const auto& rel_name : prim.relationship_names()) {
      std::vector<CrateField> flds;
      CrateField vf;
      vf.token_index.value = InternToken("variability");
      vf.value_rep = ValueRep::Make(CrateTypeId::Variability, 1, false, true);
      flds.push_back(vf);
      if (const std::vector<Path>* tgts = prim.relationship(rel_name)) {
        std::vector<std::string> targets;
        for (const auto& p : *tgts)
          if (!p.str().empty()) targets.push_back(p.str());
        if (!targets.empty()) {
          uint64_t blk = StorePathListOp(targets);
          CrateField rf;
          rf.token_index.value = InternToken("targetPaths");
          rf.value_rep =
              ValueRep::Make(CrateTypeId::PathListOp, blk, false, false);
          flds.push_back(rf);
        }
      }
      EmitPropertySpec(base + "." + rel_name, SpecType::Relationship, flds);
      prop_names.push_back(rel_name);
    }

    return prop_names;
  }

  // Numeric POD array types whose on-disk block is self-contained (no token /
  // string indices into a source-specific table) and so can be copied verbatim.
  static bool IsPodPassThroughType(CrateTypeId t) {
    switch (t) {
      case CrateTypeId::Bool:
      case CrateTypeId::Int:
      case CrateTypeId::UInt:
      case CrateTypeId::Int64:
      case CrateTypeId::UInt64:
      case CrateTypeId::Half:
      case CrateTypeId::Float:
      case CrateTypeId::Double:
      case CrateTypeId::Vec2f:
      case CrateTypeId::Vec3f:
      case CrateTypeId::Vec4f:
      case CrateTypeId::Vec2d:
      case CrateTypeId::Vec3d:
      case CrateTypeId::Vec4d:
      case CrateTypeId::Vec2h:
      case CrateTypeId::Vec3h:
      case CrateTypeId::Vec4h:
      case CrateTypeId::Vec2i:
      case CrateTypeId::Vec3i:
      case CrateTypeId::Vec4i:
      case CrateTypeId::Quatf:
      case CrateTypeId::Quatd:
      case CrateTypeId::Quath:
      case CrateTypeId::Matrix2d:
      case CrateTypeId::Matrix3d:
      case CrateTypeId::Matrix4d:
        return true;
      default:
        return false;
    }
  }

  // The numeric-array block format ([u64 count][raw] and the integer
  // delta+LZ4 encoding) is stable across all supported crate versions, so a
  // block read from any supported source can be written verbatim.
  static bool VersionPassThroughOk(CrateVersion v) {
    return v.major == kCrateMinVersionMajor && v.minor >= kCrateMinVersionMinor &&
           v.minor <= kCrateMaxVersionMinor;
  }

  // Attempt to write an unmodified, crate-backed array by copying its source
  // block verbatim (no decode -> re-encode). Returns false (leaving the value
  // untouched and unmaterialized) when pass-through is not provably safe.
  bool TryPassThrough(const Value& val, ValueRep* out_rep) {
    const LazyArrayRef* lr = val.lazy_ref();
    if (!lr || !lr->source) return false;
    // Need a known, in-bounds block (empty arrays / unknown layouts decline).
    if (lr->block_len == 0) return false;
    // Only self-contained numeric POD blocks (never token/string/asset/dict).
    if (!IsPodPassThroughType(lr->crate_type)) return false;
    // The writer must emit the same on-disk type it would re-encode to.
    if (ToCrateTypeId(val.type_id()) != lr->crate_type) return false;
    // Encoding must be stable between source and the version we write.
    if (!VersionPassThroughOk(lr->source->version())) return false;

    const size_t sz = lr->source->size();
    if (lr->block_offset > sz || lr->block_len > sz - lr->block_offset) return false;

    // Reference the source block in place; bytes are copied to the output buffer
    // only at WriteValueSection time (no intermediate staging copy).
    DataBlock block;
    block.type = val.type_id();
    block.src = lr->source;  // pins the source buffer alive until the write
    block.src_offset = lr->block_offset;
    block.src_len = lr->block_len;
    uint64_t idx = value_data_.size();
    value_data_.push_back(std::move(block));

    *out_rep = ValueRep::Make(lr->crate_type, idx, /*array=*/true,
                              /*inlined=*/false, lr->rep.is_compressed());
    return true;
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

    // Fast path: copy an unmodified, crate-backed array straight through from
    // its source buffer, skipping decode -> re-encode of large payloads. This
    // must run before any accessor below (which would materialize the array).
    if (is_array && val.is_lazy() && !val.is_dirty()) {
      ValueRep pt;
      if (TryPassThrough(val, &pt)) {
        ++arrays_passed_through_;
        return pt;
      }
      ++arrays_reencoded_;  // fell back to full decode + re-encode below
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
        // AssetPath: type stays AssetPath; payload is a token index (the path
        // string interned as a token), matching the reader's UnpackAssetPath.
        uint32_t tok_idx = InternToken(*s);
        return ValueRep::Make(CrateTypeId::AssetPath, tok_idx, false, true);
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

      // Empty array (count == 0): pxrUSD encodes this as payload 0 with NO data
      // block (a count=0 block makes pxr/legacy readers reject it). The next
      // reader also treats payload==0 as an empty array.
      if (count == 0) {
        return ValueRep::Make(crate_type, 0, true, false);
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
        // Store as Double (pxrUSD/legacy reader reject an uninlined TimeCode).
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // endTimeCode
      {
        CrateField f;
        f.token_index.value = InternToken("endTimeCode");
        double v = layer.meta().endTimeCode;
        uint64_t data_idx = StoreValueData(&v, sizeof(v), TypeId::Double);
        // Store as Double (pxrUSD/legacy reader reject an uninlined TimeCode).
        f.value_rep = ValueRep::Make(CrateTypeId::Double, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // doc
      if (!layer.meta().doc.empty()) {
        CrateField f;
        f.token_index.value = InternToken("doc");
        uint32_t str_idx = InternString(layer.meta().doc);
        f.value_rep = ValueRep::Make(CrateTypeId::String, str_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // comment
      if (!layer.meta().comment.empty()) {
        CrateField f;
        f.token_index.value = InternToken("comment");
        uint32_t str_idx = InternString(layer.meta().comment);
        f.value_rep = ValueRep::Make(CrateTypeId::String, str_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // subLayers (token[] — all entries round-trip; the reader reads them back)
      if (!layer.meta().subLayers.empty()) {
        CrateField f;
        f.token_index.value = InternToken("subLayers");
        f.value_rep = EncodeValue(Value::MakeTokenArray(layer.meta().subLayers));
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

    // Pre-scan variant holder prims ("/owning/{vset=var}") to recover, per
    // owning prim, each variant set's variant names — needed to emit
    // variantSetChildren (on the owning prim) and variantChildren (on each
    // VariantSet spec) so pxrUSD can enumerate the variants of an unflattened
    // layer (tinyusdz reconstructs them from the paths, but pxr needs these).
    std::unordered_map<std::string,
        std::map<std::string, std::vector<std::string>>> variant_names;
    for (const auto& p : layer.prims()) {
      const std::string& nm = p.name();
      if (nm.size() < 3 || nm.front() != '{' || nm.back() != '}') continue;
      size_t eq = nm.find('=');
      if (eq == std::string::npos) continue;
      std::string var = nm.substr(eq + 1, nm.size() - eq - 2);  // between = and }
      if (var.empty()) continue;  // "{vset=}" is the VariantSet decl, not a variant
      const std::string& path = p.path().str();
      size_t lb = path.find("/{");
      if (lb == std::string::npos) continue;
      variant_names[path.substr(0, lb)][nm.substr(1, eq - 1)].push_back(var);
    }

    // 2. Prim specs
    std::unordered_map<std::string, uint32_t> path_to_fs_idx;
    for (size_t i = 0; i < layer.prims().size(); ++i) {
      const PrimSpec& prim = layer.prims()[i];
      // Variant-namespace prims ("/Prim/{vset=sel}/..."): emit them ONLY for an
      // unflattened layer (round-trip), detected by their owning prim still
      // carrying variant sets. After flattening, the compositor bakes + clears
      // variantSets, so these become leftover and are dropped.
      {
        const std::string& pp = prim.path().str();
        size_t lb = pp.find("/{");
        if (lb != std::string::npos) {
          const PrimSpec* owner = layer.prim_at_path(pp.substr(0, lb));
          if (!owner || owner->meta().variantSets.empty()) continue;
        }
      }
      std::vector<uint32_t> fieldset;

      // A variant-namespace HOLDER ("/Prim/{vset=sel}" or "/Prim/{vset=}") is a
      // Variant/VariantSet spec, which (per pxrUSD) carries neither specifier
      // nor typeName — emitting them makes pxr warn. Variant CHILD prims
      // ("/Prim/{vset=sel}/Geo") have ordinary names and keep both.
      const std::string& prim_nm = prim.name();
      const bool is_variant_holder = prim_nm.size() >= 2 &&
          prim_nm.front() == '{' && prim_nm.back() == '}';

      // specifier (pxrUSD convention: specifier first, then typeName)
      if (!is_variant_holder) {
        CrateField f;
        f.token_index.value = InternToken("specifier");
        uint64_t spec_val = static_cast<uint64_t>(prim.specifier());
        f.value_rep = ValueRep::Make(CrateTypeId::Specifier, spec_val, false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // typeName
      if (!is_variant_holder && !prim.type_name().empty()) {
        CrateField f;
        f.token_index.value = InternToken("typeName");
        f.value_rep = ValueRep::Make(CrateTypeId::Token,
                                     InternToken(prim.type_name()),
                                     false, true);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      }

      // variantChildren on a VariantSet spec ("/Prim/{vset=}"): the names of the
      // variants in that set, so pxrUSD can enumerate them.
      if (is_variant_holder && prim_nm.size() >= 3 &&
          prim_nm[prim_nm.size() - 2] == '=') {  // "{vset=}"
        const std::string& pth = prim.path().str();
        size_t lb = pth.find("/{");
        std::string owning = (lb == std::string::npos) ? std::string()
                                                       : pth.substr(0, lb);
        std::string vset = prim_nm.substr(1, prim_nm.size() - 3);  // "{vset=}"->vset
        auto oit = variant_names.find(owning);
        if (oit != variant_names.end()) {
          auto vit = oit->second.find(vset);
          if (vit != oit->second.end() && !vit->second.empty()) {
            fieldset.push_back(static_cast<uint32_t>(fields_.size()));
            fields_.push_back(MakeTokenVectorField("variantChildren", vit->second));
          }
        }
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

      // Properties, relationships, connections and time samples are emitted as
      // separate pxrUSD-style Attribute/Relationship specs (below). Collect the
      // child property names here for the prim's "properties" field.
      std::vector<std::string> prop_names = EmitPrimProperties(prim);

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

      // Write a list of strings as a token[] array field. Each element is a
      // separate token, so the reader reconstructs the full list — no lossy
      // separator joining and no first-element-only truncation.
      auto add_token_array_field = [&](const std::string& name,
                                       const std::vector<std::string>& values) {
        if (values.empty()) return;
        CrateField f;
        f.token_index.value = InternToken(name);
        f.value_rep = EncodeValue(Value::MakeTokenArray(values));
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(f);
      };

      // Composition arcs + list metadata as token[] fields (full round-trip).
      add_token_array_field("references", prim.meta().references);
      add_token_array_field("payload", prim.meta().payloads);
      add_token_array_field("inherits", prim.meta().inherits);
      add_token_array_field("specializes", prim.meta().specializes);
      if (!prim.meta().comment.empty()) {
        add_string_field("comment", prim.meta().comment);
      }
      // variantSelection: a VariantSelectionMap of each set's selected variant.
      // The reader rebuilds the VariantSetData from this plus the bracketed
      // holder prims, so no separate variantSets token field is emitted. (The
      // bracketed holder/child prims carry the variant CONTENT and are emitted
      // above for unflattened layers.)
      if (!prim.meta().variantSets.empty()) {
        std::vector<std::pair<std::string, std::string>> sels;
        for (const auto& vs : prim.meta().variantSets) {
          if (!vs.selected.empty()) sels.emplace_back(vs.name, vs.selected);
        }
        if (!sels.empty()) {
          uint64_t blk = StoreVariantSelectionMap(sels);
          CrateField vf;
          vf.token_index.value = InternToken("variantSelection");
          vf.value_rep =
              ValueRep::Make(CrateTypeId::VariantSelectionMap, blk, false, false);
          fieldset.push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(vf);
        }
        // variantSetChildren: the set names on this prim (so pxr enumerates the
        // sets). Derived from the variant holder prims present in the layer.
        auto oit = variant_names.find(prim.path().str());
        if (oit != variant_names.end() && !oit->second.empty()) {
          std::vector<std::string> set_names;
          set_names.reserve(oit->second.size());
          for (const auto& kv : oit->second) set_names.push_back(kv.first);
          fieldset.push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(MakeTokenVectorField("variantSetChildren", set_names));
        }
      }

      // "properties" field: TokenVector listing this prim's child property and
      // relationship names (pxrUSD enumerates properties from it). The next
      // reader rebuilds properties from the separate specs, so it ignores this.
      if (!prop_names.empty()) {
        size_t n = prop_names.size();
        std::vector<uint8_t> raw(8 + n * 4);
        uint64_t cnt = n;
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t i = 0; i < n; ++i) {
          uint32_t tok = InternToken(prop_names[i]);
          std::memcpy(raw.data() + 8 + i * 4, &tok, 4);
        }
        uint64_t data_idx = value_data_.size();
        value_data_.push_back({TypeId::Token, std::move(raw)});
        CrateField pf;
        pf.token_index.value = InternToken("properties");
        pf.value_rep =
            ValueRep::Make(CrateTypeId::TokenVector, data_idx, false, false);
        fieldset.push_back(static_cast<uint32_t>(fields_.size()));
        fields_.push_back(pf);
      }

      // Record fieldset
      uint32_t fs_idx = static_cast<uint32_t>(fieldsets_.size());
      fieldsets_.push_back(std::move(fieldset));
      path_to_fs_idx[prim.path().str()] = fs_idx;

      // Create spec. A prim whose LAST path element is a variant selector
      // ("{vset=sel}") is a Variant spec; "{vset=}" is the VariantSet
      // declaration. (Variant CHILD prims like "/Prim/{vset=sel}/Geo" keep a
      // normal name -> Prim.) Using the right spec type keeps the unflattened
      // output readable by pxrUSD (which rejects a Prim named "{...}").
      CrateSpec spec;
      spec.path_index.value = InternPath(prim.path().str());
      spec.fieldset_index.value = fs_idx;
      spec.spec_type = SpecType::Prim;
      {
        const std::string& nm = prim.name();
        if (nm.size() >= 2 && nm.front() == '{' && nm.back() == '}') {
          spec.spec_type = (nm.find('=') == nm.size() - 2)
                               ? SpecType::VariantSet   // "{vset=}"
                               : SpecType::Variant;     // "{vset=sel}"
        }
      }
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
        size_t n = root_child_tokens.size();
        std::vector<uint8_t> raw(8 + n * 4);
        uint64_t cnt = n;
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t i = 0; i < n; ++i) {
          std::memcpy(raw.data() + 8 + i * 4, &root_child_tokens[i], 4);
        }
        uint64_t data_idx = value_data_.size();
        value_data_.push_back({TypeId::Token, std::move(raw)});

        CrateField pc_field;
        pc_field.token_index.value = InternToken("primChildren");
        // pxrUSD stores primChildren as TokenVector (41), not TokenListOp (32)
        // TokenVector is a simple array: [u64 count][u32 token_idx]*count (no header byte)
        pc_field.value_rep = ValueRep::Make(CrateTypeId::TokenVector,
                                            data_idx, false, false);
        if (!fieldsets_.empty()) {
          fieldsets_[0].push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(pc_field);
        }
      }
    }

    // Build parent-path -> ordered child name tokens in a single O(P) pass,
    // replacing the previous O(P^2) per-prim child scan. Children are appended
    // in prim-iteration order, matching the prior ordering (and prim names are
    // already interned via path interning, so token indices are unchanged).
    std::unordered_map<std::string, std::vector<uint32_t>> parent_children;
    for (const auto& prim : layer.prims()) {
      const std::string& p = prim.path().str();
      if (p.empty() || p == "/") continue;
      // A variant HOLDER ("{vset=sel}"/"{vset=}") is not a prim child of its
      // owner (it is listed in variantSetChildren), so don't add it here. But a
      // variant CHILD prim (a normal name under a holder, e.g. ".../Geo") IS a
      // child of that holder and must be listed in the holder's primChildren.
      const std::string& nm = prim.name();
      if (nm.size() >= 2 && nm.front() == '{' && nm.back() == '}') continue;
      size_t slash = p.rfind('/');
      if (slash == std::string::npos) continue;
      std::string parent = (slash == 0) ? "/" : p.substr(0, slash);
      parent_children[parent].push_back(InternToken(prim.name()));
    }

    // Add primChildren to non-root prims that have children. (Variant holders
    // get them too — so pxr enumerates a variant's sub-prims; the path_to_fs_idx
    // guard below skips prims that were not emitted, e.g. baked-away holders.)
    for (const auto& prim : layer.prims()) {
      const std::string& prim_path = prim.path().str();
      if (prim_path.empty() || prim_path == "/") continue;

      auto cit = parent_children.find(prim_path);
      if (cit == parent_children.end()) continue;
      const std::vector<uint32_t>& child_tokens = cit->second;

      if (!child_tokens.empty()) {
        std::vector<uint8_t> raw(8 + child_tokens.size() * 4);
        uint64_t cnt = child_tokens.size();
        std::memcpy(raw.data(), &cnt, 8);
        for (size_t k = 0; k < child_tokens.size(); ++k) {
          std::memcpy(raw.data() + 8 + k * 4, &child_tokens[k], 4);
        }
        uint64_t data_idx = value_data_.size();
        value_data_.push_back({TypeId::Token, std::move(raw)});

        CrateField f;
        f.token_index.value = InternToken("primChildren");
        f.value_rep = ValueRep::Make(CrateTypeId::TokenVector,
                                      data_idx, false, false);
        auto it = path_to_fs_idx.find(prim_path);
        if (it != path_to_fs_idx.end() && it->second < fieldsets_.size()) {
          fieldsets_[it->second].push_back(static_cast<uint32_t>(fields_.size()));
          fields_.push_back(f);
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
    size_t total = 0;
    for (const auto& token : tokens_) total += token.size() + 1;
    raw.reserve(total);  // avoid incremental reallocations
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

    // Build 3 arrays: path indices, element token indices, jump indices.
    //
    // node index == sorted index. A lexicographic sort of the path strings is
    // exactly the compressed-tree pre-order: every structural separator ('/'
    // for child prims, '.' for properties) is a lower byte (0x2F / 0x2E) than
    // any path-element start character, so a node's descendants are always
    // contiguous immediately after it and before any sibling. We exploit that
    // to build the tree in O(n) instead of the previous O(n^2) recursive scan
    // (the old get_next_subtree did a linear sweep per node — quadratic on
    // high-prim-count / property-heavy scenes).
    std::vector<uint32_t> path_indices(num_paths);     // path index for each node
    std::vector<int32_t> element_tokens(num_paths);    // token index (negated for property)
    std::vector<int32_t> jump_indices(num_paths, -2);  // -2 = leaf (no child, no sibling)
    for (auto& idx : path_indices) idx = 0xFFFFFFFF;

    // A path is a property if it has a '.' after its last '/'. Property paths
    // are encoded as "<primpath>.<propname>" and are always tree leaves.
    auto is_property = [](const std::string& p) -> bool {
      size_t slash = p.rfind('/');
      size_t dot = p.rfind('.');
      return dot != std::string::npos &&
             (slash == std::string::npos || dot > slash);
    };
    // Last path element name ("" for root): part after '.' for a property,
    // part after the last '/' for a prim.
    auto element_name = [&](const std::string& p) -> std::string {
      if (p.empty() || p == "/") return std::string();
      if (is_property(p)) return p.substr(p.rfind('.') + 1);
      size_t pos = p.rfind('/');
      return (pos == std::string::npos) ? p : p.substr(pos + 1);
    };
    // Is `a` a proper ancestor of `b` (prefix terminated by a separator)?
    auto is_ancestor = [](const std::string& a, const std::string& b) -> bool {
      if (a.empty()) return false;
      if (a == "/") return b.size() > 1 && b[0] == '/';
      if (b.size() <= a.size()) return false;
      if (b.compare(0, a.size(), a) != 0) return false;
      char c = b[a.size()];
      return c == '/' || c == '.';
    };

    // Direct parent of each node via a monotonic ancestor stack. Each node is
    // pushed and popped at most once => O(n) amortized. The deepest ancestor
    // remaining on the stack after popping non-ancestors is the direct parent.
    constexpr uint32_t kNoNode = 0xFFFFFFFF;
    std::vector<uint32_t> parent(num_paths, kNoNode);
    {
      std::vector<uint32_t> stack;
      stack.reserve(64);
      for (uint32_t i = 0; i < num_paths; ++i) {
        const std::string& cur = sorted[i].full_path;
        while (!stack.empty() &&
               !is_ancestor(sorted[stack.back()].full_path, cur)) {
          stack.pop_back();
        }
        parent[i] = stack.empty() ? kNoNode : stack.back();
        stack.push_back(i);
      }
    }

    // Next-sibling links: chain each parent's children in sorted order.
    std::vector<uint32_t> next_sibling(num_paths, kNoNode);
    {
      std::unordered_map<uint32_t, uint32_t> last_child;
      last_child.reserve(num_paths * 2);
      for (uint32_t i = 0; i < num_paths; ++i) {
        uint32_t par = parent[i];
        auto it = last_child.find(par);
        if (it != last_child.end()) next_sibling[it->second] = i;
        last_child[par] = i;
      }
    }

    // Emit each node. has_child == "the next node in pre-order is our child".
    for (uint32_t i = 0; i < num_paths; ++i) {
      path_indices[i] = sorted[i].index;
      const std::string& p = sorted[i].full_path;
      const bool is_root = (p == "/");
      int32_t tok = static_cast<int32_t>(
          InternToken(is_root ? std::string() : element_name(p)));
      // Property element tokens are negated (pxr's is_property marker).
      if (!is_root && is_property(p)) tok = -tok;
      element_tokens[i] = tok;

      const bool has_child = (i + 1 < num_paths) && (parent[i + 1] == i);
      const bool has_sibling = (next_sibling[i] != kNoNode);
      if (has_sibling && has_child) {
        jump_indices[i] = static_cast<int32_t>(next_sibling[i] - i);
      } else if (has_sibling) {
        jump_indices[i] = 0;
      } else if (has_child) {
        jump_indices[i] = -1;
      } else {
        jump_indices[i] = -2;
      }
    }

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
        if (payload != 0 && payload < value_offsets_.size()) {
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
      if (payload != 0 && payload < value_offsets_.size()) {
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
      current_offset += value_data_[i].size();
    }

    // Second pass: patch all field ValueReps with actual file offsets (absolute).
    for (auto& field : fields_) {
      if (field.value_rep.is_inlined()) continue;

      CrateTypeId type = field.value_rep.type_id();
      uint64_t data_idx = field.value_rep.payload();

      // payload 0 (non-inlined) is the empty-array marker (block 0 is reserved
      // and never referenced) — leave it as 0 so it is not relocated.
      if (data_idx == 0) continue;

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
      writer_.write_bytes(value_data_[i].bytes(), value_data_[i].size());
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
