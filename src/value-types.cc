// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
#include "value-types.hh"

#include <type_traits>

#include "str-util.hh"
#include "value-pprint.hh"
#include "value-eval-util.hh"

//
#include "common-macros.inc"
#include "math-util.inc"

// For compile-time map
// Another candidate is frozen: https://github.com/serge-sans-paille/frozen
//
#include "external/mapbox/eternal/include/mapbox/eternal.hpp"

namespace tinyusdz {
namespace value {

// Static member definition for ValueView
// This is a placeholder value used for type checking - the warnings are acceptable here
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"

Value ValueView::value_placeholder_;
#pragma clang diagnostic pop
#endif // __clang__

//
// Supported type for `Linear` interpolation
//
// half, float, double, TimeCode(double)
// matrix2d, matrix3d, matrix4d,
// float2h, float3h, float4h
// float2f, float3f, float4f
// float2d, float3d, float4d
// quath, quatf, quatd
// (use slerp for quaternion type)
bool IsLerpSupportedType(uint32_t tyid) {

  // TODO: Directly get underlying_typeid
  bool has_underlying_tyid{false};
  uint32_t underlying_tyid{TYPE_ID_INVALID};

  if (auto pv = TryGetUnderlyingTypeName(tyid)) {
    underlying_tyid = GetTypeId(pv.value());
    has_underlying_tyid = true;
  } 

  // See also for underlying_type_id to simplify check for Role types(e.g. color3f)
#define IS_SUPPORTED_TYPE(__tyid, __ty) \
  if (__tyid == value::TypeTraits<__ty>::type_id()) { \
    return true; \
  } else if (__tyid == value::TypeTraits<__ty>::underlying_type_id()) { \
    return true; \
  } else if (__tyid & value::TYPE_ID_STL_ARRAY_BIT) { \
    if ((__tyid & (~value::TYPE_ID_STL_ARRAY_BIT)) == (value::TypeTraits<__ty>::type_id())) { \
      return true; \
    } else if ((__tyid & (~value::TYPE_ID_STL_ARRAY_BIT)) == (value::TypeTraits<__ty>::underlying_type_id())) { \
      return true; \
    } \
  }

  // Assume __uty is underlying_type.
#define IS_SUPPORTED_UNDERLYING_TYPE(__utyid, __uty) \
  if (__utyid == value::TypeTraits<__uty>::type_id()) { \
    return true; \
  } else if (__utyid & value::TYPE_ID_STL_ARRAY_BIT) { \
    if ((__utyid & (~value::TYPE_ID_STL_ARRAY_BIT)) == (value::TypeTraits<__uty>::type_id())) { \
      return true; \
    } \
  }

  IS_SUPPORTED_TYPE(tyid, value::half)
  IS_SUPPORTED_TYPE(tyid, value::half2)
  IS_SUPPORTED_TYPE(tyid, value::half3)
  IS_SUPPORTED_TYPE(tyid, value::half4)
  IS_SUPPORTED_TYPE(tyid, float)
  IS_SUPPORTED_TYPE(tyid, value::float2)
  IS_SUPPORTED_TYPE(tyid, value::float3)
  IS_SUPPORTED_TYPE(tyid, value::float4)
  IS_SUPPORTED_TYPE(tyid, double)
  IS_SUPPORTED_TYPE(tyid, value::double2)
  IS_SUPPORTED_TYPE(tyid, value::double3)
  IS_SUPPORTED_TYPE(tyid, value::double4)
  IS_SUPPORTED_TYPE(tyid, value::quath)
  IS_SUPPORTED_TYPE(tyid, value::quatf)
  IS_SUPPORTED_TYPE(tyid, value::quatd)
  IS_SUPPORTED_TYPE(tyid, value::matrix2d)
  IS_SUPPORTED_TYPE(tyid, value::matrix3d)
  IS_SUPPORTED_TYPE(tyid, value::matrix4d)

  if (has_underlying_tyid) {
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::half)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::half2)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::half3)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::half4)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, float)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::float2)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::float3)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::float4)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, double)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::double2)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::double3)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::double4)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::quath)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::quatf)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::quatd)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::matrix2d)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::matrix3d)
    IS_SUPPORTED_UNDERLYING_TYPE(underlying_tyid, value::matrix4d)
  }

#undef IS_SUPPORTED_TYPE
#undef IS_SUPPORTED_UNDERLYING_TYPE

  return false;

}

bool Lerp(const value::Value &a, const value::Value &b, double dt, value::Value *dst) {
  if (!dst) {
    return false;
  }

  if (a.type_id() != b.type_id()) {
    return false;
  }

  uint32_t tyid = a.type_id();

  if (!IsLerpSupportedType(tyid)) {
    return false;
  }

  bool ok{false};
  value::Value result;

#define DO_LERP(__ty) \
  if (tyid == value::TypeTraits<__ty>::type_id()) { \
    const __ty *v0 = a.as<__ty>(); \
    const __ty *v1 = b.as<__ty>(); \
    __ty c; \
    if (v0 && v1) { \
      c = lerp(*v0, *v1, dt); \
      result = c; \
      ok = true; \
    } \
  } else if (tyid == value::TypeTraits<std::vector<__ty>>::type_id()) { \
    const std::vector<__ty> *v0 = a.as<std::vector<__ty>>(); \
    const std::vector<__ty> *v1 = b.as<std::vector<__ty>>(); \
    std::vector<__ty> c; \
    if (v0 && v1) { \
      c = lerp(*v0, *v1, dt); \
      result = c; \
      ok = true; \
    } \
  } else

  DO_LERP(value::half)
  DO_LERP(value::half2)
  DO_LERP(value::half3)
  DO_LERP(value::half4)
  DO_LERP(float)
  DO_LERP(value::float2)
  DO_LERP(value::float3)
  DO_LERP(value::float4)
  DO_LERP(double)
  DO_LERP(value::double2)
  DO_LERP(value::double3)
  DO_LERP(value::double4)
  DO_LERP(value::quath)
  DO_LERP(value::quatf)
  DO_LERP(value::quatd)
  DO_LERP(value::color3h)
  DO_LERP(value::color3f)
  DO_LERP(value::color3d)
  DO_LERP(value::color4h)
  DO_LERP(value::color4f)
  DO_LERP(value::color4d)
  DO_LERP(value::point3h)
  DO_LERP(value::point3f)
  DO_LERP(value::point3d)
  DO_LERP(value::normal3h)
  DO_LERP(value::normal3f)
  DO_LERP(value::normal3d)
  DO_LERP(value::vector3h)
  DO_LERP(value::vector3f)
  DO_LERP(value::vector3d)
  DO_LERP(value::texcoord2h)
  DO_LERP(value::texcoord2f)
  DO_LERP(value::texcoord2d)
  DO_LERP(value::texcoord3h)
  DO_LERP(value::texcoord3f)
  DO_LERP(value::texcoord3d)
  {
    DCOUT("TODO: type " << GetTypeName(tyid));
  }

#undef DO_LERP

  if (ok) {
    (*dst) = result;
  }

  return ok;
}

nonstd::optional<std::string> TryGetTypeName(uint32_t tyid) {
  MAPBOX_ETERNAL_CONSTEXPR const auto tynamemap =
      mapbox::eternal::map<uint32_t, mapbox::eternal::string>({
          {TYPE_ID_TOKEN, kToken},
          {TYPE_ID_STRING, kString},
          {TYPE_ID_STRING, kPath},
          {TYPE_ID_ASSET_PATH, kAssetPath},
          {TYPE_ID_DICT, kDictionary},
          {TYPE_ID_TIMECODE, kTimeCode},
          {TYPE_ID_BOOL, kBool},
          {TYPE_ID_UCHAR, kUChar},
          {TYPE_ID_HALF, kHalf},
          {TYPE_ID_INT32, kInt},
          {TYPE_ID_UINT32, kUInt},
          {TYPE_ID_INT64, kInt64},
          {TYPE_ID_UINT64, kUInt64},
          {TYPE_ID_INT2, kInt2},
          {TYPE_ID_INT3, kInt3},
          {TYPE_ID_INT4, kInt4},
          {TYPE_ID_UINT2, kUInt2},
          {TYPE_ID_UINT3, kUInt3},
          {TYPE_ID_UINT4, kUInt4},
          {TYPE_ID_HALF2, kHalf2},
          {TYPE_ID_HALF3, kHalf3},
          {TYPE_ID_HALF4, kHalf4},
          {TYPE_ID_MATRIX2D, kMatrix2d},
          {TYPE_ID_MATRIX3D, kMatrix3d},
          {TYPE_ID_MATRIX4D, kMatrix4d},
          {TYPE_ID_FLOAT, kFloat},
          {TYPE_ID_FLOAT2, kFloat2},
          {TYPE_ID_FLOAT3, kFloat3},
          {TYPE_ID_FLOAT4, kFloat4},
          {TYPE_ID_DOUBLE, kDouble},
          {TYPE_ID_DOUBLE2, kDouble2},
          {TYPE_ID_DOUBLE3, kDouble3},
          {TYPE_ID_DOUBLE4, kDouble4},
          {TYPE_ID_QUATH, kQuath},
          {TYPE_ID_QUATF, kQuatf},
          {TYPE_ID_QUATD, kQuatd},
          {TYPE_ID_VECTOR3H, kVector3h},
          {TYPE_ID_VECTOR3F, kVector3f},
          {TYPE_ID_VECTOR3D, kVector3d},
          {TYPE_ID_POINT3H, kPoint3h},
          {TYPE_ID_POINT3F, kPoint3f},
          {TYPE_ID_POINT3D, kPoint3d},
          {TYPE_ID_NORMAL3H, kNormal3h},
          {TYPE_ID_NORMAL3F, kNormal3f},
          {TYPE_ID_NORMAL3D, kNormal3d},
          {TYPE_ID_COLOR3F, kColor3f},
          {TYPE_ID_COLOR3D, kColor3d},
          {TYPE_ID_COLOR4F, kColor4f},
          {TYPE_ID_COLOR4D, kColor4d},
          {TYPE_ID_FRAME4D, kFrame4d},
          {TYPE_ID_TEXCOORD2H, kTexCoord2h},
          {TYPE_ID_TEXCOORD2F, kTexCoord2f},
          {TYPE_ID_TEXCOORD2D, kTexCoord2d},
          {TYPE_ID_TEXCOORD3H, kTexCoord3h},
          {TYPE_ID_TEXCOORD3F, kTexCoord3f},
          {TYPE_ID_TEXCOORD3D, kTexCoord3d},
          {TYPE_ID_RELATIONSHIP, kRelationship},
          // Special vector types with dedicated type IDs
          {TYPE_ID_TOKEN_VECTOR, "token[]"},
          {TYPE_ID_PATH_VECTOR, "Path[]"},
      });

  bool array_bit = (TYPE_ID_STL_ARRAY_BIT & tyid);
  uint32_t scalar_tid = tyid & (~TYPE_ID_STL_ARRAY_BIT);

  auto ret = tynamemap.find(scalar_tid);
  if (ret != tynamemap.end()) {
    std::string s = ret->second.c_str();
    if (array_bit) {
      s += "[]";
    }
    return std::move(s);
  }

  return nonstd::nullopt;
}

