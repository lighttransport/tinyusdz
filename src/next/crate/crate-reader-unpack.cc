// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader scalar/value unpackers

#include "crate-reader-internal.hh"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {

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

  // Anonymous TimeSamples carry no property name at this level. Per-property
  // fields are decoded by BuildStage() through DecodeTimeSamples(); this generic
  // value-unpack fallback only validates and drops unattachable data.
  return false;
}

bool CrateReader::Impl::DecodeTimeSamples(
    ValueRep rep, std::vector<std::pair<double, Value>>* out) {
  out->clear();
  if (rep.is_inlined()) return false;  // inlined TimeSamples not produced

  // pxr crate TimeSamples are DOUBLY recursive (crateFile.cpp RecursiveRead):
  //   at payload P:        [i64 off_t]                 -> times block at P+off_t
  //   at P+off_t:          [ValueRep times][i64 off_v] -> values block follows
  //   at (P+off_t+8)+off_v:[u64 N][ValueRep vals[N]]
  // Each `[i64 off]` is a relative jump: after reading the 8-byte offset at
  // position Q, the target is Q+off (i.e. seek_from_current(off-8)).
  const int64_t P = static_cast<int64_t>(rep.payload());
  if (P < 0 || !reader_->seek(static_cast<size_t>(P))) return false;

  int64_t off_t = 0;
  if (!reader_->read(&off_t, 8)) return false;
  const int64_t times_pos = P + off_t;  // Q=P, target=Q+off_t
  if (times_pos < 0 || !reader_->seek(static_cast<size_t>(times_pos))) return false;
  uint64_t times_rep_raw = 0;
  if (!reader_->read_u64(times_rep_raw)) return false;
  const int64_t off_v_field = times_pos + 8;  // the values' recursive-offset field

  Value times_val;
  if (!UnpackValue(ValueRep(times_rep_raw), times_val)) return false;
  const std::vector<double>* times = times_val.as_double_array();
  if (!times) return false;

  // Values block: follow the second recursive offset.
  if (!reader_->seek(static_cast<size_t>(off_v_field))) return false;
  int64_t off_v = 0;
  if (!reader_->read(&off_v, 8)) return false;
  const int64_t vals_pos = off_v_field + off_v;  // Q=off_v_field, target=Q+off_v
  if (vals_pos < 0 || !reader_->seek(static_cast<size_t>(vals_pos))) return false;
  uint64_t n = 0;
  if (!reader_->read_u64(n)) return false;
  if (n > options_.max_array_elements || n > 100000000ull) return false;

  // Read all sample ValueReps before decoding (decoding seeks elsewhere).
  std::vector<ValueRep> sample_reps(static_cast<size_t>(n));
  for (uint64_t i = 0; i < n; ++i) {
    uint64_t raw = 0;
    if (!reader_->read_u64(raw)) return false;
    sample_reps[static_cast<size_t>(i)] = ValueRep(raw);
  }

  const size_t count = std::min<size_t>(times->size(), sample_reps.size());
  out->reserve(count);
  for (size_t i = 0; i < count; ++i) {
    Value v;  // a ValueBlock sample (no authored value at this time) stays empty
    UnpackValue(sample_reps[i], v);
    out->emplace_back((*times)[i], std::move(v));
  }
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

bool CrateReader::Impl::UnpackMatrix2d(ValueRep rep, Value& out) {
  // 2x2 matrix. Inline form uses packed 8-bit payloads; fallback is a direct
  // read of 4 doubles. The inline form preserves legacy pxrUSD encoding for
  // integer-valued matrices.
  if (rep.is_inlined()) {
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    double m[4] = {0.0};
    m[0] = double(b[0]);
    m[1] = double(b[1]);
    m[2] = double(b[2]);
    m[3] = double(b[3]);
    out = Value::MakeMatrix2d(m);
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeMatrix2d(data);
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


}  // namespace next
}  // namespace tinyusdz
