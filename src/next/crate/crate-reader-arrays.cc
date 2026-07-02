// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC Crate Reader array unpackers

#include "crate-reader-internal.hh"

#include "crate-data-source.hh"
#include "lazy-array.hh"
#include "safe-arithmetic.hh"

#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace tinyusdz {
namespace next {

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
    if (!reader()->seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!reader()->read_u64(count)) return false;
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
  // run) -- so a malformed count (e.g. 1e9 matrix4d = 128 GB) would OOM before
  // that check. Bound it here against the file-size-relative cap. For an
  // uncompressed array the element bytes must physically be present in the file;
  // for a compressed array the file-relative cap still limits the decoded size.
  {
    const uint64_t stride = CrateArrayElemStride(type_id);
    const uint64_t elem_bytes = stride ? stride : 1;
    if (!compressed && stride > 0 && !reader()->has_elements(
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
    if (!reader()->read_u64(comp_size)) return false;
    const size_t comp_bytes = static_cast<size_t>(comp_size);
    if (comp_bytes != comp_size) {
      AddWarning("Compressed integer array size exceeds addressable memory");
      return false;
    }
    array_scratch().compressed_data.resize(8 + comp_bytes);
    std::memcpy(array_scratch().compressed_data.data(), &comp_size, 8);
    if (!reader()->read(array_scratch().compressed_data.data() + 8, comp_bytes)) return false;
    DecompressResult dr = DecompressCompressedU32(
        array_scratch().compressed_data.data(),
        array_scratch().compressed_data.size(), dst, n);
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
    if (!reader()->read_u64(comp_size)) return false;
    const size_t comp_bytes = static_cast<size_t>(comp_size);
    if (comp_bytes != comp_size) {
      AddWarning("Compressed integer array size exceeds addressable memory");
      return false;
    }
    array_scratch().compressed_data.resize(8 + comp_bytes);
    std::memcpy(array_scratch().compressed_data.data(), &comp_size, 8);
    if (!reader()->read(array_scratch().compressed_data.data() + 8, comp_bytes)) return false;
    DecompressResult dr = DecompressCompressedU64(
        array_scratch().compressed_data.data(),
        array_scratch().compressed_data.size(), dst, n);
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
    if (!reader()->has_elements(count, elem_size)) return false;
    return reader()->read(dst, static_cast<size_t>(count) * elem_size);
  };

  // Decode a compressed floating-point array (pxrUSD format): [i8 code] then
  // 'i' -> compressed int32s cast to T; 't' -> [u32 lutSize][T lut][compressed
  // u32 indices]. Fills `n` scalar elements of T into `dst`.
  auto read_compressed_floating_n = [&](auto* dst, size_t n) -> bool {
    using T = typename std::remove_pointer<decltype(dst)>::type;
    int8_t code = 0;
    if (!reader()->read_i8(code)) return false;
    if (code == 'i') {
      auto& ints = array_scratch().u32_indices;
      ints.resize(n);
      if (!read_compressed_u32_n(ints.data(), n)) return false;
      for (size_t i = 0; i < n; ++i) {
        dst[i] = static_cast<T>(static_cast<int32_t>(ints[i]));
      }
      return true;
    }
    if (code == 't') {
      uint32_t lut_size = 0;
      if (!reader()->read_u32(lut_size)) return false;
      if (lut_size == 0 || lut_size > options_.max_array_elements) return false;
      // The LUT holds lut_size elements read directly from the file; bound the
      // count against the remaining file before allocating to avoid a
      // malformed-count huge allocation ahead of the read below.
      if (!reader()->has_elements(size_t(lut_size), sizeof(T))) return false;
      size_t lut_bytes;
      if (!safe::mul(size_t(lut_size), sizeof(T), &lut_bytes)) return false;
      if constexpr (std::is_same_v<T, float>) {
        array_scratch().float_values.resize(lut_size);
        if (!reader()->read(array_scratch().float_values.data(), lut_bytes)) return false;
      } else {
        array_scratch().double_values.resize(lut_size);
        if (!reader()->read(array_scratch().double_values.data(), lut_bytes)) return false;
      }
      auto& idxs = array_scratch().u32_indices;
      idxs.resize(n);
      if (!read_compressed_u32_n(idxs.data(), n)) return false;
      for (size_t i = 0; i < n; ++i) {
        if (idxs[i] >= lut_size) return false;
        if constexpr (std::is_same_v<T, float>) {
          dst[i] = array_scratch().float_values[idxs[i]];
        } else {
          dst[i] = array_scratch().double_values[idxs[i]];
        }
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
    if (!reader()->read_i8(code)) return false;
    if (code == 'i') {
      auto& ints = array_scratch().u32_indices;
      ints.resize(n);
      if (!read_compressed_u32_n(ints.data(), n)) return false;
      for (size_t i = 0; i < n; ++i) {
        dst[i] = FloatToHalf(static_cast<float>(static_cast<int32_t>(ints[i])));
      }
      return true;
    }
    if (code == 't') {
      uint32_t lut_size = 0;
      if (!reader()->read_u32(lut_size)) return false;
      if (lut_size == 0 || lut_size > options_.max_array_elements) return false;
      if (!reader()->has_elements(size_t(lut_size), sizeof(uint16_t))) return false;
      auto& lut = array_scratch().half_values;
      lut.resize(lut_size);
      size_t lut_bytes;
      if (!safe::mul(size_t(lut_size), sizeof(uint16_t), &lut_bytes)) return false;
      if (!reader()->read(lut.data(), lut_bytes)) return false;
      auto& idxs = array_scratch().u32_indices;
      idxs.resize(n);
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
      auto& bytes = array_scratch().u8_values;
      bytes.resize(static_cast<size_t>(count));
      if (!read_raw(bytes.data(), sizeof(uint8_t))) return false;
      out = Value::MakeBoolArrayFromBytes(std::move(bytes));
      return true;
    }
    case CrateTypeId::Token: {
      const size_t count_value = static_cast<size_t>(count);
      auto& idxs = array_scratch().u32_indices;
      idxs.resize(count_value);
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u32(idxs.data())) return false;
      } else if (!read_raw(idxs.data(), sizeof(uint32_t))) {
        return false;
      }
      std::vector<std::string> data;
      data.reserve(count_value);
      for (size_t i = 0; i < count_value; i++) {
        if (idxs[i] >= tokens_.size()) return false;
        const auto tok = tokens_.view(idxs[i]);
        data.emplace_back(tok);
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
      auto& halfs = array_scratch().half_values;
      halfs.resize(scalars);
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

}  // namespace next
}  // namespace tinyusdz