std::string GetTypeName(uint32_t tyid) {
  auto ret = TryGetTypeName(tyid);

  if (!ret) {
    return "(GetTypeName) [[Unknown or unimplemented/unsupported type_id: " +
           std::to_string(tyid) + "]]";
  }

  return ret.value();
}

nonstd::optional<uint32_t> TryGetTypeId(const std::string &tyname) {
  MAPBOX_ETERNAL_CONSTEXPR const auto tyidmap =
      mapbox::eternal::hash_map<mapbox::eternal::string, uint32_t>({
          {kToken, TYPE_ID_TOKEN},
          {kString, TYPE_ID_STRING},
          {kPath, TYPE_ID_STRING},
          {kAssetPath, TYPE_ID_ASSET_PATH},
          {kDictionary, TYPE_ID_DICT},
          {kTimeCode, TYPE_ID_TIMECODE},
          {kBool, TYPE_ID_BOOL},
          {kUChar, TYPE_ID_UCHAR},
          {kHalf, TYPE_ID_HALF},
          {kInt, TYPE_ID_INT32},
          {kUInt, TYPE_ID_UINT32},
          {kInt64, TYPE_ID_INT64},
          {kUInt64, TYPE_ID_UINT64},
          {kInt2, TYPE_ID_INT2},
          {kInt3, TYPE_ID_INT3},
          {kInt4, TYPE_ID_INT4},
          {kUInt2, TYPE_ID_UINT2},
          {kUInt3, TYPE_ID_UINT3},
          {kUInt4, TYPE_ID_UINT4},
          {kHalf2, TYPE_ID_HALF2},
          {kHalf3, TYPE_ID_HALF3},
          {kHalf4, TYPE_ID_HALF4},
          {kMatrix2d, TYPE_ID_MATRIX2D},
          {kMatrix3d, TYPE_ID_MATRIX3D},
          {kMatrix4d, TYPE_ID_MATRIX4D},
          {kFloat, TYPE_ID_FLOAT},
          {kFloat2, TYPE_ID_FLOAT2},
          {kFloat3, TYPE_ID_FLOAT3},
          {kFloat4, TYPE_ID_FLOAT4},
          {kDouble, TYPE_ID_DOUBLE},
          {kDouble2, TYPE_ID_DOUBLE2},
          {kDouble3, TYPE_ID_DOUBLE3},
          {kDouble4, TYPE_ID_DOUBLE4},
          {kQuath, TYPE_ID_QUATH},
          {kQuatf, TYPE_ID_QUATF},
          {kQuatd, TYPE_ID_QUATD},
          {kVector3h, TYPE_ID_VECTOR3H},
          {kVector3f, TYPE_ID_VECTOR3F},
          {kVector3d, TYPE_ID_VECTOR3D},
          {kPoint3h, TYPE_ID_POINT3H},
          {kPoint3f, TYPE_ID_POINT3F},
          {kPoint3d, TYPE_ID_POINT3D},
          {kNormal3h, TYPE_ID_NORMAL3H},
          {kNormal3f, TYPE_ID_NORMAL3F},
          {kNormal3d, TYPE_ID_NORMAL3D},
          {kColor3f, TYPE_ID_COLOR3F},
          {kColor3d, TYPE_ID_COLOR3D},
          {kColor4f, TYPE_ID_COLOR4F},
          {kColor4d, TYPE_ID_COLOR4D},
          {kFrame4d, TYPE_ID_FRAME4D},
          {kTexCoord2h, TYPE_ID_TEXCOORD2H},
          {kTexCoord2f, TYPE_ID_TEXCOORD2F},
          {kTexCoord2d, TYPE_ID_TEXCOORD2D},
          {kTexCoord3h, TYPE_ID_TEXCOORD3H},
          {kTexCoord3f, TYPE_ID_TEXCOORD3F},
          {kTexCoord3d, TYPE_ID_TEXCOORD3D},
          {kRelationship, TYPE_ID_RELATIONSHIP},
      });

  std::string s = tyname;
  uint32_t array_bit = 0;
  if (endsWith(tyname, "[]")) {
    s = removeSuffix(s, "[]");
    array_bit |= TYPE_ID_STL_ARRAY_BIT;
  }

  // It looks USD does not support 2D array type, so no further `[]` check

  auto ret = tyidmap.find(s.c_str());
  if (ret != tyidmap.end()) {
    return ret->second | array_bit;
  }

  return nonstd::nullopt;
}

uint32_t GetTypeId(const std::string &tyname) {
  auto ret = TryGetTypeId(tyname);

  if (!ret) {
    return TYPE_ID_INVALID;
  }

  return ret.value();
}

nonstd::optional<uint32_t> TryGetUnderlyingTypeId(const std::string &tyname) {
  MAPBOX_ETERNAL_CONSTEXPR const auto utyidmap =
      mapbox::eternal::hash_map<mapbox::eternal::string, uint32_t>({
        {kPoint3h, TYPE_ID_HALF3},
        {kPoint3f, TYPE_ID_FLOAT3},
        {kPoint3d, TYPE_ID_DOUBLE3},
        {kNormal3h, TYPE_ID_HALF3},
        {kNormal3f, TYPE_ID_FLOAT3},
        {kNormal3d, TYPE_ID_DOUBLE3},
        {kVector3h, TYPE_ID_HALF3},
        {kVector3f, TYPE_ID_FLOAT3},
        {kVector3d, TYPE_ID_DOUBLE3},
        {kColor3h, TYPE_ID_HALF3},
        {kColor3f, TYPE_ID_FLOAT3},
        {kColor3d, TYPE_ID_DOUBLE3},
        {kColor4h, TYPE_ID_HALF4},
        {kColor4f, TYPE_ID_FLOAT4},
        {kColor4d, TYPE_ID_DOUBLE4},
        {kTexCoord2h, TYPE_ID_HALF2},
        {kTexCoord2f, TYPE_ID_FLOAT2},
        {kTexCoord2d, TYPE_ID_DOUBLE3},
        {kTexCoord3h, TYPE_ID_HALF3},
        {kTexCoord3f, TYPE_ID_FLOAT3},
        {kTexCoord3d, TYPE_ID_DOUBLE4},
        {kFrame4d, TYPE_ID_MATRIX4D},
  });

  {
    std::string s = tyname;
    uint32_t array_bit = 0;
    if (endsWith(tyname, "[]")) {
      s = removeSuffix(s, "[]");
      array_bit |= TYPE_ID_STL_ARRAY_BIT;
    }

    auto ret = utyidmap.find(s.c_str());
    if (ret != utyidmap.end()) {
      return ret->second | array_bit;
    }
  }

  // Fallback
  return TryGetTypeId(tyname);
}

uint32_t GetUnderlyingTypeId(const std::string &tyname) {
  auto ret = TryGetUnderlyingTypeId(tyname);

  if (!ret) {
    return TYPE_ID_INVALID;
  }

  return ret.value();
}

nonstd::optional<uint32_t> TryGetUnderlyingTypeId(uint32_t tyid) {
  MAPBOX_ETERNAL_CONSTEXPR const auto utyidmap =
      mapbox::eternal::map<uint32_t, uint32_t>({
        {TYPE_ID_POINT3H, TYPE_ID_HALF3},
        {TYPE_ID_POINT3F, TYPE_ID_FLOAT3},
        {TYPE_ID_POINT3D, TYPE_ID_DOUBLE3},
        {TYPE_ID_NORMAL3H, TYPE_ID_HALF3},
        {TYPE_ID_NORMAL3F, TYPE_ID_FLOAT3},
        {TYPE_ID_NORMAL3D, TYPE_ID_DOUBLE3},
        {TYPE_ID_VECTOR3H, TYPE_ID_HALF3},
        {TYPE_ID_VECTOR3F, TYPE_ID_FLOAT3},
        {TYPE_ID_VECTOR3D, TYPE_ID_DOUBLE3},
        {TYPE_ID_COLOR3H, TYPE_ID_HALF3},
        {TYPE_ID_COLOR3F, TYPE_ID_FLOAT3},
        {TYPE_ID_COLOR3D, TYPE_ID_DOUBLE3},
        {TYPE_ID_COLOR4H, TYPE_ID_HALF4},
        {TYPE_ID_COLOR4F, TYPE_ID_FLOAT4},
        {TYPE_ID_COLOR4D, TYPE_ID_DOUBLE4},
        {TYPE_ID_TEXCOORD2H, TYPE_ID_HALF2},
        {TYPE_ID_TEXCOORD2F, TYPE_ID_FLOAT2},
        {TYPE_ID_TEXCOORD2D, TYPE_ID_DOUBLE2},
        {TYPE_ID_TEXCOORD3H, TYPE_ID_HALF3},
        {TYPE_ID_TEXCOORD3F, TYPE_ID_FLOAT3},
        {TYPE_ID_TEXCOORD3D, TYPE_ID_DOUBLE3},
        {TYPE_ID_FRAME4D, TYPE_ID_MATRIX4D},
  });

  uint32_t array_bit = (TYPE_ID_STL_ARRAY_BIT & tyid);
  uint32_t scalar_tid = tyid & (~TYPE_ID_STL_ARRAY_BIT);

  auto ret = utyidmap.find(scalar_tid);
  if (ret != utyidmap.end()) {
    return ret->second | array_bit;
  }

  // Not a role type, return the original type id
  return tyid;
}

