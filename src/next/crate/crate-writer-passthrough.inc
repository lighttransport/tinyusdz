// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - CrateWriter::Impl lazy-array pass-through helpers.
// Included inside CrateWriter::Impl private section.

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
    CacheBlockHash(&block);
    uint64_t idx = InternBlock(std::move(block));

    *out_rep = ValueRep::Make(lr->crate_type, idx, /*array=*/true,
                              /*inlined=*/false, lr->rep.is_compressed());
    return true;
  }
