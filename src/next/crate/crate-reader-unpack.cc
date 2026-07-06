// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader scalar/value unpackers

#include "crate-reader-internal.hh"
#include "lazy-array.hh"

#include <cstdint>
#include <cstring>
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t v;
  if (!reader()->read_i32(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackUInt(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(static_cast<uint32_t>(rep.payload()));
    return true;
  }
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint32_t v;
  if (!reader()->read_u32(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackInt64(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(static_cast<int64_t>(rep.payload_as_offset()));
    return true;
  }
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int64_t v;
  if (!reader()->read_i64(v)) return false;
  out = Value(v);
  return true;
}

bool CrateReader::Impl::UnpackUInt64(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    out = Value(static_cast<uint64_t>(rep.payload()));
    return true;
  }
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint64_t v;
  if (!reader()->read_u64(v)) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float v;
  if (!reader()->read_f32(v)) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double v;
  if (!reader()->read_f64(v)) return false;
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
  if (!reader()->seek(static_cast<size_t>(header_offset))) return false;

  // Skip fwd1(8) + times_rep(8) + fwd2(8) to reach the sample count.
  if (!reader()->skip(24)) return false;

  uint64_t num_samples;
  if (!reader()->read_u64(num_samples)) return false;
  if (num_samples > 1000000) {
    AddWarning("Too many time samples");
    return false;
  }

  // Anonymous TimeSamples carry no property name at this level. Per-property
  // fields are decoded by BuildStage() through DecodeTimeSamples(); this generic
  // value-unpack fallback only validates and drops unattachable data.
  return false;
}

bool CrateReader::Impl::DecodeTimeSamples(ValueRep rep,
                                          DecodedTimeSamples* out) {
  out->times.clear();
  out->lazy = LazyTimeSamplesRef{};
  out->eager.clear();

  if (rep.is_inlined()) return false;  // inlined TimeSamples not produced

  // pxr crate TimeSamples are DOUBLY recursive (crateFile.cpp RecursiveRead):
  //   at payload P:        [i64 off_t]                 -> times block at P+off_t
  //   at P+off_t:          [ValueRep times][i64 off_v] -> values block follows
  //   at (P+off_t+8)+off_v:[u64 N][ValueRep vals[N]]
  // Each `[i64 off]` is a relative jump: after reading the 8-byte offset at
  // position Q, the target is Q+off (i.e. seek_from_current(off-8)).
  const int64_t P = static_cast<int64_t>(rep.payload());
  if (P < 0 || !reader()->seek(static_cast<size_t>(P))) return false;

  int64_t off_t = 0;
  if (!reader()->read(&off_t, 8)) return false;
  const int64_t times_pos = P + off_t;  // Q=P, target=Q+off_t
  if (times_pos < 0 || !reader()->seek(static_cast<size_t>(times_pos))) return false;
  uint64_t times_rep_raw = 0;
  if (!reader()->read_u64(times_rep_raw)) return false;
  const int64_t off_v_field = times_pos + 8;  // the values' recursive-offset field

  Value times_val;
  if (!UnpackValue(ValueRep(times_rep_raw), times_val)) {
    return false;
  }
  const std::vector<double>* times = times_val.as_double_array();
  if (!times) {
    return false;
  }

  // Values block: follow the second recursive offset. Read each ValueRep and decode
  // only the number of pairs that are actually needed; if the file carries
  // extra sample entries (or fewer times), still consume all sample records.
  if (!reader()->seek(static_cast<size_t>(off_v_field))) return false;
  int64_t off_v = 0;
  if (!reader()->read(&off_v, 8)) {
    AddWarning("TimeSamples: failed reading value block offset");
    return false;
  }
  const int64_t vals_pos = off_v_field + off_v;  // Q=off_v_field, target=Q+off_v
  if (vals_pos < 0 || !reader()->seek(static_cast<size_t>(vals_pos))) return false;
  uint64_t n = 0;
  if (!reader()->read_u64(n)) {
    AddWarning("TimeSamples: failed reading sample count");
    return false;
  }
  if (n > options_.max_array_elements || n > 100000000ull) return false;

  const size_t sample_count = static_cast<size_t>(n);
  const size_t pair_count = std::min<size_t>(times->size(), sample_count);
  if (sample_count != pair_count) {
    AddWarning("TimeSamples sample count mismatch");
  }

  const size_t values_list_end = static_cast<size_t>(vals_pos) + 8 + sample_count * 8;
  if (values_list_end < static_cast<size_t>(vals_pos)) {
    AddWarning("TimeSamples: sample list offset overflow");
    return false;
  }
  if (values_list_end > reader()->size()) {
    return false;
  }

  auto& sample_raws = array_scratch().u64_values;
  sample_raws.clear();
  if (sample_raws.capacity() < pair_count) {
    sample_raws.reserve(pair_count);
  }

  // Read all sample ValueReps we'll decode first, so unpacking those values cannot
  // disturb the shared stream cursor.
  for (size_t i = 0; i < pair_count; ++i) {
    uint64_t raw = 0;
    if (!reader()->read_u64(raw)) return false;
    sample_raws.push_back(raw);
  }

  if (sample_count > pair_count) {
    if (!reader()->skip((sample_count - pair_count) * 8)) return false;
  }

  // Lazy path: when every sample rep is a gate-approved scalar, keep the
  // values as an undecoded reference into the retained source and hand back
  // only the (small) times keys — materializing 24-B Values (plus boxed
  // vec/matrix payloads) per sample is the dominant peak-RSS cost on
  // animation-dense scenes. Any ineligible rep falls back to the eager
  // decode below for the whole property.
  if (options_.lazy_arrays && source_ && pair_count > 0) {
    bool all_eligible = true;
    for (size_t i = 0; i < pair_count; ++i) {
      if (!IsLazyEligibleTimeSampleRep(*source_, ValueRep(sample_raws[i]))) {
        all_eligible = false;
        break;
      }
    }
    if (all_eligible) {
      out->times.assign(times->begin(), times->begin() + pair_count);
      out->lazy.source = source_;
      out->lazy.vals_pos = static_cast<uint64_t>(vals_pos);
      out->lazy.count = pair_count;
      return reader()->seek(values_list_end);
    }
  }

  out->eager.reserve(pair_count);
  for (size_t i = 0; i < pair_count; ++i) {
    Value v;
    const uint64_t raw = sample_raws[i];
    if (!UnpackValue(ValueRep(raw), v)) {
      return false;
    }
    out->eager.emplace_back((*times)[i], std::move(v));
  }
  if (!reader()->seek(values_list_end)) return false;
  return true;
}

bool CrateReader::Impl::UnpackToken(ValueRep rep, Value& out) {
  std::string_view s;
  if (!GetToken(static_cast<uint32_t>(rep.payload()), &s)) return false;
  out = Value::MakeToken(s);
  return true;
}

bool CrateReader::Impl::UnpackString(ValueRep rep, Value& out) {
  std::string_view s;
  if (!GetString(static_cast<uint32_t>(rep.payload()), &s)) return false;
  out = Value(s);
  return true;
}

bool CrateReader::Impl::UnpackAssetPath(ValueRep rep, Value& out) {
  std::string_view s;
  if (!GetToken(static_cast<uint32_t>(rep.payload()), &s)) return false;
  out = Value::MakeAssetPath(s);
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[2];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[3];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[4];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[2];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[3];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[4];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[9];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[16];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader()->read(data, sizeof(data))) return false;
  out = Value::MakeMatrix2d(data);
  return true;
}

bool CrateReader::Impl::UnpackSpecifier(ValueRep rep, Value& out) {
  uint32_t spec = static_cast<uint32_t>(rep.payload());
  const char* names[] = {"def", "over", "class"};
  if (spec < 3) {
    out = Value::MakeToken(std::string_view(names[spec]));
  } else {
    out = Value::MakeToken(std::string_view("def"));
  }
  return true;
}

bool CrateReader::Impl::UnpackVariability(ValueRep rep, Value& out) {
  uint32_t var = static_cast<uint32_t>(rep.payload());
  out = Value::MakeToken(std::string_view(var == 0 ? "varying" : "uniform"));
  return true;
}

// ============================================================
// Integer vector unpackers
// ============================================================

bool CrateReader::Impl::UnpackVec2i(ValueRep rep, Value& out) {
  // 8 bytes — never inlinable (inline payload is 6 bytes).
  if (rep.is_inlined()) return false;
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t data[2];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t data[3];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  int32_t data[4];
  if (!reader()->read(data, sizeof(data))) return false;
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
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint16_t h;
  uint8_t hb[2];
  if (!reader()->read(hb, 2)) return false;
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
    if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader()->read(raw, sizeof(raw))) return false;
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
    if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader()->read(raw, sizeof(raw))) return false;
  }
  out = Value::MakeFloat3(half_to_float(raw[0]), half_to_float(raw[1]), half_to_float(raw[2]));
  return true;
}