uint32_t GetUnderlyingTypeId(uint32_t tyid) {
  auto ret = TryGetUnderlyingTypeId(tyid);

  if (!ret) {
    return TYPE_ID_INVALID;
  }

  return ret.value();
}

nonstd::optional<std::string> TryGetUnderlyingTypeName(const uint32_t tyid) {
  MAPBOX_ETERNAL_CONSTEXPR const auto utynamemap =
      mapbox::eternal::map<uint32_t, mapbox::eternal::string>({
        {TYPE_ID_POINT3H, kHalf3},
        {TYPE_ID_POINT3F, kFloat3},
        {TYPE_ID_POINT3D, kDouble3},
        {TYPE_ID_NORMAL3H, kHalf3},
        {TYPE_ID_NORMAL3F, kFloat3},
        {TYPE_ID_NORMAL3D, kDouble3},
        {TYPE_ID_VECTOR3H, kHalf3},
        {TYPE_ID_VECTOR3F, kFloat3},
        {TYPE_ID_VECTOR3D, kDouble3},
        {TYPE_ID_COLOR3H, kHalf3},
        {TYPE_ID_COLOR3F, kFloat3},
        {TYPE_ID_COLOR3D, kDouble3},
        {TYPE_ID_COLOR4H, kHalf4},
        {TYPE_ID_COLOR4F, kFloat4},
        {TYPE_ID_COLOR4D, kDouble4},
        {TYPE_ID_TEXCOORD2H, kHalf2},
        {TYPE_ID_TEXCOORD2F, kFloat2},
        {TYPE_ID_TEXCOORD2D, kDouble2},
        {TYPE_ID_TEXCOORD3H, kHalf3},
        {TYPE_ID_TEXCOORD3F, kFloat3},
        {TYPE_ID_TEXCOORD3D, kDouble3},
        {TYPE_ID_FRAME4D, kMatrix4d},
  });

  {
  bool array_bit = (TYPE_ID_STL_ARRAY_BIT & tyid);
  uint32_t scalar_tid = tyid & (~TYPE_ID_STL_ARRAY_BIT);

  auto ret = utynamemap.find(scalar_tid);
  if (ret != utynamemap.end()) {
    std::string s = ret->second.c_str();
    if (array_bit) {
      s += "[]";
    }
    return std::move(s);
  }
  }

  return TryGetTypeName(tyid);

}

std::string GetUnderlyingTypeName(uint32_t tyid) {
  auto ret = TryGetUnderlyingTypeName(tyid);

  if (!ret) {
    return "(GetUnderlyingTypeName) [[Unknown or unimplemented/unsupported type_id: " +
           std::to_string(tyid) + "]]";
  }

  return ret.value();
}

bool IsRoleType(const std::string &tyname) {
  return GetUnderlyingTypeId(tyname) != value::TYPE_ID_INVALID;
}

bool IsRoleType(const uint32_t tyid) {
  // For role types, GetUnderlyingTypeId returns a different type id
  // For non-role types, it returns the same type id
  uint32_t scalar_tid = tyid & (~TYPE_ID_STL_ARRAY_BIT);
  return GetUnderlyingTypeId(scalar_tid) != scalar_tid;
}

//
// half float
//
namespace {

// https://www.realtime.bc.ca/articles/endian-safe.html
union HostEndianness {
  int i;
  char c[sizeof(int)];

  HostEndianness() : i(1) {}

  bool isBig() const { return c[0] == 0; }
  bool isLittle() const { return c[0] != 0; }
};

// https://gist.github.com/rygorous/2156668
// Little endian
union FP32le {
  unsigned int u;
  float f;
  struct {
    unsigned int Mantissa : 23;
    unsigned int Exponent : 8;
    unsigned int Sign : 1;
  } s;
};

// Big endian
union FP32be {
  unsigned int u;
  float f;
  struct {
    unsigned int Sign : 1;
    unsigned int Exponent : 8;
    unsigned int Mantissa : 23;
  } s;
};

// Little endian
union float16le {
  unsigned short u;
  struct {
    unsigned int Mantissa : 10;
    unsigned int Exponent : 5;
    unsigned int Sign : 1;
  } s;
};

// Big endian
union float16be {
  unsigned short u;
  struct {
    unsigned int Sign : 1;
    unsigned int Exponent : 5;
    unsigned int Mantissa : 10;
  } s;
};

float half_to_float_le(float16le h) {
  static const FP32le magic = {113 << 23};
  static const unsigned int shifted_exp = 0x7c00
                                          << 13;  // exponent mask after shift
  FP32le o;

  o.u = (h.u & 0x7fffU) << 13U;           // exponent/mantissa bits
  unsigned int exp_ = shifted_exp & o.u;  // just the exponent
  o.u += (127 - 15) << 23;                // exponent adjust

  // handle exponent special cases
  if (exp_ == shifted_exp)    // Inf/NaN?
    o.u += (128 - 16) << 23;  // extra exp adjust
  else if (exp_ == 0)         // Zero/Denormal?
  {
    o.u += 1 << 23;  // extra exp adjust
    o.f -= magic.f;  // renormalize
  }

  o.u |= (h.u & 0x8000U) << 16U;  // sign bit
  return o.f;
}

float half_to_float_be(float16be h) {
  static const FP32be magic = {113 << 23};
  static const unsigned int shifted_exp = 0x7c00
                                          << 13;  // exponent mask after shift
  FP32be o;

  o.u = (h.u & 0x7fffU) << 13U;           // exponent/mantissa bits
  unsigned int exp_ = shifted_exp & o.u;  // just the exponent
  o.u += (127 - 15) << 23;                // exponent adjust

  // handle exponent special cases
  if (exp_ == shifted_exp)    // Inf/NaN?
    o.u += (128 - 16) << 23;  // extra exp adjust
  else if (exp_ == 0)         // Zero/Denormal?
  {
    o.u += 1 << 23;  // extra exp adjust
    o.f -= magic.f;  // renormalize
  }

  o.u |= (h.u & 0x8000U) << 16U;  // sign bit
  return o.f;
}

half float_to_half_full_be(float _f) {
  FP32be f;
  f.f = _f;
  float16be o = {0};

  // Based on ISPC reference code (with minor modifications)
  if (f.s.Exponent == 0)  // Signed zero/denormal (which will underflow)
    o.s.Exponent = 0;
  else if (f.s.Exponent == 255)  // Inf or NaN (all exponent bits set)
  {
    o.s.Exponent = 31;
    o.s.Mantissa = f.s.Mantissa ? 0x200 : 0;  // NaN->qNaN and Inf->Inf
  } else                                      // Normalized number
  {
    // Exponent unbias the single, then bias the halfp
    int newexp = f.s.Exponent - 127 + 15;
    if (newexp >= 31)  // Overflow, return signed infinity
      o.s.Exponent = 31;
    else if (newexp <= 0)  // Underflow
    {
      if ((14 - newexp) <= 24)  // Mantissa might be non-zero
      {
        unsigned int mant = f.s.Mantissa | 0x800000;  // Hidden 1 bit
        o.s.Mantissa = mant >> (14 - newexp);
        if ((mant >> (13 - newexp)) & 1)  // Check for rounding
          o.u++;  // Round, might overflow into exp bit, but this is OK
      }
    } else {
      o.s.Exponent = static_cast<unsigned int>(newexp);
      o.s.Mantissa = f.s.Mantissa >> 13;
      if (f.s.Mantissa & 0x1000)  // Check for rounding
        o.u++;                    // Round, might overflow to inf, this is OK
    }
  }

  o.s.Sign = f.s.Sign;

  half ret;
  ret.value = (*reinterpret_cast<const uint16_t *>(&o));

  return ret;
}

half float_to_half_full_le(float _f) {
  FP32le f;
  f.f = _f;
  float16le o = {0};

  // Based on ISPC reference code (with minor modifications)
  if (f.s.Exponent == 0)  // Signed zero/denormal (which will underflow)
    o.s.Exponent = 0;
  else if (f.s.Exponent == 255)  // Inf or NaN (all exponent bits set)
  {
    o.s.Exponent = 31;
    o.s.Mantissa = f.s.Mantissa ? 0x200 : 0;  // NaN->qNaN and Inf->Inf
  } else                                      // Normalized number
  {
    // Exponent unbias the single, then bias the halfp
    int newexp = f.s.Exponent - 127 + 15;
    if (newexp >= 31)  // Overflow, return signed infinity
      o.s.Exponent = 31;
    else if (newexp <= 0)  // Underflow
    {
      if ((14 - newexp) <= 24)  // Mantissa might be non-zero
      {
        unsigned int mant = f.s.Mantissa | 0x800000;  // Hidden 1 bit
        o.s.Mantissa = mant >> (14 - newexp);
        if ((mant >> (13 - newexp)) & 1)  // Check for rounding
          o.u++;  // Round, might overflow into exp bit, but this is OK
      }
    } else {
      o.s.Exponent = static_cast<unsigned int>(newexp);
      o.s.Mantissa = f.s.Mantissa >> 13;
      if (f.s.Mantissa & 0x1000)  // Check for rounding
        o.u++;                    // Round, might overflow to inf, this is OK
    }
  }

  o.s.Sign = f.s.Sign;

  half ret;
  ret.value = (*reinterpret_cast<const uint16_t *>(&o));
  return ret;
}

}  // namespace

float half_to_float(half h) {
  // TODO: Compile time detection of endianness
  HostEndianness endian;

  if (endian.isBig()) {
    float16be f;
    f.u = h.value;
    return half_to_float_be(f);
  } else if (endian.isLittle()) {
    float16le f;
    f.u = h.value;
    return half_to_float_le(f);
  }

  ///???
  return std::numeric_limits<float>::quiet_NaN();
}

half float_to_half_full(float _f) {
  // TODO: Compile time detection of endianness
  HostEndianness endian;

  if (endian.isBig()) {
    return float_to_half_full_be(_f);
  } else if (endian.isLittle()) {
    return float_to_half_full_le(_f);
  }

  ///???
  half fp16{0};  // TODO: Raise exception or return NaN
  return fp16;
}

