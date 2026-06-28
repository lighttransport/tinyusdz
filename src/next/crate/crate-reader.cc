// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader Implementation

#include "crate-reader-internal.hh"
#include "../strfmt.hh"
#include "crate-data-source.hh"
#include "lazy-array.hh"
#include "stream-reader.hh"
#include "../types/type-info.hh"
#include "../layer/layer.hh"
#include "../parser/lexer.hh"          // dict-as-USDA-text decode
#include "../parser/value-parser.hh"  // ParseDict

#include "safe-arithmetic.hh"

#include <fstream>
#include <unordered_map>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace tinyusdz {
namespace next {

// Decode a dictionary that was written as USDA dict text in a String field
// (see crate-writer add_dict_field). Returns an empty Value on failure.
static Value ParseDictText(const std::string& text) {
  if (text.empty()) return Value();
  Lexer lexer(text.data(), text.size());
  ParseResult r = ParseDict(lexer);
  return r.success ? std::move(r.value) : Value();
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
    case CrateTypeId::Matrix2d: return UnpackMatrix2d(rep, out);
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

    case CrateTypeId::Invalid:     // an empty VtValue (e.g. an empty dict entry)
      out = Value();
      return true;

    case CrateTypeId::ValueBlock:  // a blocked (`= None`) value: a real opinion
      out = Value::MakeBlock();    // that suppresses weaker values and re-emits
      return true;                 // `= None` (not a declared-only attribute).

    // TokenVector / StringVector / DoubleVector: std::vector<T> stored as a
    // non-array value ([u64 count][elements]); an empty vector inlines as
    // payload 0.
    case CrateTypeId::TokenVector:
    case CrateTypeId::StringVector: {
      if (rep.payload() == 0) {
        out = Value::MakeTokenArray(std::vector<std::string>{});
        return true;
      }
      if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
      uint64_t n = 0;
      if (!reader_->read_u64(n)) return false;
      if (n > options_.max_array_elements) return false;
      // The on-disk payload is n uint32_t indices; require them to physically
      // fit in the remaining file before allocating idxs(n)/data(n). Otherwise a
      // malformed count would drive a multi-GB allocation before the read below.
      if (!reader_->has_elements(static_cast<size_t>(n), sizeof(uint32_t))) return false;
      std::vector<uint32_t> idxs(static_cast<size_t>(n));
      size_t bytes;
      if (!safe::mul(static_cast<size_t>(n), sizeof(uint32_t), &bytes)) return false;
      if (n && !reader_->read(idxs.data(), bytes)) return false;
      std::vector<std::string> data(static_cast<size_t>(n));
      for (size_t i = 0; i < n; i++) {
        std::string s;
        // TokenVector indexes the token table; StringVector the string table.
        if (type_id == CrateTypeId::TokenVector) {
          if (idxs[i] >= tokens_.size()) return false;
          s = tokens_.str(idxs[i]);
        } else {
          GetString(idxs[i], s);
        }
        data[i] = std::move(s);
      }
      out = Value::MakeTokenArray(std::move(data));
      return true;
    }

    case CrateTypeId::DoubleVector: {
      if (rep.payload() == 0) {
        out = Value::MakeDoubleArray(std::vector<double>{});
        return true;
      }
      if (!reader_->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
      uint64_t n = 0;
      if (!reader_->read_u64(n)) return false;
      if (n > options_.max_array_elements) return false;
      // n doubles must physically be present in the remaining file before we
      // allocate; bound the count against the file to avoid a malformed-count
      // multi-GB allocation ahead of the read below.
      if (!reader_->has_elements(static_cast<size_t>(n), sizeof(double))) return false;
      std::vector<double> data(static_cast<size_t>(n));
      size_t bytes;
      if (!safe::mul(static_cast<size_t>(n), sizeof(double), &bytes)) return false;
      if (n && !reader_->read(data.data(), bytes)) return false;
      out = Value::MakeDoubleArray(std::move(data));
      return true;
    }

    case CrateTypeId::TokenListOp:
    case CrateTypeId::StringListOp: {
      std::vector<std::string> toks;
      if (!DecodeTokenListOp(rep, toks)) return false;
      out = Value::MakeTokenArray(std::move(toks));
      return true;
    }

    case CrateTypeId::Dictionary:
      return DecodeDictionary(rep, out, 0);

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
    case CrateTypeId::Vec4f:
    case CrateTypeId::Quatf:
    case CrateTypeId::Double:
    case CrateTypeId::Vec2d:
    case CrateTypeId::Vec3d:
    case CrateTypeId::Vec4d:
    case CrateTypeId::Quatd:
    case CrateTypeId::Matrix2d:
    case CrateTypeId::Matrix3d:
    case CrateTypeId::Matrix4d:
    case CrateTypeId::Half:
    case CrateTypeId::Vec2h:
    case CrateTypeId::Vec3h:
    case CrateTypeId::Vec4h:
    case CrateTypeId::Quath:
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

  // Guard the element allocation BEFORE allocating any std::vector(count). The
  // per-type cases below allocate count*elem bytes first and only then call
  // read_raw() (whose has_elements() check would catch an over-long uncompressed
  // run) — so a malformed count (e.g. 1e9 matrix4d = 128 GB) would OOM before
  // that check. Bound it here against the file-size-relative cap. For an
  // uncompressed array the element bytes must physically be present in the file;
  // for a compressed array the file-relative cap still limits the decoded size.
  {
    const uint64_t stride = CrateArrayElemStride(type_id);
    const uint64_t elem_bytes = stride ? stride : 1;
    if (!compressed && stride > 0 && !reader_->has_elements(
            static_cast<size_t>(count), static_cast<size_t>(stride))) {
      AddWarning("Array element count exceeds available file bytes");
      return false;
    }
    if (elem_bytes != 0 &&
        count > (std::numeric_limits<uint64_t>::max)() / elem_bytes) {
      AddWarning("Array payload byte size overflow");
      return false;
    }
    if (!CheckByteAllocation(count * elem_bytes, "Array payload")) {
      return false;
    }
  }

  // Decode a compressed integer array ([u64 compSize][LZ4(delta) blob]) in place.
  auto read_compressed_u32_n = [&](uint32_t* dst, size_t n) -> bool {
    uint64_t comp_size;
    if (!reader_->read_u64(comp_size)) return false;
    std::vector<uint8_t> blob;
    if (!reader_->read(blob, static_cast<size_t>(comp_size))) return false;  // overflow-safe
    std::vector<uint8_t> with_prefix(8 + blob.size());
    std::memcpy(with_prefix.data(), &comp_size, 8);
    if (!blob.empty()) std::memcpy(with_prefix.data() + 8, blob.data(), blob.size());
    DecompressResult dr = DecompressCompressedU32(
        with_prefix.data(), with_prefix.size(), dst, n);
    if (!dr.success) {
      AddWarning("Failed to decompress integer array: " + dr.error);
      return false;
    }
    return true;
  };
  auto read_compressed_u32 = [&](uint32_t* dst) -> bool {
    return read_compressed_u32_n(dst, static_cast<size_t>(count));
  };

  auto read_compressed_u64_n = [&](uint64_t* dst, size_t n) -> bool {
    uint64_t comp_size;
    if (!reader_->read_u64(comp_size)) return false;
    std::vector<uint8_t> blob;
    if (!reader_->read(blob, static_cast<size_t>(comp_size))) return false;
    std::vector<uint8_t> with_prefix(8 + blob.size());
    std::memcpy(with_prefix.data(), &comp_size, 8);
    if (!blob.empty()) std::memcpy(with_prefix.data() + 8, blob.data(), blob.size());
    DecompressResult dr = DecompressCompressedU64(
        with_prefix.data(), with_prefix.size(), dst, n);
    if (!dr.success) {
      AddWarning("Failed to decompress 64-bit integer array: " + dr.error);
      return false;
    }
    return true;
  };
  auto read_compressed_u64 = [&](uint64_t* dst) -> bool {
    return read_compressed_u64_n(dst, static_cast<size_t>(count));
  };

  // Read `count` raw elements of `elem_size` bytes into `dst` (overflow-safe).
  auto read_raw = [&](void* dst, size_t elem_size) -> bool {
    if (count == 0) return true;
    if (!reader_->has_elements(count, elem_size)) return false;
    return reader_->read(dst, static_cast<size_t>(count) * elem_size);
  };

  // Decode a compressed floating-point array (pxrUSD format): [i8 code] then
  // 'i' -> compressed int32s cast to T; 't' -> [u32 lutSize][T lut][compressed
  // u32 indices]. Fills `n` scalar elements of T into `dst`.
  auto read_compressed_floating_n = [&](auto* dst, size_t n) -> bool {
    using T = typename std::remove_pointer<decltype(dst)>::type;
    int8_t code = 0;
    if (!reader_->read_i8(code)) return false;
    if (code == 'i') {
      std::vector<uint32_t> ints(n);
      if (!read_compressed_u32_n(ints.data(), n)) return false;
      for (size_t i = 0; i < n; ++i) {
        dst[i] = static_cast<T>(static_cast<int32_t>(ints[i]));
      }
      return true;
    }
    if (code == 't') {
      uint32_t lut_size = 0;
      if (!reader_->read_u32(lut_size)) return false;
      if (lut_size == 0 || lut_size > options_.max_array_elements) return false;
      // The LUT holds lut_size elements read directly from the file; bound the
      // count against the remaining file before allocating to avoid a
      // malformed-count huge allocation ahead of the read below.
      if (!reader_->has_elements(size_t(lut_size), sizeof(T))) return false;
      std::vector<T> lut(lut_size);
      size_t lut_bytes;
      if (!safe::mul(size_t(lut_size), sizeof(T), &lut_bytes)) return false;
      if (!reader_->read(lut.data(), lut_bytes)) return false;
      std::vector<uint32_t> idxs(n);
      if (!read_compressed_u32_n(idxs.data(), n)) return false;
      for (size_t i = 0; i < n; ++i) {
        if (idxs[i] >= lut_size) return false;
        dst[i] = lut[idxs[i]];
      }
      return true;
    }
    AddWarning("Unknown compressed floating-point array code");
    return false;
  };
  auto read_compressed_floating = [&](auto* dst) -> bool {
    return read_compressed_floating_n(dst, static_cast<size_t>(count));
  };

  // Decode a pxrUSD compressed scalar half array into raw half (uint16) values.
  // Same [i8 code] framing as the floating decoder, but code 'i' reconstructs
  // each value as GfHalf(int) and the 't' lookup table holds 2-byte halfs.
  auto read_compressed_half_n = [&](uint16_t* dst, size_t n) -> bool {
    int8_t code = 0;
    if (!reader_->read_i8(code)) return false;
    if (code == 'i') {
      std::vector<uint32_t> ints(n);
      if (!read_compressed_u32_n(ints.data(), n)) return false;
      for (size_t i = 0; i < n; ++i) {
        dst[i] = FloatToHalf(static_cast<float>(static_cast<int32_t>(ints[i])));
      }
      return true;
    }
    if (code == 't') {
      uint32_t lut_size = 0;
      if (!reader_->read_u32(lut_size)) return false;
      if (lut_size == 0 || lut_size > options_.max_array_elements) return false;
      if (!reader_->has_elements(size_t(lut_size), sizeof(uint16_t))) return false;
      std::vector<uint16_t> lut(lut_size);
      size_t lut_bytes;
      if (!safe::mul(size_t(lut_size), sizeof(uint16_t), &lut_bytes)) return false;
      if (!reader_->read(lut.data(), lut_bytes)) return false;
      std::vector<uint32_t> idxs(n);
      if (!read_compressed_u32_n(idxs.data(), n)) return false;
      for (size_t i = 0; i < n; ++i) {
        if (idxs[i] >= lut_size) return false;
        dst[i] = lut[idxs[i]];
      }
      return true;
    }
    AddWarning("Unknown compressed half-precision array code");
    return false;
  };
  switch (type_id) {
    case CrateTypeId::Float: {
      std::vector<float> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_floating(data.data())) return false;
      } else if (!read_raw(data.data(), sizeof(float))) {
        return false;
      }
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
      size_t data_size;
      if (!safe::mul(static_cast<size_t>(count), size_t(2), &data_size)) {
        return false;
      }
      std::vector<float> data(data_size);
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_floating_n(data.data(), data_size)) return false;
      } else if (!read_raw(data.data(), 2 * sizeof(float))) {
        return false;
      }
      out = Value::MakeFloat2Array(std::move(data));
      return true;
    }
    case CrateTypeId::Vec3f: {
      size_t data_size;
      if (!safe::mul(static_cast<size_t>(count), size_t(3), &data_size)) {
        return false;
      }
      std::vector<float> data(data_size);
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_floating_n(data.data(), data_size)) return false;
      } else if (!read_raw(data.data(), 3 * sizeof(float))) {
        return false;
      }
      out = Value::MakeFloat3Array(std::move(data));
      return true;
    }
    case CrateTypeId::Double: {
      std::vector<double> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_floating(data.data())) return false;
      } else if (!read_raw(data.data(), sizeof(double))) {
        return false;
      }
      out = Value::MakeDoubleArray(std::move(data));
      return true;
    }
    case CrateTypeId::Int64: {
      std::vector<int64_t> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u64(reinterpret_cast<uint64_t*>(data.data()))) return false;
      } else if (!read_raw(data.data(), sizeof(int64_t))) {
        return false;
      }
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
      std::vector<uint64_t> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u64(data.data())) return false;
      } else if (!read_raw(data.data(), sizeof(uint64_t))) {
        return false;
      }
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
      std::vector<uint32_t> idxs(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u32(idxs.data())) return false;
      } else if (!read_raw(idxs.data(), sizeof(uint32_t))) {
        return false;
      }
      std::vector<std::string> data(static_cast<size_t>(count));
      for (size_t i = 0; i < count; i++) {
        if (idxs[i] >= tokens_.size()) return false;
        data[i] = tokens_.str(idxs[i]);
      }
      out = Value::MakeTokenArray(std::move(data));
      return true;
    }
    // Vector / quaternion / matrix arrays stored as flat float or double
    // buffers (uncompressed in crate). Decoded generically via the element
    // stride so each type doesn't need a near-identical case.
    case CrateTypeId::Vec4f:
    case CrateTypeId::Quatf: {
      const uint32_t stride_bytes = CrateArrayElemStride(type_id);  // e.g. 16
      const uint32_t comps = stride_bytes / 4;
      size_t scalars;
      if (!safe::mul(static_cast<size_t>(count), size_t(comps), &scalars)) return false;
      std::vector<float> data(scalars);
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_floating_n(data.data(), scalars)) return false;
      } else if (!read_raw(data.data(), stride_bytes)) {
        return false;
      }
      out = Value::MakeFloatCompArray(std::move(data),
                                      CrateArrayValueType(type_id), comps);
      return true;
    }
    case CrateTypeId::Vec2d:
    case CrateTypeId::Vec3d:
    case CrateTypeId::Vec4d:
    case CrateTypeId::Quatd:
    case CrateTypeId::Matrix2d:
    case CrateTypeId::Matrix3d:
    case CrateTypeId::Matrix4d: {
      const uint32_t stride_bytes = CrateArrayElemStride(type_id);  // e.g. 128
      const uint32_t comps = stride_bytes / 8;
      size_t scalars;
      if (!safe::mul(static_cast<size_t>(count), size_t(comps), &scalars)) return false;
      std::vector<double> data(scalars);
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_floating_n(data.data(), scalars)) return false;
      } else if (!read_raw(data.data(), stride_bytes)) {
        return false;
      }
      out = Value::MakeDoubleCompArray(std::move(data),
                                       CrateArrayValueType(type_id), comps);
      return true;
    }
    case CrateTypeId::Half:
    case CrateTypeId::Vec2h:
    case CrateTypeId::Vec3h:
    case CrateTypeId::Vec4h:
    case CrateTypeId::Quath: {
      const uint32_t comps = CrateArrayElemStride(type_id) / 2;  // 2 bytes/half
      size_t scalars;
      if (comps == 0 || !safe::mul(static_cast<size_t>(count), size_t(comps), &scalars)) return false;
      std::vector<uint16_t> halfs(scalars);
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_half_n(halfs.data(), scalars)) return false;
      } else if (!read_raw(halfs.data(), comps * 2)) {
        return false;
      }
      std::vector<float> data(scalars);
      for (size_t i = 0; i < scalars; ++i) data[i] = HalfToFloat(halfs[i]);
      out = Value::MakeFloatCompArray(std::move(data),
                                      CrateArrayValueType(type_id), comps);
      return true;
    }
    default:
      AddWarning(std::string("Unsupported array type: ") + CrateTypeIdName(type_id));
      return false;
  }
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
      } else if (field.first == "framesPerSecond") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().framesPerSecond = *d;
          layer.meta().framesPerSecond_set = true;
        }
      } else if (field.first == "kilogramsPerUnit") {
        const double* d = field.second.as_double();
        if (d) {
          layer.meta().kilogramsPerUnit = *d;
          layer.meta().kilogramsPerUnit_set = true;
        }
      } else if (field.first == "customLayerData" ||
                 field.first == "expressionVariables") {
        // pxr-authored: binary VtDictionary; next-authored: USDA dict text.
        Value d;
        if (field.second.is_dictionary()) {
          d = std::move(field.second);
        } else if (const std::string* s = field.second.as_string()) {
          d = ParseDictText(*s);
        } else if (const std::string* s = field.second.as_token()) {
          d = ParseDictText(*s);
        }
        if (d.is_dictionary()) {
          if (field.first == "customLayerData")
            layer.meta().customLayerData = std::move(d);
          else
            layer.meta().expressionVariables = std::move(d);
        }
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
    uint32_t fieldset_index = 0;
  };

  // Variant content lives in the layer as bracketed holder/child prims
  // ("/Prim/{vset=sel}" + descendants); their attributes attach to those prims
  // via attr_map (so the layer fully represents the variants, enabling both the
  // compositor — which reads the selected holder — and unflattened round-trip).
  // We only need to know, per owning prim, which sets are selected.
  //   owning prim path -> { variantSet -> selected variant } (all selections).
  std::unordered_map<std::string, std::map<std::string, std::string>> variant_sel;

  std::vector<PrimEntry> prim_entries;
  std::vector<std::pair<std::string, ValueRep>> raw_field_scratch;
  std::vector<std::pair<std::string, Value>> value_field_scratch;
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
      if (ResolveFieldsetRaw(spec.fieldset_index.value, raw_field_scratch)) {
        for (auto& f : raw_field_scratch) {
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
    entry.fieldset_index = spec.fieldset_index.value;

    ResolveFieldset(spec.fieldset_index.value, value_field_scratch);

    // Untyped prims stay untyped: composition fills the type from the
    // referenced prim (a forced "Xform" default would mask e.g. a referenced
    // Mesh definition; the writer already skips empty type names).
    entry.type_name.clear();
    entry.specifier = PrimSpecifier::Def;
    for (auto& f : value_field_scratch) {
      if (f.first == "typeName") {
        if (const std::string* s = f.second.as_token()) entry.type_name = *s;
      } else if (f.first == "specifier") {
        if (const std::string* s = f.second.as_token()) {
          if (*s == "over") entry.specifier = PrimSpecifier::Over;
          else if (*s == "class") entry.specifier = PrimSpecifier::Class;
        }
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
    bool custom = false;
    std::vector<std::pair<double, Value>> time_samples;
    // Per-property metadata (round-tripped via attribute spec fields).
    std::string interpolation;
    std::string color_space;
    int32_t element_size = 1;
    bool has_interpolation = false;
    bool has_color_space = false;
    bool has_element_size = false;
    Value custom_data;  // dictionary
  };
  struct RelInfo {
    std::string name;
    std::vector<std::string> targets;
    bool uniform = false;
  };
  std::unordered_map<std::string, std::vector<AttrInfo>> attr_map;
  std::unordered_map<std::string, std::vector<RelInfo>> rel_map;
  std::vector<std::pair<std::string, ValueRep>> property_raw_field_scratch;

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

    if (!ResolveFieldsetRaw(spec.fieldset_index.value, property_raw_field_scratch)) continue;

    if (is_rel) {
      RelInfo ri;
      ri.name = std::move(prop_name);
      for (auto& f : property_raw_field_scratch) {
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
    for (auto& f : property_raw_field_scratch) {
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
      } else if (f.first == "timeSamples") {
        DecodeTimeSamples(f.second, &ai.time_samples);
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
      } else if (f.first == "custom") {
        // The legacy `custom` qualifier (pxr stores a bool field). Preserved on
        // read; the USDA writer only re-emits it under emit_custom/--openusd-compat.
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const bool* b = v.as_bool()) ai.custom = *b;
        }
      } else if (f.first == "interpolation") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) {
            ai.interpolation = *s;
            ai.has_interpolation = true;
          }
        }
      } else if (f.first == "colorSpace") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const std::string* s = v.as_token()) {
            ai.color_space = *s;
            ai.has_color_space = true;
          }
        }
      } else if (f.first == "elementSize") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (const int32_t* i = v.as_int()) {
            ai.element_size = *i;
            ai.has_element_size = true;
          }
        }
      } else if (f.first == "customData") {
        Value v;
        if (UnpackValue(f.second, v)) {
          if (v.is_dictionary()) {
            ai.custom_data = std::move(v);
          } else if (const std::string* s = v.as_string()) {
            ai.custom_data = ParseDictText(*s);
          } else if (const std::string* s = v.as_token()) {
            ai.custom_data = ParseDictText(*s);
          }
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
  std::vector<std::pair<std::string, Value>> prim_value_field_scratch;

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

    // Phase 7 S5: decode a `<arc>_listOp` companion token[] (marker-delimited
    // prepend/append/delete/order sublists) into an ArcEdit.
    auto decode_arc_listop = [&](const Value& v, ArcEdit& e) {
      const std::vector<std::string>* arr = v.as_token_array();
      if (!arr) return;
      e.authored = true;
      e.is_explicit = false;
      std::vector<std::string>* cur = nullptr;
      for (const std::string& s : *arr) {
        if (s == "\x01P") cur = &e.prepended;
        else if (s == "\x01A") cur = &e.appended;
        else if (s == "\x01D") cur = &e.deleted;
        else if (s == "\x01O") cur = &e.ordered;
        else if (s == "\x01N") cur = nullptr;
        else if (cur) cur->push_back(s);
      }
    };

    ResolveFieldset(entry.fieldset_index, prim_value_field_scratch);

    // Add properties and extract composition arcs / metadata into PrimSpecMeta
    for (auto& field : prim_value_field_scratch) {
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
            ps->meta().doc() = *s;
          else if (const std::string* s = field.second.as_token())
            ps->meta().doc() = *s;
          continue;
        }
      }

      // Composition arc + metadata fields: store in PrimSpecMeta, not as
      // regular properties. (Guarded by ps; if current() were null we simply
      // skip them rather than crash.)
      if (ps) {
        if (field.first == "apiSchemas") {
          // Applied API schemas. pxrUSD stores this as a TokenListOp; by the
          // time it reaches this field loop UnpackValue has flattened it to its
          // effective token list. Route it to PrimSpecMeta (the composed/flatten
          // form pxr writes as `apiSchemas = [...]` in the metadata block);
          // otherwise it leaks as a phantom `token[] apiSchemas` body property.
          append_token_list(field.second, ps->meta().apiSchemas(), "apiSchemas");
          continue;
        }
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
        if (field.first == "references_listOp") {
          decode_arc_listop(field.second, ps->meta().ensure_arc_edits().references);
          continue;
        }
        if (field.first == "payload_listOp") {
          decode_arc_listop(field.second, ps->meta().ensure_arc_edits().payloads);
          continue;
        }
        if (field.first == "inherits_listOp") {
          decode_arc_listop(field.second, ps->meta().ensure_arc_edits().inherits);
          continue;
        }
        if (field.first == "specializes_listOp") {
          decode_arc_listop(field.second,
                            ps->meta().ensure_arc_edits().specializes);
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
            ps->meta().comment() = *s;
          else if (const std::string* s = field.second.as_string())
            ps->meta().comment() = *s;
          continue;
        }
        if (field.first == "kind") {
          if (const std::string* s = field.second.as_token())
            ps->meta().kind() = *s;
          else if (const std::string* s = field.second.as_string())
            ps->meta().kind() = *s;
          continue;
        }
        if (field.first == "displayName") {
          if (const std::string* s = field.second.as_string())
            ps->meta().displayName() = *s;
          else if (const std::string* s = field.second.as_token())
            ps->meta().displayName() = *s;
          continue;
        }
        if (field.first == "instanceable") {
          if (const bool* b = field.second.as_bool())
            ps->meta().instanceable = *b;
          continue;
        }
        if (field.first == "customData" || field.first == "assetInfo" ||
            field.first == "sdrMetadata" || field.first == "clips") {
          // pxr-authored: a binary VtDictionary (already decoded to a Dictionary
          // Value). next-authored: USDA dict text in a String field.
          Value d;
          if (field.second.is_dictionary()) {
            d = std::move(field.second);
          } else if (const std::string* s = field.second.as_string()) {
            d = ParseDictText(*s);
          } else if (const std::string* s = field.second.as_token()) {
            d = ParseDictText(*s);
          }
          if (d.is_dictionary()) {
            if (field.first == "customData")
              ps->meta().customData() = std::move(d);
            else if (field.first == "assetInfo")
              ps->meta().assetInfo() = std::move(d);
            else if (field.first == "sdrMetadata")
              ps->meta().sdrMetadata() = std::move(d);
            else
              ps->meta().clips() = std::move(d);
          }
          continue;
        }
        if (field.first == "variantSets") {
          // Writer stores the variant-set names only; reconstruct name entries.
          std::vector<std::string> names;
          append_token_list(field.second, names, "variantSets");
          for (auto& n : names) {
            VariantSetData vsd;
            vsd.name = std::move(n);
            ps->meta().variantSets().push_back(std::move(vsd));
          }
          continue;
        }
      }
      // A loose sibling "variability" field cannot be re-associated with a
      // property here; consume it so it does not surface as a stray property.
      // Per-property variability is decoded above through Attribute/Relationship
      // specs and preserved as kFlagUniform.
      if (field.first == "variability") continue;

      // Reserved Sdf children-key ordering fields. pxrUSD stores prim/property
      // order in these (SdfChildrenKeys); USDA flatten output does NOT emit them
      // as body attributes -- order is implicit in the authored child/property
      // sequence. Composition derives child order from child_indices() directly
      // (see cache.cc ComposeInto), so consume these so they do not leak as
      // phantom `token[] primChildren`/`properties` attributes.
      if (field.first == "primChildren" || field.first == "properties" ||
          field.first == "propertyChildren" ||
          field.first == "variantChildren" ||
          field.first == "variantSetChildren") {
        continue;
      }

      // `variantSetNames` (the declared list of variant-set names) likewise must
      // not leak as a phantom `string[] variantSetNames` body property. The
      // compositor drives variants from the bracketed-holder specs + the
      // variantSelection (not from this list), and flatten output drops variant
      // metadata entirely (see cache.cc ComposeInto), so consume it.
      if (field.first == "variantSetNames") {
        continue;
      }

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
        if (ai.custom) flags |= PropSlot::kFlagCustom;
        if (ai.is_connection) flags |= PropSlot::kFlagConnection;
        // The writer gates timeSamples emission on the slot flag, so mark a
        // time-sampled attribute (the value lives in add_time_sample below).
        if (!ai.time_samples.empty()) flags |= PropSlot::kFlagTimeSampled;
        const bool is_array =
            ai.type_name.size() >= 2 &&
            ai.type_name.compare(ai.type_name.size() - 2, 2, "[]") == 0;
        if (ai.has_default) {
          ps->add_property(ai.name, std::move(ai.default_value), flags);
        } else {
          // Connection-only / declared-only / timeSamples-only attribute:
          // register a typed slot with no authored default so it round-trips.
          if (is_array) flags |= PropSlot::kFlagArray;
          std::string base = is_array
              ? ai.type_name.substr(0, ai.type_name.size() - 2)
              : ai.type_name;
          TypeId tid = GetTypeIdFromName(base.c_str());
          ps->add_property_slot(GetPropNameTable().intern(ai.name), tid, flags);
        }
        // Time samples (an attribute may have timeSamples with or without a
        // default).
        if (!ai.time_samples.empty()) {
          PropNameId nid = GetPropNameTable().intern(ai.name);
          for (auto& ts : ai.time_samples) {
            ps->add_time_sample(nid, ts.first, std::move(ts.second));
          }
        }
        if (!ai.type_name.empty()) {
          ps->set_property_type_name(ai.name, ai.type_name);
        }
        for (const auto& t : ai.connection_targets) {
          ps->add_connection(ai.name, Path(t));
        }
        // Per-property metadata.
        if (ai.has_interpolation || ai.has_color_space || ai.has_element_size ||
            ai.custom_data.is_dictionary()) {
          PropMeta& pm = ps->ensure_property_meta(ai.name);
          if (ai.has_interpolation) {
            pm.interpolation = ai.interpolation;
            pm.authored |= PropMeta::kInterpolation;
          }
          if (ai.has_color_space) {
            pm.colorSpace = ai.color_space;
            pm.authored |= PropMeta::kColorSpace;
          }
          if (ai.has_element_size) {
            pm.elementSize = ai.element_size;
            pm.authored |= PropMeta::kElementSize;
          }
          if (ai.custom_data.is_dictionary()) {
            pm.customData = std::move(ai.custom_data);
            pm.authored |= PropMeta::kCustomData;
          }
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
        ps->meta().variantSets().push_back(std::move(vsd));
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


}  // namespace next
}  // namespace tinyusdz