bool CrateReader::Impl::UnpackVec4h(ValueRep rep, Value& out) {
  // 8 bytes — never inlinable (inline payload is 6 bytes).
  if (rep.is_inlined()) return false;
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint16_t raw[4];
  if (!reader()->read(raw, sizeof(raw))) return false;
  out = Value::MakeFloat4(half_to_float(raw[0]), half_to_float(raw[1]),
                           half_to_float(raw[2]), half_to_float(raw[3]));
  return true;
}

bool CrateReader::Impl::UnpackQuath(ValueRep rep, Value& out) {
  // 8 bytes — never inlinable (inline payload is 6 bytes).
  if (rep.is_inlined()) return false;
  if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint16_t raw[4];
  if (!reader()->read(raw, sizeof(raw))) return false;
  out = Value::MakeQuatf(half_to_float(raw[0]), half_to_float(raw[1]),
                          half_to_float(raw[2]), half_to_float(raw[3]));
  return true;
}

// ============================================================
// Deferred (lazy) time-sample scalar decode
//
// Free functions over a retained CrateDataSource, mirroring the Unpack*
// scalar members above so a time-sample value can be decoded on demand long
// after the reader is gone. The eligibility gate and the decoder MUST stay in
// sync: a rep the gate accepts is a rep the decoder cannot fail on (beyond
// the bounds checks the gate already performed).
// ============================================================