matrix2f::matrix2f(const matrix2d &src) {
  (*this) = src;
}

matrix2f &matrix2f::operator=(const matrix2d &src) {

  for (size_t j = 0; j < 2; j++) {
    for (size_t i = 0; i < 2; i++) {
      m[j][i] = float(src.m[j][i]);
    }
  }

  return *this;
}

matrix3f::matrix3f(const matrix3d &src) {
  (*this) = src;
}

matrix3f &matrix3f::operator=(const matrix3d &src) {

  for (size_t j = 0; j < 3; j++) {
    for (size_t i = 0; i < 3; i++) {
      m[j][i] = float(src.m[j][i]);
    }
  }

  return *this;
}

matrix4f::matrix4f(const matrix4d &src) {
  (*this) = src;
}

matrix4f &matrix4f::operator=(const matrix4d &src) {

  for (size_t j = 0; j < 4; j++) {
    for (size_t i = 0; i < 4; i++) {
      m[j][i] = float(src.m[j][i]);
    }
  }

  return *this;
}

matrix2d &matrix2d::operator=(const matrix2f &src) {

  for (size_t j = 0; j < 2; j++) {
    for (size_t i = 0; i < 2; i++) {
      m[j][i] = double(src.m[j][i]);
    }
  }

  return *this;
}

matrix3d &matrix3d::operator=(const matrix3f &src) {

  for (size_t j = 0; j < 3; j++) {
    for (size_t i = 0; i < 3; i++) {
      m[j][i] = double(src.m[j][i]);
    }
  }

  return *this;
}

matrix4d &matrix4d::operator=(const matrix4f &src) {

  for (size_t j = 0; j < 4; j++) {
    for (size_t i = 0; i < 4; i++) {
      m[j][i] = double(src.m[j][i]);
    }
  }

  return *this;
}

// NOTE: RoleTypeCast and UpcastType are utility functions needed by both implementations
// They will be defined after the Value class implementation methods

#if !defined(TUSDZ_NEW_32BYTE_VALUE) && !defined(TUSDZ_NEW_VALUE_TYPE)

size_t Value::array_size() const {
  if (!is_array()) {
    return 0;
  }

  // primvar types only.

#define APPLY_FUNC_TO_TYPES(__FUNC) \
  __FUNC(bool)                 \
  __FUNC(value::token)                 \
  __FUNC(std::string)                 \
  __FUNC(StringData)                 \
  __FUNC(half)                 \
  __FUNC(half2)                \
  __FUNC(half3)                \
  __FUNC(half4)                \
  __FUNC(int32_t)              \
  __FUNC(uint32_t)             \
  __FUNC(int2)                 \
  __FUNC(int3)                 \
  __FUNC(int4)                 \
  __FUNC(uint2)                \
  __FUNC(uint3)                \
  __FUNC(uint4)                \
  __FUNC(int64_t)              \
  __FUNC(uint64_t)             \
  __FUNC(float)                \
  __FUNC(float2)               \
  __FUNC(float3)               \
  __FUNC(float4)               \
  __FUNC(double)               \
  __FUNC(double2)              \
  __FUNC(double3)              \
  __FUNC(double4)              \
  __FUNC(quath)                \
  __FUNC(quatf)                \
  __FUNC(quatd)                \
  __FUNC(normal3h)             \
  __FUNC(normal3f)             \
  __FUNC(normal3d)             \
  __FUNC(vector3h)             \
  __FUNC(vector3f)             \
  __FUNC(vector3d)             \
  __FUNC(point3h)              \
  __FUNC(point3f)              \
  __FUNC(point3d)              \
  __FUNC(color3f)              \
  __FUNC(color3d)              \
  __FUNC(color4h)              \
  __FUNC(color4f)              \
  __FUNC(color4d)              \
  __FUNC(texcoord2h)           \
  __FUNC(texcoord2f)           \
  __FUNC(texcoord2d)           \
  __FUNC(texcoord3h)           \
  __FUNC(texcoord3f)           \
  __FUNC(texcoord3d) \
  __FUNC(matrix2d) \
  __FUNC(matrix3d) \
  __FUNC(matrix4d) \
  __FUNC(frame4d)

#define ARRAY_SIZE_GET(__ty) case value::TypeTraits<__ty>::type_id() | value::TYPE_ID_STL_ARRAY_BIT: { \
    if (auto pv = v_.cast<std::vector<__ty>>()) { \
      return pv->size(); \
    } \
    return 0; \
  }


  switch (v_.type_id()) {
    APPLY_FUNC_TO_TYPES(ARRAY_SIZE_GET)
    default:
      return 0;
  }

#undef ARRAY_SIZE_GET
#undef APPLY_FUNC_TO_TYPES
}

//
// Compile-time validation for safe role type casting.
// These static_asserts ensure that the zero-copy cast is safe:
//   1. Both types must have the same size
//   2. Both types must have the same alignment
//   3. Both types must be trivially copyable (standard layout)
//
#define VALIDATE_ROLE_TYPE_CAST(__roleTy, __srcBaseTy)                         \
  static_assert(sizeof(__roleTy) == sizeof(__srcBaseTy),                       \
                "Role type and base type must have same size");                \
  static_assert(alignof(__roleTy) == alignof(__srcBaseTy),                     \
                "Role type and base type must have same alignment");           \
  static_assert(std::is_trivially_copyable<__roleTy>::value,                   \
                "Role type must be trivially copyable");                       \
  static_assert(std::is_trivially_copyable<__srcBaseTy>::value,                \
                "Base type must be trivially copyable");

// Validate all supported role type cast combinations at compile time
// texcoord types
VALIDATE_ROLE_TYPE_CAST(value::texcoord2h, value::half2)
VALIDATE_ROLE_TYPE_CAST(value::texcoord2f, value::float2)
VALIDATE_ROLE_TYPE_CAST(value::texcoord2d, value::double2)
VALIDATE_ROLE_TYPE_CAST(value::texcoord3h, value::half3)
VALIDATE_ROLE_TYPE_CAST(value::texcoord3f, value::float3)
VALIDATE_ROLE_TYPE_CAST(value::texcoord3d, value::double3)

// normal types
VALIDATE_ROLE_TYPE_CAST(value::normal3h, value::half3)
VALIDATE_ROLE_TYPE_CAST(value::normal3f, value::float3)
VALIDATE_ROLE_TYPE_CAST(value::normal3d, value::double3)

// vector types
VALIDATE_ROLE_TYPE_CAST(value::vector3h, value::half3)
VALIDATE_ROLE_TYPE_CAST(value::vector3f, value::float3)
VALIDATE_ROLE_TYPE_CAST(value::vector3d, value::double3)

// point types
VALIDATE_ROLE_TYPE_CAST(value::point3h, value::half3)
VALIDATE_ROLE_TYPE_CAST(value::point3f, value::float3)
VALIDATE_ROLE_TYPE_CAST(value::point3d, value::double3)

// color types
VALIDATE_ROLE_TYPE_CAST(value::color3h, value::half3)
VALIDATE_ROLE_TYPE_CAST(value::color3f, value::float3)
VALIDATE_ROLE_TYPE_CAST(value::color3d, value::double3)
VALIDATE_ROLE_TYPE_CAST(value::color4h, value::half4)
VALIDATE_ROLE_TYPE_CAST(value::color4f, value::float4)
VALIDATE_ROLE_TYPE_CAST(value::color4d, value::double4)

// frame type
VALIDATE_ROLE_TYPE_CAST(value::frame4d, value::matrix4d)

#undef VALIDATE_ROLE_TYPE_CAST

#endif // !TUSDZ_NEW_32BYTE_VALUE && !TUSDZ_NEW_VALUE_TYPE

bool RoleTypeCast(const uint32_t roleTyId, value::Value &inout) {
  const uint32_t srcUnderlyingTyId = inout.underlying_type_id();

  DCOUT("input type = " << inout.type_name());

  // Zero-copy role type cast: just change the vtable pointer.
  // This works because role types have identical memory layout to their base types.
  // The compile-time validation above ensures this is always safe.
#define ROLE_TYPE_CAST(__roleTy, __srcBaseTy)                                  \
  {                                                                            \
    static_assert(value::TypeTraits<__roleTy>::size() ==                       \
                      value::TypeTraits<__srcBaseTy>::size(),                  \
                  "Role type and base type must have same size");              \
    if (srcUnderlyingTyId == value::TypeTraits<__srcBaseTy>::type_id()) {      \
      if (roleTyId == value::TypeTraits<__roleTy>::type_id()) {                \
        inout.get_raw_mutable().unsafe_reinterpret_as<__roleTy>();             \
        return true;                                                           \
      }                                                                        \
    } else if (srcUnderlyingTyId ==                                            \
               (value::TypeTraits<__srcBaseTy>::type_id() |                    \
                value::TYPE_ID_STL_ARRAY_BIT)) {                                \
      if (roleTyId == value::TypeTraits<std::vector<__roleTy>>::type_id()) {   \
        inout.get_raw_mutable().unsafe_reinterpret_as<std::vector<__roleTy>>();\
        return true;                                                           \
      }                                                                        \
    }                                                                          \
  }

  ROLE_TYPE_CAST(value::texcoord2h, value::half2)
  ROLE_TYPE_CAST(value::texcoord2f, value::float2)
  ROLE_TYPE_CAST(value::texcoord2d, value::double2)

  ROLE_TYPE_CAST(value::texcoord3h, value::half3)
  ROLE_TYPE_CAST(value::texcoord3f, value::float3)
  ROLE_TYPE_CAST(value::texcoord3d, value::double3)

  ROLE_TYPE_CAST(value::normal3h, value::half3)
  ROLE_TYPE_CAST(value::normal3f, value::float3)
  ROLE_TYPE_CAST(value::normal3d, value::double3)

  ROLE_TYPE_CAST(value::vector3h, value::half3)
  ROLE_TYPE_CAST(value::vector3f, value::float3)
  ROLE_TYPE_CAST(value::vector3d, value::double3)

  ROLE_TYPE_CAST(value::point3h, value::half3)
  ROLE_TYPE_CAST(value::point3f, value::float3)
  ROLE_TYPE_CAST(value::point3d, value::double3)

  ROLE_TYPE_CAST(value::color3h, value::half3)
  ROLE_TYPE_CAST(value::color3f, value::float3)
  ROLE_TYPE_CAST(value::color3d, value::double3)

  ROLE_TYPE_CAST(value::color4h, value::half4)
  ROLE_TYPE_CAST(value::color4f, value::float4)
  ROLE_TYPE_CAST(value::color4d, value::double4)

  ROLE_TYPE_CAST(value::frame4d, value::matrix4d)

#undef ROLE_TYPE_CAST

  return false;
}

