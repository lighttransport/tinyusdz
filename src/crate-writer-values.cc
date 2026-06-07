// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Experimental USDC (Crate) File Writer — per-type value serialization.
// Split out of crate-writer.cc: CrateWriter::WriteValueData + ::TryInlineValue
// (the ~1750-line per-type binary value (de)serialization — every USD scalar/
// array/dict/listop type writes its own layout) plus their file-local helpers
// (ConvertValueToCrateValue, BuildListOpHeader). Compiling them as their own TU
// shortens the build critical path — crate-writer.cc was the tallest pole
// (~10.8s @ -O3). All the WRITE_*/DEDUP_*/TRY_*/CANNOT_INLINE/CONVERT_CRATE_VALUE
// macros are #defined+#undef'd inside the bodies, so they travel here intact.
// WriteCompressedArray32/64, CompressData and the GetOrCreate* dedup tables stay
// CrateWriter members in crate-writer.cc; these methods just call them.
#include "crate-writer.hh"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>

#include "common-macros.inc"
#include "safe-arithmetic.hh"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wexceptions"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

namespace tinyusdz {
namespace experimental {

namespace {

/// Build a ListOpHeader from any ListOp<T> (identical logic for all types)
template<typename T>
ListOpHeader BuildListOpHeader(const ListOp<T>& listop) {
  ListOpHeader header;
  header.bits = 0;
  if (listop.IsExplicit()) header.bits |= ListOpHeader::IsExplicitBit;
  if (listop.HasExplicitItems()) header.bits |= ListOpHeader::HasExplicitItemsBit;
  if (listop.HasAddedItems()) header.bits |= ListOpHeader::HasAddedItemsBit;
  if (listop.HasDeletedItems()) header.bits |= ListOpHeader::HasDeletedItemsBit;
  if (listop.HasOrderedItems()) header.bits |= ListOpHeader::HasOrderedItemsBit;
  if (listop.HasPrependedItems()) header.bits |= ListOpHeader::HasPrependedItemsBit;
  if (listop.HasAppendedItems()) header.bits |= ListOpHeader::HasAppendedItemsBit;
  return header;  // NRVO
}

} // anonymous namespace

/// Helper to convert value::Value to CrateValue for TimeSamples serialization
/// Returns true if conversion succeeded
static bool ConvertValueToCrateValue(const value::Value& val, crate::CrateValue* out, std::string* err) {
  if (!out) {
    if (err) *err = "ConvertValueToCrateValue: output is null";
    return false;
  }

  uint32_t type_id = val.type_id();

  // Macro to reduce repetitive scalar/vector type dispatch
#define CONVERT_CRATE_VALUE(CppType) \
  if (auto* v = val.as<CppType>()) { \
    out->Set(*v); \
    return true; \
  } else

  // Scalar numeric types
  CONVERT_CRATE_VALUE(bool)
  CONVERT_CRATE_VALUE(uint8_t)  // uchar
  CONVERT_CRATE_VALUE(int32_t)
  CONVERT_CRATE_VALUE(uint32_t)
  CONVERT_CRATE_VALUE(int64_t)
  CONVERT_CRATE_VALUE(uint64_t)
  CONVERT_CRATE_VALUE(value::half)
  CONVERT_CRATE_VALUE(float)
  CONVERT_CRATE_VALUE(double)
  // Vector types
  CONVERT_CRATE_VALUE(value::float2)
  CONVERT_CRATE_VALUE(value::float3)
  CONVERT_CRATE_VALUE(value::float4)
  CONVERT_CRATE_VALUE(value::double2)
  CONVERT_CRATE_VALUE(value::double3)
  CONVERT_CRATE_VALUE(value::double4)
  CONVERT_CRATE_VALUE(value::int2)
  CONVERT_CRATE_VALUE(value::int3)
  CONVERT_CRATE_VALUE(value::int4)
  // Array types - numeric scalars
  CONVERT_CRATE_VALUE(std::vector<bool>)
  CONVERT_CRATE_VALUE(std::vector<uint8_t>)  // uchar[]
  CONVERT_CRATE_VALUE(std::vector<int32_t>)
  CONVERT_CRATE_VALUE(std::vector<uint32_t>)
  CONVERT_CRATE_VALUE(std::vector<int64_t>)
  CONVERT_CRATE_VALUE(std::vector<uint64_t>)
  CONVERT_CRATE_VALUE(std::vector<value::half>)
  CONVERT_CRATE_VALUE(std::vector<float>)
  CONVERT_CRATE_VALUE(std::vector<double>)
  // Vector arrays
  CONVERT_CRATE_VALUE(std::vector<value::float2>)
  CONVERT_CRATE_VALUE(std::vector<value::float3>)
  CONVERT_CRATE_VALUE(std::vector<value::float4>)
  CONVERT_CRATE_VALUE(std::vector<value::double2>)
  CONVERT_CRATE_VALUE(std::vector<value::double3>)
  CONVERT_CRATE_VALUE(std::vector<value::double4>)
  CONVERT_CRATE_VALUE(std::vector<value::int2>)
  CONVERT_CRATE_VALUE(std::vector<value::int3>)
  CONVERT_CRATE_VALUE(std::vector<value::int4>)
  // Token/String/AssetPath types
  CONVERT_CRATE_VALUE(value::token)
  CONVERT_CRATE_VALUE(std::string)
  CONVERT_CRATE_VALUE(value::AssetPath)
  // Token/String/AssetPath arrays
  CONVERT_CRATE_VALUE(std::vector<value::token>)
  CONVERT_CRATE_VALUE(std::vector<std::string>)
  CONVERT_CRATE_VALUE(std::vector<value::AssetPath>)
  // Half-vec / matrix / quaternion scalars
  CONVERT_CRATE_VALUE(value::half2)
  CONVERT_CRATE_VALUE(value::half3)
  CONVERT_CRATE_VALUE(value::half4)
  CONVERT_CRATE_VALUE(value::matrix2d)
  CONVERT_CRATE_VALUE(value::matrix3d)
  CONVERT_CRATE_VALUE(value::matrix4d)
  CONVERT_CRATE_VALUE(value::quath)
  CONVERT_CRATE_VALUE(value::quatf)
  CONVERT_CRATE_VALUE(value::quatd)
  // Half-vec / matrix / quaternion arrays
  CONVERT_CRATE_VALUE(std::vector<value::half2>)
  CONVERT_CRATE_VALUE(std::vector<value::half3>)
  CONVERT_CRATE_VALUE(std::vector<value::half4>)
  CONVERT_CRATE_VALUE(std::vector<value::matrix2d>)
  CONVERT_CRATE_VALUE(std::vector<value::matrix3d>)
  CONVERT_CRATE_VALUE(std::vector<value::matrix4d>)
  CONVERT_CRATE_VALUE(std::vector<value::quath>)
  CONVERT_CRATE_VALUE(std::vector<value::quatf>)
  CONVERT_CRATE_VALUE(std::vector<value::quatd>)

#undef CONVERT_CRATE_VALUE
  // fall through to unmatched type handling
  {}

  // Phase 5.7: Custom/Unregistered value types
  // For unknown types, attempt to encode as an unregistered value
  // This allows custom attributes with user-defined types to be stored
  const std::string& type_name = val.type_name();

  DCOUT("[ConvertValueToCrateValue] Unmatched type: type_name='" << type_name
        << "' type_id=" << type_id);

  if (!type_name.empty()) {
    // Try to encode as Dictionary (most flexible representation)
    if (auto* v = val.as<Dictionary>()) {
      out->Set(*v);
      DCOUT("[ConvertValueToCrateValue] Encoded custom/unregistered value as Dictionary: "
            << type_name);
      return true;
    }

    // Try to encode as generic string representation
    // This is a fallback for values that can be stringified
    DCOUT("[ConvertValueToCrateValue] Encoding custom type as Dictionary: "
          << type_name << " (type_id=" << type_id << ")");

    // For now, we store the type name in a dictionary as metadata
    Dictionary custom_dict;
    custom_dict["__type__"] = type_name;
    custom_dict["__note__"] = std::string("Custom unregistered value type - type information preserved in metadata");
    out->Set(custom_dict);
    return true;
  }

  // Truly unsupported type - no type name available
  if (err) {
    *err = "ConvertValueToCrateValue: Unsupported type_id " + std::to_string(type_id) +
           " (type_name: " + val.type_name() + ")";
  }
  return false;
}

// Build the canonical deduplication key for an out-of-line CrateValue.
//
// Covers every out-of-line array element type (and the out-of-line vec/matrix/
// quat scalar types, e.g. animated matrix4d transforms) that
// ConvertValueToCrateValue can emit. Role types (texcoord2f, point3f, normal3f,
// color*, etc.) are already normalized to their base type (float2, float3, etc.) by
// ConvertValueToCrateValue's non-strict `as<>`, so no separate role entries are
// needed and coverage is complete by construction.
//
// Returns false for types we intentionally don't deduplicate (dictionaries /
// list ops / paths and other layout-coupled recursive values); the caller then
// packs them normally.
//
// For float/double-component types, *element_size is the PER-COMPONENT width (4
// or 8) so NanAwareHash canonicalizes +0.0/-0.0 per component; binary types use
// element_size 1 with is_float=false (raw-byte hashing).
static constexpr uint32_t kDedupArrayTagBit = 1u << 31;

bool CrateWriter::ComputeValueDedupDescriptor(const crate::CrateValue& cv,
                                              std::vector<char>* bytes,
                                              size_t* element_size,
                                              bool* is_float,
                                              uint32_t* wire_tag) {
#define DEDUP_DESC_ARRAY(Type, ElemSize, IsFloat, CrateType)                 \
  if (auto* arr = cv.as<std::vector<Type>>()) {                              \
    size_t bsz;                                                              \
    if (!safe::mul(arr->size(), sizeof(Type), &bsz)) return false;          \
    bytes->resize(bsz);                                                      \
    if (bsz) std::memcpy(bytes->data(), arr->data(), bsz);                   \
    *element_size = (ElemSize); *is_float = (IsFloat);                       \
    *wire_tag = kDedupArrayTagBit |                                          \
        static_cast<uint32_t>(crate::CrateDataTypeId::CrateType);            \
    return true;                                                             \
  }
#define DEDUP_DESC_SCALAR(Type, ElemSize, IsFloat, CrateType)                \
  if (auto* p = cv.as<Type>()) {                                             \
    bytes->resize(sizeof(Type));                                             \
    std::memcpy(bytes->data(), p, sizeof(Type));                             \
    *element_size = (ElemSize); *is_float = (IsFloat);                       \
    *wire_tag = static_cast<uint32_t>(crate::CrateDataTypeId::CrateType);    \
    return true;                                                             \
  }

  // Float/double component arrays (per-component element_size, NaN/+0/-0-aware).
  DEDUP_DESC_ARRAY(float, sizeof(float), true, CRATE_DATA_TYPE_FLOAT)
  DEDUP_DESC_ARRAY(double, sizeof(double), true, CRATE_DATA_TYPE_DOUBLE)
  DEDUP_DESC_ARRAY(value::float2, sizeof(float), true, CRATE_DATA_TYPE_VEC2F)
  DEDUP_DESC_ARRAY(value::float3, sizeof(float), true, CRATE_DATA_TYPE_VEC3F)
  DEDUP_DESC_ARRAY(value::float4, sizeof(float), true, CRATE_DATA_TYPE_VEC4F)
  DEDUP_DESC_ARRAY(value::double2, sizeof(double), true, CRATE_DATA_TYPE_VEC2D)
  DEDUP_DESC_ARRAY(value::double3, sizeof(double), true, CRATE_DATA_TYPE_VEC3D)
  DEDUP_DESC_ARRAY(value::double4, sizeof(double), true, CRATE_DATA_TYPE_VEC4D)
  DEDUP_DESC_ARRAY(value::matrix2d, sizeof(double), true, CRATE_DATA_TYPE_MATRIX2D)
  DEDUP_DESC_ARRAY(value::matrix3d, sizeof(double), true, CRATE_DATA_TYPE_MATRIX3D)
  DEDUP_DESC_ARRAY(value::matrix4d, sizeof(double), true, CRATE_DATA_TYPE_MATRIX4D)
  DEDUP_DESC_ARRAY(value::quatf, sizeof(float), true, CRATE_DATA_TYPE_QUATF)
  DEDUP_DESC_ARRAY(value::quatd, sizeof(double), true, CRATE_DATA_TYPE_QUATD)
  // std::vector<bool> is the bit-packed standard-library specialization, so it
  // has no contiguous `.data()` and cannot go through DEDUP_DESC_ARRAY. Expand
  // it to one byte per element so it gets a stable dedup key like every other
  // array type. Without this, animated bool[] (visibility/mask) timesamples are
  // never deduplicated and re-expand to N full copies on write (the cause of
  // a 78 MB -> 384 MB USDC roundtrip blowup observed on a large animated scene).
  if (auto* barr = cv.as<std::vector<bool>>()) {
    bytes->resize(barr->size());
    for (size_t i = 0; i < barr->size(); ++i) {
      (*bytes)[i] = (*barr)[i] ? char(1) : char(0);
    }
    *element_size = 1;
    *is_float = false;
    *wire_tag = kDedupArrayTagBit |
        static_cast<uint32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_BOOL);
    return true;
  }