namespace {

// Non-inlined fixed-size payload length per eligible scalar type; 0 when the
// type has no eligible non-inlined form (Bool is inline-only in crate files).
size_t EligibleScalarPodSize(CrateTypeId t) {
  switch (t) {
    case CrateTypeId::Int:
    case CrateTypeId::UInt:
    case CrateTypeId::UChar:  // decoded via the UInt path, 4-byte read
    case CrateTypeId::Float:
      return 4;
    case CrateTypeId::Int64:
    case CrateTypeId::UInt64:
    case CrateTypeId::Double:
    case CrateTypeId::TimeCode:
      return 8;
    case CrateTypeId::Half: return 2;
    case CrateTypeId::Vec2h: return 4;
    case CrateTypeId::Vec3h: return 6;
    case CrateTypeId::Vec4h: return 8;
    case CrateTypeId::Quath: return 8;
    case CrateTypeId::Vec2f: return 8;
    case CrateTypeId::Vec3f: return 12;
    case CrateTypeId::Vec4f: return 16;
    case CrateTypeId::Vec2d: return 16;
    case CrateTypeId::Vec3d: return 24;
    case CrateTypeId::Vec4d: return 32;
    case CrateTypeId::Vec2i: return 8;
    case CrateTypeId::Vec3i: return 12;
    case CrateTypeId::Vec4i: return 16;
    case CrateTypeId::Quatf: return 16;
    case CrateTypeId::Quatd: return 32;
    case CrateTypeId::Matrix2d: return 32;
    case CrateTypeId::Matrix3d: return 72;
    case CrateTypeId::Matrix4d: return 128;
    default: return 0;
  }
}

// Whether the eager Unpack* member decodes the inlined form of `t` (types
// whose inlined form is rejected there — Vec2i/Vec4h/Quath — must be
// rejected here too so the lazy path never accepts a rep eager would drop).
bool EligibleInlinedScalar(CrateTypeId t) {
  switch (t) {
    case CrateTypeId::Bool:
    case CrateTypeId::Int:
    case CrateTypeId::UInt:
    case CrateTypeId::UChar:
    case CrateTypeId::Int64:
    case CrateTypeId::UInt64:
    case CrateTypeId::Float:
    case CrateTypeId::Double:
    case CrateTypeId::TimeCode:
    case CrateTypeId::Half:
    case CrateTypeId::Vec2h:
    case CrateTypeId::Vec3h:
    case CrateTypeId::Vec2f:
    case CrateTypeId::Vec3f:
    case CrateTypeId::Vec4f:
    case CrateTypeId::Vec2d:
    case CrateTypeId::Vec3d:
    case CrateTypeId::Vec4d:
    case CrateTypeId::Vec3i:
    case CrateTypeId::Vec4i:
    case CrateTypeId::Quatf:
    case CrateTypeId::Quatd:
    case CrateTypeId::Matrix2d:
    case CrateTypeId::Matrix3d:
    case CrateTypeId::Matrix4d:
      return true;
    default:
      return false;
  }
}

// Copy a non-inlined fixed-size payload out of the retained buffer.
bool ReadScalarPayload(const CrateDataSource& src, ValueRep rep, void* dst,
                       size_t len) {
  const int64_t off = rep.payload_as_offset();
  if (off < 0) return false;
  const uint64_t o = static_cast<uint64_t>(off);
  if (o > src.size() || src.size() - o < len) return false;
  std::memcpy(dst, src.base() + o, len);
  return true;
}

// Unpack the int8-component inlined vector/matrix payload (pxrUSD packs
// integer-valued components as signed bytes in the low payload bits).
void InlinedInt8Components(ValueRep rep, int8_t b[8]) {
  const uint64_t p = rep.payload();
  for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
}

}  // namespace