// TODO: Use template
bool UpcastType(const std::string &reqType, value::Value &inout) {
  // `reqType` may be Role type. Get underlying type
  uint32_t tyid;
  if (auto pv = value::TryGetUnderlyingTypeId(reqType)) {
    tyid = pv.value();
  } else {
    // Invalid reqType.
    return false;
  }

  bool reqTypeArray = false;
  //uint32_t baseReqTyId;
  DCOUT("UpcastType trial: reqTy : " << reqType
                                     << ", valtype = " << inout.type_name());

  if (endsWith(reqType, "[]")) {
    reqTypeArray = true;
    //baseReqTyId = value::GetTypeId(removeSuffix(reqType, "[]"));
  } else {
    //baseReqTyId = value::GetTypeId(reqType);
  }
  DCOUT("is array: " << reqTypeArray);

  // For array
  if (reqTypeArray) {
    // TODO
  } else {
    if (tyid == value::TYPE_ID_FLOAT) {
      float dst;
      if (auto pv = inout.get_value<value::half>()) {
        dst = half_to_float(pv.value());
        inout = dst;
        return true;
      }
    } else if (tyid == value::TYPE_ID_FLOAT2) {
      if (auto pv = inout.get_value<value::half2>()) {
        value::float2 dst;
        value::half2 v = pv.value();
        dst[0] = half_to_float(v[0]);
        dst[1] = half_to_float(v[1]);
        inout = dst;
        return true;
      }

    } else if (tyid == value::TYPE_ID_FLOAT3) {
      value::float3 dst;
      if (auto pv = inout.get_value<value::half3>()) {
        value::half3 v = pv.value();
        dst[0] = half_to_float(v[0]);
        dst[1] = half_to_float(v[1]);
        dst[2] = half_to_float(v[2]);
        inout = dst;
        return true;
      }
    } else if (tyid == value::TYPE_ID_FLOAT4) {
      value::float4 dst;
      if (auto pv = inout.get_value<value::half4>()) {
        value::half4 v = pv.value();
        dst[0] = half_to_float(v[0]);
        dst[1] = half_to_float(v[1]);
        dst[2] = half_to_float(v[2]);
        dst[3] = half_to_float(v[3]);
        inout = dst;
        return true;
      }
    } else if (tyid == value::TYPE_ID_DOUBLE) {
      double dst;
      if (auto pv = inout.get_value<value::half>()) {
        dst = double(half_to_float(pv.value()));
        inout = dst;
        return true;
      }
    } else if (tyid == value::TYPE_ID_DOUBLE2) {
      value::double2 dst;
      if (auto pv = inout.get_value<value::half2>()) {
        value::half2 v = pv.value();
        dst[0] = double(half_to_float(v[0]));
        dst[1] = double(half_to_float(v[1]));
        inout = dst;
        return true;
      }
    } else if (tyid == value::TYPE_ID_DOUBLE3) {
      value::double3 dst;
      if (auto pv = inout.get_value<value::half3>()) {
        value::half3 v = pv.value();
        dst[0] = double(half_to_float(v[0]));
        dst[1] = double(half_to_float(v[1]));
        dst[2] = double(half_to_float(v[2]));
        inout = dst;
        return true;
      }
    } else if (tyid == value::TYPE_ID_DOUBLE4) {
      value::double4 dst;
      if (auto pv = inout.get_value<value::half4>()) {
        value::half4 v = pv.value();
        dst[0] = double(half_to_float(v[0]));
        dst[1] = double(half_to_float(v[1]));
        dst[2] = double(half_to_float(v[2]));
        dst[3] = double(half_to_float(v[3]));
        inout = dst;
        return true;
      }
    }
  }

  return false;
}

#if 0
bool FlexibleTypeCast(const value::Value &src, value::Value &dst) {
  uint32_t src_utype_id = src.type_id();
  uint32_t dst_utype_id = src.type_id();

  if (src_utype_id == value::TypeTraits<int32_t>::type_id()) {

  }

  // TODO

  return false;
}
#endif

#if !defined(TUSDZ_NEW_32BYTE_VALUE) && !defined(TUSDZ_NEW_VALUE_TYPE)

// Get byte size for a given type_id
static size_t GetTypeSize(uint32_t type_id) {
  // Remove array bit if present
  uint32_t base_type_id = type_id & (~TYPE_ID_STL_ARRAY_BIT);
  
  // Create a compile-time lookup table using switch
  switch (base_type_id) {
    // Primitives
    case TYPE_ID_BOOL: return sizeof(bool);
    case TYPE_ID_CHAR: return sizeof(char);
    case TYPE_ID_CHAR2: return sizeof(char) * 2;
    case TYPE_ID_CHAR3: return sizeof(char) * 3;
    case TYPE_ID_CHAR4: return sizeof(char) * 4;
    
    // Half precision
    case TYPE_ID_HALF: return sizeof(half);
    case TYPE_ID_HALF2: return sizeof(half) * 2;
    case TYPE_ID_HALF3: return sizeof(half) * 3;
    case TYPE_ID_HALF4: return sizeof(half) * 4;
    
    // Integers
    case TYPE_ID_INT32: return sizeof(int32_t);
    case TYPE_ID_INT2: return sizeof(int32_t) * 2;
    case TYPE_ID_INT3: return sizeof(int32_t) * 3;
    case TYPE_ID_INT4: return sizeof(int32_t) * 4;
    case TYPE_ID_INT64: return sizeof(int64_t);
    
    // Unsigned integers
    case TYPE_ID_UCHAR: return sizeof(uint8_t);
    case TYPE_ID_UCHAR2: return sizeof(uint8_t) * 2;
    case TYPE_ID_UCHAR3: return sizeof(uint8_t) * 3;
    case TYPE_ID_UCHAR4: return sizeof(uint8_t) * 4;
    case TYPE_ID_UINT32: return sizeof(uint32_t);
    case TYPE_ID_UINT2: return sizeof(uint32_t) * 2;
    case TYPE_ID_UINT3: return sizeof(uint32_t) * 3;
    case TYPE_ID_UINT4: return sizeof(uint32_t) * 4;
    case TYPE_ID_UINT64: return sizeof(uint64_t);
    
    // Short integers
    case TYPE_ID_SHORT: return sizeof(int16_t);
    case TYPE_ID_SHORT2: return sizeof(int16_t) * 2;
    case TYPE_ID_SHORT3: return sizeof(int16_t) * 3;
    case TYPE_ID_SHORT4: return sizeof(int16_t) * 4;
    case TYPE_ID_USHORT: return sizeof(uint16_t);
    case TYPE_ID_USHORT2: return sizeof(uint16_t) * 2;
    case TYPE_ID_USHORT3: return sizeof(uint16_t) * 3;
    case TYPE_ID_USHORT4: return sizeof(uint16_t) * 4;
    
    // Floats
    case TYPE_ID_FLOAT: return sizeof(float);
    case TYPE_ID_FLOAT2: return sizeof(float) * 2;
    case TYPE_ID_FLOAT3: return sizeof(float) * 3;
    case TYPE_ID_FLOAT4: return sizeof(float) * 4;
    
    // Doubles
    case TYPE_ID_DOUBLE: return sizeof(double);
    case TYPE_ID_DOUBLE2: return sizeof(double) * 2;
    case TYPE_ID_DOUBLE3: return sizeof(double) * 3;
    case TYPE_ID_DOUBLE4: return sizeof(double) * 4;
    
    // Quaternions
    case TYPE_ID_QUATH: return sizeof(half) * 4;
    case TYPE_ID_QUATF: return sizeof(float) * 4;
    case TYPE_ID_QUATD: return sizeof(double) * 4;
    
    // Matrices
    case TYPE_ID_MATRIX2F: return sizeof(float) * 4;   // 2x2
    case TYPE_ID_MATRIX3F: return sizeof(float) * 9;   // 3x3
    case TYPE_ID_MATRIX4F: return sizeof(float) * 16;  // 4x4
    case TYPE_ID_MATRIX2D: return sizeof(double) * 4;  // 2x2
    case TYPE_ID_MATRIX3D: return sizeof(double) * 9;  // 3x3
    case TYPE_ID_MATRIX4D: return sizeof(double) * 16; // 4x4
    
    // Colors (role types - same memory as their underlying types)
    case TYPE_ID_COLOR3H: return sizeof(half) * 3;
    case TYPE_ID_COLOR3F: return sizeof(float) * 3;
    case TYPE_ID_COLOR3D: return sizeof(double) * 3;
    case TYPE_ID_COLOR4H: return sizeof(half) * 4;
    case TYPE_ID_COLOR4F: return sizeof(float) * 4;
    case TYPE_ID_COLOR4D: return sizeof(double) * 4;
    
    // Points (role types)
    case TYPE_ID_POINT3H: return sizeof(half) * 3;
    case TYPE_ID_POINT3F: return sizeof(float) * 3;
    case TYPE_ID_POINT3D: return sizeof(double) * 3;
    
    // Normals (role types)
    case TYPE_ID_NORMAL3H: return sizeof(half) * 3;
    case TYPE_ID_NORMAL3F: return sizeof(float) * 3;
    case TYPE_ID_NORMAL3D: return sizeof(double) * 3;
    
    // Vectors (role types)
    case TYPE_ID_VECTOR3H: return sizeof(half) * 3;
    case TYPE_ID_VECTOR3F: return sizeof(float) * 3;
    case TYPE_ID_VECTOR3D: return sizeof(double) * 3;
    
    // Texture coordinates (role types)
    case TYPE_ID_TEXCOORD2H: return sizeof(half) * 2;
    case TYPE_ID_TEXCOORD2F: return sizeof(float) * 2;
    case TYPE_ID_TEXCOORD2D: return sizeof(double) * 2;
    case TYPE_ID_TEXCOORD3H: return sizeof(half) * 3;
    case TYPE_ID_TEXCOORD3F: return sizeof(float) * 3;
    case TYPE_ID_TEXCOORD3D: return sizeof(double) * 3;
    
    // Special types
    case TYPE_ID_FRAME4D: return sizeof(double) * 16; // 4x4 matrix
    case TYPE_ID_EXTENT: return sizeof(float) * 6;    // float3[2]
    case TYPE_ID_TIMECODE: return sizeof(double);
    
    // String/token types - estimate with typical sizes
    case TYPE_ID_TOKEN: return 32;  // Estimate for typical token string
    case TYPE_ID_STRING: return 64; // Estimate for typical string
    case TYPE_ID_STRING_DATA: return 64; // Estimate for string data
    case TYPE_ID_ASSET_PATH: return 128; // Estimate for asset paths
    
    // Special values
    case TYPE_ID_VOID: return 0;
    case TYPE_ID_NULL: return 0;
    case TYPE_ID_MONOSTATE: return 0;
    case TYPE_ID_VALUEBLOCK: return 0;
    
    // Complex types - return base struct size
    case TYPE_ID_DICT: return sizeof(void*) * 2; // Rough estimate for map overhead
    case TYPE_ID_CUSTOMDATA: return sizeof(void*) * 2;
    
    // Default for unknown types
    default: return sizeof(void*); // Pointer size as fallback
  }
}