  // Raw-byte (binary) arrays.
  DEDUP_DESC_ARRAY(uint8_t, 1, false, CRATE_DATA_TYPE_UCHAR)  // uchar[]
  DEDUP_DESC_ARRAY(int32_t, 1, false, CRATE_DATA_TYPE_INT)
  DEDUP_DESC_ARRAY(uint32_t, 1, false, CRATE_DATA_TYPE_UINT)
  DEDUP_DESC_ARRAY(int64_t, 1, false, CRATE_DATA_TYPE_INT64)
  DEDUP_DESC_ARRAY(uint64_t, 1, false, CRATE_DATA_TYPE_UINT64)
  DEDUP_DESC_ARRAY(value::half, 1, false, CRATE_DATA_TYPE_HALF)
  DEDUP_DESC_ARRAY(value::half2, 1, false, CRATE_DATA_TYPE_VEC2H)
  DEDUP_DESC_ARRAY(value::half3, 1, false, CRATE_DATA_TYPE_VEC3H)
  DEDUP_DESC_ARRAY(value::half4, 1, false, CRATE_DATA_TYPE_VEC4H)
  DEDUP_DESC_ARRAY(value::int2, 1, false, CRATE_DATA_TYPE_VEC2I)
  DEDUP_DESC_ARRAY(value::int3, 1, false, CRATE_DATA_TYPE_VEC3I)
  DEDUP_DESC_ARRAY(value::int4, 1, false, CRATE_DATA_TYPE_VEC4I)
  DEDUP_DESC_ARRAY(value::quath, 1, false, CRATE_DATA_TYPE_QUATH)

  // Out-of-line scalars (vec/matrix/quat). Checked after arrays so an array
  // never matches a scalar branch (distinct types).
  DEDUP_DESC_SCALAR(double, sizeof(double), true, CRATE_DATA_TYPE_DOUBLE)
  DEDUP_DESC_SCALAR(int64_t, 1, false, CRATE_DATA_TYPE_INT64)
  DEDUP_DESC_SCALAR(uint64_t, 1, false, CRATE_DATA_TYPE_UINT64)
  DEDUP_DESC_SCALAR(value::float2, sizeof(float), true, CRATE_DATA_TYPE_VEC2F)
  DEDUP_DESC_SCALAR(value::float3, sizeof(float), true, CRATE_DATA_TYPE_VEC3F)
  DEDUP_DESC_SCALAR(value::float4, sizeof(float), true, CRATE_DATA_TYPE_VEC4F)
  DEDUP_DESC_SCALAR(value::double2, sizeof(double), true, CRATE_DATA_TYPE_VEC2D)
  DEDUP_DESC_SCALAR(value::double3, sizeof(double), true, CRATE_DATA_TYPE_VEC3D)
  DEDUP_DESC_SCALAR(value::double4, sizeof(double), true, CRATE_DATA_TYPE_VEC4D)
  DEDUP_DESC_SCALAR(value::matrix2d, sizeof(double), true, CRATE_DATA_TYPE_MATRIX2D)
  DEDUP_DESC_SCALAR(value::matrix3d, sizeof(double), true, CRATE_DATA_TYPE_MATRIX3D)
  DEDUP_DESC_SCALAR(value::matrix4d, sizeof(double), true, CRATE_DATA_TYPE_MATRIX4D)
  DEDUP_DESC_SCALAR(value::quatf, sizeof(float), true, CRATE_DATA_TYPE_QUATF)
  DEDUP_DESC_SCALAR(value::quatd, sizeof(double), true, CRATE_DATA_TYPE_QUATD)
  DEDUP_DESC_SCALAR(value::quath, 1, false, CRATE_DATA_TYPE_QUATH)
  DEDUP_DESC_SCALAR(value::half2, 1, false, CRATE_DATA_TYPE_VEC2H)
  DEDUP_DESC_SCALAR(value::half3, 1, false, CRATE_DATA_TYPE_VEC3H)
  DEDUP_DESC_SCALAR(value::half4, 1, false, CRATE_DATA_TYPE_VEC4H)
  DEDUP_DESC_SCALAR(value::int2, 1, false, CRATE_DATA_TYPE_VEC2I)
  DEDUP_DESC_SCALAR(value::int3, 1, false, CRATE_DATA_TYPE_VEC3I)
  DEDUP_DESC_SCALAR(value::int4, 1, false, CRATE_DATA_TYPE_VEC4I)

#undef DEDUP_DESC_ARRAY
#undef DEDUP_DESC_SCALAR

