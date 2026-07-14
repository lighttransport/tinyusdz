// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader scalar/value unpackers

#include "crate-reader-internal.hh"
#include "../writer/value-printer.hh"
#include "../types/spline.hh"

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
    // pxr inlines int64 as an exact int32 in the low payload bits: a
    // negative value has all-ones low 32 bits with ZERO high bits, so
    // sign-extending from bit 47 (payload_as_offset) would decode -1 as
    // +4294967295. Reinterpret the low 32 bits as int32 when the high
    // payload bits are clear; keep the old-TinyUSDZ 48-bit sign extension
    // only when they are set.
    const uint64_t payload48 = rep.payload();
    int64_t value = 0;
    if ((payload48 >> 32) == 0) {
      int32_t low32 = 0;
      const uint32_t bits = static_cast<uint32_t>(payload48);
      std::memcpy(&low32, &bits, sizeof(low32));
      value = static_cast<int64_t>(low32);
    } else {
      value = rep.payload_as_offset();  // 48-bit sign extension
    }
    out = Value(value);
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
    // pxr never emits an inlined TimeSamples rep (crateFile.cpp packs
    // TimeSamples exclusively through the recursive-offset block form; there
    // is no _EncodeInline for it). An inlined rep here is malformed — fail
    // instead of fabricating a value from the payload bits (this branch used
    // to conjure an int2 out of garbage).
    return false;
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
  // Each sample ValueRep is an 8-byte read that immediately follows; require the
  // file to actually hold n*8 bytes before allocating the vector, so a tiny file
  // cannot demand an ~800 MB allocation via an absurd count (has_elements uses
  // division, so no count*8 overflow).
  if (n > 0 && !reader_->has_elements(static_cast<size_t>(n), 8)) return false;

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