#endif // !TUSDZ_NEW_32BYTE_VALUE && !TUSDZ_NEW_VALUE_TYPE

#if !defined(TUSDZ_NEW_32BYTE_VALUE) && !defined(TUSDZ_NEW_VALUE_TYPE)

size_t Value::estimate_memory_usage() const {
  size_t total_size = sizeof(Value); // Base object size
  
  if (is_empty() || is_none()) {
    return total_size;
  }
  
  uint32_t tid = type_id();
  
  // Check if it's an array type
  if (tid & TYPE_ID_STL_ARRAY_BIT) {
    // For arrays, compute element size * array count
    size_t element_size = GetTypeSize(tid);
    size_t element_count = array_size();
    
    // Add array storage overhead (vector typically has 3 pointers)
    total_size += sizeof(void*) * 3; 
    
    // Add actual data size
    total_size += element_size * element_count;
    
    // Handle special cases for string arrays
    uint32_t base_type = tid & (~TYPE_ID_STL_ARRAY_BIT);
    if (base_type == TYPE_ID_STRING || base_type == TYPE_ID_TOKEN || 
        base_type == TYPE_ID_STRING_DATA || base_type == TYPE_ID_ASSET_PATH) {
      // For string arrays, add estimated string sizes
      if (auto* vec = as<std::vector<std::string>>()) {
        for (const auto& str : *vec) {
          total_size += str.capacity();
        }
      } else if (auto* tokVec = as<std::vector<value::token>>()) {
        for (const auto& tok : *tokVec) {
          total_size += tok.str().capacity();
        }
      }
    }
  } else {
    // For scalar types
    size_t type_size = GetTypeSize(tid);
    total_size += type_size;
    
    // Handle dynamic string types specially
    if (tid == TYPE_ID_STRING || tid == TYPE_ID_STRING_DATA) {
      if (auto* str = as<std::string>()) {
        total_size += str->capacity();
      }
    } else if (tid == TYPE_ID_TOKEN) {
      if (auto* tok = as<value::token>()) {
        total_size += tok->str().capacity();
      }
    } else if (tid == TYPE_ID_ASSET_PATH) {
      if (auto* path = as<value::AssetPath>()) {
        total_size += path->GetAssetPath().length();
        total_size += path->GetResolvedPath().length();
      }
    } else if (tid == TYPE_ID_DICT || tid == TYPE_ID_CUSTOMDATA) {
      // For dictionary types, estimate based on typical usage
      if (auto* dict = as<value::dict>()) {
        // Map overhead + estimated key/value sizes
        total_size += dict->size() * (32 + sizeof(void*) * 4);
        // Recursively compute values (simplified - just add base estimates)
        for (const auto& kv : *dict) {
          total_size += kv.first.capacity();
          // For values, use a rough estimate
          total_size += 64; // Average value size estimate
        }
      }
    }
  }
  
  return total_size;
}

#endif // !TUSDZ_NEW_32BYTE_VALUE && !TUSDZ_NEW_VALUE_TYPE

bool TimeSamples::has_sample_at(const double t) const {
  if (_dirty) {
    update();
  }

  const auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample &s) {
    return math::is_close(t, s.t);
  });

  return (it != _samples.end());
}

bool TimeSamples::get_sample_at(const double t, Sample **dst) {
  if (!dst) {
    return false;
  }

  if (_dirty) {
    update();
  }

  const auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample &sample) {
    return math::is_close(t, sample.t);
  });

  if (it != _samples.end()) {
    (*dst) = const_cast<Sample*>(&(*it));
    return true;  // Found the sample!
  }
  return false;
}

#if defined(TUSDZ_NEW_32BYTE_VALUE) || defined(TUSDZ_NEW_VALUE_TYPE)
//
// New optimized Value implementation (32 bytes)
//

//
// Helper macros for type dispatch
//
#define DISPATCH_SCALAR_TYPE(MACRO) \
  MACRO(bool) \
  MACRO(uint8_t) \
  MACRO(char) \
  MACRO(int16_t) \
  MACRO(uint16_t) \
  MACRO(int32_t) \
  MACRO(uint32_t) \
  MACRO(int64_t) \
  MACRO(uint64_t) \
  MACRO(half) \
  MACRO(float) \
  MACRO(double) \
  MACRO(half2) \
  MACRO(half3) \
  MACRO(half4) \
  MACRO(int2) \
  MACRO(int3) \
  MACRO(int4) \
  MACRO(uint2) \
  MACRO(uint3) \
  MACRO(uint4) \
  MACRO(float2) \
  MACRO(float3) \
  MACRO(float4) \
  MACRO(double2) \
  MACRO(double3) \
  MACRO(double4) \
  MACRO(quath) \
  MACRO(quatf) \
  MACRO(quatd) \
  MACRO(matrix2f) \
  MACRO(matrix3f) \
  MACRO(matrix4f) \
  MACRO(matrix2d) \
  MACRO(matrix3d) \
  MACRO(matrix4d) \
  MACRO(normal3h) \
  MACRO(normal3f) \
  MACRO(normal3d) \
  MACRO(vector3h) \
  MACRO(vector3f) \
  MACRO(vector3d) \
  MACRO(point3h) \
  MACRO(point3f) \
  MACRO(point3d) \
  MACRO(color3h) \
  MACRO(color3f) \
  MACRO(color3d) \
  MACRO(color4h) \
  MACRO(color4f) \
  MACRO(color4d) \
  MACRO(texcoord2h) \
  MACRO(texcoord2f) \
  MACRO(texcoord2d) \
  MACRO(texcoord3h) \
  MACRO(texcoord3f) \
  MACRO(texcoord3d) \
  MACRO(frame4d)

#define DISPATCH_STRING_TYPE(MACRO) \
  MACRO(std::string) \
  MACRO(StringData) \
  MACRO(token) \
  MACRO(AssetPath)

// Complex types that are always heap-allocated
// Only types that are visible in value-types.cc are included here
// Note: Path and TimeSamples have TypeTraits in prim-types.hh (not included here)
// so they are handled separately using TYPE_ID constants
#define DISPATCH_COMPLEX_TYPE(MACRO) \
  MACRO(dict) \
  MACRO(timecode) \
  MACRO(ValueBlock)

// Helper to get array class from flags
static inline Value::ArrayClass GetArrayClass(uint8_t flags) {
  if (!(flags & Value::kArrayBitFlag)) {
    return Value::ArrayClass::Invalid;
  }
  uint8_t class_bits = (flags & Value::kArrayClassMask) >> Value::kArrayClassShift;
  return static_cast<Value::ArrayClass>(class_bits);
}

//
// Copy data from another Value
//
void Value::copy_data_from(const Value& rhs) {
  // CRITICAL: Always clear data_ first to prevent garbage
  std::memset(data_, 0, sizeof(data_));

  if (rhs.is_empty()) {
    return;
  }

  // Check if heap-allocated
  if (rhs.flags_ & kHeapAllocatedFlag) {
    // Need to deep copy heap-allocated data
    // Extract the pointer from rhs.data_
    void* src_ptr;
    std::memcpy(&src_ptr, rhs.data_, sizeof(void*));

    // Dispatch based on type_id to call appropriate copy constructor

#define COPY_SCALAR_TYPE(T) \
    if (rhs.type_id_ == TypeTraits<T>::type_id()) { \
      const T* src = reinterpret_cast<const T*>(src_ptr); \
      T* dst = new T(*src); \
      void* dst_ptr = reinterpret_cast<void*>(dst); \
      std::memcpy(data_, &dst_ptr, sizeof(void*)); \
      return; \
    }

// Check both std::vector (STL_ARRAY_BIT only) and TypedArray (both STL_ARRAY_BIT and TYPED_ARRAY_BIT)
#define COPY_ARRAY_TYPE_VECTOR(T) \
    if (rhs.type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT)) { \
      ArrayClass aclass = GetArrayClass(rhs.flags_); \
      if (aclass == ArrayClass::StdVector) { \
        const std::vector<T>* src = reinterpret_cast<const std::vector<T>*>(src_ptr); \
        std::vector<T>* dst = new std::vector<T>(*src); \
        void* dst_ptr = reinterpret_cast<void*>(dst); \
        std::memcpy(data_, &dst_ptr, sizeof(void*)); \
        return; \
      } \
    }

#define COPY_TYPED_ARRAY_TYPE(T) \
    if (rhs.type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT | TYPE_ID_TYPED_ARRAY_BIT)) { \
      ArrayClass aclass = GetArrayClass(rhs.flags_); \
      if (aclass == ArrayClass::TypedArray) { \
        const TypedArray<T>* src = reinterpret_cast<const TypedArray<T>*>(src_ptr); \
        TypedArray<T>* dst = new TypedArray<T>(*src); \
        void* dst_ptr = reinterpret_cast<void*>(dst); \
        std::memcpy(data_, &dst_ptr, sizeof(void*)); \
        return; \
      } \
    }

    DISPATCH_SCALAR_TYPE(COPY_SCALAR_TYPE)
    DISPATCH_SCALAR_TYPE(COPY_ARRAY_TYPE_VECTOR)
    DISPATCH_SCALAR_TYPE(COPY_TYPED_ARRAY_TYPE)
    DISPATCH_STRING_TYPE(COPY_SCALAR_TYPE)
    DISPATCH_STRING_TYPE(COPY_ARRAY_TYPE_VECTOR)
    DISPATCH_STRING_TYPE(COPY_TYPED_ARRAY_TYPE)
    DISPATCH_COMPLEX_TYPE(COPY_SCALAR_TYPE)

    // Special vector types with dedicated type IDs (not following base_type | array_bits pattern)
    if (rhs.type_id_ == TYPE_ID_TOKEN_VECTOR) {
      const std::vector<token>* src = reinterpret_cast<const std::vector<token>*>(src_ptr);
      std::vector<token>* dst = new std::vector<token>(*src);
      void* dst_ptr = reinterpret_cast<void*>(dst);
      std::memcpy(data_, &dst_ptr, sizeof(void*));
      return;
    }