bool IsLazyEligibleTimeSampleRep(const CrateDataSource& source, ValueRep rep) {
  if (rep.is_array() || rep.is_compressed()) return false;
  const CrateTypeId t = rep.type_id();
  if (t == CrateTypeId::ValueBlock) return true;  // decodes to MakeBlock()
  if (rep.is_inlined()) return EligibleInlinedScalar(t);
  const size_t len = EligibleScalarPodSize(t);
  if (len == 0) return false;
  const int64_t off = rep.payload_as_offset();
  if (off < 0) return false;
  const uint64_t o = static_cast<uint64_t>(off);
  return o <= source.size() && source.size() - o >= len;
}

bool ReadLazyTimeSampleRep(const LazyTimeSamplesRef& ref, uint64_t index,
                           ValueRep* out) {
  if (!ref.source || index >= ref.count) return false;
  const uint64_t pos = ref.vals_pos + 8 + index * 8;
  const size_t size = ref.source->size();
  if (pos > size || size - pos < 8) return false;
  uint64_t raw;
  std::memcpy(&raw, ref.source->base() + pos, 8);
  *out = ValueRep(raw);
  return true;
}

bool DecodeCrateScalarValue(const CrateDataSource& source, ValueRep rep,
                            Value* out) {
  if (rep.is_array() || rep.is_compressed()) return false;
  const CrateTypeId t = rep.type_id();

  if (t == CrateTypeId::ValueBlock) {
    *out = Value::MakeBlock();
    return true;
  }

  if (rep.is_inlined()) {
    if (!EligibleInlinedScalar(t)) return false;
    switch (t) {
      case CrateTypeId::Bool:
        *out = Value(rep.payload() != 0);
        return true;
      case CrateTypeId::Int:
        *out = Value(static_cast<int32_t>(rep.payload()));
        return true;
      case CrateTypeId::UInt:
      case CrateTypeId::UChar:
        *out = Value(static_cast<uint32_t>(rep.payload()));
        return true;
      case CrateTypeId::Int64:
        *out = Value(static_cast<int64_t>(rep.payload_as_offset()));
        return true;
      case CrateTypeId::UInt64:
        *out = Value(static_cast<uint64_t>(rep.payload()));
        return true;
      case CrateTypeId::Float: {
        const uint32_t bits = static_cast<uint32_t>(rep.payload());
        float v;
        std::memcpy(&v, &bits, sizeof(float));
        *out = Value(v);
        return true;
      }
      case CrateTypeId::Double:
      case CrateTypeId::TimeCode: {
        // pxrUSD inlines a double as a 32-bit float in the low payload bits.
        const uint32_t bits = static_cast<uint32_t>(rep.payload() & 0xFFFFFFFFu);
        float f;
        std::memcpy(&f, &bits, sizeof(float));
        *out = Value(static_cast<double>(f));
        return true;
      }
      case CrateTypeId::Half:
        *out = Value(half_to_float(static_cast<uint16_t>(rep.payload() & 0xFFFF)));
        return true;
      case CrateTypeId::Vec2h: {
        const uint64_t p = rep.payload();
        *out = Value::MakeFloat2(
            half_to_float(static_cast<uint16_t>(p & 0xFFFF)),
            half_to_float(static_cast<uint16_t>((p >> 16) & 0xFFFF)));
        return true;
      }
      case CrateTypeId::Vec3h: {
        const uint64_t p = rep.payload();
        *out = Value::MakeFloat3(
            half_to_float(static_cast<uint16_t>(p & 0xFFFF)),
            half_to_float(static_cast<uint16_t>((p >> 16) & 0xFFFF)),
            half_to_float(static_cast<uint16_t>((p >> 32) & 0xFFFF)));
        return true;
      }
      default: {
        int8_t b[8];
        InlinedInt8Components(rep, b);
        switch (t) {
          case CrateTypeId::Vec2f:
            *out = Value::MakeFloat2(float(b[0]), float(b[1]));
            return true;
          case CrateTypeId::Vec3f:
            *out = Value::MakeFloat3(float(b[0]), float(b[1]), float(b[2]));
            return true;
          case CrateTypeId::Vec4f:
            *out = Value::MakeFloat4(float(b[0]), float(b[1]), float(b[2]),
                                     float(b[3]));
            return true;
          case CrateTypeId::Vec2d:
            *out = Value::MakeDouble2(double(b[0]), double(b[1]));
            return true;
          case CrateTypeId::Vec3d:
            *out = Value::MakeDouble3(double(b[0]), double(b[1]), double(b[2]));
            return true;
          case CrateTypeId::Vec4d:
            *out = Value::MakeDouble4(double(b[0]), double(b[1]), double(b[2]),
                                      double(b[3]));
            return true;
          case CrateTypeId::Vec3i:
            *out = Value::MakeInt3(int32_t(b[0]), int32_t(b[1]), int32_t(b[2]));
            return true;
          case CrateTypeId::Vec4i:
            *out = Value::MakeInt4(int32_t(b[0]), int32_t(b[1]), int32_t(b[2]),
                                   int32_t(b[3]));
            return true;
          case CrateTypeId::Quatf:
            *out = Value::MakeQuatf(float(b[0]), float(b[1]), float(b[2]),
                                    float(b[3]));
            return true;
          case CrateTypeId::Quatd:
            *out = Value::MakeQuatd(double(b[0]), double(b[1]), double(b[2]),
                                    double(b[3]));
            return true;
          case CrateTypeId::Matrix2d: {
            double m[4] = {0.0};
            m[0] = double(b[0]); m[1] = double(b[1]);
            m[2] = double(b[2]); m[3] = double(b[3]);
            *out = Value::MakeMatrix2d(m);
            return true;
          }
          case CrateTypeId::Matrix3d: {
            double m[9] = {0};
            m[0] = b[0]; m[4] = b[1]; m[8] = b[2];
            *out = Value::MakeMatrix3d(m);
            return true;
          }
          case CrateTypeId::Matrix4d: {
            double m[16] = {0};
            m[0] = b[0]; m[5] = b[1]; m[10] = b[2]; m[15] = b[3];
            *out = Value::MakeMatrix4d(m);
            return true;
          }
          default:
            return false;
        }
      }
    }
  }

  const size_t len = EligibleScalarPodSize(t);
  if (len == 0) return false;
  // 128 B covers the largest eligible scalar (Matrix4d).
  uint8_t buf[128];
  if (!ReadScalarPayload(source, rep, buf, len)) return false;

  switch (t) {
    case CrateTypeId::Int: {
      int32_t v; std::memcpy(&v, buf, 4); *out = Value(v); return true;
    }
    case CrateTypeId::UInt:
    case CrateTypeId::UChar: {
      uint32_t v; std::memcpy(&v, buf, 4); *out = Value(v); return true;
    }
    case CrateTypeId::Int64: {
      int64_t v; std::memcpy(&v, buf, 8); *out = Value(v); return true;
    }
    case CrateTypeId::UInt64: {
      uint64_t v; std::memcpy(&v, buf, 8); *out = Value(v); return true;
    }
    case CrateTypeId::Float: {
      float v; std::memcpy(&v, buf, 4); *out = Value(v); return true;
    }
    case CrateTypeId::Double:
    case CrateTypeId::TimeCode: {
      double v; std::memcpy(&v, buf, 8); *out = Value(v); return true;
    }
    case CrateTypeId::Half: {
      const uint16_t h = static_cast<uint16_t>(buf[0]) |
                         (static_cast<uint16_t>(buf[1]) << 8);
      *out = Value(half_to_float(h));
      return true;
    }
    case CrateTypeId::Vec2h: {
      uint16_t raw[2]; std::memcpy(raw, buf, sizeof(raw));
      *out = Value::MakeFloat2(half_to_float(raw[0]), half_to_float(raw[1]));
      return true;
    }
    case CrateTypeId::Vec3h: {
      uint16_t raw[3]; std::memcpy(raw, buf, sizeof(raw));
      *out = Value::MakeFloat3(half_to_float(raw[0]), half_to_float(raw[1]),
                               half_to_float(raw[2]));
      return true;
    }
    case CrateTypeId::Vec4h: {
      uint16_t raw[4]; std::memcpy(raw, buf, sizeof(raw));
      *out = Value::MakeFloat4(half_to_float(raw[0]), half_to_float(raw[1]),
                               half_to_float(raw[2]), half_to_float(raw[3]));
      return true;
    }
    case CrateTypeId::Quath: {
      uint16_t raw[4]; std::memcpy(raw, buf, sizeof(raw));
      *out = Value::MakeQuatf(half_to_float(raw[0]), half_to_float(raw[1]),
                              half_to_float(raw[2]), half_to_float(raw[3]));
      return true;
    }
    case CrateTypeId::Vec2f: {
      float d[2]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeFloat2(d[0], d[1]);
      return true;
    }
    case CrateTypeId::Vec3f: {
      float d[3]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeFloat3(d[0], d[1], d[2]);
      return true;
    }
    case CrateTypeId::Vec4f: {
      float d[4]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeFloat4(d[0], d[1], d[2], d[3]);
      return true;
    }
    case CrateTypeId::Vec2d: {
      double d[2]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeDouble2(d[0], d[1]);
      return true;
    }
    case CrateTypeId::Vec3d: {
      double d[3]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeDouble3(d[0], d[1], d[2]);
      return true;
    }
    case CrateTypeId::Vec4d: {
      double d[4]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeDouble4(d[0], d[1], d[2], d[3]);
      return true;
    }
    case CrateTypeId::Vec2i: {
      int32_t d[2]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeInt2(d[0], d[1]);
      return true;
    }
    case CrateTypeId::Vec3i: {
      int32_t d[3]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeInt3(d[0], d[1], d[2]);
      return true;
    }
    case CrateTypeId::Vec4i: {
      int32_t d[4]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeInt4(d[0], d[1], d[2], d[3]);
      return true;
    }
    case CrateTypeId::Quatf: {
      float d[4]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeQuatf(d[0], d[1], d[2], d[3]);
      return true;
    }
    case CrateTypeId::Quatd: {
      double d[4]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeQuatd(d[0], d[1], d[2], d[3]);
      return true;
    }
    case CrateTypeId::Matrix2d: {
      double d[4]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeMatrix2d(d);
      return true;
    }
    case CrateTypeId::Matrix3d: {
      double d[9]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeMatrix3d(d);
      return true;
    }
    case CrateTypeId::Matrix4d: {
      double d[16]; std::memcpy(d, buf, sizeof(d));
      *out = Value::MakeMatrix4d(d);
      return true;
    }
    default:
      return false;
  }
}

}  // namespace next
}  // namespace tinyusdz
