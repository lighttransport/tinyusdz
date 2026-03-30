// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader Implementation

#include "crate-reader.hh"
#include "stream-reader.hh"
#include "../types/type-info.hh"
#include "../layer/layer.hh"

#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace tinyusdz {
namespace next {

// ============================================================
// Implementation class
// ============================================================

class CrateReader::Impl {
public:
  explicit Impl(const CrateReadOptions& options) : options_(options) {}

  CrateReadResult Read(const uint8_t* data, size_t size);
  CrateReadResult ReadFile(const char* filename);

  const std::vector<std::string>& tokens() const { return tokens_; }
  const std::vector<std::string>& paths() const { return paths_; }
  const std::vector<CrateField>& fields() const { return fields_; }
  const std::vector<CrateSpec>& specs() const { return specs_; }

private:
  CrateReadOptions options_;
  std::unique_ptr<StreamReader> reader_;
  CrateReadResult result_;

  // Parsed tables
  CrateVersion version_;
  CrateTOC toc_;
  std::vector<std::string> tokens_;
  std::vector<uint32_t> string_indices_;
  std::vector<CrateField> fields_;
  std::vector<uint32_t> fieldset_indices_;
  std::vector<CrateSpec> specs_;
  std::vector<std::string> paths_;

  // Cached fieldsets
  std::unordered_map<uint32_t, std::vector<std::pair<std::string, Value>>> fieldset_cache_;

  // Parsing methods
  bool ReadBootstrap();
  bool ReadTOC();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldsets();
  bool ReadSpecs();
  bool ReadPaths();
  bool BuildStage();

  // Value unpacking
  bool UnpackValue(ValueRep rep, Value& out);
  bool UnpackArray(ValueRep rep, Value& out);

  // Type-specific unpackers
  bool UnpackBool(ValueRep rep, Value& out);
  bool UnpackInt(ValueRep rep, Value& out);
  bool UnpackUInt(ValueRep rep, Value& out);
  bool UnpackInt64(ValueRep rep, Value& out);
  bool UnpackUInt64(ValueRep rep, Value& out);
  bool UnpackFloat(ValueRep rep, Value& out);
  bool UnpackDouble(ValueRep rep, Value& out);
  bool UnpackToken(ValueRep rep, Value& out);
  bool UnpackString(ValueRep rep, Value& out);
  bool UnpackAssetPath(ValueRep rep, Value& out);
  bool UnpackVec2f(ValueRep rep, Value& out);
  bool UnpackVec3f(ValueRep rep, Value& out);
  bool UnpackVec4f(ValueRep rep, Value& out);
  bool UnpackVec2d(ValueRep rep, Value& out);
  bool UnpackVec3d(ValueRep rep, Value& out);
  bool UnpackVec4d(ValueRep rep, Value& out);
  bool UnpackQuatf(ValueRep rep, Value& out);
  bool UnpackQuatd(ValueRep rep, Value& out);
  bool UnpackMatrix3d(ValueRep rep, Value& out);
  bool UnpackMatrix4d(ValueRep rep, Value& out);
  bool UnpackSpecifier(ValueRep rep, Value& out);
  bool UnpackVariability(ValueRep rep, Value& out);

  // Helpers
  bool GetToken(uint32_t index, std::string& out);
  bool GetString(uint32_t index, std::string& out);
  bool ResolveFieldset(uint32_t fieldset_index,
                       std::vector<std::pair<std::string, Value>>& out);

  void AddError(const std::string& msg);
  void AddWarning(const std::string& msg);
};

// ============================================================
// Type-specific unpackers
// ============================================================

bool CrateReader::Impl::UnpackBool(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(rep.payload() != 0);
    return true;
  }
  return false;
}