#undef COPY_SCALAR_TYPE
#undef COPY_ARRAY_TYPE_VECTOR
#undef COPY_TYPED_ARRAY_TYPE

    // Handle types defined in prim-types.hh/cc
    // These can't be handled via TypeTraits here since we can't include prim-types.hh
    if (rhs.type_id_ == TYPE_ID_CUSTOMDATA ||
        rhs.type_id_ == TYPE_ID_PAYLOAD ||
        rhs.type_id_ == TYPE_ID_REFERENCE ||
        rhs.type_id_ == TYPE_ID_PATH ||
        rhs.type_id_ == TYPE_ID_PATH_VECTOR ||
        rhs.type_id_ == (TYPE_ID_PAYLOAD | TYPE_ID_STL_ARRAY_BIT) ||
        rhs.type_id_ == (TYPE_ID_REFERENCE | TYPE_ID_STL_ARRAY_BIT) ||
        rhs.type_id_ == TYPE_ID_LIST_OP_TOKEN ||
        rhs.type_id_ == TYPE_ID_LIST_OP_STRING ||
        rhs.type_id_ == TYPE_ID_LIST_OP_PATH ||
        rhs.type_id_ == TYPE_ID_LIST_OP_REFERENCE ||
        rhs.type_id_ == TYPE_ID_LIST_OP_PAYLOAD ||
        rhs.type_id_ == TYPE_ID_TIMESAMPLES ||
        rhs.type_id_ == TYPE_ID_VARIANT_SELECION_MAP ||
        rhs.type_id_ == (TYPE_ID_LAYER_OFFSET | TYPE_ID_STL_ARRAY_BIT)) {
      void* dst_ptr = nullptr;
      if (CopyModelValue(rhs.type_id_, &dst_ptr, src_ptr)) {
        std::memcpy(data_, &dst_ptr, sizeof(void*));
        return;
      }
    }

    // Handle MODEL types (Prim types like Xform, GeomMesh, etc.)
    // These are defined in prim-types.hh/cc which we can't include here
    if (rhs.type_id_ >= TYPE_ID_MODEL_BEGIN && rhs.type_id_ < TYPE_ID_MODEL_END) {
      void* dst_ptr = nullptr;
      if (CopyModelValue(rhs.type_id_, &dst_ptr, src_ptr)) {
        std::memcpy(data_, &dst_ptr, sizeof(void*));
        return;
      }
      // If CopyModelValue returns false, fall through to fallback
    }

    // Fallback for unknown types - cannot safely deep copy
    // Rather than creating a dangling pointer (shallow copy that becomes invalid when
    // original is destroyed), we set the Value to null to indicate copy failure.
    // This prevents use-after-free crashes at the cost of losing the data.
    // NOTE: If you encounter this, add the type to the appropriate DISPATCH macro.
    //
    // Log warning for debugging (in debug builds)
#if !defined(NDEBUG)
    std::cerr << "[WARNING] Value::copy_data_from: Unknown heap-allocated type_id="
              << rhs.type_id_ << ", cannot deep copy. Setting to null." << std::endl;
#endif
    type_id_ = TYPE_ID_NULL;
    flags_ = 0;
    std::memset(data_, 0, sizeof(data_));
  } else {
    // Inline data - simple copy
    std::memcpy(data_, rhs.data_, sizeof(data_));
  }
}

//
// Destroy heap-allocated data
//
void Value::destroy() {
  if (is_empty() || type_id_ == TYPE_ID_INVALID) {
    return;
  }

  if (!(flags_ & kHeapAllocatedFlag)) {
    // Inlined data - nothing to free
    return;
  }

  // CRITICAL SAFETY CHECK: Verify this type actually should be heap-allocated
  // If the type is small and trivially copyable, it should be inlined!
  // This protects against corrupted heap flags.
  // Check common scalar types that should NEVER be heap-allocated:
  switch (type_id_) {
    case TYPE_ID_BOOL:
    case TYPE_ID_CHAR:
    case TYPE_ID_HALF:
    case TYPE_ID_INT32:
    case TYPE_ID_UINT32:
    case TYPE_ID_INT64:
    case TYPE_ID_UINT64:
    case TYPE_ID_FLOAT:
    case TYPE_ID_DOUBLE:
    case TYPE_ID_FLOAT2:
    case TYPE_ID_FLOAT3:
    case TYPE_ID_FLOAT4:
    case TYPE_ID_DOUBLE2:
    case TYPE_ID_DOUBLE3:
    case TYPE_ID_DOUBLE4:
    case TYPE_ID_INT2:
    case TYPE_ID_INT3:
    case TYPE_ID_INT4:
      // These types should ALWAYS be inlined - heap flag is WRONG!
      // Clear the flag and return without freeing
      flags_ &= ~kHeapAllocatedFlag;
      return;
    default:
      break;  // Continue with heap deletion for other types
  }

  // Heap-allocated - need to delete based on actual type
  // Extract pointer from data_
  void* ptr;
  std::memcpy(&ptr, data_, sizeof(void*));

  // Sanity check: ptr should not be NULL for heap-allocated data
  if (ptr == nullptr) {
    // This indicates a bug - heap flag set but no valid pointer
    // Clear the flag to prevent future errors
    flags_ &= ~kHeapAllocatedFlag;
    return;
  }

  // Additional validation: check if pointer looks valid (not obviously garbage)
  // Heuristic: pointer should be heap-aligned (at least 8-byte aligned on 64-bit)
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  if ((addr == 0) || (addr < 0x1000) || (addr & 0x7) != 0) {
    // Pointer looks invalid - likely corrupted heap flag
    // Clear flag and bail out to prevent crash
    flags_ &= ~kHeapAllocatedFlag;
    return;
  }

#define DESTROY_SCALAR_TYPE(T) \
  if (type_id_ == TypeTraits<T>::type_id()) { \
    T* typed_ptr = reinterpret_cast<T*>(ptr); \
    delete typed_ptr; \
    return; \
  }

#define DESTROY_ARRAY_TYPE(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT)) { \
    std::vector<T>* typed_ptr = reinterpret_cast<std::vector<T>*>(ptr); \
    delete typed_ptr; \
    return; \
  }

#define DESTROY_TYPED_ARRAY_TYPE(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT | TYPE_ID_TYPED_ARRAY_BIT)) { \
    TypedArray<T>* typed_ptr = reinterpret_cast<TypedArray<T>*>(ptr); \
    delete typed_ptr; \
    return; \
  }

  DISPATCH_SCALAR_TYPE(DESTROY_SCALAR_TYPE)
  DISPATCH_SCALAR_TYPE(DESTROY_ARRAY_TYPE)
  DISPATCH_SCALAR_TYPE(DESTROY_TYPED_ARRAY_TYPE)
  DISPATCH_STRING_TYPE(DESTROY_SCALAR_TYPE)
  DISPATCH_STRING_TYPE(DESTROY_ARRAY_TYPE)
  DISPATCH_STRING_TYPE(DESTROY_TYPED_ARRAY_TYPE)
  DISPATCH_COMPLEX_TYPE(DESTROY_SCALAR_TYPE)

  // Special vector types with dedicated type IDs (not following base_type | array_bits pattern)
  if (type_id_ == TYPE_ID_TOKEN_VECTOR) {
    std::vector<token>* typed_ptr = reinterpret_cast<std::vector<token>*>(ptr);
    delete typed_ptr;
    return;
  }

#undef DESTROY_SCALAR_TYPE
#undef DESTROY_ARRAY_TYPE
#undef DESTROY_TYPED_ARRAY_TYPE

  // Handle types defined in prim-types.hh/cc
  if (type_id_ == TYPE_ID_CUSTOMDATA ||
      type_id_ == TYPE_ID_PAYLOAD ||
      type_id_ == TYPE_ID_REFERENCE ||
      type_id_ == TYPE_ID_PATH ||
      type_id_ == TYPE_ID_PATH_VECTOR ||
      type_id_ == (TYPE_ID_PAYLOAD | TYPE_ID_STL_ARRAY_BIT) ||
      type_id_ == (TYPE_ID_REFERENCE | TYPE_ID_STL_ARRAY_BIT) ||
      type_id_ == TYPE_ID_LIST_OP_TOKEN ||
      type_id_ == TYPE_ID_LIST_OP_STRING ||
      type_id_ == TYPE_ID_LIST_OP_PATH ||
      type_id_ == TYPE_ID_LIST_OP_REFERENCE ||
      type_id_ == TYPE_ID_LIST_OP_PAYLOAD ||
      type_id_ == TYPE_ID_TIMESAMPLES ||
      type_id_ == TYPE_ID_VARIANT_SELECION_MAP ||
      type_id_ == (TYPE_ID_LAYER_OFFSET | TYPE_ID_STL_ARRAY_BIT)) {
    if (DestroyModelValue(type_id_, ptr)) {
      return;
    }
  }

  // Handle MODEL types (Prim types like Xform, GeomMesh, etc.)
  // These are defined in prim-types.hh/cc which we can't include here
  if (type_id_ >= TYPE_ID_MODEL_BEGIN && type_id_ < TYPE_ID_MODEL_END) {
    if (DestroyModelValue(type_id_, ptr)) {
      return;
    }
    // If DestroyModelValue returns false, fall through to generic delete
  }

  // Fallback for unknown types - free the memory but can't call destructor
  // WARNING: This may cause resource leaks for types with non-trivial destructors
  if (ptr) {
    // Use operator delete to free the memory
    // Note: This doesn't call the destructor, so resources owned by the object may leak
#if !defined(NDEBUG)
    std::cerr << "[WARNING] Value::destroy: Unknown heap-allocated type_id="
              << type_id_ << ", using raw delete (destructor not called)." << std::endl;
#endif
    ::operator delete(ptr);
  }
}

