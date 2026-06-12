// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader Implementation

#include "crate-reader.hh"
#include "crate-data-source.hh"
#include "lazy-array.hh"
#include "stream-reader.hh"
#include "../types/type-info.hh"
#include "../layer/layer.hh"

#include "safe-arithmetic.hh"

#include <fstream>
#include <unordered_map>
#include <map>
#include <set>
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
  CrateReadResult ReadOwned(std::string&& owned);  // adopt bytes by move (single copy)
  CrateReadResult ReadFile(const char* filename);

  const std::vector<std::string>& tokens() const { return tokens_; }
  const std::vector<std::string>& paths() const { return paths_; }
  const std::vector<CrateField>& fields() const { return fields_; }
  const std::vector<CrateSpec>& specs() const { return specs_; }
  const std::vector<uint32_t>& fieldset_indices() const { return fieldset_indices_; }

private:
  CrateReadOptions options_;
  std::unique_ptr<StreamReader> reader_;
  // Retained crate bytes. Lazy array Values reference this; it stays alive (via
  // shared_ptr) as long as some surviving Value points into it, even after the
  // CrateReader itself is destroyed.
  std::shared_ptr<CrateDataSource> source_;
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

  // Parse from a moved-in owned buffer (shared by Read / ReadOwned / ReadFile).
  CrateReadResult ReadFromString(std::string&& bytes);

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
  bool UnpackTimeSamples(ValueRep rep, Value& out);
  bool UnpackVec2i(ValueRep rep, Value& out);
  bool UnpackVec3i(ValueRep rep, Value& out);
  bool UnpackVec4i(ValueRep rep, Value& out);
  bool UnpackHalf(ValueRep rep, Value& out);
  bool UnpackVec2h(ValueRep rep, Value& out);
  bool UnpackVec3h(ValueRep rep, Value& out);
  bool UnpackVec4h(ValueRep rep, Value& out);
  bool UnpackQuath(ValueRep rep, Value& out);

  // Helpers
  bool GetToken(uint32_t index, std::string& out);
  bool GetString(uint32_t index, std::string& out);
  bool ResolveFieldset(uint32_t fieldset_index,
                       std::vector<std::pair<std::string, Value>>& out);

  // Walk a fieldset returning (token_name, raw ValueRep) pairs without
  // unpacking. Used for Attribute/Relationship specs whose path-typed fields
  // (targetPaths / connectionPaths) UnpackValue cannot decode.
  bool ResolveFieldsetRaw(uint32_t fieldset_index,
                          std::vector<std::pair<std::string, ValueRep>>& out);

  // Decode a PathListOp (CrateTypeId 34) or PathVector (40) value into the
  // referenced path strings (resolved through the reconstructed paths_ table).
  bool DecodePathTargets(ValueRep rep, std::vector<std::string>& out);
  // Decode a ReferenceListOp (35) / PayloadListOp (55) into composition arc
  // strings "@asset@</prim>[?layerOffset=o:s]" (the form Compositor::
  // ParseReference consumes). List-edit semantics are flattened: explicit/
  // added/prepended/appended entries are kept, deleted/ordered are read and
  // discarded.
  bool DecodeReferenceListOp(ValueRep rep, bool is_payload,
                             std::vector<std::string>& out);

  // Decode a VariantSelectionMap (CrateTypeId 45): [u64 count][(u32 key_str_idx,
  // u32 val_str_idx) * count] -> (variantSet, selection) pairs.
  bool DecodeVariantSelectionMap(
      ValueRep rep, std::vector<std::pair<std::string, std::string>>& out);

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
  if (rep.is_inlined()) {
    // pxrUSD inlines a double as a 32-bit float in the low payload bits
    // (used for integer-valued doubles like startTimeCode/endTimeCode).
    uint32_t bits = static_cast<uint32_t>(rep.payload() & 0xFFFFFFFFu);
    float f;
    std::memcpy(&f, &bits, sizeof(float));
    out = Value(static_cast<double>(f));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double v;
  if (!reader_->read_f64(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackTimeSamples(ValueRep rep, Value& out) {
  (void)out;
  // TimeSamples indirection format at rep.payload():
  //   [i64 fwd1=8][ValueRep times_rep][i64 fwd2=8][u64 count][ValueRep samples[N]]
  //
  // The current writer emits every time-sampled property under a single field
  // named "timeSamples" with no link back to the owning property, so the parsed
  // (time, value) pairs cannot be re-associated with a property at this layer.
  // Rather than fabricate a placeholder token value — which would surface as a
  // bogus stray property on read — validate the header and skip the field:
  // ResolveFieldset drops fields whose UnpackValue returns false.
  //
  // NOTE: a proper fix requires a TimeSamples-aware value path and a writer that
  // names the field after its property (see the review's out-of-scope note).
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeInt2(int32_t(b[0]), int32_t(b[1]));
    return true;
  }

  uint64_t header_offset = rep.payload();
  if (!reader_->seek(static_cast<size_t>(header_offset))) return false;

  // Skip fwd1(8) + times_rep(8) + fwd2(8) to reach the sample count.
  if (!reader_->skip(24)) return false;

  uint64_t num_samples;
  if (!reader_->read_u64(num_samples)) return false;
  if (num_samples > 1000000) {
    AddWarning("Too many time samples");
    return false;
  }

  AddWarning("TimeSamples skipped: not yet wired to per-property storage");
  return false;
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
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeFloat2(float(b[0]), float(b[1]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[2];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeFloat2(data[0], data[1]);
  return true;
}

bool CrateReader::Impl::UnpackVec3f(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeFloat3(float(b[0]), float(b[1]), float(b[2]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[3];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeFloat3(data[0], data[1], data[2]);
  return true;
}

bool CrateReader::Impl::UnpackVec4f(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeFloat4(float(b[0]), float(b[1]), float(b[2]), float(b[3]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeFloat4(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackVec2d(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeDouble2(double(b[0]), double(b[1]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[2];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeDouble2(data[0], data[1]);
  return true;
}

bool CrateReader::Impl::UnpackVec3d(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeDouble3(double(b[0]), double(b[1]), double(b[2]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[3];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeDouble3(data[0], data[1], data[2]);
  return true;
}

bool CrateReader::Impl::UnpackVec4d(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeDouble4(double(b[0]), double(b[1]), double(b[2]), double(b[3]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeDouble4(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackQuatf(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeQuatf(float(b[0]), float(b[1]), float(b[2]), float(b[3]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeQuatf(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackQuatd(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeQuatd(double(b[0]), double(b[1]), double(b[2]), double(b[3]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeQuatd(data[0], data[1], data[2], data[3]);
  return true;
}

bool CrateReader::Impl::UnpackMatrix3d(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    double m[9] = {0}; m[0]=b[0]; m[4]=b[1]; m[8]=b[2]; out = Value::MakeMatrix3d(m);
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[9];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeMatrix3d(data);
  return true;
}

bool CrateReader::Impl::UnpackMatrix4d(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    double m[16] = {0}; m[0]=b[0]; m[5]=b[1]; m[10]=b[2]; m[15]=b[3]; out = Value::MakeMatrix4d(m);
    return true;
  }
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
// Integer vector unpackers
// ============================================================

bool CrateReader::Impl::UnpackVec2i(ValueRep rep, Value& out) {
  // 8 bytes — never inlinable (inline payload is 6 bytes).
  if (rep.is_inlined()) return false;
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t data[2];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeInt2(data[0], data[1]);
  return true;
}

bool CrateReader::Impl::UnpackVec3i(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeInt3(int32_t(b[0]), int32_t(b[1]), int32_t(b[2]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t data[3];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeInt3(data[0], data[1], data[2]);
  return true;
}

bool CrateReader::Impl::UnpackVec4i(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors/matrices as int8 components
    // packed in the low payload bytes (e.g. (1,0,0), identity matrix).
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeInt4(int32_t(b[0]), int32_t(b[1]), int32_t(b[2]), int32_t(b[3]));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeInt4(data[0], data[1], data[2], data[3]);
  return true;
}

// ============================================================
// Half-precision unpackers
// ============================================================

namespace {

// Decode IEEE 754 half-precision (16-bit) to float32
inline float half_to_float(uint16_t h) {
  // Sign: bit 15
  // Exponent: bits 14-10 (5 bits, bias 15)
  // Mantissa: bits 9-0 (10 bits)
  uint32_t sign = (h >> 15) & 1;
  uint32_t exp = (h >> 10) & 0x1F;
  uint32_t mant = h & 0x3FF;

  uint32_t f;
  if (exp == 0) {
    // Zero/subnormal
    if (mant == 0) {
      f = sign << 31;  // +/- zero
    } else {
      // Subnormal: normalize so the leading 1 lands at bit 10, then drop it
      // (it becomes the implicit float mantissa bit). Each left shift lowers
      // the effective exponent by one: after s shifts e == -(s+1), and the
      // float exponent field is 113 - s == 114 + e. The leading 1 must be
      // masked off (0x3FF) so it does not bleed into the exponent field.
      int e = -1;
      uint32_t m = mant;
      while (!(m & 0x400)) { m <<= 1; e--; }
      f = (sign << 31) | (static_cast<uint32_t>(114 + e) << 23) |
          ((m & 0x3FF) << 13);
    }
  } else if (exp == 31) {
    // Inf/NaN
    f = (sign << 31) | 0x7F800000 | (mant << 13);
  } else {
    // Normal
    f = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
  }
  float result;
  std::memcpy(&result, &f, sizeof(result));
  return result;
}

} // namespace

bool CrateReader::Impl::UnpackHalf(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    uint16_t h = static_cast<uint16_t>(rep.payload() & 0xFFFF);
    out = Value(half_to_float(h));
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint16_t h;
  uint8_t hb[2];
  if (!reader_->read(hb, 2)) return false;
  h = static_cast<uint16_t>(hb[0]) | (static_cast<uint16_t>(hb[1]) << 8);
  out = Value(half_to_float(h));
  return true;
}

bool CrateReader::Impl::UnpackVec2h(ValueRep rep, Value& out) {
  uint16_t raw[2];
  if (rep.is_inlined()) {
    // 4 bytes fit in the inline payload (little-endian half lanes).
    uint64_t p = rep.payload();
    raw[0] = static_cast<uint16_t>(p & 0xFFFF);
    raw[1] = static_cast<uint16_t>((p >> 16) & 0xFFFF);
  } else {
    if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader_->read(raw, sizeof(raw))) return false;
  }
  out = Value::MakeFloat2(half_to_float(raw[0]), half_to_float(raw[1]));
  return true;
}

bool CrateReader::Impl::UnpackVec3h(ValueRep rep, Value& out) {
  uint16_t raw[3];
  if (rep.is_inlined()) {
    // 6 bytes fit in the 48-bit inline payload (little-endian half lanes).
    uint64_t p = rep.payload();
    raw[0] = static_cast<uint16_t>(p & 0xFFFF);
    raw[1] = static_cast<uint16_t>((p >> 16) & 0xFFFF);
    raw[2] = static_cast<uint16_t>((p >> 32) & 0xFFFF);
  } else {
    if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader_->read(raw, sizeof(raw))) return false;
  }
  out = Value::MakeFloat3(half_to_float(raw[0]), half_to_float(raw[1]), half_to_float(raw[2]));
  return true;
}

bool CrateReader::Impl::UnpackVec4h(ValueRep rep, Value& out) {
  // 8 bytes — never inlinable (inline payload is 6 bytes).
  if (rep.is_inlined()) return false;
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint16_t raw[4];
  if (!reader_->read(raw, sizeof(raw))) return false;
  out = Value::MakeFloat4(half_to_float(raw[0]), half_to_float(raw[1]),
                           half_to_float(raw[2]), half_to_float(raw[3]));
  return true;
}

bool CrateReader::Impl::UnpackQuath(ValueRep rep, Value& out) {
  // 8 bytes — never inlinable (inline payload is 6 bytes).
  if (rep.is_inlined()) return false;
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint16_t raw[4];
  if (!reader_->read(raw, sizeof(raw))) return false;
  out = Value::MakeQuatf(half_to_float(raw[0]), half_to_float(raw[1]),
                          half_to_float(raw[2]), half_to_float(raw[3]));
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
    case CrateTypeId::TimeSamples: return UnpackTimeSamples(rep, out);
    case CrateTypeId::Half: return UnpackHalf(rep, out);
    case CrateTypeId::Vec2i: return UnpackVec2i(rep, out);
    case CrateTypeId::Vec3i: return UnpackVec3i(rep, out);
    case CrateTypeId::Vec4i: return UnpackVec4i(rep, out);
    case CrateTypeId::Vec2h: return UnpackVec2h(rep, out);
    case CrateTypeId::Vec3h: return UnpackVec3h(rep, out);
    case CrateTypeId::Vec4h: return UnpackVec4h(rep, out);
    case CrateTypeId::Quath: return UnpackQuath(rep, out);

    case CrateTypeId::ReferenceListOp:
    case CrateTypeId::PayloadListOp: {
      std::vector<std::string> arcs;
      if (!DecodeReferenceListOp(rep, type_id == CrateTypeId::PayloadListOp,
                                 arcs)) {
        return false;
      }
      out = Value::MakeTokenArray(std::move(arcs));
      return true;
    }

    default:
      AddWarning(std::string("Unsupported value type: ") + CrateTypeIdName(type_id));
      return false;
  }
}

namespace {
// True for array element types that DecodeCrateArray can materialize, so a lazy
// reference emitted for them is always safe to decode on demand. Int/UInt are
// fine whether integer-compressed or raw; the others only when uncompressed
// (compressed POD float/vec arrays do not occur and aren't decodable here).
bool IsLazyArrayType(CrateTypeId t, bool compressed) {
  switch (t) {
    case CrateTypeId::Int:
    case CrateTypeId::UInt:
      return true;
    case CrateTypeId::Float:
    case CrateTypeId::Vec2f:
    case CrateTypeId::Vec3f:
    case CrateTypeId::Double:
    case CrateTypeId::Int64:
    case CrateTypeId::UInt64:
    case CrateTypeId::Bool:
      return !compressed;
    default:
      return false;
  }
}
}  // namespace

bool CrateReader::Impl::UnpackArray(ValueRep rep, Value& out) {
  CrateTypeId type_id = rep.type_id();

  // Fast path: keep numeric POD arrays as lazy references into the retained
  // source buffer instead of decoding them now. They are materialized on first
  // access and can be written back via verbatim byte pass-through. Token/string
  // and unsupported array types fall through to eager decode below.
  if (options_.lazy_arrays && source_ &&
      IsLazyArrayType(type_id, rep.is_compressed())) {
    LazyArrayRef lr;
    if (ProbeArrayBlock(source_, rep, options_.max_array_elements, &lr)) {
      out = Value::MakeLazyArray(lr);
      return true;
    }
    // Malformed header: fall through to eager decode, which reports the error.
  }

  // Arrays below this length are always stored uncompressed even if the
  // is_compressed bit is set (matches pxrUSD / legacy core kMinCompressedArraySize).
  constexpr uint64_t kMinCompressedArraySize = 16;

  // payload()==0 (non-inlined) denotes an empty array (pxrUSD convention); no
  // data block to seek to. Otherwise seek to the block and read the count.
  uint64_t count = 0;
  if (rep.payload() != 0) {
    if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader_->read_u64(count)) return false;
  }

  // Bound the file-controlled element count before any allocation.
  if (count > options_.max_array_elements) {
    AddWarning("Array element count exceeds max_array_elements limit");
    return false;
  }

  const bool compressed = rep.is_compressed();

  // Decode a compressed integer array ([u64 compSize][LZ4(delta) blob]) in place,
  // mirroring ReadFields. dst must hold `count` uint32_t.
  auto read_compressed_u32 = [&](uint32_t* dst) -> bool {
    uint64_t comp_size;
    if (!reader_->read_u64(comp_size)) return false;
    std::vector<uint8_t> blob;
    if (!reader_->read(blob, static_cast<size_t>(comp_size))) return false;  // overflow-safe
    std::vector<uint8_t> with_prefix(8 + blob.size());
    std::memcpy(with_prefix.data(), &comp_size, 8);
    if (!blob.empty()) std::memcpy(with_prefix.data() + 8, blob.data(), blob.size());
    DecompressResult dr = DecompressCompressedU32(
        with_prefix.data(), with_prefix.size(), dst, static_cast<size_t>(count));
    if (!dr.success) {
      AddWarning("Failed to decompress integer array: " + dr.error);
      return false;
    }
    return true;
  };

  // Read `count` raw elements of `elem_size` bytes into `dst` (overflow-safe).
  auto read_raw = [&](void* dst, size_t elem_size) -> bool {
    if (count == 0) return true;
    if (!reader_->has_elements(count, elem_size)) return false;
    return reader_->read(dst, static_cast<size_t>(count) * elem_size);
  };

  switch (type_id) {
    case CrateTypeId::Float: {
      if (compressed) { AddWarning("Compressed float arrays not supported"); return false; }
      std::vector<float> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(float))) return false;
      out = Value::MakeFloatArray(std::move(data));
      return true;
    }
    case CrateTypeId::Int: {
      std::vector<int32_t> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u32(reinterpret_cast<uint32_t*>(data.data()))) return false;
      } else if (!read_raw(data.data(), sizeof(int32_t))) {
        return false;
      }
      out = Value::MakeIntArray(std::move(data));
      return true;
    }
    case CrateTypeId::Vec2f: {
      if (compressed) { AddWarning("Compressed Vec2f arrays not supported"); return false; }
      size_t data_size;
      if (!safe::mul(static_cast<size_t>(count), size_t(2), &data_size)) {
        return false;
      }
      std::vector<float> data(data_size);
      if (!read_raw(data.data(), 2 * sizeof(float))) return false;
      out = Value::MakeFloat2Array(std::move(data));
      return true;
    }
    case CrateTypeId::Vec3f: {
      if (compressed) { AddWarning("Compressed Vec3f arrays not supported"); return false; }
      size_t data_size;
      if (!safe::mul(static_cast<size_t>(count), size_t(3), &data_size)) {
        return false;
      }
      std::vector<float> data(data_size);
      if (!read_raw(data.data(), 3 * sizeof(float))) return false;
      out = Value::MakeFloat3Array(std::move(data));
      return true;
    }
    case CrateTypeId::Double: {
      if (compressed) { AddWarning("Compressed double arrays not supported"); return false; }
      std::vector<double> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(double))) return false;
      out = Value::MakeDoubleArray(std::move(data));
      return true;
    }
    case CrateTypeId::Int64: {
      if (compressed) { AddWarning("Compressed int64 arrays not supported"); return false; }
      std::vector<int64_t> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(int64_t))) return false;
      out = Value::MakeInt64Array(std::move(data));
      return true;
    }
    case CrateTypeId::UInt: {
      std::vector<uint32_t> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u32(data.data())) return false;
      } else if (!read_raw(data.data(), sizeof(uint32_t))) {
        return false;
      }
      out = Value::MakeUIntArray(std::move(data));
      return true;
    }
    case CrateTypeId::UInt64: {
      if (compressed) { AddWarning("Compressed uint64 arrays not supported"); return false; }
      std::vector<uint64_t> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(uint64_t))) return false;
      out = Value::MakeUInt64Array(std::move(data));
      return true;
    }
    case CrateTypeId::Bool: {
      if (compressed) { AddWarning("Compressed bool arrays not supported"); return false; }
      std::vector<uint8_t> bytes(static_cast<size_t>(count));
      if (!read_raw(bytes.data(), sizeof(uint8_t))) return false;
      std::vector<bool> out_bool(static_cast<size_t>(count));
      for (size_t i = 0; i < count; i++) out_bool[i] = (bytes[i] != 0);
      out = Value::MakeBoolArray(out_bool);
      return true;
    }
    case CrateTypeId::Token: {
      if (compressed) { AddWarning("Compressed token arrays not supported"); return false; }
      std::vector<uint32_t> idxs(static_cast<size_t>(count));
      if (!read_raw(idxs.data(), sizeof(uint32_t))) return false;  // bulk read indices
      std::vector<std::string> data(static_cast<size_t>(count));
      for (size_t i = 0; i < count; i++) {
        if (idxs[i] >= tokens_.size()) return false;
        data[i] = tokens_[idxs[i]];
      }
      out = Value::MakeTokenArray(std::move(data));
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

CrateReadResult CrateReader::Impl::ReadFromString(std::string&& bytes) {
  result_ = CrateReadResult();
  tokens_.clear();
  string_indices_.clear();
  fields_.clear();
  fieldset_indices_.clear();
  specs_.clear();
  paths_.clear();
  fieldset_cache_.clear();

  if (bytes.size() < kCrateBootstrapSize) {
    AddError("Invalid input data");
    return std::move(result_);
  }

  // Adopt the input bytes by MOVE — a single in-heap copy of the file. Lazy
  // array Values reference this buffer after Read() returns (they hold a
  // shared_ptr to the source); reader_ reads from it so ValueRep file offsets
  // stay consistent.
  source_ = CrateDataSource::Adopt(std::move(bytes), CrateVersion{});
  reader_ = std::make_unique<StreamReader>(source_->base(), source_->size());

  // Parse in order
  if (!ReadBootstrap()) return std::move(result_);
  source_->set_version(version_);
  if (!ReadTOC()) return std::move(result_);
  if (!ReadTokens()) return std::move(result_);
  if (!ReadStrings()) return std::move(result_);
  if (!ReadFields()) return std::move(result_);
  if (!ReadFieldsets()) return std::move(result_);
  if (!ReadSpecs()) return std::move(result_);
  if (!ReadPaths()) return std::move(result_);
  if (!BuildStage()) return std::move(result_);

  // The fieldset cache held a full second copy of every fieldset's unpacked
  // Values (including large arrays) purely to serve BuildStage. The data now
  // lives in the built Stage/Layer, so release the duplicate to reclaim memory.
  fieldset_cache_.clear();

  result_.success = result_.errors.empty();
  result_.version = version_;
  return std::move(result_);
}

CrateReadResult CrateReader::Impl::Read(const uint8_t* data, size_t size) {
  if (!data) {
    result_ = CrateReadResult();
    AddError("Invalid input data");
    return std::move(result_);
  }
  // Const input: caller may free `data`, so copy it into an owned buffer once.
  return ReadFromString(std::string(reinterpret_cast<const char*>(data), size));
}

CrateReadResult CrateReader::Impl::ReadOwned(std::string&& owned) {
  return ReadFromString(std::move(owned));
}

CrateReadResult CrateReader::Impl::ReadFile(const char* filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    CrateReadResult result;
    result.errors.push_back({0, std::string("Failed to open file: ") + filename});
    return result;
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  // Read straight into the owned buffer (single copy; no intermediate vector).
  std::string data(static_cast<size_t>(size), '\0');
  if (!file.read(&data[0], size)) {
    CrateReadResult result;
    result.errors.push_back({0, "Failed to read file contents"});
    return result;
  }

  return ReadOwned(std::move(data));
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

    // Overflow-safe: s.start + s.size as int64 can wrap. Check each term
    // against the file size with subtraction that cannot overflow.
    const size_t fsize = reader_->size();
    if (s.start < 0 || s.size < 0 ||
        static_cast<size_t>(s.start) > fsize ||
        static_cast<size_t>(s.size) > fsize - static_cast<size_t>(s.start)) {
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

  // Use the check-before-resize read overload so a bogus compressed_size
  // cannot trigger a huge allocation before the bounds check.
  std::vector<uint8_t> compressed;
  if (!reader_->read(compressed, static_cast<size_t>(compressed_size))) {
    AddError("Failed to read compressed tokens");
    return false;
  }

  DecompressResult dr = DecompressCrateBlob(compressed.data(), compressed.size(),
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

  std::vector<uint8_t> indices_data;
  if (!reader_->read(indices_data, static_cast<size_t>(indices_size))) {
    AddError("Failed to read field indices");
    return false;
  }

  // Try pxrUSD format (n_chunks LZ4 + delta-coded) first, fall back to legacy
  std::vector<uint32_t> token_indices_vec(static_cast<size_t>(num_fields));
  // Include u64 compressed_size prefix (DecompressCompressedU32 expects it)
  std::vector<uint8_t> indices_with_prefix(8 + indices_data.size());
  std::memcpy(indices_with_prefix.data(), &indices_size, 8);
  std::memcpy(indices_with_prefix.data() + 8, indices_data.data(), indices_data.size());
  DecompressResult dr = DecompressCompressedU32(indices_with_prefix.data(), indices_with_prefix.size(),
                                                 token_indices_vec.data(),
                                                 static_cast<size_t>(num_fields));
  if (!dr.success) {
    // Fall back to legacy format
    dr = DecompressIntegers(indices_with_prefix.data() + 8, indices_with_prefix.size() - 8,
                            static_cast<size_t>(num_fields), false);
    if (!dr.success) {
      AddError("Failed to decompress field indices: " + dr.error);
      return false;
    }
    if (dr.data.size() < num_fields * sizeof(uint32_t)) {
      AddError("Decompressed field indices shorter than expected");
      return false;
    }
    std::memcpy(token_indices_vec.data(), dr.data.data(),
                num_fields * sizeof(uint32_t));
  }

  const uint32_t* token_indices = token_indices_vec.data();

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
    std::vector<uint8_t> reps_data;
    if (!reader_->read(reps_data, static_cast<size_t>(reps_size))) {
      AddError("Failed to read compressed value reps");
      return false;
    }

    DecompressResult rdr = DecompressCrateBlob(reps_data.data(), reps_data.size(),
                                               static_cast<size_t>(num_fields * 8));
    if (!rdr.success) {
      AddError("Failed to decompress value reps");
      return false;
    }
    if (rdr.data.size() < num_fields * 8) {
      AddError("Decompressed value reps shorter than expected");
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

  // fieldset_indices_ entries index into fields_; bound the count to avoid a
  // huge allocation from a malformed value (no dedicated max, reuse max_fields).
  if (num_fieldsets > options_.max_fields) {
    AddError("Too many fieldset indices");
    return false;
  }

  if (section->size < 8) {
    AddError("FIELDSETS section too small");
    return false;
  }
  size_t data_size = static_cast<size_t>(section->size) - 8;
  std::vector<uint8_t> data;
  if (!reader_->read(data, data_size)) {
    AddError("Failed to read fieldset data");
    return false;
  }

  fieldset_indices_.resize(static_cast<size_t>(num_fieldsets));
  DecompressResult dr = DecompressCompressedU32(data.data(), data.size(),
                                                 fieldset_indices_.data(),
                                                 static_cast<size_t>(num_fieldsets));
  if (!dr.success) {
    // Legacy fallback: try EncodeIntegers (common-prefix, no LZ4)
    dr = DecompressIntegers(data.data(), data.size(),
                            static_cast<size_t>(num_fieldsets), false);
    if (!dr.success) {
      AddError("Failed to decompress fieldsets: " + dr.error);
      return false;
    }
    if (dr.data.size() < num_fieldsets * sizeof(uint32_t)) {
      AddError("Decompressed fieldsets shorter than expected");
      return false;
    }
    std::memcpy(fieldset_indices_.data(), dr.data.data(), num_fieldsets * sizeof(uint32_t));
  }

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

  if (section->size < 8) {
    AddError("SPECS section too small");
    return false;
  }

  specs_.resize(static_cast<size_t>(num_specs));

  uint64_t remaining_size = static_cast<uint64_t>(section->size) - 8;

  // Read 3 compressed integer arrays (delta+LZ4 Usd_IntegerCompression format)
  auto read_comp_array = [&](uint32_t* dst, size_t count, uint64_t& remaining) -> bool {
    if (remaining < 8) {
      AddError("Specs section: not enough data for array size");
      return false;
    }
    uint64_t comp_size;
    if (!reader_->read_u64(comp_size)) {
      AddError("Failed to read specs array compressed size");
      return false;
    }
    remaining -= 8;

    if (comp_size > remaining) {
      AddError("Specs compressed size exceeds remaining section data");
      return false;
    }

    // Include u64 compressed_size prefix (DecompressCompressedU32 expects it)
    std::vector<uint8_t> comp_data(8 + static_cast<size_t>(comp_size));
    std::memcpy(comp_data.data(), &comp_size, 8);
    if (!reader_->read(comp_data.data() + 8, static_cast<size_t>(comp_size))) {
      AddError("Failed to read specs compressed data");
      return false;
    }
    remaining -= comp_size;

    DecompressResult dr = DecompressCompressedU32(comp_data.data(), comp_data.size(),
                                                   dst, count);
    if (!dr.success) {
      // Fallback: try legacy EncodeIntegers (common-prefix, no LZ4)
      dr = DecompressIntegers(comp_data.data(), comp_data.size(), count, false);
      if (!dr.success) {
        AddError("Failed to decompress specs array: " + dr.error);
        return false;
      }
      if (dr.data.size() < count * sizeof(uint32_t)) {
        AddError("Decompressed specs array shorter than expected");
        return false;
      }
      std::memcpy(dst, dr.data.data(), count * sizeof(uint32_t));
    }
    return true;
  };

  std::vector<uint32_t> path_vals(static_cast<size_t>(num_specs));
  std::vector<uint32_t> fieldset_vals(static_cast<size_t>(num_specs));
  std::vector<uint32_t> type_vals(static_cast<size_t>(num_specs));

  if (read_comp_array(path_vals.data(), static_cast<size_t>(num_specs), remaining_size) &&
      read_comp_array(fieldset_vals.data(), static_cast<size_t>(num_specs), remaining_size) &&
      read_comp_array(type_vals.data(), static_cast<size_t>(num_specs), remaining_size)) {
    for (size_t i = 0; i < num_specs; i++) {
      specs_[i].path_index.value = path_vals[i];
      specs_[i].fieldset_index.value = fieldset_vals[i];
      specs_[i].spec_type = static_cast<SpecType>(type_vals[i]);
    }
    return true;
  }

  // Fallback: try legacy single-array format
  if (!reader_->seek(static_cast<size_t>(section->start) + 8)) {
    AddError("Failed to seek to specs data for legacy read");
    return false;
  }
  size_t legacy_size = static_cast<size_t>(section->size) - 8;
  std::vector<uint8_t> legacy_data(legacy_size);
  if (!reader_->read(legacy_data.data(), legacy_size)) {
    AddError("Failed to read specs legacy data");
    return false;
  }
  if (legacy_size == num_specs * 12) {
    const uint8_t* ptr = legacy_data.data();
    for (size_t i = 0; i < num_specs; i++) {
      std::memcpy(&specs_[i].path_index.value, ptr, 4); ptr += 4;
      std::memcpy(&specs_[i].fieldset_index.value, ptr, 4); ptr += 4;
      uint32_t spec_type;
      std::memcpy(&spec_type, ptr, 4); ptr += 4;
      specs_[i].spec_type = static_cast<SpecType>(spec_type);
    }
  } else {
    DecompressResult dr = DecompressIntegers(legacy_data.data(), legacy_data.size(),
                                              static_cast<size_t>(num_specs * 3), false);
    if (dr.success) {
      const uint32_t* vals = reinterpret_cast<const uint32_t*>(dr.data.data());
      for (size_t i = 0; i < num_specs; i++) {
        specs_[i].path_index.value = vals[i * 3];
        specs_[i].fieldset_index.value = vals[i * 3 + 1];
        specs_[i].spec_type = static_cast<SpecType>(vals[i * 3 + 2]);
      }
    } else {
      AddError("Failed to decompress specs with any format");
      return false;
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

  if (num_paths == 0) {
    paths_.resize(1);
    paths_[0] = "/";
    return true;
  }

  if (num_paths > options_.max_paths) {
    AddError("Too many paths");
    return false;
  }

  // pxrUSD writes two u64 values: [total_path_count] [encoded_tree_node_count]
  uint64_t num_encoded = num_paths;  // default: same as total
  if (!reader_->read_u64(num_encoded)) {
    // Might be a single-count format (no encoded count)
    num_encoded = num_paths;
  }

  if (num_encoded > options_.max_paths) {
    AddError("Too many encoded path nodes");
    return false;
  }

  paths_.resize(static_cast<size_t>(num_paths));
  if (paths_.size() > 0) {
    paths_[0] = "/";
  }
  size_t n = static_cast<size_t>(num_encoded);

  // Read 3 compressed integer arrays (delta+LZ4 format)
  auto read_comp_array = [&](uint32_t* dst, size_t count, const char* name) -> bool {
    uint64_t comp_size;
    if (!reader_->read_u64(comp_size)) {
      AddError(std::string("Failed to read ") + name + " compressed size");
      return false;
    }
    // Bound the compressed size against remaining bytes before allocating.
    if (comp_size > reader_->remaining()) {
      AddError(std::string(name) + " compressed size exceeds remaining data");
      return false;
    }
    // Include u64 compressed_size prefix (DecompressCompressedU32 expects it)
    std::vector<uint8_t> comp_data(8 + static_cast<size_t>(comp_size));
    std::memcpy(comp_data.data(), &comp_size, 8);
    if (!reader_->read(comp_data.data() + 8, static_cast<size_t>(comp_size))) {
      AddError(std::string("Failed to read ") + name + " compressed data");
      return false;
    }
    DecompressResult dr = DecompressCompressedU32(comp_data.data(), comp_data.size(),
                                                   dst, count);
    if (!dr.success) {
      // Fallback: legacy EncodeIntegers (common-prefix, no LZ4)
      dr = DecompressIntegers(comp_data.data() + 8, static_cast<size_t>(comp_size), count, false);
      if (!dr.success) {
        AddError(std::string("Failed to decompress ") + name + ": " + dr.error);
        return false;
      }
      if (dr.data.size() < count * sizeof(uint32_t)) {
        AddError(std::string("Decompressed ") + name + " shorter than expected");
        return false;
      }
      std::memcpy(dst, dr.data.data(), count * sizeof(uint32_t));
    }
    return true;
  };

  std::vector<uint32_t> path_indices(n);
  std::vector<uint32_t> element_tokens(n);
  std::vector<uint32_t> jump_raw(n);  // stored as uint32_t, interpreted as int32_t

  if (!read_comp_array(path_indices.data(), n, "path indices") ||
      !read_comp_array(element_tokens.data(), n, "element tokens") ||
      !read_comp_array(jump_raw.data(), n, "jump indices")) {
    return false;
  }

  // Reconstruct paths from the compressed tree by navigating jump offsets.
  //
  // Nodes are emitted in pre-order. jump semantics (matching the writer):
  //   jump  > 0 : node has a child (at i+1) AND a sibling (at i+jump)
  //   jump == 0 : node has a sibling only (at i+1), no child
  //   jump == -1: node has a child only (at i+1), no sibling
  //   jump == -2: leaf (no child, no sibling)
  //
  // The previous decoder kept a flat ancestor stack and popped only ONCE per
  // leaf, so it could not unwind multiple levels at a subtree boundary — the
  // stack grew without bound on deep/many-sibling trees, giving O(n^2) memory
  // (every path got longer) and corrupted/colliding paths. Passing the parent
  // path down the recursion and following the jump offset to each sibling is
  // O(num_nodes) and correct.
  paths_.assign(static_cast<size_t>(num_paths), std::string());

  auto element_for = [&](size_t i, bool& is_prop) -> std::string {
    int32_t elem_token = static_cast<int32_t>(element_tokens[i]);
    is_prop = elem_token < 0;
    // Promote to int64 before negating (-INT32_MIN is UB).
    uint32_t token_idx = is_prop
        ? static_cast<uint32_t>(-static_cast<int64_t>(elem_token))
        : static_cast<uint32_t>(elem_token);
    if (token_idx < tokens_.size()) return tokens_[token_idx];
    return "__unknown_token_" + std::to_string(token_idx);
  };

  // Recurse over a sibling chain that all share `parent` (the parent prim path,
  // without any property '.' prefix). Depth is bounded by max_path_depth.
  std::function<void(size_t, const std::string&, size_t)> build =
      [&](size_t i, const std::string& parent, size_t depth) {
        while (i < n) {
          bool is_prop = false;
          std::string elem = element_for(i, is_prop);
          int32_t jump = static_cast<int32_t>(jump_raw[i]);

          bool is_root = parent.empty() && (elem.empty() || elem == "/");
          std::string prim_path;  // base path (no '.' prefix) for children
          if (is_root) {
            prim_path = "/";
          } else if (parent.empty() || parent == "/") {
            prim_path = "/" + elem;
          } else {
            prim_path = parent + "/" + elem;
          }

          uint32_t store_idx = path_indices[i];
          if (store_idx < num_paths) {
            paths_[store_idx] = is_prop ? ("." + prim_path) : prim_path;
          }

          const bool has_child = (jump == -1 || jump > 0);
          const bool has_sibling = (jump == 0 || jump > 0);

          if (has_child && depth < options_.max_path_depth) {
            build(i + 1, prim_path, depth + 1);
          }
          if (!has_sibling) return;
          i += (jump > 0) ? static_cast<size_t>(jump) : 1;
        }
      };
  build(0, std::string(), 0);

  return true;
}

bool CrateReader::Impl::BuildStage() {
  // Create layer and builder
  Layer layer;
  LayerBuilder builder(layer);

  // First, process the PseudoRoot spec to extract layer metadata
  for (const auto& spec : specs_) {
    if (spec.spec_type != SpecType::PseudoRoot) continue;
    if (spec.path_index.value >= paths_.size()) continue;

    std::vector<std::pair<std::string, Value>> fields;
    if (!ResolveFieldset(spec.fieldset_index.value, fields)) {
      AddWarning("Failed to resolve pseudo-root fieldset");
    }

    for (auto& field : fields) {
      if (field.first == "defaultPrim") {
        if (const std::string* s = field.second.as_token())
          layer.meta().defaultPrim = *s;
      } else if (field.first == "upAxis") {
        if (const std::string* s = field.second.as_token())
          layer.meta().upAxis = *s;
      } else if (field.first == "metersPerUnit") {
        const double* d = field.second.as_double();
        if (d) layer.meta().metersPerUnit = *d;
      } else if (field.first == "timeCodesPerSecond") {
        const double* d = field.second.as_double();
        if (d) layer.meta().timeCodesPerSecond = *d;
      } else if (field.first == "startTimeCode") {
        const double* d = field.second.as_double();
        if (d) layer.meta().startTimeCode = *d;
      } else if (field.first == "endTimeCode") {
        const double* d = field.second.as_double();
        if (d) layer.meta().endTimeCode = *d;
      } else if (field.first == "doc") {
        if (const std::string* s = field.second.as_string())
          layer.meta().doc = *s;
      } else if (field.first == "comment") {
        if (const std::string* s = field.second.as_string())
          layer.meta().comment = *s;
      } else if (field.first == "subLayers") {
        if (const std::vector<std::string>* arr = field.second.as_token_array()) {
          for (const auto& s : *arr) layer.meta().subLayers.push_back(s);
        } else if (const std::string* s = field.second.as_string()) {
          layer.meta().subLayers.push_back(*s);
        } else if (const std::string* s = field.second.as_token()) {
          layer.meta().subLayers.push_back(*s);
        }
      }
    }
    break; // Only process first PseudoRoot
  }

  // Process prim specs to build prims with hierarchy from paths
  struct PrimEntry {
    std::string full_path;
    std::string name;
    std::string type_name;
    PrimSpecifier specifier;
    std::vector<std::pair<std::string, Value>> fields;
  };

  // Variant content lives in the layer as bracketed holder/child prims
  // ("/Prim/{vset=sel}" + descendants); their attributes attach to those prims
  // via attr_map (so the layer fully represents the variants, enabling both the
  // compositor — which reads the selected holder — and unflattened round-trip).
  // We only need to know, per owning prim, which sets are selected.
  //   owning prim path -> { variantSet -> selected variant } (all selections).
  std::unordered_map<std::string, std::map<std::string, std::string>> variant_sel;

  std::vector<PrimEntry> prim_entries;
  for (const auto& spec : specs_) {
    // Build Prim specs AND variant-namespace holders (Variant/VariantSet specs
    // at "/Prim/{vset=sel}" / "/Prim/{vset=}"). The holders are needed so that
    // variant CHILD prims ("/Prim/{vset=sel}/Geo") get a correct path from the
    // depth-stack builder (begin_prim recomputes path from its parent). Holders
    // and variant sub-prims are grafted onto the owning prim by the compositor
    // on selection and skipped by the writer (their paths contain '{').
    const bool is_prim = spec.spec_type == SpecType::Prim;
    const bool is_variant = spec.spec_type == SpecType::Variant ||
                            spec.spec_type == SpecType::VariantSet;
    if (!is_prim && !is_variant) continue;

    if (spec.path_index.value >= paths_.size()) continue;
    std::string full_path = paths_[spec.path_index.value];
    if (full_path.empty() || full_path == "/") continue;
    const bool bracketed = full_path.find('{') != std::string::npos;
    // Only real (non-bracketed) Prim specs carry a variantSelection.
    if (is_variant && !bracketed) continue;  // defensive

    // Decode the prim's variantSelection (a VariantSelectionMap that
    // UnpackValue/ResolveFieldset cannot represent). Captures every selection
    // so prims with multiple variant sets compose correctly. (Only for the
    // owning, non-bracketed prim.)
    if (!bracketed) {
      std::vector<std::pair<std::string, ValueRep>> raw;
      if (ResolveFieldsetRaw(spec.fieldset_index.value, raw)) {
        for (auto& f : raw) {
          if (f.first == "variantSelection") {
            std::vector<std::pair<std::string, std::string>> sels;
            if (DecodeVariantSelectionMap(f.second, sels)) {
              for (auto& kv : sels) variant_sel[full_path][kv.first] = kv.second;
            }
            break;
          }
        }
      }
    }

    size_t last_slash = full_path.rfind('/');
    std::string prim_name = (last_slash != std::string::npos && last_slash < full_path.size() - 1)
                            ? full_path.substr(last_slash + 1) : full_path;
    if (prim_name.empty()) continue;

    PrimEntry entry;
    entry.full_path = full_path;
    entry.name = prim_name;

    std::vector<std::pair<std::string, Value>> fields;
    ResolveFieldset(spec.fieldset_index.value, fields);

    // Untyped prims stay untyped: composition fills the type from the
    // referenced prim (a forced "Xform" default would mask e.g. a referenced
    // Mesh definition; the writer already skips empty type names).
    entry.type_name.clear();
    entry.specifier = PrimSpecifier::Def;
    for (auto& f : fields) {
      if (f.first == "typeName") {
        if (const std::string* s = f.second.as_token()) entry.type_name = *s;
      } else if (f.first == "specifier") {
        if (const std::string* s = f.second.as_token()) {
          if (*s == "over") entry.specifier = PrimSpecifier::Over;
          else if (*s == "class") entry.specifier = PrimSpecifier::Class;
        }
      } else {
        entry.fields.push_back(std::move(f));
      }
    }
    prim_entries.push_back(std::move(entry));
  }

  // Collect separate Attribute and Relationship specs. pxrUSD-authored crates
  // store each property as its own spec at "<primpath>/<name>" (rendered with a
  // leading '.' marker by ReadPaths), rather than inline in the prim's
  // fieldset. We capture, for each property: its declared typeName, its
  // `default` value (lazy if an array — no payload decode), any connection
  // targets, relationship targets, and uniform variability. These are attached
  // to the parent prim below so the writer can faithfully re-emit them.
  struct AttrInfo {
    std::string name;
    std::string type_name;
    bool has_default = false;
    Value default_value;
    bool is_connection = false;
    std::vector<std::string> connection_targets;
    bool uniform = false;
  };
  struct RelInfo {
    std::string name;
    std::vector<std::string> targets;
    bool uniform = false;
  };
  std::unordered_map<std::string, std::vector<AttrInfo>> attr_map;
  std::unordered_map<std::string, std::vector<RelInfo>> rel_map;

  // Split a property spec path ".<primpath>/<name>" into (primpath, name).
  auto split_prop_path = [](const std::string& raw, std::string& prim_path,
                            std::string& prop_name) -> bool {
    if (raw.empty()) return false;
    size_t begin = (raw[0] == '.') ? 1 : 0;
    size_t slash = raw.rfind('/');
    if (slash == std::string::npos || slash < begin) return false;
    prim_path = raw.substr(begin, slash - begin);
    prop_name = raw.substr(slash + 1);
    return !prim_path.empty() && !prop_name.empty();
  };

  for (const auto& spec : specs_) {
    const bool is_attr = spec.spec_type == SpecType::Attribute;
    const bool is_rel = spec.spec_type == SpecType::Relationship;
    if (!is_attr && !is_rel) continue;
    if (spec.path_index.value >= paths_.size()) continue;
    const std::string& raw_path = paths_[spec.path_index.value];

    // Variant attributes (bracketed paths) attach to their bracketed holder /
    // child prim like any other attribute (split_prop_path handles them): e.g.
    // ".Prim/{vset=sel}/prop" -> prim_path "/Prim/{vset=sel}" (the holder),
    // ".Prim/{vset=sel}/Geo/attr" -> "/Prim/{vset=sel}/Geo" (a variant child).
    // The compositor reads the selected holder; the writer re-emits it.
    std::string prim_path, prop_name;
    if (!split_prop_path(raw_path, prim_path, prop_name)) {
      continue;
    }

    std::vector<std::pair<std::string, ValueRep>> raw_fields;
    if (!ResolveFieldsetRaw(spec.fieldset_index.value, raw_fields)) continue;

    if (is_rel) {
      RelInfo ri;
      ri.name = std::move(prop_name);
      for (auto& f : raw_fields) {
        if (f.first == "targetPaths") {
          DecodePathTargets(f.second, ri.targets);
        } else if (f.first == "variability") {
          Value v;
          if (UnpackValue(f.second, v)) {
            if (const std::string* s = v.as_token()) ri.uniform = (*s == "uniform");
          }
        }
      }
      rel_map[prim_path].push_back(std::move(ri));
      continue;
    }

    // Attribute spec.
    AttrInfo ai;
    ai.name = std::move(prop_name);
    for (auto& f : raw_fields) {
      if (f.first == "typeName") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) ai.type_name = *s;
        }
      } else if (f.first == "default") {
        Value v;
        if (UnpackValue(f.second, v)) {
          ai.default_value = std::move(v);
          ai.has_default = true;
        }
      } else if (f.first == "connectionPaths") {
        if (DecodePathTargets(f.second, ai.connection_targets) &&
            !ai.connection_targets.empty()) {
          ai.is_connection = true;
        }
      } else if (f.first == "variability") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) ai.uniform = (*s == "uniform");
        }
      }
    }
    attr_map[prim_path].push_back(std::move(ai));
  }

  // Sort by full path (produces correct depth-first order with parents before children)
  std::sort(prim_entries.begin(), prim_entries.end(),
    [](const PrimEntry& a, const PrimEntry& b) {
      return a.full_path < b.full_path;
    });

  // Build hierarchy using depth-based stack management
  // Stack keeps ancestor paths at each depth level
  std::vector<std::string> prim_stack;

  for (auto& entry : prim_entries) {
    // Compute depth of this prim (number of '/' in path)
    size_t depth = std::count(entry.full_path.begin(), entry.full_path.end(), '/');

    // Pop stack until we're at the correct parent level
    while (prim_stack.size() > depth - 1) {
      builder.end_prim();
      prim_stack.pop_back();
    }

    // Begin this prim
    builder.begin_prim(entry.name, entry.type_name, entry.specifier);
    prim_stack.push_back(entry.name);

    // builder.current() is valid immediately after begin_prim; capture it once
    // and guard the metadata derefs (every other current() site is guarded too).
    PrimSpec* ps = builder.current();

    // Extracts a token-list metadata field (written as a token array, with
    // single-token / string fallbacks for older encodings). Warns rather than
    // silently dropping a known arc field that fails to decode.
    auto append_token_list = [&](const Value& v, std::vector<std::string>& dst,
                                 const char* field_name) {
      if (const std::vector<std::string>* arr = v.as_token_array()) {
        for (const auto& s : *arr) dst.push_back(s);
      } else if (const std::string* s = v.as_token()) {
        dst.push_back(*s);
      } else if (const std::string* s = v.as_string()) {
        dst.push_back(*s);
      } else {
        AddWarning(std::string("Composition arc field '") + field_name +
                   "' has unexpected encoding; dropped");
      }
    };

    // Add properties and extract composition arcs / metadata into PrimSpecMeta
    for (auto& field : entry.fields) {
      if (field.first == "typeName" || field.first == "specifier") continue;

      // Reserved prim metadata stored inline in the prim spec (pxrUSD keeps
      // these in the prim's fieldset, not as separate property specs). Route
      // them to PrimSpecMeta so they do not leak in as phantom properties.
      if (ps) {
        if (field.first == "active") {
          if (const bool* b = field.second.as_bool()) ps->meta().active = *b;
          continue;
        }
        if (field.first == "hidden") {
          if (const bool* b = field.second.as_bool()) ps->meta().hidden = *b;
          continue;
        }
        if (field.first == "doc") {
          if (const std::string* s = field.second.as_string())
            ps->meta().doc = *s;
          else if (const std::string* s = field.second.as_token())
            ps->meta().doc = *s;
          continue;
        }
      }

      // Composition arc + metadata fields: store in PrimSpecMeta, not as
      // regular properties. (Guarded by ps; if current() were null we simply
      // skip them rather than crash.)
      if (ps) {
        if (field.first == "references") {
          append_token_list(field.second, ps->meta().references, "references");
          continue;
        }
        if (field.first == "payload") {
          append_token_list(field.second, ps->meta().payloads, "payload");
          continue;
        }
        if (field.first == "inherits") {
          append_token_list(field.second, ps->meta().inherits, "inherits");
          continue;
        }
        if (field.first == "specializes") {
          append_token_list(field.second, ps->meta().specializes, "specializes");
          continue;
        }
        if (field.first == "variantSelection") {
          if (const std::string* s = field.second.as_token())
            ps->meta().variantSelection = *s;
          else if (const std::string* s = field.second.as_string())
            ps->meta().variantSelection = *s;
          continue;
        }
        if (field.first == "comment") {
          if (const std::string* s = field.second.as_token())
            ps->meta().comment = *s;
          else if (const std::string* s = field.second.as_string())
            ps->meta().comment = *s;
          continue;
        }
        if (field.first == "variantSets") {
          // Writer stores the variant-set names only; reconstruct name entries.
          std::vector<std::string> names;
          append_token_list(field.second, names, "variantSets");
          for (auto& n : names) {
            VariantSetData vsd;
            vsd.name = std::move(n);
            ps->meta().variantSets.push_back(std::move(vsd));
          }
          continue;
        }
      }
      // The sibling "variability" field cannot be re-associated with its
      // property in the current format; consume it so it does not surface as a
      // stray property. (Uniformity is not yet round-tripped — see review note.)
      if (field.first == "variability") continue;

      uint16_t flags = 0;
      builder.add_property(field.first, std::move(field.second), flags);
    }

    // Attach separate Attribute / Relationship specs that belong to this prim.
    // The dedup guard keeps any inline field authoritative.
    auto am = attr_map.find(entry.full_path);
    if (ps && am != attr_map.end()) {
      for (auto& ai : am->second) {
        if (ps->property(ai.name)) continue;  // inline opinion wins
        uint16_t flags = 0;
        if (ai.uniform) flags |= PropSlot::kFlagUniform;
        if (ai.is_connection) flags |= PropSlot::kFlagConnection;
        const bool is_array =
            ai.type_name.size() >= 2 &&
            ai.type_name.compare(ai.type_name.size() - 2, 2, "[]") == 0;
        if (ai.has_default) {
          ps->add_property(ai.name, std::move(ai.default_value), flags);
        } else {
          // Connection-only or declared-only attribute: register a typed slot
          // with no authored default value so it round-trips.
          if (is_array) flags |= PropSlot::kFlagArray;
          std::string base = is_array
              ? ai.type_name.substr(0, ai.type_name.size() - 2)
              : ai.type_name;
          TypeId tid = GetTypeIdFromName(base.c_str());
          ps->add_property_slot(GetPropNameTable().intern(ai.name), tid, flags);
        }
        if (!ai.type_name.empty()) {
          ps->set_property_type_name(ai.name, ai.type_name);
        }
        for (const auto& t : ai.connection_targets) {
          ps->add_connection(ai.name, Path(t));
        }
      }
    }
    auto rm = rel_map.find(entry.full_path);
    if (ps && rm != rel_map.end()) {
      for (auto& ri : rm->second) {
        // A target-less relationship is recorded with a single empty Path
        // marker (the writer emits no targetPaths for it); relationships with
        // targets push one Path per target.
        if (ri.targets.empty()) {
          ps->add_relationship(ri.name, Path());
        } else {
          for (const auto& t : ri.targets) ps->add_relationship(ri.name, Path(t));
        }
      }
    }

    // Variant sets: attach a VariantSetData{name, selected} per selected set.
    // The variant CONTENT (properties + child prims) lives in the layer's
    // bracketed holder prims, which the compositor reads on selection and the
    // writer re-emits; the model only carries the selection here.
    auto sel = variant_sel.find(entry.full_path);
    if (ps && sel != variant_sel.end()) {
      for (const auto& kv : sel->second) {
        VariantSetData vsd;
        vsd.name = kv.first;
        vsd.selected = kv.second;
        ps->meta().variantSets.push_back(std::move(vsd));
      }
      // Keep the first selection in the legacy single-string field too.
      if (!sel->second.empty()) {
        const auto& first = *sel->second.begin();
        ps->meta().variantSelection = first.first + "=" + first.second;
      }
    }
  }

  // Close remaining prims
  while (!prim_stack.empty()) {
    builder.end_prim();
    prim_stack.pop_back();
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

bool CrateReader::Impl::ResolveFieldsetRaw(
    uint32_t fieldset_index,
    std::vector<std::pair<std::string, ValueRep>>& out) {
  out.clear();
  if (fieldset_index >= fieldset_indices_.size()) return false;

  size_t start = fieldset_index;
  while (start < fieldset_indices_.size()) {
    uint32_t field_idx = fieldset_indices_[start];
    if (field_idx == 0xFFFFFFFF) break;
    if (field_idx < fields_.size()) {
      const CrateField& field = fields_[field_idx];
      std::string name;
      if (GetToken(field.token_index.value, name)) {
        out.emplace_back(std::move(name), field.value_rep);
      }
    }
    start++;
  }
  return true;
}

bool CrateReader::Impl::DecodePathTargets(ValueRep rep,
                                          std::vector<std::string>& out) {
  // Reads one or more [u64 count][u32 path_index * count] runs of path indices
  // (uncompressed, per pxrUSD). PathVector has a single run; PathListOp is
  // prefixed by a 1-byte ListOpHeader selecting which sublists are present. We
  // flatten every present sublist's targets (sufficient for round-tripping
  // relationship/connection targets; list-edit semantics are not preserved).
  CrateTypeId tid = rep.type_id();
  if (tid != CrateTypeId::PathVector && tid != CrateTypeId::PathListOp) {
    return false;
  }
  if (rep.payload() == 0) return true;  // empty
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  auto read_run = [&]() -> bool {
    uint64_t n = 0;
    if (!reader_->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    for (uint64_t i = 0; i < n; ++i) {
      uint32_t idx = 0;
      if (!reader_->read_u32(idx)) return false;
      if (idx < paths_.size()) {
        // paths_ renders a property path as ".<primpath>/<prop>"; convert to the
        // canonical USD form "<primpath>.<prop>" so targets re-intern correctly
        // (and survive repeated round-trips). Prim targets pass through as-is.
        const std::string& p = paths_[idx];
        if (!p.empty() && p[0] == '.') {
          std::string body = p.substr(1);          // "/a/b/prop"
          size_t slash = body.rfind('/');
          if (slash != std::string::npos) {
            out.push_back(body.substr(0, slash) + "." + body.substr(slash + 1));
          } else {
            out.push_back(body);
          }
        } else {
          out.push_back(p);                        // prim path "/a/b"
        }
      }
    }
    return true;
  };

  if (tid == CrateTypeId::PathVector) {
    return read_run();
  }

  // PathListOp: [u8 header][present sublists...]. ListOpHeader bits (pxrUSD):
  //   0x02 explicit, 0x04 added, 0x08 deleted, 0x10 ordered,
  //   0x20 prepended, 0x40 appended. Read order matches the legacy reader:
  //   explicit, added, prepended, appended, deleted, ordered.
  uint8_t bits = 0;
  if (!reader_->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  if ((bits & kHasExplicit) && !read_run()) return false;
  if ((bits & kHasAdded) && !read_run()) return false;
  if ((bits & kHasPrepended) && !read_run()) return false;
  if ((bits & kHasAppended) && !read_run()) return false;
  if ((bits & kHasDeleted) && !read_run()) return false;
  if ((bits & kHasOrdered) && !read_run()) return false;
  return true;
}

bool CrateReader::Impl::DecodeReferenceListOp(ValueRep rep, bool is_payload,
                                              std::vector<std::string>& out) {
  const CrateTypeId tid = rep.type_id();
  if (tid != CrateTypeId::ReferenceListOp && tid != CrateTypeId::PayloadListOp) {
    return false;
  }
  if (rep.payload() == 0) return true;  // empty listop
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  // Payload items carry a LayerOffset only from crate 0.8.0 on.
  const bool payload_has_offset =
      !is_payload || (version_.minor >= 8);

  // Read one SdfReference/SdfPayload item; when `keep`, append its arc string.
  auto read_item = [&](bool keep) -> bool {
    uint32_t asset_idx = 0, path_idx = 0;
    if (!reader_->read_u32(asset_idx) || !reader_->read_u32(path_idx)) {
      return false;
    }
    double offset = 0.0, scale = 1.0;
    if (payload_has_offset) {
      if (!reader_->read_f64(offset) || !reader_->read_f64(scale)) {
        return false;
      }
    }
    if (!is_payload) {
      // customData dict: u64 count, then per-entry recursive offsets. UE/pxr
      // references rarely author it; decoding requires recursive value reads,
      // so bail (drop the whole listop with a warning) when non-empty.
      uint64_t dict_count = 0;
      if (!reader_->read_u64(dict_count)) return false;
      if (dict_count != 0) {
        AddWarning("Reference customData is not supported; arc dropped");
        return false;
      }
    }
    if (!keep) return true;

    std::string asset;
    GetString(asset_idx, asset);
    std::string prim = (path_idx < paths_.size()) ? paths_[path_idx] : "";
    std::string arc = "@" + asset + "@";
    if (!prim.empty() && prim != "/") arc += "<" + prim + ">";
    if (offset != 0.0 || scale != 1.0) {
      arc += "?layerOffset=" + std::to_string(offset) + ":" +
             std::to_string(scale);
    }
    out.push_back(std::move(arc));
    return true;
  };

  auto read_run = [&](bool keep) -> bool {
    uint64_t n = 0;
    if (!reader_->read_u64(n)) return false;
    if (n > options_.max_array_elements) return false;
    for (uint64_t i = 0; i < n; ++i) {
      if (!read_item(keep)) return false;
    }
    return true;
  };

  // ListOpHeader bits / read order match DecodePathTargets.
  uint8_t bits = 0;
  if (!reader_->read_u8(bits)) return false;
  const uint8_t kHasExplicit = 0x02, kHasAdded = 0x04, kHasDeleted = 0x08,
                kHasOrdered = 0x10, kHasPrepended = 0x20, kHasAppended = 0x40;
  if ((bits & kHasExplicit) && !read_run(true)) return false;
  if ((bits & kHasAdded) && !read_run(true)) return false;
  if ((bits & kHasPrepended) && !read_run(true)) return false;
  if ((bits & kHasAppended) && !read_run(true)) return false;
  if ((bits & kHasDeleted) && !read_run(false)) return false;
  if ((bits & kHasOrdered) && !read_run(false)) return false;
  return true;
}

bool CrateReader::Impl::DecodeVariantSelectionMap(
    ValueRep rep, std::vector<std::pair<std::string, std::string>>& out) {
  if (rep.type_id() != CrateTypeId::VariantSelectionMap) return false;
  if (rep.payload() == 0) return true;  // empty map
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t count = 0;
  if (!reader_->read_u64(count)) return false;
  if (count > options_.max_array_elements) return false;
  for (uint64_t i = 0; i < count; ++i) {
    uint32_t k = 0, v = 0;
    if (!reader_->read_u32(k) || !reader_->read_u32(v)) return false;
    std::string key, val;
    GetString(k, key);
    GetString(v, val);
    out.emplace_back(std::move(key), std::move(val));
  }
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

CrateReadResult CrateReader::ReadOwned(std::string&& owned) {
  return impl_->ReadOwned(std::move(owned));
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

const std::vector<uint32_t>& CrateReader::fieldset_indices() const {
  return impl_->fieldset_indices();
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