  if (auto* ts = cv.as<value::TimeSamples>()) {
    if (!ts->is_using_binary_storage()) {
      return false;
    }

    bytes->clear();
    auto append_raw = [&](const void* ptr, size_t nbytes) {
      if (nbytes == 0) {
        return;
      }
      const char* p = reinterpret_cast<const char*>(ptr);
      bytes->insert(bytes->end(), p, p + nbytes);
    };
    auto append_u64 = [&](uint64_t v) {
      append_raw(&v, sizeof(v));
    };
    auto append_u32 = [&](uint32_t v) {
      append_raw(&v, sizeof(v));
    };
    auto append_u8 = [&](uint8_t v) {
      append_raw(&v, sizeof(v));
    };

    const uint64_t sample_count = static_cast<uint64_t>(ts->size());
    append_u64(sample_count);
    append_u32(ts->type_id());
    append_u32(ts->element_size());
    append_u8(ts->is_array() ? uint8_t(1) : uint8_t(0));

    const std::vector<double>& times = ts->get_times();
    append_u64(static_cast<uint64_t>(times.size()));
    append_raw(times.data(), times.size() * sizeof(double));

    const Buffer<16>& blocked = ts->get_blocked();
    append_u64(static_cast<uint64_t>(blocked.size()));
    append_raw(blocked.data(), blocked.size());

    const std::vector<size_t>& offsets = ts->get_data_offsets();
    append_u64(static_cast<uint64_t>(offsets.size()));
    for (size_t off : offsets) {
      uint64_t wire_off = (off == value::TimeSamples::BLOCKED_OFFSET)
          ? std::numeric_limits<uint64_t>::max()
          : static_cast<uint64_t>(off);
      append_u64(wire_off);
    }

    const std::vector<uint32_t>& counts = ts->get_array_counts();
    append_u64(static_cast<uint64_t>(counts.size()));
    append_raw(counts.data(), counts.size() * sizeof(uint32_t));

    const std::vector<uint8_t>& data = ts->get_data();
    append_u64(static_cast<uint64_t>(data.size()));
    append_raw(data.data(), data.size());

    *element_size = 1;
    *is_float = false;
    *wire_tag = static_cast<uint32_t>(
        crate::CrateDataTypeId::CRATE_DATA_TYPE_TIME_SAMPLES);
    return true;
  }

  // String/token arrays: canonical variable-length serialization
  // (count + (len + bytes) per element) as the dedup key.
  if (auto* string_arr = cv.as<std::vector<std::string>>()) {
    size_t total = sizeof(uint64_t);
    for (const auto& s : *string_arr) total += sizeof(uint64_t) + s.size();
    bytes->clear();
    bytes->reserve(total);
    uint64_t count = string_arr->size();
    bytes->insert(bytes->end(), reinterpret_cast<const char*>(&count),
                  reinterpret_cast<const char*>(&count) + sizeof(uint64_t));
    for (const auto& s : *string_arr) {
      uint64_t len = s.size();
      bytes->insert(bytes->end(), reinterpret_cast<const char*>(&len),
                    reinterpret_cast<const char*>(&len) + sizeof(uint64_t));
      bytes->insert(bytes->end(), s.begin(), s.end());
    }
    *element_size = 1;
    *is_float = false;
    *wire_tag = kDedupArrayTagBit |
        static_cast<uint32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_STRING);
    return true;
  }
  if (auto* token_arr = cv.as<std::vector<value::token>>()) {
    size_t total = sizeof(uint64_t);
    for (const auto& t : *token_arr) total += sizeof(uint64_t) + t.str().size();
    bytes->clear();
    bytes->reserve(total);
    uint64_t count = token_arr->size();
    bytes->insert(bytes->end(), reinterpret_cast<const char*>(&count),
                  reinterpret_cast<const char*>(&count) + sizeof(uint64_t));
    for (const auto& t : *token_arr) {
      uint64_t len = t.str().size();
      bytes->insert(bytes->end(), reinterpret_cast<const char*>(&len),
                    reinterpret_cast<const char*>(&len) + sizeof(uint64_t));
      bytes->insert(bytes->end(), t.str().begin(), t.str().end());
    }
    *element_size = 1;
    *is_float = false;
    *wire_tag = kDedupArrayTagBit |
        static_cast<uint32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_TOKEN);
    return true;
  }
  // asset[] is serialized as the asset-path strings only (resolved paths are not
  // written), so the path text is the correct, complete dedup key.
  if (auto* asset_arr = cv.as<std::vector<value::AssetPath>>()) {
    size_t total = sizeof(uint64_t);
    for (const auto& a : *asset_arr) {
      total += sizeof(uint64_t) + a.GetAssetPath().size();
    }
    bytes->clear();
    bytes->reserve(total);
    uint64_t count = asset_arr->size();
    bytes->insert(bytes->end(), reinterpret_cast<const char*>(&count),
                  reinterpret_cast<const char*>(&count) + sizeof(uint64_t));
    for (const auto& a : *asset_arr) {
      const std::string& s = a.GetAssetPath();
      uint64_t len = s.size();
      bytes->insert(bytes->end(), reinterpret_cast<const char*>(&len),
                    reinterpret_cast<const char*>(&len) + sizeof(uint64_t));
      bytes->insert(bytes->end(), s.begin(), s.end());
    }
    *element_size = 1;
    *is_float = false;
    *wire_tag = kDedupArrayTagBit |
        static_cast<uint32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_ASSET_PATH);
    return true;
  }

  return false;
}

size_t CrateWriter::GetValueDedupBudgetBytes() const {
  if (options_.max_memory_bytes <= 0) {
    return (std::numeric_limits<size_t>::max)();
  }

  constexpr uint64_t kMaxDedupBudgetBytes = 512ull * 1024ull * 1024ull;
  uint64_t budget = static_cast<uint64_t>(options_.max_memory_bytes) / 4ull;
  if (budget > kMaxDedupBudgetBytes) {
    budget = kMaxDedupBudgetBytes;
  }

#if SIZE_MAX < UINT64_MAX
  if (budget > (std::numeric_limits<size_t>::max)()) {
    return (std::numeric_limits<size_t>::max)();
  }
#endif
  return static_cast<size_t>(budget);
}

bool CrateWriter::CanRetainDeduplicatedValue(size_t byte_count) const {
  size_t retained_bytes = 0;
  if (!safe::add(byte_count, sizeof(ValueDedupEntry), &retained_bytes)) {
    return false;
  }

  size_t next_dedup_bytes = 0;
  if (!safe::add(value_dedup_bytes_, retained_bytes, &next_dedup_bytes)) {
    return false;
  }
  if (next_dedup_bytes > GetValueDedupBudgetBytes()) {
    return false;
  }

  // Guard the int64_t cast below against size_t overflow. Only meaningful where
  // size_t can exceed INT64_MAX (64-bit); on 32-bit targets (e.g. wasm) size_t
  // never does, so the check is omitted to avoid a tautological-compare warning.
#if SIZE_MAX > INT64_MAX
  if (retained_bytes > static_cast<size_t>((std::numeric_limits<int64_t>::max)())) {
    return false;
  }
#endif
  return !WouldExceedMemoryLimit(static_cast<int64_t>(retained_bytes));
}

bool CrateWriter::LookupDeduplicatedValue(const std::vector<char>& bytes,
                                          size_t element_size, bool is_float,
                                          uint32_t wire_tag,
                                          crate::ValueRep* rep) const {
  const size_t h = NanAwareHash::combine(
      NanAwareHash::hash_buffer(bytes.data(), bytes.size(), element_size,
                                is_float),
      wire_tag);

  auto range = value_dedup_map_.equal_range(h);
  for (auto it = range.first; it != range.second; ++it) {
    const auto& entry = it->second;
    if (entry.wire_tag == wire_tag &&
        entry.bytes.size() == bytes.size() &&
        entry.element_size == element_size &&
        entry.is_float == is_float &&
        NanAwareHash::buffers_equal(entry.bytes.data(), bytes.data(),
                                    bytes.size(), element_size, is_float)) {
      if (rep) {
        *rep = crate::ValueRep(entry.rep_data);
      }
      return true;
    }
  }
  return false;
}

void CrateWriter::RetainDeduplicatedValue(size_t hash, std::vector<char> bytes,
                                          size_t element_size, bool is_float,
                                          uint32_t wire_tag,
                                          const crate::ValueRep& rep) {
  size_t retained_bytes = 0;
  if (!safe::add(bytes.size(), sizeof(ValueDedupEntry), &retained_bytes)) {
    return;
  }
  if (!CanRetainDeduplicatedValue(bytes.size())) {
    return;
  }

  value_dedup_bytes_ += retained_bytes;
  memory_used_estimate_ += static_cast<int64_t>(retained_bytes);
  value_dedup_map_.emplace(hash, ValueDedupEntry{
      std::move(bytes), element_size, is_float, wire_tag, rep.GetData(),
      retained_bytes});
}

int64_t CrateWriter::WriteValueData(const crate::CrateValue& value,
                                    bool* is_compressed,
                                    std::string* err) {
  if (is_compressed) {
    (*is_compressed) = false;
  }

  // Save current position
  int64_t current_pos = Tell();

  // Seek to end of value data section
  if (!Seek(value_data_end_offset_)) {
    if (err) *err = "Failed to seek to value data section";
    return -1;
  }

  int64_t value_offset = Tell();

  // Phase 1: Write out-of-line value data based on type
  // This handles values that cannot be inlined in the 48-bit payload

  // Double - 8 bytes
  if (auto* double_val = value.as<double>()) {
    if (!Write(*double_val)) {
      if (err) *err = "Failed to write double value";
      return -1;
    }
  }
  // Int64 - 8 bytes (when doesn't fit in 48 bits)
  else if (auto* int64_val = value.as<int64_t>()) {
    if (!Write(*int64_val)) {
      if (err) *err = "Failed to write int64 value";
      return -1;
    }
  }
  // UInt64 - 8 bytes (when doesn't fit in 48 bits)
  else if (auto* uint64_val = value.as<uint64_t>()) {
    if (!Write(*uint64_val)) {
      if (err) *err = "Failed to write uint64 value";
      return -1;
    }
  }
  // Scalar vector/matrix/quaternion macros
  // A. Indexable vectors (float/double/int 2/3/4)
#define WRITE_VEC_SCALAR(Type, N, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    for (size_t i = 0; i < N; ++i) { \
      if (!Write((*v)[i])) { \
        if (err) *err = "Failed to write " TypeName " component"; \
        return -1; \
      } \
    } \
  }
  // B. Half vector (needs .value suffix)