//
// Get array size for array types (NEW implementation)
//
size_t Value::array_size() const {
  if (!is_array()) {
    return 0;
  }

  // Arrays must be heap-allocated (std::vector and TypedArray are not trivially copyable)
  if (!(flags_ & kHeapAllocatedFlag)) {
    // This shouldn't happen for properly constructed arrays, but return 0 for safety
    return 0;
  }

  // Extract pointer from data_
  void* ptr;
  std::memcpy(&ptr, data_, sizeof(void*));

#define GET_ARRAY_SIZE_VECTOR(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT)) { \
    const std::vector<T>* vec = reinterpret_cast<const std::vector<T>*>(ptr); \
    return vec->size(); \
  }

#define GET_ARRAY_SIZE_TYPED(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT | TYPE_ID_TYPED_ARRAY_BIT)) { \
    const TypedArray<T>* arr = reinterpret_cast<const TypedArray<T>*>(ptr); \
    return arr->size(); \
  }

  DISPATCH_SCALAR_TYPE(GET_ARRAY_SIZE_VECTOR)
  DISPATCH_SCALAR_TYPE(GET_ARRAY_SIZE_TYPED)

#undef GET_ARRAY_SIZE_VECTOR
#undef GET_ARRAY_SIZE_TYPED

  return 0;
}

//
// Estimate memory usage
//
size_t Value::estimate_memory_usage() const {
  size_t total = sizeof(Value);  // Base object size

  if (is_empty() || is_none()) {
    return total;
  }

  if (!(flags_ & kHeapAllocatedFlag)) {
    // Inlined - no additional memory
    return total;
  }

  // Heap-allocated - estimate based on type

#define ESTIMATE_SCALAR_SIZE(T) \
  if (type_id_ == TypeTraits<T>::type_id()) { \
    return total + sizeof(T); \
  }

#define ESTIMATE_ARRAY_SIZE_VECTOR(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT)) { \
    void* ptr; \
    std::memcpy(&ptr, data_, sizeof(void*)); \
    const std::vector<T>* vec = reinterpret_cast<const std::vector<T>*>(ptr); \
    return total + sizeof(std::vector<T>) + (vec->capacity() * sizeof(T)); \
  }

#define ESTIMATE_ARRAY_SIZE_TYPED(T) \
  if (type_id_ == (TypeTraits<T>::type_id() | TYPE_ID_STL_ARRAY_BIT | TYPE_ID_TYPED_ARRAY_BIT)) { \
    void* ptr; \
    std::memcpy(&ptr, data_, sizeof(void*)); \
    const TypedArray<T>* arr = reinterpret_cast<const TypedArray<T>*>(ptr); \
    return total + sizeof(TypedArray<T>) + (arr->size() * sizeof(T)); \
  }

  DISPATCH_SCALAR_TYPE(ESTIMATE_SCALAR_SIZE)
  DISPATCH_SCALAR_TYPE(ESTIMATE_ARRAY_SIZE_VECTOR)
  DISPATCH_SCALAR_TYPE(ESTIMATE_ARRAY_SIZE_TYPED)

#undef ESTIMATE_SCALAR_SIZE
#undef ESTIMATE_ARRAY_SIZE_VECTOR
#undef ESTIMATE_ARRAY_SIZE_TYPED

  // String types need special handling
  void* ptr;
  std::memcpy(&ptr, data_, sizeof(void*));

  if (type_id_ == TYPE_ID_STRING) {
    const std::string* str = reinterpret_cast<const std::string*>(ptr);
    return total + sizeof(std::string) + str->capacity();
  }

  if (type_id_ == (TYPE_ID_STRING | TYPE_ID_STL_ARRAY_BIT)) {
    const std::vector<std::string>* vec = reinterpret_cast<const std::vector<std::string>*>(ptr);
    size_t string_mem = 0;
    for (const auto& s : *vec) {
      string_mem += sizeof(std::string) + s.capacity();
    }
    return total + sizeof(std::vector<std::string>) + string_mem;
  }

  if (type_id_ == TYPE_ID_TOKEN) {
    const token* tok = reinterpret_cast<const token*>(ptr);
    return total + sizeof(token) + tok->str().capacity();
  }

  if (type_id_ == (TYPE_ID_TOKEN | TYPE_ID_STL_ARRAY_BIT)) {
    const std::vector<token>* vec = reinterpret_cast<const std::vector<token>*>(ptr);
    size_t token_mem = 0;
    for (const auto& t : *vec) {
      token_mem += sizeof(token) + t.str().capacity();
    }
    return total + sizeof(std::vector<token>) + token_mem;
  }

  // Default estimate for unknown types
  return total + 64;
}

#undef DISPATCH_SCALAR_TYPE
#undef DISPATCH_STRING_TYPE

#endif // TUSDZ_NEW_32BYTE_VALUE || TUSDZ_NEW_VALUE_TYPE

// Matrix comparison operators
bool operator==(const matrix2f &a, const matrix2f &b) {
  return math::is_close(a.m[0][0], b.m[0][0]) &&
         math::is_close(a.m[0][1], b.m[0][1]) &&
         math::is_close(a.m[1][0], b.m[1][0]) &&
         math::is_close(a.m[1][1], b.m[1][1]);
}

bool operator==(const matrix3f &a, const matrix3f &b) {
  return math::is_close(a.m[0][0], b.m[0][0]) &&
         math::is_close(a.m[0][1], b.m[0][1]) &&
         math::is_close(a.m[0][2], b.m[0][2]) &&
         math::is_close(a.m[1][0], b.m[1][0]) &&
         math::is_close(a.m[1][1], b.m[1][1]) &&
         math::is_close(a.m[1][2], b.m[1][2]) &&
         math::is_close(a.m[2][0], b.m[2][0]) &&
         math::is_close(a.m[2][1], b.m[2][1]) &&
         math::is_close(a.m[2][2], b.m[2][2]);
}

bool operator==(const matrix4f &a, const matrix4f &b) {
  return math::is_close(a.m[0][0], b.m[0][0]) &&
         math::is_close(a.m[0][1], b.m[0][1]) &&
         math::is_close(a.m[0][2], b.m[0][2]) &&
         math::is_close(a.m[0][3], b.m[0][3]) &&
         math::is_close(a.m[1][0], b.m[1][0]) &&
         math::is_close(a.m[1][1], b.m[1][1]) &&
         math::is_close(a.m[1][2], b.m[1][2]) &&
         math::is_close(a.m[1][3], b.m[1][3]) &&
         math::is_close(a.m[2][0], b.m[2][0]) &&
         math::is_close(a.m[2][1], b.m[2][1]) &&
         math::is_close(a.m[2][2], b.m[2][2]) &&
         math::is_close(a.m[2][3], b.m[2][3]) &&
         math::is_close(a.m[3][0], b.m[3][0]) &&
         math::is_close(a.m[3][1], b.m[3][1]) &&
         math::is_close(a.m[3][2], b.m[3][2]) &&
         math::is_close(a.m[3][3], b.m[3][3]);
}

bool operator==(const matrix2d &a, const matrix2d &b) {
  return math::is_close(a.m[0][0], b.m[0][0]) &&
         math::is_close(a.m[0][1], b.m[0][1]) &&
         math::is_close(a.m[1][0], b.m[1][0]) &&
         math::is_close(a.m[1][1], b.m[1][1]);
}

bool operator==(const matrix3d &a, const matrix3d &b) {
  return math::is_close(a.m[0][0], b.m[0][0]) &&
         math::is_close(a.m[0][1], b.m[0][1]) &&
         math::is_close(a.m[0][2], b.m[0][2]) &&
         math::is_close(a.m[1][0], b.m[1][0]) &&
         math::is_close(a.m[1][1], b.m[1][1]) &&
         math::is_close(a.m[1][2], b.m[1][2]) &&
         math::is_close(a.m[2][0], b.m[2][0]) &&
         math::is_close(a.m[2][1], b.m[2][1]) &&
         math::is_close(a.m[2][2], b.m[2][2]);
}

bool operator==(const matrix4d &a, const matrix4d &b) {
  return math::is_close(a.m[0][0], b.m[0][0]) &&
         math::is_close(a.m[0][1], b.m[0][1]) &&
         math::is_close(a.m[0][2], b.m[0][2]) &&
         math::is_close(a.m[0][3], b.m[0][3]) &&
         math::is_close(a.m[1][0], b.m[1][0]) &&
         math::is_close(a.m[1][1], b.m[1][1]) &&
         math::is_close(a.m[1][2], b.m[1][2]) &&
         math::is_close(a.m[1][3], b.m[1][3]) &&
         math::is_close(a.m[2][0], b.m[2][0]) &&
         math::is_close(a.m[2][1], b.m[2][1]) &&
         math::is_close(a.m[2][2], b.m[2][2]) &&
         math::is_close(a.m[2][3], b.m[2][3]) &&
         math::is_close(a.m[3][0], b.m[3][0]) &&
         math::is_close(a.m[3][1], b.m[3][1]) &&
         math::is_close(a.m[3][2], b.m[3][2]) &&
         math::is_close(a.m[3][3], b.m[3][3]);
}

}  // namespace value
}  // namespace tinyusdz