bool CrateReader::Impl::UnpackInt(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(static_cast<int32_t>(rep.payload()));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t v;
  if (!reader_->read_i32(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackUInt(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(static_cast<uint32_t>(rep.payload()));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint32_t v;
  if (!reader_->read_u32(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackInt64(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(static_cast<int64_t>(rep.payload_as_offset()));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int64_t v;
  if (!reader_->read_i64(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackUInt64(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(static_cast<uint64_t>(rep.payload()));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t v;
  if (!reader_->read_u64(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackFloat(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    uint32_t bits = static_cast<uint32_t>(rep.payload());
    float v;
    std::memcpy(&v, &bits, sizeof(float));
    out = Value(v);
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float v;
  if (!reader_->read_f32(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackDouble(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double v;
  if (!reader_->read_f64(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackToken(ValueRep rep, Value& out) {
  std::string s;
  if (!GetToken(static_cast<uint32_t>(rep.payload()), s)) return false;
  out = Value::MakeToken(std::move(s));
  return true;
}

bool CrateReader::Impl::UnpackString(ValueRep rep, Value& out) {
  std::string s;
  if (!GetString(static_cast<uint32_t>(rep.payload()), s)) return false;
  out = Value(std::move(s));
  return true;
}

bool CrateReader::Impl::UnpackAssetPath(ValueRep rep, Value& out) {
  std::string s;
  if (!GetToken(static_cast<uint32_t>(rep.payload()), s)) return false;
  out = Value::MakeAssetPath(std::move(s));
  return true;
}

bool CrateReader::Impl::UnpackVec2f(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[2];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeFloat2(data[0], data[1]);
  return true;
}

bool CrateReader::Impl::UnpackVec3f(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[3];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeFloat3(data[0], data[1], data[2]);
  return true;
}

bool CrateReader::Impl::UnpackVec4f(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeFloat4(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackVec2d(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[2];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeDouble2(data[0], data[1]);
  return true;
}

bool CrateReader::Impl::UnpackVec3d(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[3];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeDouble3(data[0], data[1], data[2]);
  return true;
}

bool CrateReader::Impl::UnpackVec4d(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeDouble4(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackQuatf(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeQuatf(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackQuatd(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeQuatd(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackMatrix3d(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[9];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeMatrix3d(data);
  return true;
}

bool CrateReader::Impl::UnpackMatrix4d(ValueRep rep, Value& out) {
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[16];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeMatrix4d(data);
  return true;
}

bool CrateReader::Impl::UnpackSpecifier(ValueRep rep, Value& out) {
  uint32_t spec = static_cast<uint32_t>(rep.payload());
  const char* names[] = {"def", "over", "class"};
  if (spec < 3) {
    out = Value::MakeToken(names[spec]);
  } else {
    out = Value::MakeToken("def");
  }
  return true;
}

bool CrateReader::Impl::UnpackVariability(ValueRep rep, Value& out) {
  uint32_t var = static_cast<uint32_t>(rep.payload());
  out = Value::MakeToken(var == 0 ? "varying" : "uniform");
  return true;
}

// ============================================================
// Main value unpacker using switch statement
// ============================================================

bool CrateReader::Impl::UnpackValue(ValueRep rep, Value& out) {
  CrateTypeId type_id = rep.type_id();

  if (rep.is_array()) {
    return UnpackArray(rep, out);
  }

  switch (type_id) {
    case CrateTypeId::Bool: return UnpackBool(rep, out);
    case CrateTypeId::Int: return UnpackInt(rep, out);
    case CrateTypeId::UInt:
    case CrateTypeId::UChar: return UnpackUInt(rep, out);
    case CrateTypeId::Int64: return UnpackInt64(rep, out);
    case CrateTypeId::UInt64: return UnpackUInt64(rep, out);
    case CrateTypeId::Float: return UnpackFloat(rep, out);
    case CrateTypeId::Double: return UnpackDouble(rep, out);
    case CrateTypeId::Token: return UnpackToken(rep, out);
    case CrateTypeId::String: return UnpackString(rep, out);
    case CrateTypeId::AssetPath: return UnpackAssetPath(rep, out);
    case CrateTypeId::Vec2f: return UnpackVec2f(rep, out);
    case CrateTypeId::Vec3f: return UnpackVec3f(rep, out);
    case CrateTypeId::Vec4f: return UnpackVec4f(rep, out);
    case CrateTypeId::Vec2d: return UnpackVec2d(rep, out);
    case CrateTypeId::Vec3d: return UnpackVec3d(rep, out);
    case CrateTypeId::Vec4d: return UnpackVec4d(rep, out);
    case CrateTypeId::Quatf: return UnpackQuatf(rep, out);
    case CrateTypeId::Quatd: return UnpackQuatd(rep, out);
    case CrateTypeId::Matrix3d: return UnpackMatrix3d(rep, out);
    case CrateTypeId::Matrix4d: return UnpackMatrix4d(rep, out);
    case CrateTypeId::Specifier: return UnpackSpecifier(rep, out);
    case CrateTypeId::Variability: return UnpackVariability(rep, out);
    case CrateTypeId::TimeCode: return UnpackDouble(rep, out);

    default:
      AddWarning(std::string("Unsupported value type: ") + CrateTypeIdName(type_id));
      return false;
  }
}

bool CrateReader::Impl::UnpackArray(ValueRep rep, Value& out) {
  CrateTypeId type_id = rep.type_id();

  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) {
    return false;
  }

  uint64_t count;
  if (!reader_->read_u64(count)) {
    return false;
  }

  switch (type_id) {
    case CrateTypeId::Float: {
      std::vector<float> data(static_cast<size_t>(count));
      if (!reader_->read(data.data(), count * sizeof(float))) return false;
      out = Value::MakeFloatArray(std::move(data));
      return true;
    }
    case CrateTypeId::Int: {
      std::vector<int32_t> data(static_cast<size_t>(count));
      if (!reader_->read(data.data(), count * sizeof(int32_t))) return false;
      out = Value::MakeIntArray(std::move(data));
      return true;
    }
    case CrateTypeId::Vec3f: {
      std::vector<float> data(static_cast<size_t>(count * 3));
      if (!reader_->read(data.data(), count * 3 * sizeof(float))) return false;
      out = Value::MakeFloat3Array(std::move(data));
      return true;
    }
    default:
      AddWarning(std::string("Unsupported array type: ") + CrateTypeIdName(type_id));
      return false;
  }
}

// ============================================================
// Implementation methods
// ============================================================

CrateReadResult CrateReader::Impl::Read(const uint8_t* data, size_t size) {
  result_ = CrateReadResult();
  tokens_.clear();
  string_indices_.clear();
  fields_.clear();
  fieldset_indices_.clear();
  specs_.clear();
  paths_.clear();
  fieldset_cache_.clear();

  if (!data || size < kCrateBootstrapSize) {
    AddError("Invalid input data");
    return std::move(result_);
  }

  reader_ = std::make_unique<StreamReader>(data, size);

  // Parse in order
  if (!ReadBootstrap()) return std::move(result_);
  if (!ReadTOC()) return std::move(result_);
  if (!ReadTokens()) return std::move(result_);
  if (!ReadStrings()) return std::move(result_);
  if (!ReadFields()) return std::move(result_);
  if (!ReadFieldsets()) return std::move(result_);
  if (!ReadSpecs()) return std::move(result_);
  if (!ReadPaths()) return std::move(result_);
  if (!BuildStage()) return std::move(result_);

  result_.success = result_.errors.empty();
  result_.version = version_;
  return std::move(result_);
}

CrateReadResult CrateReader::Impl::ReadFile(const char* filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    CrateReadResult result;
    result.errors.push_back({0, std::string("Failed to open file: ") + filename});
    return result;
  }

  size_t size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> data(size);
  if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
    CrateReadResult result;
    result.errors.push_back({0, "Failed to read file contents"});
    return result;
  }

  return Read(data.data(), data.size());
}

bool CrateReader::Impl::ReadBootstrap() {
  // Check magic
  char magic[8];
  if (!reader_->read(magic, 8)) {
    AddError("Failed to read magic bytes");
    return false;
  }
  if (std::memcmp(magic, kCrateMagic, 8) != 0) {
    AddError("Invalid USDC magic bytes");
    return false;
  }

  // Read version
  uint8_t version_bytes[8];
  if (!reader_->read(version_bytes, 8)) {
    AddError("Failed to read version");
    return false;
  }
  version_.major = version_bytes[0];
  version_.minor = version_bytes[1];
  version_.patch = version_bytes[2];

  if (!version_.is_valid()) {
    AddError("Unsupported USDC version: " + version_.to_string());
    return false;
  }

  // Read TOC offset
  int64_t toc_offset;
  if (!reader_->read_i64(toc_offset)) {
    AddError("Failed to read TOC offset");
    return false;
  }

  if (toc_offset < static_cast<int64_t>(kCrateBootstrapSize) ||
      toc_offset >= static_cast<int64_t>(reader_->size())) {
    AddError("Invalid TOC offset");
    return false;
  }

  // Seek to TOC
  if (!reader_->seek(static_cast<size_t>(toc_offset))) {
    AddError("Failed to seek to TOC");
    return false;
  }

  return true;
}

bool CrateReader::Impl::ReadTOC() {
  uint64_t num_sections;
  if (!reader_->read_u64(num_sections)) {
    AddError("Failed to read section count");
    return false;
  }

  if (num_sections > 100) {
    AddError("Too many sections in TOC");
    return false;
  }

  toc_.sections.resize(static_cast<size_t>(num_sections));

  for (size_t i = 0; i < num_sections; i++) {
    CrateSection& s = toc_.sections[i];
    if (!reader_->read(s.name, 16)) {
      AddError("Failed to read section name");
      return false;
    }
    if (!reader_->read_i64(s.start)) {
      AddError("Failed to read section start");
      return false;
    }
    if (!reader_->read_i64(s.size)) {
      AddError("Failed to read section size");
      return false;
    }

    if (s.start < 0 || s.size < 0 ||
        static_cast<size_t>(s.start + s.size) > reader_->size()) {
      AddError("Invalid section bounds: " + s.name_str());
      return false;
    }
  }

  return true;
}

bool CrateReader::Impl::ReadTokens() {
  const CrateSection* section = toc_.find("TOKENS");
  if (!section) {
    AddError("Missing TOKENS section");
    return false;
  }

  if (!reader_->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to TOKENS");
    return false;
  }

  uint64_t num_tokens;
  if (!reader_->read_u64(num_tokens)) {
    AddError("Failed to read token count");
    return false;
  }

  if (num_tokens > options_.max_tokens) {
    AddError("Too many tokens");
    return false;
  }

  uint64_t uncompressed_size, compressed_size;
  if (!reader_->read_u64(uncompressed_size) || !reader_->read_u64(compressed_size)) {
    AddError("Failed to read token compression info");
    return false;
  }

  std::vector<uint8_t> compressed(static_cast<size_t>(compressed_size));
  if (!reader_->read(compressed.data(), static_cast<size_t>(compressed_size))) {
    AddError("Failed to read compressed tokens");
    return false;
  }

  DecompressResult dr = DecompressLZ4(compressed.data(), compressed.size(),
                                       static_cast<size_t>(uncompressed_size));
  if (!dr.success) {
    AddError("Failed to decompress tokens: " + dr.error);
    return false;
  }

  tokens_.reserve(static_cast<size_t>(num_tokens));
  const char* ptr = reinterpret_cast<const char*>(dr.data.data());
  const char* end = ptr + dr.data.size();

  while (ptr < end && tokens_.size() < num_tokens) {
    std::string token(ptr);
    tokens_.push_back(std::move(token));
    ptr += tokens_.back().size() + 1;
  }

  if (tokens_.size() != num_tokens) {
    AddWarning("Token count mismatch");
  }

  return true;
}

bool CrateReader::Impl::ReadStrings() {
  const CrateSection* section = toc_.find("STRINGS");
  if (!section) {
    return true;
  }

  if (!reader_->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to STRINGS");
    return false;
  }

  uint64_t num_strings;
  if (!reader_->read_u64(num_strings)) {
    AddError("Failed to read string count");
    return false;
  }

  if (num_strings > options_.max_strings) {
    AddError("Too many strings");
    return false;
  }

  string_indices_.resize(static_cast<size_t>(num_strings));
  for (size_t i = 0; i < num_strings; i++) {
    uint32_t idx;
    if (!reader_->read_u32(idx)) {
      AddError("Failed to read string index");
      return false;
    }
    string_indices_[i] = idx;
  }

  return true;
}

bool CrateReader::Impl::ReadFields() {
  const CrateSection* section = toc_.find("FIELDS");
  if (!section) {
    AddError("Missing FIELDS section");
    return false;
  }

  if (!reader_->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to FIELDS");
    return false;
  }

  uint64_t num_fields;
  if (!reader_->read_u64(num_fields)) {
    AddError("Failed to read field count");
    return false;
  }

  if (num_fields > options_.max_fields) {
    AddError("Too many fields");
    return false;
  }

  uint64_t indices_size;
  if (!reader_->read_u64(indices_size)) {
    AddError("Failed to read field indices size");
    return false;
  }

  std::vector<uint8_t> indices_data(static_cast<size_t>(indices_size));
  if (!reader_->read(indices_data.data(), static_cast<size_t>(indices_size))) {
    AddError("Failed to read field indices");
    return false;
  }

  DecompressResult dr = DecompressIntegers(indices_data.data(), indices_data.size(),
                                            static_cast<size_t>(num_fields), false);
  if (!dr.success) {
    AddError("Failed to decompress field indices: " + dr.error);
    return false;
  }

  const uint32_t* token_indices = reinterpret_cast<const uint32_t*>(dr.data.data());

  uint64_t reps_size;
  if (!reader_->read_u64(reps_size)) {
    AddError("Failed to read reps size");
    return false;
  }

  std::vector<uint64_t> value_reps(static_cast<size_t>(num_fields));

  if (reps_size == num_fields * 8) {
    if (!reader_->read(value_reps.data(), static_cast<size_t>(reps_size))) {
      AddError("Failed to read value reps");
      return false;
    }
  } else {
    std::vector<uint8_t> reps_data(static_cast<size_t>(reps_size));
    if (!reader_->read(reps_data.data(), static_cast<size_t>(reps_size))) {
      AddError("Failed to read compressed value reps");
      return false;
    }

    DecompressResult rdr = DecompressLZ4(reps_data.data(), reps_data.size(),
                                          static_cast<size_t>(num_fields * 8));
    if (!rdr.success) {
      AddError("Failed to decompress value reps");
      return false;
    }
    std::memcpy(value_reps.data(), rdr.data.data(), num_fields * 8);
  }

  fields_.resize(static_cast<size_t>(num_fields));
  for (size_t i = 0; i < num_fields; i++) {
    fields_[i].token_index.value = token_indices[i];
    fields_[i].value_rep = ValueRep(value_reps[i]);
  }

  return true;
}

bool CrateReader::Impl::ReadFieldsets() {
  const CrateSection* section = toc_.find("FIELDSETS");
  if (!section) {
    AddError("Missing FIELDSETS section");
    return false;
  }

  if (!reader_->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to FIELDSETS");
    return false;
  }

  uint64_t num_fieldsets;
  if (!reader_->read_u64(num_fieldsets)) {
    AddError("Failed to read fieldset count");
    return false;
  }

  size_t data_size = static_cast<size_t>(section->size) - 8;
  std::vector<uint8_t> data(data_size);
  if (!reader_->read(data.data(), data_size)) {
    AddError("Failed to read fieldset data");
    return false;
  }

  DecompressResult dr = DecompressIntegers(data.data(), data.size(),
                                            static_cast<size_t>(num_fieldsets), false);
  if (!dr.success) {
    AddError("Failed to decompress fieldsets: " + dr.error);
    return false;
  }

  fieldset_indices_.resize(static_cast<size_t>(num_fieldsets));
  std::memcpy(fieldset_indices_.data(), dr.data.data(), num_fieldsets * sizeof(uint32_t));

  return true;
}

bool CrateReader::Impl::ReadSpecs() {
  const CrateSection* section = toc_.find("SPECS");
  if (!section) {
    AddError("Missing SPECS section");
    return false;
  }

  if (!reader_->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to SPECS");
    return false;
  }

  uint64_t num_specs;
  if (!reader_->read_u64(num_specs)) {
    AddError("Failed to read spec count");
    return false;
  }

  if (num_specs > options_.max_specs) {
    AddError("Too many specs");
    return false;
  }

  specs_.resize(static_cast<size_t>(num_specs));

  size_t remaining = static_cast<size_t>(section->size) - 8;
  std::vector<uint8_t> data(remaining);
  if (!reader_->read(data.data(), remaining)) {
    AddError("Failed to read specs data");
    return false;
  }

  if (remaining == num_specs * 12) {
    const uint8_t* ptr = data.data();
    for (size_t i = 0; i < num_specs; i++) {
      std::memcpy(&specs_[i].path_index.value, ptr, 4); ptr += 4;
      std::memcpy(&specs_[i].fieldset_index.value, ptr, 4); ptr += 4;
      uint32_t spec_type;
      std::memcpy(&spec_type, ptr, 4); ptr += 4;
      specs_[i].spec_type = static_cast<SpecType>(spec_type);
    }
  } else {
    AddWarning("Compressed specs not fully implemented");
    DecompressResult dr = DecompressIntegers(data.data(), remaining,
                                              static_cast<size_t>(num_specs * 3), false);
    if (dr.success) {
      const uint32_t* vals = reinterpret_cast<const uint32_t*>(dr.data.data());
      for (size_t i = 0; i < num_specs; i++) {
        specs_[i].path_index.value = vals[i * 3];
        specs_[i].fieldset_index.value = vals[i * 3 + 1];
        specs_[i].spec_type = static_cast<SpecType>(vals[i * 3 + 2]);
      }
    }
  }

  return true;
}

bool CrateReader::Impl::ReadPaths() {
  const CrateSection* section = toc_.find("PATHS");
  if (!section) {
    AddError("Missing PATHS section");
    return false;
  }

  if (!reader_->seek(static_cast<size_t>(section->start))) {
    AddError("Failed to seek to PATHS");
    return false;
  }

  uint64_t num_paths;
  if (!reader_->read_u64(num_paths)) {
    AddError("Failed to read path count");
    return false;
  }

  paths_.resize(static_cast<size_t>(num_paths));

  size_t remaining = static_cast<size_t>(section->size) - 8;
  std::vector<uint8_t> data(remaining);
  if (!reader_->read(data.data(), remaining)) {
    AddError("Failed to read paths data");
    return false;
  }

  paths_[0] = "/";
  for (size_t i = 1; i < num_paths && i < specs_.size(); i++) {
    paths_[i] = "/Prim" + std::to_string(i);
  }

  return true;
}

bool CrateReader::Impl::BuildStage() {
  // Create layer and builder
  Layer layer;
  LayerBuilder builder(layer);

  // Process specs to build prims directly into the layer
  for (const auto& spec : specs_) {
    if (spec.spec_type != SpecType::Prim && spec.spec_type != SpecType::PseudoRoot) {
      continue;
    }

    // Get prim name from path
    std::string prim_name;
    if (spec.path_index.value < paths_.size()) {
      std::string path = paths_[spec.path_index.value];
      size_t last_slash = path.rfind('/');
      if (last_slash != std::string::npos && last_slash < path.size() - 1) {
        prim_name = path.substr(last_slash + 1);
      } else {
        prim_name = path;
      }
    }

    if (prim_name.empty() || prim_name == "/") {
      continue;  // Skip pseudo-root
    }

    // Get fields for this spec
    std::vector<std::pair<std::string, Value>> fields;
    ResolveFieldset(spec.fieldset_index.value, fields);

    // Extract type name and specifier
    std::string type_name;
    PrimSpecifier specifier = PrimSpecifier::Def;

    for (const auto& field : fields) {
      if (field.first == "typeName") {
        if (const std::string* s = field.second.as_token()) {
          type_name = *s;
        }
      } else if (field.first == "specifier") {
        if (const std::string* s = field.second.as_token()) {
          if (*s == "over") specifier = PrimSpecifier::Over;
          else if (*s == "class") specifier = PrimSpecifier::Class;
        }
      }
    }

    // Begin prim
    builder.begin_prim(prim_name, type_name, specifier);

    // Add properties (skip metadata fields)
    for (auto& field : fields) {
      if (field.first == "typeName" || field.first == "specifier" ||
          field.first == "variability") {
        continue;
      }

      // Check for uniform variability
      uint16_t flags = 0;
      // TODO: Properly parse variability from field

      builder.add_property(field.first, std::move(field.second), flags);
    }

    builder.end_prim();
  }

  // Finalize
  builder.finalize();

  // Create stage from layer
  result_.stage.SetRootLayer(std::move(layer));

  return true;
}

bool CrateReader::Impl::ResolveFieldset(uint32_t fieldset_index,
                                         std::vector<std::pair<std::string, Value>>& out) {
  out.clear();

  auto it = fieldset_cache_.find(fieldset_index);
  if (it != fieldset_cache_.end()) {
    out = it->second;
    return true;
  }

  if (fieldset_index >= fieldset_indices_.size()) {
    return false;
  }

  size_t start = fieldset_index;
  while (start < fieldset_indices_.size()) {
    uint32_t field_idx = fieldset_indices_[start];
    if (field_idx == 0xFFFFFFFF) break;

    if (field_idx < fields_.size()) {
      const CrateField& field = fields_[field_idx];

      std::string name;
      if (GetToken(field.token_index.value, name)) {
        Value value;
        if (UnpackValue(field.value_rep, value)) {
          out.emplace_back(std::move(name), std::move(value));
        }
      }
    }
    start++;
  }

  fieldset_cache_[fieldset_index] = out;
  return true;
}

bool CrateReader::Impl::GetToken(uint32_t index, std::string& out) {
  if (index >= tokens_.size()) {
    return false;
  }
  out = tokens_[index];
  return true;
}

bool CrateReader::Impl::GetString(uint32_t index, std::string& out) {
  if (index >= string_indices_.size()) {
    return false;
  }
  return GetToken(string_indices_[index], out);
}

void CrateReader::Impl::AddError(const std::string& msg) {
  CrateError err;
  err.offset = reader_ ? reader_->position() : 0;
  err.message = msg;
  result_.errors.push_back(err);
}

void CrateReader::Impl::AddWarning(const std::string& msg) {
  result_.warnings.push_back(msg);
}

// ============================================================
// CrateReader public interface
// ============================================================

CrateReader::CrateReader(const CrateReadOptions& options)
    : impl_(std::make_unique<Impl>(options)) {}

CrateReader::~CrateReader() = default;

CrateReader::CrateReader(CrateReader&&) noexcept = default;
CrateReader& CrateReader::operator=(CrateReader&&) noexcept = default;

CrateReadResult CrateReader::Read(const uint8_t* data, size_t size) {
  return impl_->Read(data, size);
}

CrateReadResult CrateReader::ReadFile(const char* filename) {
  return impl_->ReadFile(filename);
}

const std::vector<std::string>& CrateReader::tokens() const {
  return impl_->tokens();
}

const std::vector<std::string>& CrateReader::paths() const {
  return impl_->paths();
}

const std::vector<CrateField>& CrateReader::fields() const {
  return impl_->fields();
}

const std::vector<CrateSpec>& CrateReader::specs() const {
  return impl_->specs();
}

// ============================================================
// Utility functions
// ============================================================

bool IsUSDCData(const uint8_t* data, size_t size) {
  if (!data || size < 8) return false;
  return std::memcmp(data, kCrateMagic, 8) == 0;
}

bool IsUSDCFile(const char* filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) return false;

  char magic[8];
  file.read(magic, 8);
  if (!file) return false;

  return std::memcmp(magic, kCrateMagic, 8) == 0;
}

}  // namespace next
}  // namespace tinyusdz