bool CrateReader::Impl::DecodeSplineToText(ValueRep rep, std::string* out) {
  if (!out) return false;
  if (rep.is_inlined()) return false;  // splines are heap-stored
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;

  uint64_t blob_size = 0;
  if (!reader_->read_u64(blob_size)) return false;
  // The blob is bounded by the file; guard the allocation against a bogus size.
  if (blob_size > options_.max_array_elements || blob_size > 100000000ull)
    return false;
  if (blob_size > 0 && !reader_->has_elements(static_cast<size_t>(blob_size), 1))
    return false;
  std::vector<uint8_t> blob(static_cast<size_t>(blob_size));
  if (blob_size > 0 && !reader_->read(blob.data(), static_cast<size_t>(blob_size)))
    return false;

  SplineData sd;
  std::string serr;
  if (!DecodeSplineBinary(blob.data(), blob.size(), &sd, &serr)) return false;

  // Per-knot customData is an unordered_map<double, VtDictionary> written
  // directly after the spline blob.
  uint64_t custom_count = 0;
  if (!reader_->read_u64(custom_count) ||
      custom_count > options_.max_array_elements) {
    return false;
  }
  for (uint64_t ci = 0; ci < custom_count; ++ci) {
    double knot_time = 0.0;
    uint64_t dict_count = 0;
    if (!reader_->read_f64(knot_time) || !reader_->read_u64(dict_count) ||
        dict_count > options_.max_array_elements) {
      return false;
    }
    Value dict_value = Value::MakeDictionary();
    Dict* dict = dict_value.as_dictionary();
    for (uint64_t di = 0; di < dict_count; ++di) {
      uint32_t key_index = 0;
      if (!reader_->read_u32(key_index)) return false;
      std::string key;
      GetString(key_index, key);
      const size_t value_start = reader_->position();
      uint64_t recursive_offset_raw = 0;
      if (!reader_->read_u64(recursive_offset_raw)) return false;
      const size_t rep_position = static_cast<size_t>(
          static_cast<int64_t>(value_start) +
          static_cast<int64_t>(recursive_offset_raw));
      if (!reader_->seek(rep_position)) return false;
      uint64_t raw = 0;
      if (!reader_->read_u64(raw)) return false;
      const size_t next_entry = reader_->position();
      ValueRep value_rep(raw);
      Value value;
      if (value_rep.type_id() == CrateTypeId::Dictionary) {
        if (!DecodeDictionary(value_rep, value, 1)) return false;
      } else if (value_rep.type_id() != CrateTypeId::Invalid &&
                 !UnpackValue(value_rep, value)) {
        return false;
      }
      dict->set(std::move(key), std::move(value));
      if (!reader_->seek(next_entry)) return false;
    }
    for (SplineKnot& knot : sd.knots) {
      if (knot.time == knot_time) {
        knot.custom_data_source = PrintValue(dict_value);
        break;
      }
    }
  }
  *out = FormatSplineText(sd, "");
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
    // Reconstruct as Float3 — the rep type is Vec3f; producing Half3 here
    // broke every as_float3() consumer (identity xformOp:scale/rotateXYZ).
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
    // Reconstruct as Float4 (rep type is Vec4f), not Half4 — see UnpackVec3f.
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
  // Crate / GfQuat layout is imaginary-first (x, y, z, w); internal layout is
  // real-first (w, x, y, z). Swizzle on read (the writer swizzles back).
  if (rep.is_inlined()) {
    // pxr never inlines quaternions: crateValueInliners.h has _EncodeInline
    // overloads only for scalars, GfVec (int8 lanes), GfMatrix (int8 diag)
    // and VtDictionary — no GfQuat overload — and sizeof(GfQuat*) > 4 rules
    // out the always-inlined path. Fail instead of fabricating a quat from
    // garbage payload bits.
    return false;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  float data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeQuatf(data[3], data[0], data[1], data[2]);
  return true;
}

bool CrateReader::Impl::UnpackQuatd(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxr never inlines quaternions (see UnpackQuatf) — fail instead of
    // fabricating a value.
    return false;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  double data[4];
  if (!reader_->read(data, sizeof(data))) return false;
  out = Value::MakeQuatd(data[3], data[0], data[1], data[2]);
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
    // Diagonal int8 encoding, matching Matrix3d/Matrix4d inline forms.
    double m[4] = {0.0};
    m[0] = double(b[0]);
    m[3] = double(b[1]);
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

bool CrateReader::Impl::UnpackPermission(ValueRep rep, Value& out) {
  // SdfPermission enum: 0 = public, 1 = private.
  out = Value::MakeToken(rep.payload() == 0 ? "public" : "private");
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
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued vectors as int8 components.
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    out = Value::MakeInt2(int32_t(b[0]), int32_t(b[1]));
    return true;
  }
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

bool CrateReader::Impl::UnpackHalf(ValueRep rep, Value& out) {
  // Preserve the half domain: the usda parser stores scalar halves as raw
  // half bits under TypeId::Half, so the crate reader must match for value
  // equality across formats.
  uint16_t h;
  if (rep.is_inlined()) {
    h = static_cast<uint16_t>(rep.payload() & 0xFFFF);
  } else {
    if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    uint8_t hb[2];
    if (!reader_->read(hb, 2)) return false;
    h = static_cast<uint16_t>(hb[0]) | (static_cast<uint16_t>(hb[1]) << 8);
  }
  out = Value::MakeFromRaw(TypeId::Half, &h);
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
  out = Value::MakeFromRaw(TypeId::Half2, raw);
  return true;
}

bool CrateReader::Impl::UnpackVec3h(ValueRep rep, Value& out) {
  uint16_t raw[3];
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued half vectors as int8 components (verified
    // against pxr crates: Vec3h (1,2,3) -> payload 0x030201), not half lanes.
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    // Keep the declared type: emit Half3 (raw half-bit lanes), not Float3 —
    // the same attribute must not change TypeId based on whether pxr
    // happened to inline it.
    for (int i = 0; i < 3; ++i) raw[i] = FloatToHalf(float(b[i]));
    out = Value::MakeFromRaw(TypeId::Half3, raw);
    return true;
  }
  {
    if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader_->read(raw, sizeof(raw))) return false;
  }
  out = Value::MakeFromRaw(TypeId::Half3, raw);
  return true;
}

bool CrateReader::Impl::UnpackVec4h(ValueRep rep, Value& out) {
  if (rep.is_inlined()) {
    // pxrUSD inlines integer-valued half vectors as int8 components.
    uint64_t p = rep.payload();
    int8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<int8_t>((p >> (8 * i)) & 0xFF);
    // Keep the declared type: emit Half4 (raw half-bit lanes), not Float4.
    uint16_t half_bits[4];
    for (int i = 0; i < 4; ++i) half_bits[i] = FloatToHalf(float(b[i]));
    out = Value::MakeFromRaw(TypeId::Half4, half_bits);
    return true;
  }
  if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
  uint16_t raw[4];
  if (!reader_->read(raw, sizeof(raw))) return false;
  out = Value::MakeFromRaw(TypeId::Half4, raw);
  return true;
}

bool CrateReader::Impl::UnpackQuath(ValueRep rep, Value& out) {
  // Imaginary-first on disk; internal is real-first raw half bits (matching
  // the usda parser's Quath SBO layout).
  uint16_t raw[4];
  if (rep.is_inlined()) {
    // pxr never inlines quaternions (see UnpackQuatf) — fail instead of
    // fabricating a value.
    return false;
  }
  {
    if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader_->read(raw, sizeof(raw))) return false;
  }
  const uint16_t wxyz[4] = {raw[3], raw[0], raw[1], raw[2]};
  out = Value::MakeFromRaw(TypeId::Quath, wxyz);
  return true;
}


}  // namespace next
}  // namespace tinyusdz