#define WRITE_HALFVEC_SCALAR(Type, N, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    for (size_t i = 0; i < N; ++i) { \
      if (!Write((*v)[i].value)) { \
        if (err) *err = "Failed to write " TypeName " component"; \
        return -1; \
      } \
    } \
  }
  // C. Matrices (binary-serializable struct, write as contiguous bytes)
#define WRITE_MATRIX_SCALAR(Type, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    if (!WriteBytes(v, sizeof(Type))) { \
      if (err) *err = "Failed to write " TypeName; \
      return -1; \
    } \
  }
  // D. Quaternions. Crate wire layout is [x, y, z, w] = (imag.x,
  // imag.y, imag.z, real). See value-types.hh:957 — note that USDA
  // (ASCII) uses the opposite [w, x, y, z] order at the textual layer.
  // tinyusdz's `value::quat{f,d}` struct matches the Crate layout
  // (`{imag, real}`), so emit imag first, then real.
#define WRITE_QUAT_SCALAR(Type, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    if (!Write(v->imag[0]) || !Write(v->imag[1]) || \
        !Write(v->imag[2]) || !Write(v->real)) { \
      if (err) *err = "Failed to write " TypeName " components"; \
      return -1; \
    } \
  }
  // E. Half-precision quaternion (needs .value on each component)
#define WRITE_QUATH_SCALAR(Type, TypeName) \
  else if (auto* v = value.as<Type>()) { \
    if (!Write(v->imag[0].value) || !Write(v->imag[1].value) || \
        !Write(v->imag[2].value) || !Write(v->real.value)) { \
      if (err) *err = "Failed to write " TypeName " components"; \
      return -1; \
    } \
  }

  WRITE_VEC_SCALAR(value::float2, 2, "Vec2f")
  WRITE_VEC_SCALAR(value::double2, 2, "Vec2d")
  WRITE_VEC_SCALAR(value::int2, 2, "Vec2i")
  WRITE_VEC_SCALAR(value::float3, 3, "Vec3f")
  WRITE_VEC_SCALAR(value::double3, 3, "Vec3d")
  WRITE_VEC_SCALAR(value::int3, 3, "Vec3i")
  WRITE_HALFVEC_SCALAR(value::half2, 2, "Vec2h")
  WRITE_HALFVEC_SCALAR(value::half3, 3, "Vec3h")
  WRITE_HALFVEC_SCALAR(value::half4, 4, "Vec4h")
  WRITE_VEC_SCALAR(value::float4, 4, "Vec4f")
  WRITE_VEC_SCALAR(value::double4, 4, "Vec4d")
  WRITE_VEC_SCALAR(value::int4, 4, "Vec4i")
  WRITE_MATRIX_SCALAR(value::matrix2d, "Matrix2d")
  WRITE_MATRIX_SCALAR(value::matrix3d, "Matrix3d")
  WRITE_MATRIX_SCALAR(value::matrix4d, "Matrix4d")
  WRITE_QUATH_SCALAR(value::quath, "Quath")
  WRITE_QUAT_SCALAR(value::quatf, "Quatf")
  WRITE_QUAT_SCALAR(value::quatd, "Quatd")

#undef WRITE_VEC_SCALAR
#undef WRITE_HALFVEC_SCALAR
#undef WRITE_MATRIX_SCALAR
#undef WRITE_QUAT_SCALAR
#undef WRITE_QUATH_SCALAR
  // Phase 1: Array serialization
  // Arrays are written as: uint64_t count + elements
  // For bool arrays, each bool is written as 1 byte

  // Bool array - special handling (each bool = 1 byte)
  else if (auto* bool_array = value.as<std::vector<bool>>()) {
    uint64_t count = bool_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write bool array count";
      return -1;
    }
    // Write each bool as a byte
    for (bool b : *bool_array) {
      uint8_t byte = b ? 1 : 0;
      if (!Write(byte)) {
        if (err) *err = "Failed to write bool array element";
        return -1;
      }
    }
  }
  // Byte array
  else if (auto* byte_array = value.as<std::vector<uint8_t>>()) {
    uint64_t count = byte_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write byte array count";
      return -1;
    }
    if (count > 0 && !WriteBytes(byte_array->data(), count * sizeof(uint8_t))) {
      if (err) *err = "Failed to write byte array data";
      return -1;
    }
  }
  // Int array
  // Integer arrays — compressed via WriteCompressedArray32/64 helpers
  else if (auto* int_array = value.as<std::vector<int32_t>>()) {
    uint64_t count = int_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write int array count"; return -1; }
    // int32 data is bit-compatible with uint32 for compression
    if (WriteCompressedArray32(reinterpret_cast<const uint32_t*>(int_array->data()),
                               count, "int", is_compressed, err) < 0) {
      return -1;
    }
  }
  else if (auto* uint_array = value.as<std::vector<uint32_t>>()) {
    uint64_t count = uint_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write uint array count"; return -1; }
    if (WriteCompressedArray32(uint_array->data(), count, "uint",
                               is_compressed, err) < 0) {
      return -1;
    }
  }
  else if (auto* int64_array = value.as<std::vector<int64_t>>()) {
    uint64_t count = int64_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write int64 array count"; return -1; }
    if (WriteCompressedArray64(reinterpret_cast<const uint64_t*>(int64_array->data()),
                               count, "int64", is_compressed, err) < 0) {
      return -1;
    }
  }
  else if (auto* uint64_array = value.as<std::vector<uint64_t>>()) {
    uint64_t count = uint64_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write uint64 array count"; return -1; }
    if (WriteCompressedArray64(uint64_array->data(), count, "uint64",
                               is_compressed, err) < 0) {
      return -1;
    }
  }
  // Half array — convert to uint32 for compression
  else if (auto* half_array = value.as<std::vector<value::half>>()) {
    uint64_t count = half_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write half array count"; return -1; }
    for (const auto& val : *half_array) {
      if (!Write(val.value)) {
        if (err) *err = "Failed to write half array element";
        return -1;
      }
    }
  }
  // Float arrays use a tagged compression format in the reader.
  // Until the writer emits that exact format, keep them uncompressed.
  else if (auto* float_array = value.as<std::vector<float>>()) {
    uint64_t count = float_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write float array count"; return -1; }
    size_t byte_count;
    if (!safe::mul(float_array->size(), sizeof(float), &byte_count)) {
      if (err) *err = "Integer overflow: float_array->size() * sizeof(float)";
      return -1;
    }
    if (!WriteBytes(float_array->data(), byte_count)) {
      if (err) *err = "Failed to write float array data";
      return -1;
    }
  }
  // Double arrays use a tagged compression format in the reader.
  // Until the writer emits that exact format, keep them uncompressed.
  else if (auto* double_array = value.as<std::vector<double>>()) {
    uint64_t count = double_array->size();
    if (!Write(count)) { if (err) *err = "Failed to write double array count"; return -1; }
    size_t byte_count;
    if (!safe::mul(double_array->size(), sizeof(double), &byte_count)) {
      if (err) *err = "Integer overflow: double_array->size() * sizeof(double)";
      return -1;
    }
    if (!WriteBytes(double_array->data(), byte_count)) {
      if (err) *err = "Failed to write double array data";
      return -1;
    }
  }
  // Vector/Quaternion array macros
#define WRITE_VEC_ARRAY(ElemType, N, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& elem : *arr) { \
      for (size_t i = 0; i < N; ++i) { \
        if (!Write(elem[i])) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
      } \
    } \
  }
  // Same [x, y, z, w] = (imag, real) Crate layout as the scalar path
  // above (see value-types.hh:957). Could memcpy since our struct
  // matches; per-component for symmetry.
#define WRITE_QUAT_ARRAY(ElemType, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& q : *arr) { \
      bool qok = Write(q.imag[0]) && Write(q.imag[1]) && Write(q.imag[2]) && Write(q.real); \
      if (!qok) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
    } \
  }
#define WRITE_QUATH_ARRAY(ElemType, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& q : *arr) { \
      bool qok = Write(q.imag[0].value) && Write(q.imag[1].value) && Write(q.imag[2].value) && Write(q.real.value); \
      if (!qok) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
    } \
  }

  WRITE_VEC_ARRAY(value::float2, 2, "Vec2f")
  WRITE_VEC_ARRAY(value::float3, 3, "Vec3f")
  WRITE_VEC_ARRAY(value::float4, 4, "Vec4f")
  WRITE_VEC_ARRAY(value::double2, 2, "Vec2d")
  WRITE_VEC_ARRAY(value::double3, 3, "Vec3d")
  WRITE_VEC_ARRAY(value::double4, 4, "Vec4d")
  WRITE_VEC_ARRAY(value::int2, 2, "Vec2i")
  WRITE_VEC_ARRAY(value::int3, 3, "Vec3i")
  WRITE_VEC_ARRAY(value::int4, 4, "Vec4i")
  WRITE_VEC_ARRAY(value::uint2, 2, "Vec2u")
  WRITE_VEC_ARRAY(value::uint3, 3, "Vec3u")
  WRITE_VEC_ARRAY(value::uint4, 4, "Vec4u")
  WRITE_QUATH_ARRAY(value::quath, "Quath")
  WRITE_QUAT_ARRAY(value::quatf, "Quatf")
  WRITE_QUAT_ARRAY(value::quatd, "Quatd")

  // Half-vector arrays — write raw uint16 per component
