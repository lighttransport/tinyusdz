// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Crate Format Implementation

#include "crate-format.hh"

// Include LZ4 from existing TinyUSDZ
#include "../../lz4/lz4.h"

#include <cstring>

namespace tinyusdz {
namespace next {

const char* CrateTypeIdName(CrateTypeId id) {
  switch (id) {
    case CrateTypeId::Invalid: return "Invalid";
    case CrateTypeId::Bool: return "Bool";
    case CrateTypeId::UChar: return "UChar";
    case CrateTypeId::Int: return "Int";
    case CrateTypeId::UInt: return "UInt";
    case CrateTypeId::Int64: return "Int64";
    case CrateTypeId::UInt64: return "UInt64";
    case CrateTypeId::Half: return "Half";
    case CrateTypeId::Float: return "Float";
    case CrateTypeId::Double: return "Double";
    case CrateTypeId::String: return "String";
    case CrateTypeId::Token: return "Token";
    case CrateTypeId::AssetPath: return "AssetPath";
    case CrateTypeId::Matrix2d: return "Matrix2d";
    case CrateTypeId::Matrix3d: return "Matrix3d";
    case CrateTypeId::Matrix4d: return "Matrix4d";
    case CrateTypeId::Quatd: return "Quatd";
    case CrateTypeId::Quatf: return "Quatf";
    case CrateTypeId::Quath: return "Quath";
    case CrateTypeId::Vec2d: return "Vec2d";
    case CrateTypeId::Vec2f: return "Vec2f";
    case CrateTypeId::Vec2h: return "Vec2h";
    case CrateTypeId::Vec2i: return "Vec2i";
    case CrateTypeId::Vec3d: return "Vec3d";
    case CrateTypeId::Vec3f: return "Vec3f";
    case CrateTypeId::Vec3h: return "Vec3h";
    case CrateTypeId::Vec3i: return "Vec3i";
    case CrateTypeId::Vec4d: return "Vec4d";
    case CrateTypeId::Vec4f: return "Vec4f";
    case CrateTypeId::Vec4h: return "Vec4h";
    case CrateTypeId::Vec4i: return "Vec4i";
    case CrateTypeId::Dictionary: return "Dictionary";
    case CrateTypeId::TokenListOp: return "TokenListOp";
    case CrateTypeId::StringListOp: return "StringListOp";
    case CrateTypeId::PathListOp: return "PathListOp";
    case CrateTypeId::ReferenceListOp: return "ReferenceListOp";
    case CrateTypeId::IntListOp: return "IntListOp";
    case CrateTypeId::Int64ListOp: return "Int64ListOp";
    case CrateTypeId::UIntListOp: return "UIntListOp";
    case CrateTypeId::UInt64ListOp: return "UInt64ListOp";
    case CrateTypeId::PathVector: return "PathVector";
    case CrateTypeId::TokenVector: return "TokenVector";
    case CrateTypeId::Specifier: return "Specifier";
    case CrateTypeId::Permission: return "Permission";
    case CrateTypeId::Variability: return "Variability";
    case CrateTypeId::VariantSelectionMap: return "VariantSelectionMap";
    case CrateTypeId::TimeSamples: return "TimeSamples";
    case CrateTypeId::Payload: return "Payload";
    case CrateTypeId::DoubleVector: return "DoubleVector";
    case CrateTypeId::LayerOffsetVector: return "LayerOffsetVector";
    case CrateTypeId::StringVector: return "StringVector";
    case CrateTypeId::ValueBlock: return "ValueBlock";
    case CrateTypeId::Value: return "Value";
    case CrateTypeId::UnregisteredValue: return "UnregisteredValue";
    case CrateTypeId::UnregisteredValueListOp: return "UnregisteredValueListOp";
    case CrateTypeId::PayloadListOp: return "PayloadListOp";
    case CrateTypeId::TimeCode: return "TimeCode";
  }
  return "Unknown";
}

size_t CrateTypeIdSize(CrateTypeId id) {
  switch (id) {
    case CrateTypeId::Bool: return 1;
    case CrateTypeId::UChar: return 1;
    case CrateTypeId::Int: return 4;
    case CrateTypeId::UInt: return 4;
    case CrateTypeId::Int64: return 8;
    case CrateTypeId::UInt64: return 8;
    case CrateTypeId::Half: return 2;
    case CrateTypeId::Float: return 4;
    case CrateTypeId::Double: return 8;
    case CrateTypeId::Matrix2d: return 4 * 8;   // 4 doubles
    case CrateTypeId::Matrix3d: return 9 * 8;   // 9 doubles
    case CrateTypeId::Matrix4d: return 16 * 8;  // 16 doubles
    case CrateTypeId::Quatd: return 4 * 8;      // 4 doubles
    case CrateTypeId::Quatf: return 4 * 4;      // 4 floats
    case CrateTypeId::Quath: return 4 * 2;      // 4 halfs
    case CrateTypeId::Vec2d: return 2 * 8;
    case CrateTypeId::Vec2f: return 2 * 4;
    case CrateTypeId::Vec2h: return 2 * 2;
    case CrateTypeId::Vec2i: return 2 * 4;
    case CrateTypeId::Vec3d: return 3 * 8;
    case CrateTypeId::Vec3f: return 3 * 4;
    case CrateTypeId::Vec3h: return 3 * 2;
    case CrateTypeId::Vec3i: return 3 * 4;
    case CrateTypeId::Vec4d: return 4 * 8;
    case CrateTypeId::Vec4f: return 4 * 4;
    case CrateTypeId::Vec4h: return 4 * 2;
    case CrateTypeId::Vec4i: return 4 * 4;
    case CrateTypeId::TimeCode: return 8;       // double

    // Variable-size types
    case CrateTypeId::String:
    case CrateTypeId::Token:
    case CrateTypeId::AssetPath:
    case CrateTypeId::Dictionary:
    case CrateTypeId::TimeSamples:
    default:
      return 0;
  }
}

DecompressResult DecompressLZ4(const uint8_t* src, size_t src_size,
                                size_t uncompressed_size) {
  DecompressResult result;

  if (!src || src_size == 0) {
    result.error = "Empty input";
    return result;
  }

  if (uncompressed_size == 0) {
    result.success = true;
    return result;
  }

  // Sanity check - don't allocate absurd amounts
  if (uncompressed_size > 1024 * 1024 * 1024) {  // 1GB limit
    result.error = "Uncompressed size too large";
    return result;
  }

  result.data.resize(uncompressed_size);

  int decoded = LZ4_decompress_safe(
      reinterpret_cast<const char*>(src),
      reinterpret_cast<char*>(result.data.data()),
      static_cast<int>(src_size),
      static_cast<int>(uncompressed_size));

  if (decoded < 0) {
    result.error = "LZ4 decompression failed";
    result.data.clear();
    return result;
  }

  if (static_cast<size_t>(decoded) != uncompressed_size) {
    result.error = "LZ4 decompression size mismatch";
    result.data.clear();
    return result;
  }

  result.success = true;
  return result;
}

// USD Integer compression decoder
// Based on pxrUSD's Usd_IntegerCompression
namespace {

// Decode integers encoded with USD's integer compression
bool DecodeIntegers(const uint8_t* src, size_t src_size,
                    uint32_t* dst, size_t count) {
  if (count == 0) return true;
  if (!src || src_size == 0) return false;

  size_t src_idx = 0;
  size_t dst_idx = 0;

  // Read common prefix
  if (src_idx >= src_size) return false;
  uint32_t common = 0;
  uint8_t common_bytes = src[src_idx++];

  for (int i = 0; i < common_bytes && src_idx < src_size; i++) {
    common |= static_cast<uint32_t>(src[src_idx++]) << (i * 8);
  }

  // Decode each integer
  while (dst_idx < count && src_idx < src_size) {
    uint8_t code = src[src_idx++];
    uint8_t num_bytes = code & 0x03;
    uint32_t value = common;

    if (num_bytes > 0) {
      uint32_t suffix = 0;
      for (int i = 0; i < num_bytes && src_idx < src_size; i++) {
        suffix |= static_cast<uint32_t>(src[src_idx++]) << (i * 8);
      }
      // Shift common and add suffix
      value = (common << (num_bytes * 8)) | suffix;
    }

    // Handle run-length encoding
    uint8_t run_length = (code >> 2) + 1;
    for (int i = 0; i < run_length && dst_idx < count; i++) {
      dst[dst_idx++] = value;
    }
  }

  return dst_idx == count;
}

bool DecodeIntegers64(const uint8_t* src, size_t src_size,
                      uint64_t* dst, size_t count) {
  if (count == 0) return true;
  if (!src || src_size == 0) return false;

  // For 64-bit, decode as pairs of 32-bit
  std::vector<uint32_t> lo(count), hi(count);

  size_t half_size = src_size / 2;
  if (!DecodeIntegers(src, half_size, lo.data(), count)) return false;
  if (!DecodeIntegers(src + half_size, src_size - half_size, hi.data(), count)) return false;

  for (size_t i = 0; i < count; i++) {
    dst[i] = (static_cast<uint64_t>(hi[i]) << 32) | lo[i];
  }

  return true;
}

}  // anonymous namespace

DecompressResult DecompressIntegers(const uint8_t* src, size_t src_size,
                                     size_t num_integers, bool is_64bit) {
  DecompressResult result;

  if (num_integers == 0) {
    result.success = true;
    return result;
  }

  if (!src || src_size == 0) {
    result.error = "Empty input";
    return result;
  }

  if (is_64bit) {
    result.data.resize(num_integers * sizeof(uint64_t));
    if (!DecodeIntegers64(src, src_size,
                          reinterpret_cast<uint64_t*>(result.data.data()),
                          num_integers)) {
      result.error = "Integer decompression failed";
      result.data.clear();
      return result;
    }
  } else {
    result.data.resize(num_integers * sizeof(uint32_t));
    if (!DecodeIntegers(src, src_size,
                        reinterpret_cast<uint32_t*>(result.data.data()),
                        num_integers)) {
      result.error = "Integer decompression failed";
      result.data.clear();
      return result;
    }
  }

  result.success = true;
  return result;
}

}  // namespace next
}  // namespace tinyusdz
