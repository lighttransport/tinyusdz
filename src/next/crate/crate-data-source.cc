// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate data source implementation

#include "crate-data-source.hh"

#include "lazy-array.hh"
#include "stream-reader.hh"
#include "../types/value.hh"

#include <cstring>
#include <limits>

// Posix mmap is used to back a CrateDataSource directly off the file (Phase
// 8.3). WASM (emscripten/wasi) and non-posix platforms keep the owned-buffer
// path; MmapFile() then returns nullptr and the reader falls back.
#if !defined(TINYUSDZ_NEXT_NO_MMAP) && !defined(__EMSCRIPTEN__) && \
    !defined(__wasi__) &&                                         \
    (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
#define TINYUSDZ_NEXT_HAVE_MMAP 1
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tinyusdz {
namespace next {

// ============================================================
// CrateDataSource
// ============================================================

std::shared_ptr<CrateDataSource> CrateDataSource::Adopt(
    std::string&& bytes, CrateVersion version, std::vector<std::string>&& tokens,
    std::vector<uint32_t>&& string_indices) {
  // Can't use make_shared with a private ctor; construct then wrap.
  std::shared_ptr<CrateDataSource> ds(new CrateDataSource());
  ds->bytes_ = std::move(bytes);
  ds->version_ = version;
  ds->tokens_ = std::move(tokens);
  ds->string_indices_ = std::move(string_indices);
  return ds;
}

std::shared_ptr<CrateDataSource> CrateDataSource::Adopt(std::string&& bytes,
                                                        CrateVersion version) {
  std::shared_ptr<CrateDataSource> ds(new CrateDataSource());
  ds->bytes_ = std::move(bytes);
  ds->version_ = version;
  return ds;
}

std::shared_ptr<CrateDataSource> CrateDataSource::MmapFile(
    const std::string& filename) {
#if defined(TINYUSDZ_NEXT_HAVE_MMAP)
  int fd = ::open(filename.c_str(), O_RDONLY);
  if (fd < 0) return nullptr;
  struct stat st;
  if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
    ::close(fd);
    return nullptr;
  }
  const size_t len = static_cast<size_t>(st.st_size);
  void* addr = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
  ::close(fd);  // the mapping survives the descriptor being closed.
  if (addr == MAP_FAILED) return nullptr;
  std::shared_ptr<CrateDataSource> ds(new CrateDataSource());
  ds->mmap_addr_ = addr;
  ds->mmap_size_ = len;
  ds->mmap_base_ = reinterpret_cast<const uint8_t*>(addr);
  return ds;
#else
  (void)filename;
  return nullptr;
#endif
}

CrateDataSource::~CrateDataSource() {
#if defined(TINYUSDZ_NEXT_HAVE_MMAP)
  if (mmap_addr_) {
    ::munmap(mmap_addr_, mmap_size_);
    mmap_addr_ = nullptr;
  }
#endif
}

bool CrateDataSource::MaterializeArray(const LazyArrayRef& ref, Value* out) const {
  if (!out) return false;
  return DecodeCrateArray(base(), size(), ref.rep, tokens_,
                          /*max_elements=*/1024ull * 1024ull * 1024ull, out);
}

// ============================================================
// Type metadata
// ============================================================

uint32_t CrateArrayElemStride(CrateTypeId id) {
  switch (id) {
    case CrateTypeId::Bool:
    case CrateTypeId::UChar:
      return 1;
    case CrateTypeId::Half:
      return 2;
    case CrateTypeId::Int:
    case CrateTypeId::UInt:
    case CrateTypeId::Float:
    case CrateTypeId::Token:
    case CrateTypeId::String:
    case CrateTypeId::AssetPath:
      return 4;
    case CrateTypeId::Int64:
    case CrateTypeId::UInt64:
    case CrateTypeId::Double:
    case CrateTypeId::Vec2f:
    case CrateTypeId::Vec2i:
    case CrateTypeId::Vec4h:
    case CrateTypeId::Quath:
      return 8;
    case CrateTypeId::Vec3f:
    case CrateTypeId::Vec3i:
      return 12;
    case CrateTypeId::Vec4f:
    case CrateTypeId::Vec4i:
    case CrateTypeId::Vec2d:
    case CrateTypeId::Quatf:
      return 16;
    case CrateTypeId::Vec2h:
      return 4;
    case CrateTypeId::Vec3h:
      return 6;
    case CrateTypeId::Vec3d:
      return 24;
    case CrateTypeId::Vec4d:
    case CrateTypeId::Quatd:
    case CrateTypeId::Matrix2d:
      return 32;
    case CrateTypeId::Matrix3d:
      return 72;
    case CrateTypeId::Matrix4d:
      return 128;
    default:
      return 0;
  }
}

TypeId CrateArrayValueType(CrateTypeId id) {
  switch (id) {
    case CrateTypeId::Bool:
      return TypeId::Bool;
    case CrateTypeId::UChar:
    case CrateTypeId::UInt:
      return TypeId::UInt;
    case CrateTypeId::Int:
      return TypeId::Int;
    case CrateTypeId::Int64:
      return TypeId::Int64;
    case CrateTypeId::UInt64:
      return TypeId::UInt64;
    case CrateTypeId::Half:
      return TypeId::Half;
    case CrateTypeId::Float:
      return TypeId::Float;
    case CrateTypeId::Double:
      return TypeId::Double;
    case CrateTypeId::String:
      return TypeId::String;
    case CrateTypeId::Token:
      return TypeId::Token;
    case CrateTypeId::AssetPath:
      return TypeId::AssetPath;
    case CrateTypeId::Vec2f:
      return TypeId::Float2;
    case CrateTypeId::Vec3f:
      return TypeId::Float3;
    case CrateTypeId::Vec4f:
      return TypeId::Float4;
    case CrateTypeId::Vec2d:
      return TypeId::Double2;
    case CrateTypeId::Vec3d:
      return TypeId::Double3;
    case CrateTypeId::Vec4d:
      return TypeId::Double4;
    case CrateTypeId::Vec2h:
      return TypeId::Half2;
    case CrateTypeId::Vec3h:
      return TypeId::Half3;
    case CrateTypeId::Vec4h:
      return TypeId::Half4;
    case CrateTypeId::Vec2i:
      return TypeId::Int2;
    case CrateTypeId::Vec3i:
      return TypeId::Int3;
    case CrateTypeId::Vec4i:
      return TypeId::Int4;
    case CrateTypeId::Quatf:
      return TypeId::Quatf;
    case CrateTypeId::Quatd:
      return TypeId::Quatd;
    case CrateTypeId::Quath:
      return TypeId::Quath;
    case CrateTypeId::Matrix2d:
      return TypeId::Matrix2d;
    case CrateTypeId::Matrix3d:
      return TypeId::Matrix3d;
    case CrateTypeId::Matrix4d:
      return TypeId::Matrix4d;
    default:
      return TypeId::Invalid;
  }
}

// ============================================================
// ProbeArrayBlock
// ============================================================

bool ProbeArrayBlock(const std::shared_ptr<CrateDataSource>& source, ValueRep rep,
                     size_t max_elements, LazyArrayRef* out) {
  if (!source || !out) return false;

  out->source = source;
  out->rep = rep;
  out->crate_type = rep.type_id();
  out->value_type = CrateArrayValueType(rep.type_id());
  out->src_elem_stride = CrateArrayElemStride(rep.type_id());
  out->is_compressed = rep.is_compressed();
  out->element_count = 0;
  out->block_offset = 0;
  out->block_len = 0;

  // payload()==0 (non-inlined) denotes an empty array (pxrUSD convention).
  if (rep.payload() == 0) {
    return true;
  }

  StreamReader r(source->base(), source->size());
  const size_t off = static_cast<size_t>(rep.payload_as_offset());
  if (!r.seek(off)) return false;
  uint64_t count = 0;
  if (!r.read_u64(count)) return false;
  if (count > max_elements) return false;

  out->element_count = count;
  out->block_offset = off;

  const CrateTypeId t = rep.type_id();
  const bool compressed = rep.is_compressed();
  constexpr uint64_t kMinCompressedArraySize = 16;

  if (compressed && (t == CrateTypeId::Int || t == CrateTypeId::UInt) &&
      count >= kMinCompressedArraySize) {
    // Block layout: [u64 count][u64 comp_size][comp_size bytes].
    uint64_t comp_size = 0;
    if (!r.read_u64(comp_size)) return false;
    out->block_len = 8ull + 8ull + comp_size;
  } else if (!compressed && out->src_elem_stride > 0) {
    // Block layout: [u64 count][count*stride bytes].
    out->block_len = 8ull + count * uint64_t(out->src_elem_stride);
  } else {
    // Unknown layout (e.g. a compressed array of an unsupported type) — leave
    // block_len = 0 so write-time pass-through declines and re-encodes instead.
    out->block_len = 0;
  }

  // Validate that a known block stays in bounds; otherwise mark it unknown.
  if (out->block_len > 0) {
    if (off > source->size() || out->block_len > source->size() - off) {
      out->block_len = 0;
    }
  }

  return true;
}

// ============================================================
// DecodeCrateArray — mirrors CrateReader::Impl::UnpackArray
// ============================================================

bool DecodeCrateArray(const uint8_t* base, size_t size, ValueRep rep,
                      const std::vector<std::string>& tokens, size_t max_elements,
                      Value* out) {
  if (!out) return false;
  const CrateTypeId type_id = rep.type_id();

  // Arrays below this length are stored uncompressed even if the compressed bit
  // is set (matches pxrUSD / legacy core kMinCompressedArraySize).
  constexpr uint64_t kMinCompressedArraySize = 16;

  StreamReader r(base, size);

  uint64_t count = 0;
  if (rep.payload() != 0) {
    if (!r.seek(static_cast<size_t>(rep.payload_as_offset()))) return false;
    if (!r.read_u64(count)) return false;
  }
  if (count > max_elements) return false;

  const bool compressed = rep.is_compressed();

  // Guard the per-element allocation BEFORE allocating any std::vector(count):
  // the cases below allocate count*elem bytes and only then call read_raw()
  // (whose has_elements() check is too late). A malformed count (e.g. 1e9
  // matrix4d = 128 GB) would OOM first. For an uncompressed array the bytes must
  // be present in the file; for any array, cap the decoded size at the input
  // size times the maximum stream compression ratio (mirrors the eager reader's
  // CheckByteAllocation).
  {
    const uint64_t stride = CrateArrayElemStride(type_id);
    const uint64_t elem_bytes = stride ? stride : 1;
    if (!compressed && stride > 0 &&
        !r.has_elements(static_cast<size_t>(count), static_cast<size_t>(stride))) {
      return false;
    }
    constexpr uint64_t kMaxRatio = 256;
    constexpr uint64_t kSlack = 64ull * 1024 * 1024;
    constexpr uint64_t kU64Max = (std::numeric_limits<uint64_t>::max)();
    const uint64_t fsz = static_cast<uint64_t>(size);
    const uint64_t cap = (fsz > (kU64Max - kSlack) / kMaxRatio)
                             ? kU64Max
                             : fsz * kMaxRatio + kSlack;
    if (count > cap / elem_bytes) return false;  // count*elem_bytes > cap
  }

  auto read_compressed_u32 = [&](uint32_t* dst) -> bool {
    uint64_t comp_size;
    if (!r.read_u64(comp_size)) return false;
    std::vector<uint8_t> blob;
    if (!r.read(blob, static_cast<size_t>(comp_size))) return false;
    std::vector<uint8_t> with_prefix(8 + blob.size());
    std::memcpy(with_prefix.data(), &comp_size, 8);
    if (!blob.empty()) std::memcpy(with_prefix.data() + 8, blob.data(), blob.size());
    DecompressResult dr = DecompressCompressedU32(
        with_prefix.data(), with_prefix.size(), dst, static_cast<size_t>(count));
    return dr.success;
  };

  auto read_raw = [&](void* dst, size_t elem_size) -> bool {
    if (count == 0) return true;
    if (!r.has_elements(count, elem_size)) return false;
    return r.read(dst, static_cast<size_t>(count) * elem_size);
  };

  switch (type_id) {
    case CrateTypeId::Float: {
      if (compressed) return false;
      std::vector<float> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(float))) return false;
      *out = Value::MakeFloatArray(std::move(data));
      return true;
    }
    case CrateTypeId::Int: {
      std::vector<int32_t> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u32(reinterpret_cast<uint32_t*>(data.data()))) return false;
      } else if (!read_raw(data.data(), sizeof(int32_t))) {
        return false;
      }
      *out = Value::MakeIntArray(std::move(data));
      return true;
    }
    case CrateTypeId::Vec2f: {
      if (compressed) return false;
      std::vector<float> data(static_cast<size_t>(count) * 2);
      if (!read_raw(data.data(), 2 * sizeof(float))) return false;
      *out = Value::MakeFloat2Array(std::move(data));
      return true;
    }
    case CrateTypeId::Vec3f: {
      if (compressed) return false;
      std::vector<float> data(static_cast<size_t>(count) * 3);
      if (!read_raw(data.data(), 3 * sizeof(float))) return false;
      *out = Value::MakeFloat3Array(std::move(data));
      return true;
    }
    case CrateTypeId::Double: {
      if (compressed) return false;
      std::vector<double> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(double))) return false;
      *out = Value::MakeDoubleArray(std::move(data));
      return true;
    }
    case CrateTypeId::Int64: {
      if (compressed) return false;
      std::vector<int64_t> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(int64_t))) return false;
      *out = Value::MakeInt64Array(std::move(data));
      return true;
    }
    case CrateTypeId::UInt: {
      std::vector<uint32_t> data(static_cast<size_t>(count));
      if (compressed && count >= kMinCompressedArraySize) {
        if (!read_compressed_u32(data.data())) return false;
      } else if (!read_raw(data.data(), sizeof(uint32_t))) {
        return false;
      }
      *out = Value::MakeUIntArray(std::move(data));
      return true;
    }
    case CrateTypeId::UInt64: {
      if (compressed) return false;
      std::vector<uint64_t> data(static_cast<size_t>(count));
      if (!read_raw(data.data(), sizeof(uint64_t))) return false;
      *out = Value::MakeUInt64Array(std::move(data));
      return true;
    }
    case CrateTypeId::Bool: {
      if (compressed) return false;
      std::vector<uint8_t> bytes(static_cast<size_t>(count));
      if (!read_raw(bytes.data(), sizeof(uint8_t))) return false;
      std::vector<bool> out_bool(static_cast<size_t>(count));
      for (size_t i = 0; i < count; i++) out_bool[i] = (bytes[i] != 0);
      *out = Value::MakeBoolArray(out_bool);
      return true;
    }
    case CrateTypeId::Token: {
      if (compressed) return false;
      std::vector<uint32_t> idxs(static_cast<size_t>(count));
      if (!read_raw(idxs.data(), sizeof(uint32_t))) return false;
      std::vector<std::string> data(static_cast<size_t>(count));
      for (size_t i = 0; i < count; i++) {
        if (idxs[i] >= tokens.size()) return false;
        data[i] = tokens[idxs[i]];
      }
      *out = Value::MakeTokenArray(std::move(data));
      return true;
    }
    case CrateTypeId::Vec4f:
    case CrateTypeId::Quatf: {
      if (compressed) return false;
      const uint32_t stride_bytes = CrateArrayElemStride(type_id);
      const uint32_t comps = stride_bytes / 4;
      if (comps == 0 ||
          count > (std::numeric_limits<size_t>::max)() / comps) {
        return false;
      }
      std::vector<float> data(static_cast<size_t>(count) * comps);
      if (!read_raw(data.data(), stride_bytes)) return false;
      *out = Value::MakeFloatCompArray(std::move(data),
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
      if (compressed) return false;
      const uint32_t stride_bytes = CrateArrayElemStride(type_id);
      const uint32_t comps = stride_bytes / 8;
      if (comps == 0 ||
          count > (std::numeric_limits<size_t>::max)() / comps) {
        return false;
      }
      std::vector<double> data(static_cast<size_t>(count) * comps);
      if (!read_raw(data.data(), stride_bytes)) return false;
      *out = Value::MakeDoubleCompArray(std::move(data),
                                        CrateArrayValueType(type_id), comps);
      return true;
    }
    case CrateTypeId::Half:
    case CrateTypeId::Vec2h:
    case CrateTypeId::Vec3h:
    case CrateTypeId::Vec4h:
    case CrateTypeId::Quath: {
      if (compressed) return false;
      const uint32_t comps = CrateArrayElemStride(type_id) / 2;  // 2 bytes/half
      if (comps == 0 ||
          count > (std::numeric_limits<size_t>::max)() / comps) {
        return false;
      }
      std::vector<uint16_t> halfs(static_cast<size_t>(count) * comps);
      if (!read_raw(halfs.data(), comps * 2)) return false;
      std::vector<float> data(halfs.size());
      for (size_t i = 0; i < halfs.size(); ++i) data[i] = HalfToFloat(halfs[i]);
      *out = Value::MakeFloatCompArray(std::move(data),
                                       CrateArrayValueType(type_id), comps);
      return true;
    }
    default:
      // No concrete Value storage for this array type yet.
      return false;
  }
}

}  // namespace next
}  // namespace tinyusdz