#define WRITE_HALFVEC_ARRAY(ElemType, N, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    for (const auto& elem : *arr) { \
      for (size_t i = 0; i < N; ++i) { \
        if (!Write(elem[i].value)) { if (err) *err = "Failed to write " TypeName " array element"; return -1; } \
      } \
    } \
  }

  WRITE_HALFVEC_ARRAY(value::half2, 2, "Half2")
  WRITE_HALFVEC_ARRAY(value::half3, 3, "Half3")
  WRITE_HALFVEC_ARRAY(value::half4, 4, "Half4")

  // Matrix arrays — write raw doubles (sizeof(ElemType) doubles per element)
#define WRITE_MATRIX_ARRAY(ElemType, TypeName) \
  else if (auto* arr = value.as<std::vector<ElemType>>()) { \
    uint64_t count = arr->size(); \
    if (!Write(count)) { if (err) *err = "Failed to write " TypeName " array count"; return -1; } \
    if (count > 0 && !WriteBytes(arr->data(), count * sizeof(ElemType))) { \
      if (err) *err = "Failed to write " TypeName " array data"; \
      return -1; \
    } \
  }

  WRITE_MATRIX_ARRAY(value::matrix2d, "Matrix2d")
  WRITE_MATRIX_ARRAY(value::matrix3d, "Matrix3d")
  WRITE_MATRIX_ARRAY(value::matrix4d, "Matrix4d")

#undef WRITE_VEC_ARRAY
#undef WRITE_QUAT_ARRAY
#undef WRITE_QUATH_ARRAY
#undef WRITE_HALFVEC_ARRAY
#undef WRITE_MATRIX_ARRAY
  // String array - special handling (strings are stored as indices)
  else if (auto* string_array = value.as<std::vector<std::string>>()) {
    uint64_t count = string_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write string array count";
      return -1;
    }
    for (const auto& str : *string_array) {
      crate::StringIndex idx = GetOrCreateString(str);
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write string array element index";
        return -1;
      }
    }
  }
  // Asset array - reader expects StringIndex elements (uninlined/array path
  // in crate-reader-values.cc). Note inlined scalar AssetPath uses a
  // TokenIndex; arrays use StringIndex. See the asymmetric reader at
  // CRATE_DATA_TYPE_ASSET_PATH.
  else if (auto* asset_array = value.as<std::vector<value::AssetPath>>()) {
    uint64_t count = asset_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write asset[] count";
      return -1;
    }
    for (const auto& ap : *asset_array) {
      crate::StringIndex idx = GetOrCreateString(ap.GetAssetPath());
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write asset[] element index";
        return -1;
      }
    }
  }
  // Token array - special handling (tokens are stored as indices)
  else if (auto* token_array = value.as<std::vector<value::token>>()) {
    uint64_t count = token_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write token array count";
      return -1;
    }
    for (const auto& tok : *token_array) {
      crate::TokenIndex idx = GetOrCreateToken(tok.str());
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write token array element index";
        return -1;
      }
    }
  }
  // Path array - special handling (paths are stored as indices)
  else if (auto* path_array = value.as<std::vector<Path>>()) {
    uint64_t count = path_array->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write path array count";
      return -1;
    }
    for (const auto& path : *path_array) {
      crate::PathIndex idx = GetOrCreatePath(path);
      if (!Write(idx.value)) {
        if (err) *err = "Failed to write path array element index";
        return -1;
      }
    }
  }
  // Phase 2: Dictionary serialization
  // Dictionary format (recursive offset pattern as expected by TinyUSDZ reader):
  // uint64_t count + for each: (StringIndex key, int64_t offset, ValueRep)
  // The offset is relative to the position after reading the offset field.
  // offset=8 means ValueRep immediately follows the offset field.
  //
  // IMPORTANT: We must reserve space for the entire dictionary structure FIRST,
  // then call PackValue for each value. Otherwise, nested PackValue calls for
  // out-of-line values would write to value_data_end_offset_ which still points
  // to the start of the dictionary, corrupting the data.
  else if (auto* dict_val = value.as<value::dict>()) {
    uint64_t count = dict_val->size();

    // Calculate size of dictionary structure:
    // 8 bytes for count + (4 + 8 + 8) bytes per entry = 8 + 20*count
    int64_t dict_struct_size = 8 + (count * 20);
    int64_t dict_struct_start = Tell();

    // Reserve space by writing zeros
    {
      std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
      if (!WriteBytes(zeros.data(), zeros.size())) {
        if (err) *err = "Failed to reserve dictionary space";
        return -1;
      }
    }

    // Update value_data_end_offset_ NOW, before calling PackValue
    // This ensures nested writes go AFTER the dictionary structure
    value_data_end_offset_ = Tell();

    // Now pack all values first (this may write out-of-line data after dict structure)
    // IMPORTANT: PackValue calls will further update value_data_end_offset_,
    // so we need to capture the final end position before seeking back
    std::vector<crate::ValueRep> value_reps;
    value_reps.reserve(count);

    // Dict value packing: any_value_cast dispatch (value::dict uses any_value)
#define TRY_PACK_DICT(Type) \
      else if (auto* typed = value::any_value_cast<Type>(&kv.second)) { \
        crate::CrateValue cv; cv.Set(*typed); \
        value_rep = PackValue(cv, err); value_packed = true; }

    for (const auto& kv : *dict_val) {
      crate::ValueRep value_rep;
      bool value_packed = false;

      if (auto* v = value::any_value_cast<int32_t>(&kv.second)) {
        crate::CrateValue cv; cv.Set(*v);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      else if (auto* v = value::any_value_cast<int>(&kv.second)) {
        crate::CrateValue cv; cv.Set(static_cast<int32_t>(*v));
        value_rep = PackValue(cv, err); value_packed = true;
      }
      TRY_PACK_DICT(uint32_t)
      TRY_PACK_DICT(float)
      TRY_PACK_DICT(double)
      TRY_PACK_DICT(bool)
      TRY_PACK_DICT(std::string)
      TRY_PACK_DICT(value::token)
      else {
        if (err) *err = "Unsupported dictionary value type";
        return -1;
      }
#undef TRY_PACK_DICT

      if (!value_packed) {
        if (err) *err = "Failed to pack dictionary value";
        return -1;
      }

      value_reps.push_back(value_rep);
    }

    // Now go back and write the dictionary structure
    if (!Seek(dict_struct_start)) {
      if (err) *err = "Failed to seek to dictionary structure start";
      return -1;
    }

    // Write count
    if (!Write(count)) {
      if (err) *err = "Failed to write dictionary count";
      return -1;
    }

    // Write each (key, offset, ValueRep) tuple
    size_t idx = 0;
    for (const auto& kv : *dict_val) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write dictionary key index";
        return -1;
      }

      // Write offset = 8 (ValueRep immediately follows the offset)
      int64_t offset = 8;
      if (!Write(offset)) {
        if (err) *err = "Failed to write dictionary value offset";
        return -1;
      }

      // Write ValueRep
      if (!Write(value_reps[idx].GetData())) {
        if (err) *err = "Failed to write dictionary ValueRep";
        return -1;
      }

      ++idx;
    }

    // IMPORTANT: Seek to value_data_end_offset_, not saved_pos!
    // value_data_end_offset_ has been updated by all the PackValue calls to point to
    // the end of all written data. saved_pos was captured before we packed values.
    if (!Seek(value_data_end_offset_)) {
      if (err) *err = "Failed to seek to end of value data";
      return -1;
    }
  }
  // Phase 2: CustomDataType serialization (similar to dictionary but uses MetaVariable)
  // CustomDataType = std::map<std::string, MetaVariable> where MetaVariable wraps value::Value
  // Uses same offset pattern as dictionary: StringIndex key + int64_t offset, ValueRep
  //
  // IMPORTANT: We must reserve space for the entire dictionary structure FIRST,
  // then call PackValue for each value. Otherwise, nested PackValue calls for
  // out-of-line values would write to value_data_end_offset_ which still points
  // to the start of the dictionary, corrupting the data.
  else if (auto* custom_data = value.as<CustomDataType>()) {
    uint64_t count = custom_data->size();

    // Calculate size of dictionary structure:
    // 8 bytes for count + (4 + 8 + 8) bytes per entry = 8 + 20*count
    int64_t dict_struct_size = 8 + (count * 20);
    int64_t dict_struct_start = Tell();

    // Reserve space by writing zeros
    {
      std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
      if (!WriteBytes(zeros.data(), zeros.size())) {
        if (err) *err = "Failed to reserve CustomDataType space";
        return -1;
      }
    }

    // Update value_data_end_offset_ NOW, before calling PackValue
    // This ensures nested writes go AFTER the dictionary structure
    value_data_end_offset_ = Tell();

    // Now pack all values first (this may write out-of-line data after dict structure)
    // IMPORTANT: PackValue calls will further update value_data_end_offset_,
    // so we need to capture the final end position before seeking back
    std::vector<crate::ValueRep> value_reps;
    value_reps.reserve(count);

    for (const auto& kv : *custom_data) {
      // Get the raw value::Value from MetaVariable and pack it
      const value::Value& raw_value = kv.second.get_raw_value();
      crate::ValueRep value_rep;
      bool value_packed = false;

      // CustomDataType value packing: value::Value dispatch
#define TRY_PACK_AS(Type) \
      else if (auto* typed = raw_value.as<Type>()) { \
        crate::CrateValue cv; cv.Set(*typed); \
        value_rep = PackValue(cv, err); value_packed = true; }

      if (auto* v = raw_value.as<int32_t>()) {
        crate::CrateValue cv; cv.Set(*v);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      else if (auto* v = raw_value.as<int>()) {
        crate::CrateValue cv; cv.Set(static_cast<int32_t>(*v));
        value_rep = PackValue(cv, err); value_packed = true;
      }
      TRY_PACK_AS(uint32_t)
      TRY_PACK_AS(int64_t)
      TRY_PACK_AS(uint64_t)
      TRY_PACK_AS(value::half)
      TRY_PACK_AS(float)
      TRY_PACK_AS(double)
      TRY_PACK_AS(bool)
      TRY_PACK_AS(std::string)
      // StringData: extract string value from metadata wrapper
      else if (auto* sd = raw_value.as<value::StringData>()) {
        crate::CrateValue cv; cv.Set(sd->value);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      // Fallback: type_id says string but as<string>() failed
      else if (raw_value.type_id() == value::TYPE_ID_STRING) {
        auto str_opt = kv.second.get_value<std::string>();
        if (str_opt) {
          crate::CrateValue cv; cv.Set(*str_opt);
          value_rep = PackValue(cv, err); value_packed = true;
        }
      }
      TRY_PACK_AS(value::token)
      TRY_PACK_AS(CustomDataType)
      TRY_PACK_AS(std::vector<std::string>)
      // StringData array: convert to string array
      else if (auto* sda = raw_value.as<std::vector<value::StringData>>()) {
        std::vector<std::string> sa;
        sa.reserve(sda->size());
        for (const auto& sd : *sda) { sa.push_back(sd.value); }
        crate::CrateValue cv; cv.Set(sa);
        value_rep = PackValue(cv, err); value_packed = true;
      }
      // Fallback: string array via type_name or is_array+TYPE_ID_STRING
      else if (raw_value.type_name() == "string[]" || (raw_value.is_array() && raw_value.type_id() == value::TYPE_ID_STRING)) {
        if (auto* sa = raw_value.as<std::vector<std::string>>()) {
          crate::CrateValue cv; cv.Set(*sa);
          value_rep = PackValue(cv, err); value_packed = true;
        } else if (auto* ta = raw_value.as<std::vector<value::token>>()) {
          std::vector<std::string> sa;
          sa.reserve(ta->size());
          for (const auto& t : *ta) { sa.push_back(t.str()); }
          crate::CrateValue cv; cv.Set(sa);
          value_rep = PackValue(cv, err); value_packed = true;
        } else {
          auto opt = kv.second.get_value<std::vector<std::string>>();
          if (opt) {
            crate::CrateValue cv; cv.Set(*opt);
            value_rep = PackValue(cv, err); value_packed = true;
          }
        }
      }
      TRY_PACK_AS(std::vector<value::token>)
      TRY_PACK_AS(std::vector<float>)
      TRY_PACK_AS(std::vector<double>)
      TRY_PACK_AS(std::vector<int32_t>)
      TRY_PACK_AS(std::vector<bool>)
      TRY_PACK_AS(std::vector<uint32_t>)
      TRY_PACK_AS(std::vector<int64_t>)
      TRY_PACK_AS(std::vector<uint64_t>)
      TRY_PACK_AS(std::vector<value::half>)
      // Vec scalars (used by clips dict: double2 active/times entries, etc.)
      TRY_PACK_AS(value::float2)
      TRY_PACK_AS(value::float3)
      TRY_PACK_AS(value::float4)
      TRY_PACK_AS(value::double2)
      TRY_PACK_AS(value::double3)
      TRY_PACK_AS(value::double4)
      TRY_PACK_AS(value::int2)
      TRY_PACK_AS(value::int3)
      TRY_PACK_AS(value::int4)
      // Vec arrays
      TRY_PACK_AS(std::vector<value::float2>)
      TRY_PACK_AS(std::vector<value::float3>)
      TRY_PACK_AS(std::vector<value::float4>)
      TRY_PACK_AS(std::vector<value::double2>)
      TRY_PACK_AS(std::vector<value::double3>)
      TRY_PACK_AS(std::vector<value::double4>)
      TRY_PACK_AS(std::vector<value::int2>)
      TRY_PACK_AS(std::vector<value::int3>)
      TRY_PACK_AS(std::vector<value::int4>)
      // Asset paths (clips: assetPaths, manifestAssetPath)
      TRY_PACK_AS(value::AssetPath)
      TRY_PACK_AS(std::vector<value::AssetPath>)
      else {
        if (err) *err = "Unsupported CustomDataType value type: " + std::string(raw_value.type_name()) + " (type_id=" + std::to_string(raw_value.type_id()) + ")";
        return -1;
      }
#undef TRY_PACK_AS

      if (!value_packed) {
        if (err) *err = "Failed to pack CustomDataType value";
        return -1;
      }

      value_reps.push_back(value_rep);
    }

    // Now go back and write the dictionary structure
    if (!Seek(dict_struct_start)) {
      if (err) *err = "Failed to seek to dictionary structure start";
      return -1;
    }

    // Write count
    if (!Write(count)) {
      if (err) *err = "Failed to write CustomDataType count";
      return -1;
    }
    // Write each (key, offset, ValueRep) tuple
    size_t idx = 0;
    for (const auto& kv : *custom_data) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write CustomDataType key index";
        return -1;
      }

      // Write offset = 8 (ValueRep immediately follows the offset)
      int64_t offset = 8;
      if (!Write(offset)) {
        if (err) *err = "Failed to write CustomDataType value offset";
        return -1;
      }

      // Write ValueRep
      if (!Write(value_reps[idx].GetData())) {
        if (err) *err = "Failed to write CustomDataType ValueRep";
        return -1;
      }

      ++idx;
    }

    // IMPORTANT: Seek to value_data_end_offset_, not saved_pos!
    // value_data_end_offset_ has been updated by all the PackValue calls to point to
    // the end of all written data.
    if (!Seek(value_data_end_offset_)) {
      if (err) *err = "Failed to seek back after CustomDataType";
      return -1;
    }
  }
  // ListOp serialization macros
  // Dispatches all 6 list categories (explicit, added, prepended, appended, deleted, ordered)
#define WRITE_LISTOP_ITEMS(listop_ptr, writeListFn, TypeName) \
    if (listop_ptr->HasExplicitItems() && !writeListFn(listop_ptr->GetExplicitItems())) { \
      if (err) { *err = "Failed to write " TypeName " explicit items"; } \
      return -1; } \
    if (listop_ptr->HasAddedItems() && !writeListFn(listop_ptr->GetAddedItems())) { \
      if (err) { *err = "Failed to write " TypeName " added items"; } \
      return -1; } \
    if (listop_ptr->HasPrependedItems() && !writeListFn(listop_ptr->GetPrependedItems())) { \
      if (err) { *err = "Failed to write " TypeName " prepended items"; } \
      return -1; } \
    if (listop_ptr->HasAppendedItems() && !writeListFn(listop_ptr->GetAppendedItems())) { \
      if (err) { *err = "Failed to write " TypeName " appended items"; } \
      return -1; } \
    if (listop_ptr->HasDeletedItems() && !writeListFn(listop_ptr->GetDeletedItems())) { \
      if (err) { *err = "Failed to write " TypeName " deleted items"; } \
      return -1; } \
    if (listop_ptr->HasOrderedItems() && !writeListFn(listop_ptr->GetOrderedItems())) { \
      if (err) { *err = "Failed to write " TypeName " ordered items"; } \
      return -1; }

  // TokenListOp
  else if (auto* token_listop = value.as<ListOp<value::token>>()) {
    auto header = BuildListOpHeader(*token_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write TokenListOp header"; return -1; }
    auto writeTokenList = [&](const std::vector<value::token>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& tok : list) {
        crate::TokenIndex idx = GetOrCreateToken(tok.str());
        if (!Write(idx.value)) return false;
      }
      return true;
    };
    WRITE_LISTOP_ITEMS(token_listop, writeTokenList, "TokenListOp")
  }
  // StringListOp
  else if (auto* string_listop = value.as<ListOp<std::string>>()) {
    auto header = BuildListOpHeader(*string_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write StringListOp header"; return -1; }
    auto writeStringList = [&](const std::vector<std::string>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& str : list) {
        crate::StringIndex idx = GetOrCreateString(str);
        if (!Write(idx.value)) return false;
      }
      return true;
    };
    WRITE_LISTOP_ITEMS(string_listop, writeStringList, "StringListOp")
  }
  // PathListOp
  else if (auto* path_listop = value.as<ListOp<Path>>()) {
    auto header = BuildListOpHeader(*path_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write PathListOp header"; return -1; }
    auto writePathList = [&](const std::vector<Path>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& path : list) {
        crate::PathIndex idx = GetOrCreatePath(path);
        if (!Write(idx.value)) return false;
      }
      return true;
    };
    WRITE_LISTOP_ITEMS(path_listop, writePathList, "PathListOp")
  }
  // Reference serialization
  else if (auto* ref_val = value.as<Reference>()) {
    // Reference format: StringIndex (asset_path) + PathIndex (prim_path) + LayerOffset (2 doubles) + Dictionary (customData)

    // Write asset path as StringIndex
    crate::StringIndex asset_idx = GetOrCreateString(ref_val->asset_path.GetAssetPath());
    if (!Write(asset_idx.value)) {
      if (err) *err = "Failed to write Reference asset_path";
      return -1;
    }

    // Write prim path as PathIndex
    crate::PathIndex prim_idx = GetOrCreatePath(ref_val->prim_path);
    if (!Write(prim_idx.value)) {
      if (err) *err = "Failed to write Reference prim_path";
      return -1;
    }

    // Write LayerOffset (2 doubles)
    if (!Write(ref_val->layerOffset._offset)) {
      if (err) *err = "Failed to write Reference LayerOffset offset";
      return -1;
    }
    if (!Write(ref_val->layerOffset._scale)) {
      if (err) *err = "Failed to write Reference LayerOffset scale";
      return -1;
    }

    // Write customData dictionary using the same reserve-pack-seek
    // pattern as top-level Dictionary serialization (see line ~1929).
    // PackValue may write out-of-line value data (doubles, int64 above
    // 2^47, large arrays) — we reserve dict frame first, then pack
    // (allowing nested writes to land after the frame), then come back
    // to fill in the (StringIndex key, int64 offset, ValueRep) tuples.
    {
      uint64_t dict_count = ref_val->customData.size();
      int64_t dict_struct_size = 8 + (int64_t)(dict_count * 20);
      int64_t dict_struct_start = Tell();

      // Reserve.
      {
        std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
        if (!WriteBytes(zeros.data(), zeros.size())) {
          if (err) *err = "Failed to reserve Reference customData space";
          return -1;
        }
      }
      value_data_end_offset_ = Tell();

      // Pack.
      std::vector<crate::ValueRep> value_reps;
      value_reps.reserve(dict_count);
      for (const auto& kv : ref_val->customData) {
        crate::ValueRep value_rep;
        bool packed = false;
        if (auto v = kv.second.get_value<int32_t>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<float>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<double>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<bool>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<std::string>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto sd = kv.second.get_value<value::StringData>()) {
          crate::CrateValue cv; cv.Set(sd->value);
          value_rep = PackValue(cv, err); packed = true;
        }
        else if (auto v = kv.second.get_value<int64_t>()) {
          crate::CrateValue cv; cv.Set(*v);
          value_rep = PackValue(cv, err); packed = true;
        }
        if (!packed) {
          if (err) *err = "Unsupported Reference customData value type: "
                          + kv.second.type_name();
          return -1;
        }
        value_reps.push_back(value_rep);
      }

      // Fill in dict frame.
      if (!Seek(dict_struct_start)) {
        if (err) *err = "Failed to seek to Reference customData start";
        return -1;
      }
      if (!Write(dict_count)) {
        if (err) *err = "Failed to write Reference customData count";
        return -1;
      }
      size_t idx = 0;
      for (const auto& kv : ref_val->customData) {
        if (!Write(GetOrCreateString(kv.first).value)) {
          if (err) *err = "Failed to write Reference customData key";
          return -1;
        }
        const int64_t offset = 8;
        if (!Write(offset)) {
          if (err) *err = "Failed to write Reference customData offset";
          return -1;
        }
        if (!Write(value_reps[idx].GetData())) {
          if (err) *err = "Failed to write Reference customData value";
          return -1;
        }
        ++idx;
      }

      // Resume at end of out-of-line value data.
      if (!Seek(value_data_end_offset_)) {
        if (err) *err = "Failed to seek past Reference customData value data";
        return -1;
      }
    }
  }
  // Payload serialization
  else if (auto* payload_val = value.as<Payload>()) {
    // Payload format: StringIndex (asset_path) + PathIndex (prim_path) + LayerOffset (2 doubles)

    // Write asset path as StringIndex
    crate::StringIndex asset_idx = GetOrCreateString(payload_val->asset_path.GetAssetPath());
    if (!Write(asset_idx.value)) {
      if (err) *err = "Failed to write Payload asset_path";
      return -1;
    }

    // Write prim path as PathIndex
    crate::PathIndex prim_idx = GetOrCreatePath(payload_val->prim_path);
    if (!Write(prim_idx.value)) {
      if (err) *err = "Failed to write Payload prim_path";
      return -1;
    }

    // Write LayerOffset (2 doubles)
    if (!Write(payload_val->layerOffset._offset)) {
      if (err) *err = "Failed to write Payload LayerOffset offset";
      return -1;
    }
    if (!Write(payload_val->layerOffset._scale)) {
      if (err) *err = "Failed to write Payload LayerOffset scale";
      return -1;
    }
  }
  // ReferenceListOp serialization
  else if (auto* ref_listop = value.as<ListOp<Reference>>()) {
    auto header = BuildListOpHeader(*ref_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write ReferenceListOp header"; return -1; }

    auto writeRefList = [&](const std::vector<Reference>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& ref : list) {
        // Write each Reference inline (same format as single Reference above)
        crate::StringIndex asset_idx = GetOrCreateString(ref.asset_path.GetAssetPath());
        if (!Write(asset_idx.value)) return false;

        crate::PathIndex prim_idx = GetOrCreatePath(ref.prim_path);
        if (!Write(prim_idx.value)) return false;

        if (!Write(ref.layerOffset._offset)) return false;
        if (!Write(ref.layerOffset._scale)) return false;

        // Write customData dictionary using reserve-pack-seek pattern
        // (same as single-Reference + top-level Dictionary): tolerates
        // out-of-line PackValue writes for doubles, large int64,
        // arrays, etc.
        {
          uint64_t dict_count = ref.customData.size();
          int64_t dict_struct_size = 8 + (int64_t)(dict_count * 20);
          int64_t dict_struct_start = Tell();

          {
            std::vector<char> zeros(static_cast<size_t>(dict_struct_size), 0);
            if (!WriteBytes(zeros.data(), zeros.size())) return false;
          }
          value_data_end_offset_ = Tell();

          std::vector<crate::ValueRep> value_reps;
          value_reps.reserve(dict_count);
          for (const auto& kv : ref.customData) {
            crate::ValueRep value_rep;
            bool value_written = false;
            if (auto v = kv.second.get_value<int32_t>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<float>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<double>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<bool>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<std::string>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto sd = kv.second.get_value<value::StringData>()) {
              crate::CrateValue cv; cv.Set(sd->value);
              value_rep = PackValue(cv, err); value_written = true;
            }
            else if (auto v = kv.second.get_value<int64_t>()) {
              crate::CrateValue cv; cv.Set(*v);
              value_rep = PackValue(cv, err); value_written = true;
            }
            if (!value_written) {
              if (err) *err = "Unsupported Reference customData value type: "
                              + kv.second.type_name();
              return false;
            }
            value_reps.push_back(value_rep);
          }

          if (!Seek(dict_struct_start)) return false;
          if (!Write(dict_count)) return false;
          size_t idx = 0;
          for (const auto& kv : ref.customData) {
            if (!Write(GetOrCreateString(kv.first).value)) return false;
            const int64_t offset = 8;
            if (!Write(offset)) return false;
            if (!Write(value_reps[idx].GetData())) return false;
            ++idx;
          }
          if (!Seek(value_data_end_offset_)) return false;
        }
      }
      return true;
    };

    WRITE_LISTOP_ITEMS(ref_listop, writeRefList, "ReferenceListOp")
  }
  // PayloadListOp serialization
  else if (auto* payload_listop = value.as<ListOp<Payload>>()) {
    auto header = BuildListOpHeader(*payload_listop);
    if (!Write(header.bits)) { if (err) *err = "Failed to write PayloadListOp header"; return -1; }

    auto writePayloadList = [&](const std::vector<Payload>& list) -> bool {
      uint64_t count = list.size();
      if (!Write(count)) return false;
      for (const auto& payload : list) {
        crate::StringIndex asset_idx = GetOrCreateString(payload.asset_path.GetAssetPath());
        if (!Write(asset_idx.value)) return false;

        crate::PathIndex prim_idx = GetOrCreatePath(payload.prim_path);
        if (!Write(prim_idx.value)) return false;

        if (!Write(payload.layerOffset._offset)) return false;
        if (!Write(payload.layerOffset._scale)) return false;
      }
      return true;
    };

    WRITE_LISTOP_ITEMS(payload_listop, writePayloadList, "PayloadListOp")
  }
  // Simple value ListOps (int32, uint32, int64, uint64) — items written directly
#define WRITE_VALUE_LISTOP(ItemType, TypeName) \
  else if (auto* listop_##TypeName = value.as<ListOp<ItemType>>()) { \
    auto header = BuildListOpHeader(*listop_##TypeName); \
    if (!Write(header.bits)) { if (err) *err = "Failed to write " #TypeName "ListOp header"; return -1; } \
    auto writeList = [&](const std::vector<ItemType>& list) -> bool { \
      uint64_t cnt = list.size(); \
      if (!Write(cnt)) return false; \
      for (const auto& val : list) { if (!Write(val)) return false; } \
      return true; \
    }; \
    WRITE_LISTOP_ITEMS(listop_##TypeName, writeList, #TypeName "ListOp") \
  }

  WRITE_VALUE_LISTOP(int32_t, Int)
  WRITE_VALUE_LISTOP(uint32_t, UInt)
  WRITE_VALUE_LISTOP(int64_t, Int64)
  WRITE_VALUE_LISTOP(uint64_t, UInt64)

#undef WRITE_VALUE_LISTOP
#undef WRITE_LISTOP_ITEMS
  // VariantSelectionMap serialization
  else if (auto* variant_map = value.as<VariantSelectionMap>()) {
    // VariantSelectionMap format: uint64_t count + (StringIndex key, StringIndex value) pairs

    uint64_t count = variant_map->size();
    if (!Write(count)) {
      if (err) *err = "Failed to write VariantSelectionMap count";
      return -1;
    }

    for (const auto& kv : *variant_map) {
      // Write key as StringIndex
      crate::StringIndex key_idx = GetOrCreateString(kv.first);
      if (!Write(key_idx.value)) {
        if (err) *err = "Failed to write VariantSelectionMap key";
        return -1;
      }

      // Write value as StringIndex
      crate::StringIndex val_idx = GetOrCreateString(kv.second);
      if (!Write(val_idx.value)) {
        if (err) *err = "Failed to write VariantSelectionMap value";
        return -1;
      }
    }
  }
  // Phase 5: TimeSamples with value serialization
  else if (auto* timesamples_val = value.as<value::TimeSamples>()) {
    // TimeSamples format (recursive value pattern):
    // 1. int64_t offset1 (indirection to times_rep)
    // 2. ValueRep times_rep (type=double[], payload=offset to times data)
    // 3. int64_t offset2 (indirection to values data)
    // 4. [in value data] times data: uint64_t count + double[]
    // 5. [at offset2] values data: uint64_t count + ValueRep[]

    uint64_t num_samples = static_cast<uint64_t>(timesamples_val->size());

    // === Step 1: Write initial indirection offset (points 8 bytes ahead to times_rep) ===
    int64_t indirection_offset = 8;
    if (!Write(indirection_offset)) {
      if (err) *err = "Failed to write TimeSamples indirection offset";
      return -1;
    }

    // === Step 2: Reserve space for times_rep ValueRep (will write later) ===
    int64_t times_rep_pos = Tell();  // Position 92 (value_offset + 8)
    uint64_t placeholder = 0;
    if (!Write(placeholder)) {  // Reserve 8 bytes for times_rep
      if (err) *err = "Failed to reserve space for times ValueRep";
      return -1;
    }
    // DON'T update value_data_end_offset_ yet - we'll do it after writing all in-line data

    // === Step 4: Write second indirection offset for values ===
    // The offset points 8 bytes ahead (right after this int64) to the values count
    int64_t values_indirection_offset = 8;

    if (!Write(values_indirection_offset)) {
      if (err) *err = "Failed to write values indirection offset";
      return -1;
    }

    // === Step 5: Write values count ===
    if (!Write(num_samples)) {
      if (err) *err = "Failed to write TimeSamples value count";
      return -1;
    }

    // Update value_data_end_offset_ NOW, before writing ValueReps
    // This ensures PackValue() writes out-of-line data AFTER our inline structure
    size_t num_samples_size;
    if (!safe::mul(num_samples, size_t(8), &num_samples_size)) {
      if (err) *err = "Integer overflow: num_samples * 8";
      return -1;
    }
    value_data_end_offset_ = Tell() + num_samples_size;  // Reserve space for ValueReps

    // Read-side deduplication is encoded in the per-sample data offsets: two
    // samples that share a byte offset reference the same value block. We reuse
    // the already-written ValueRep for a repeated offset so deduplicated
    // timesamples are NOT materialized into N independent copies (which would
    // blow memory up to many GB for animated arrays). For generic (non-binary)
    // storage get_data_offsets() is empty and we fall back to per-sample
    // reconstruction + the writer-wide value dedup path in PackValue().
    // The offset fast-path is itself a deduplication mechanism (it reuses a
    // prior ValueRep), so it must honor enable_deduplication: with dedup off we
    // reconstruct and pack every sample independently (no sharing). Memory stays
    // bounded either way because get_sample_at reconstructs one sample at a time.
    const std::vector<size_t>& sample_offsets = timesamples_val->get_data_offsets();
    const bool has_offsets =
        options_.enable_deduplication && !sample_offsets.empty();
    std::unordered_map<size_t, uint64_t> offset_rep_cache;

    // Write ValueRep for each value
    for (size_t i = 0; i < num_samples; ++i) {
      // Fast path: this sample reuses an offset whose ValueRep was already
      // serialized - emit it verbatim without reconstructing the value.
      if (has_offsets && i < sample_offsets.size() &&
          sample_offsets[i] != value::TimeSamples::BLOCKED_OFFSET) {
        auto cached = offset_rep_cache.find(sample_offsets[i]);
        if (cached != offset_rep_cache.end()) {
          if (!Write(cached->second)) {
            if (err) *err = "Failed to write deduplicated TimeSamples ValueRep at index " + std::to_string(i);
            return -1;
          }
          continue;
        }
      }

      // Reconstruct only THIS sample (O(one sample) memory, not O(N)).
      value::TimeSamples::Sample sample;
      if (!timesamples_val->get_sample_at(i, &sample)) {
        if (err) *err = "Failed to reconstruct TimeSamples sample at index " + std::to_string(i);
        return -1;
      }

      // Check if this is a blocked value (ValueBlock/None)
      if (sample.blocked) {
        // Write ValueBlock ValueRep
        crate::ValueRep rep;
        rep.SetType(static_cast<int32_t>(crate::CrateDataTypeId::CRATE_DATA_TYPE_VALUE_BLOCK));
        rep.SetPayload(0);
        uint64_t rep_data = rep.GetData();
        if (!Write(rep_data)) {
          if (err) *err = "Failed to write ValueBlock ValueRep";
          return -1;
        }
        continue;
      }

      // Convert value::Value to CrateValue
      crate::CrateValue crate_value;
      if (!ConvertValueToCrateValue(sample.value, &crate_value, err)) {
        if (err) *err = "Failed to convert TimeSamples value at index " + std::to_string(i) + ": " + *err;
        return -1;
      }

      crate::ValueRep value_rep = PackValue(crate_value, err);
      if (err && !err->empty()) {
        return -1;
      }

      // Write the ValueRep
      uint64_t rep_data = value_rep.GetData();
      if (!Write(rep_data)) {
        if (err) *err = "Failed to write TimeSamples ValueRep at index " + std::to_string(i);
        return -1;
      }

      // Record this sample's source byte offset -> ValueRep so later samples
      // that share the offset (read-side dedup) reuse it without reconstruction
      // or hashing. Never cache the BLOCKED sentinel.
      if (has_offsets && i < sample_offsets.size() &&
          sample_offsets[i] != value::TimeSamples::BLOCKED_OFFSET) {
        offset_rep_cache.emplace(sample_offsets[i], rep_data);
      }
    }

    // === Step 6: Pack times array through the global value dedup path ===
    // PackValue() seeks to value_data_end_offset_ to write any new out-of-line
    // bytes, then seeks back to the ValueRep[] block. This preserves the
    // TimeSamples frame layout while allowing identical time arrays to share a
    // single double[] payload across attributes.
    std::vector<double> times;
    times.reserve(static_cast<size_t>(num_samples));
    for (size_t i = 0; i < num_samples; ++i) {
      auto time_opt = timesamples_val->get_time(i);
      if (!time_opt) {
        if (err) *err = "Failed to get time from TimeSamples at index " + std::to_string(i);
        return -1;
      }
      times.push_back(time_opt.value());
    }

    crate::CrateValue times_value;
    times_value.Set(times);
    crate::ValueRep times_rep = PackValue(times_value, err);
    if (err && !err->empty()) {
      return -1;
    }

    // === Step 7: Go back and fill in times_rep ValueRep ===
    const int64_t timesamples_end_pos = value_data_end_offset_;

    if (!Seek(times_rep_pos)) {
      if (err) *err = "Failed to seek to write times ValueRep";
      return -1;
    }

    uint64_t times_rep_data = times_rep.GetData();
    if (!Write(times_rep_data)) {
      if (err) *err = "Failed to write times ValueRep";
      return -1;
    }

    // Seek back to end
    if (!Seek(timesamples_end_pos)) {
      if (err) *err = "Failed to seek back after writing times ValueRep";
      return -1;
    }

    // Update value_data_end_offset_ to include all TimeSamples data
    value_data_end_offset_ = Tell();
  }
  // Integer ListOps are handled above (IntListOp, UIntListOp, Int64ListOp, UInt64ListOp)
  else {
    // Unsupported type for out-of-line storage
    if (err) *err = "Unsupported value type for out-of-line storage: " + std::string(value.type_name()) + " (type_id=" + std::to_string(value.type_id()) + ")";
    return -1;
  }

  // Update value data end offset
  value_data_end_offset_ = Tell();

  // Seek back to where we were
  if (!Seek(current_pos)) {
    if (err) *err = "Failed to seek back after writing value";
    return -1;
  }

  return value_offset;
}

} // namespace experimental
} // namespace tinyusdz

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
