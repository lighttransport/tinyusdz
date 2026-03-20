// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
#include "value-types.hh"

#include <type_traits>
#include <unordered_set>

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

static const std::unordered_set<uint32_t> &GetLerpSupportedTypeIds() {
  static std::unordered_set<uint32_t> s = [] {
    std::unordered_set<uint32_t> ids;
    auto add = [&](uint32_t tid, uint32_t uid) {
      ids.insert(tid);
      ids.insert(uid);
      ids.insert(tid | value::TYPE_ID_1D_ARRAY_BIT);
      ids.insert(uid | value::TYPE_ID_1D_ARRAY_BIT);
    };
    add(TypeTraits<value::half>::type_id(), TypeTraits<value::half>::underlying_type_id());
    add(TypeTraits<value::half2>::type_id(), TypeTraits<value::half2>::underlying_type_id());
    add(TypeTraits<value::half3>::type_id(), TypeTraits<value::half3>::underlying_type_id());
    add(TypeTraits<value::half4>::type_id(), TypeTraits<value::half4>::underlying_type_id());
    add(TypeTraits<float>::type_id(), TypeTraits<float>::underlying_type_id());
    add(TypeTraits<value::float2>::type_id(), TypeTraits<value::float2>::underlying_type_id());
    add(TypeTraits<value::float3>::type_id(), TypeTraits<value::float3>::underlying_type_id());
    add(TypeTraits<value::float4>::type_id(), TypeTraits<value::float4>::underlying_type_id());
    add(TypeTraits<double>::type_id(), TypeTraits<double>::underlying_type_id());
    add(TypeTraits<value::double2>::type_id(), TypeTraits<value::double2>::underlying_type_id());
    add(TypeTraits<value::double3>::type_id(), TypeTraits<value::double3>::underlying_type_id());
    add(TypeTraits<value::double4>::type_id(), TypeTraits<value::double4>::underlying_type_id());
    add(TypeTraits<value::quath>::type_id(), TypeTraits<value::quath>::underlying_type_id());
    add(TypeTraits<value::quatf>::type_id(), TypeTraits<value::quatf>::underlying_type_id());
    add(TypeTraits<value::quatd>::type_id(), TypeTraits<value::quatd>::underlying_type_id());
    add(TypeTraits<value::matrix2d>::type_id(), TypeTraits<value::matrix2d>::underlying_type_id());
    add(TypeTraits<value::matrix3d>::type_id(), TypeTraits<value::matrix3d>::underlying_type_id());
    add(TypeTraits<value::matrix4d>::type_id(), TypeTraits<value::matrix4d>::underlying_type_id());
    return ids;
  }();
  return s;
}

bool IsLerpSupportedType(uint32_t tyid) {
  const auto &ids = GetLerpSupportedTypeIds();

  // Check direct match
  if (ids.count(tyid)) {
    return true;
  }

  // Check array bit variants
  if (tyid & value::TYPE_ID_1D_ARRAY_BIT) {
    uint32_t base = tyid & (~value::TYPE_ID_1D_ARRAY_BIT);
    if (ids.count(base)) {
      return true;
    }
  }

  // Check underlying type for role types (e.g. color3f -> float3)
  if (auto pv = TryGetUnderlyingTypeName(tyid)) {
    uint32_t underlying_tyid = GetTypeId(pv.value());
    if (ids.count(underlying_tyid)) {
      return true;
    }
    if (underlying_tyid & value::TYPE_ID_1D_ARRAY_BIT) {
      uint32_t base = underlying_tyid & (~value::TYPE_ID_1D_ARRAY_BIT);
      if (ids.count(base)) {
        return true;
      }
    }
  }

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
      });

  bool array_bit = (TYPE_ID_1D_ARRAY_BIT & tyid);
  uint32_t scalar_tid = tyid & (~TYPE_ID_1D_ARRAY_BIT);

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
    array_bit |= TYPE_ID_1D_ARRAY_BIT;
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
      array_bit |= TYPE_ID_1D_ARRAY_BIT;
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
  bool array_bit = (TYPE_ID_1D_ARRAY_BIT & tyid);
  uint32_t scalar_tid = tyid & (~TYPE_ID_1D_ARRAY_BIT);

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
  return GetUnderlyingTypeId(GetTypeName(tyid)) != value::TYPE_ID_INVALID;
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

// half_to_float_le/be moved to inline in value-types.hh

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

float half_to_float(value::half h) {
  uint16_t hu = h.value;
  uint32_t o;

  o = (hu & 0x7fffU) << 13U;              // exponent/mantissa bits
  uint32_t exp_ = (0x7c00U << 13U) & o;   // just the exponent
  o += (127 - 15) << 23;                   // exponent adjust

  if (exp_ == (0x7c00U << 13U))            // Inf/NaN?
    o += (128 - 16) << 23;                 // extra exp adjust
  else if (exp_ == 0) {                    // Zero/Denormal?
    o += 1 << 23;                          // extra exp adjust
    float of;
    memcpy(&of, &o, sizeof(float));
    uint32_t magic = 113 << 23;
    float magicf;
    memcpy(&magicf, &magic, sizeof(float));
    of -= magicf;                          // renormalize
    memcpy(&o, &of, sizeof(uint32_t));
  }

  o |= (hu & 0x8000U) << 16U;             // sign bit
  float result;
  memcpy(&result, &o, sizeof(float));
  return result;
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

// half arithmetic operators

half operator+(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) + half_to_float(b));
}

half operator-(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) - half_to_float(b));
}

half operator*(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) * half_to_float(b));
}

half operator/(const half &a, const half &b) {
  return float_to_half_full(half_to_float(a) / half_to_float(b));
}

half& operator+=(half &a, const half &b) {
  a = float_to_half_full(half_to_float(a) + half_to_float(b));
  return a;
}

half& operator-=(half &a, const half &b) {
  a = float_to_half_full(half_to_float(a) - half_to_float(b));
  return a;
}

half& operator*=(half &a, const half &b) {
  a = float_to_half_full(half_to_float(a) * half_to_float(b));
  return a;
}

half& operator/=(half &a, const half &b) {
  a = float_to_half_full(half_to_float(a) / half_to_float(b));
  return a;
}

half operator+(const half &a, float b) {
  return float_to_half_full(half_to_float(a) + b);
}

half operator-(const half &a, float b) {
  return float_to_half_full(half_to_float(a) - b);
}

half operator*(const half &a, float b) {
  return float_to_half_full(half_to_float(a) * b);
}

half operator/(const half &a, float b) {
  return float_to_half_full(half_to_float(a) / b);
}

half operator+(float a, const half &b) {
  return float_to_half_full(a + half_to_float(b));
}

half operator-(float a, const half &b) {
  return float_to_half_full(a - half_to_float(b));
}

half operator*(float a, const half &b) {
  return float_to_half_full(a * half_to_float(b));
}

half operator/(float a, const half &b) {
  return float_to_half_full(a / half_to_float(b));
}

// matrix set_row, set_scale, set_translation methods

void matrix2f::set_row(uint32_t row, float x, float y) {
  if (row < 2) {
    m[row][0] = x;
    m[row][1] = y;
  }
}

void matrix2f::set_scale(float sx, float sy) {
  m[0][0] = sx;
  m[0][1] = 0.0f;

  m[1][0] = 0.0f;
  m[1][1] = sy;
}

void matrix3f::set_row(uint32_t row, float x, float y, float z) {
  if (row < 3) {
    m[row][0] = x;
    m[row][1] = y;
    m[row][2] = z;
  }
}

void matrix3f::set_scale(float sx, float sy, float sz) {
  m[0][0] = sx;
  m[0][1] = 0.0f;
  m[0][2] = 0.0f;

  m[1][0] = 0.0f;
  m[1][1] = sy;
  m[1][2] = 0.0f;

  m[2][0] = 0.0f;
  m[2][1] = 0.0f;
  m[2][2] = sz;
}

void matrix3f::set_translation(float tx, float ty, float tz) {
  m[2][0] = tx;
  m[2][1] = ty;
  m[2][2] = tz;
}

void matrix4f::set_row(uint32_t row, float x, float y, float z, float w) {
  if (row < 4) {
    m[row][0] = x;
    m[row][1] = y;
    m[row][2] = z;
    m[row][3] = w;
  }
}

void matrix4f::set_scale(float sx, float sy, float sz) {
  m[0][0] = sx;
  m[0][1] = 0.0f;
  m[0][2] = 0.0f;
  m[0][3] = 0.0f;

  m[1][0] = 0.0f;
  m[1][1] = sy;
  m[1][2] = 0.0f;
  m[1][3] = 0.0f;

  m[2][0] = 0.0f;
  m[2][1] = 0.0f;
  m[2][2] = sz;
  m[2][3] = 0.0f;

  m[3][0] = 0.0f;
  m[3][1] = 0.0f;
  m[3][2] = 0.0f;
  m[3][3] = 1.0f;
}

void matrix4f::set_translation(float tx, float ty, float tz) {
  m[3][0] = tx;
  m[3][1] = ty;
  m[3][2] = tz;
}

void matrix2d::set_row(uint32_t row, double x, double y) {
  if (row < 2) {
    m[row][0] = x;
    m[row][1] = y;
  }
}

void matrix2d::set_scale(double sx, double sy) {
  m[0][0] = sx;
  m[0][1] = 0.0;

  m[1][0] = 0.0;
  m[1][1] = sy;
}

void matrix3d::set_row(uint32_t row, double x, double y, double z) {
  if (row < 3) {
    m[row][0] = x;
    m[row][1] = y;
    m[row][2] = z;
  }
}

void matrix3d::set_scale(double sx, double sy, double sz) {
  m[0][0] = sx;
  m[0][1] = 0.0;
  m[0][2] = 0.0;

  m[1][0] = 0.0;
  m[1][1] = sy;
  m[1][2] = 0.0;

  m[2][0] = 0.0;
  m[2][1] = 0.0;
  m[2][2] = sz;
}

void matrix4d::set_row(uint32_t row, double x, double y, double z, double w) {
  if (row < 4) {
    m[row][0] = x;
    m[row][1] = y;
    m[row][2] = z;
    m[row][3] = w;
  }
}

void matrix4d::set_scale(double sx, double sy, double sz) {
  m[0][0] = sx;
  m[0][1] = 0.0;
  m[0][2] = 0.0;
  m[0][3] = 0.0;

  m[1][0] = 0.0;
  m[1][1] = sy;
  m[1][2] = 0.0;
  m[1][3] = 0.0;

  m[2][0] = 0.0;
  m[2][1] = 0.0;
  m[2][2] = sz;
  m[2][3] = 0.0;

  m[3][0] = 0.0;
  m[3][1] = 0.0;
  m[3][2] = 0.0;
  m[3][3] = 1.0;
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

#define ARRAY_SIZE_GET(__ty) case value::TypeTraits<__ty>::type_id() | value::TYPE_ID_1D_ARRAY_BIT: { \
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

bool RoleTypeCast(const uint32_t roleTyId, value::Value &inout) {
  const uint32_t srcUnderlyingTyId = inout.underlying_type_id();

  DCOUT("input type = " << inout.type_name());

  // Zero-copy role type cast: just change the type_id.
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
                value::TYPE_ID_1D_ARRAY_BIT)) {                                \
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


// Get byte size for a given type_id
static size_t GetTypeSize(uint32_t type_id) {
  // Remove array bit if present
  uint32_t base_type_id = type_id & (~TYPE_ID_1D_ARRAY_BIT);
  
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

size_t Value::estimate_memory_usage() const {
  size_t total_size = sizeof(Value); // Base object size
  
  if (is_empty() || is_none()) {
    return total_size;
  }
  
  uint32_t tid = type_id();
  
  // Check if it's an array type
  if (tid & TYPE_ID_1D_ARRAY_BIT) {
    // For arrays, compute element size * array count
    size_t element_size = GetTypeSize(tid);
    size_t element_count = array_size();
    
    // Add array storage overhead (vector typically has 3 pointers)
    total_size += sizeof(void*) * 3; 
    
    // Add actual data size
    total_size += element_size * element_count;
    
    // Handle special cases for string arrays
    uint32_t base_type = tid & (~TYPE_ID_1D_ARRAY_BIT);
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

    // For MODEL types (concrete Prim types like GeomMesh, Xform, etc.),
    // GetTypeSize returns sizeof(void*) which is wrong.
    // Use sizeof_stored() which records the actual sizeof(T) at construction time.
    if (tid >= TYPE_ID_MODEL_BEGIN && tid < TYPE_ID_MODEL_END) {
      size_t stored_size = sizeof_stored();
      if (stored_size > 0) {
        type_size = stored_size;
      }
    }

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

size_t Value::estimate_actual_usage() const {
  size_t total_size = sizeof(Value);

  if (is_empty() || is_none()) {
    return total_size;
  }

  uint32_t tid = type_id();

  if (tid & TYPE_ID_1D_ARRAY_BIT) {
    size_t element_size = GetTypeSize(tid);
    size_t element_count = array_size();

    total_size += sizeof(void*) * 3;  // vector overhead
    total_size += element_size * element_count;

    // For string arrays, use size() instead of capacity()
    uint32_t base_type = tid & (~TYPE_ID_1D_ARRAY_BIT);
    if (base_type == TYPE_ID_STRING || base_type == TYPE_ID_TOKEN ||
        base_type == TYPE_ID_STRING_DATA || base_type == TYPE_ID_ASSET_PATH) {
      if (auto* vec = as<std::vector<std::string>>()) {
        for (const auto& str : *vec) {
          total_size += str.size();
        }
      } else if (auto* tokVec = as<std::vector<value::token>>()) {
        for (const auto& tok : *tokVec) {
          total_size += tok.str().size();
        }
      }
    }
  } else {
    size_t type_size = GetTypeSize(tid);
    if (tid >= TYPE_ID_MODEL_BEGIN && tid < TYPE_ID_MODEL_END) {
      size_t stored_size = sizeof_stored();
      if (stored_size > 0) {
        type_size = stored_size;
      }
    }
    total_size += type_size;

    if (tid == TYPE_ID_STRING || tid == TYPE_ID_STRING_DATA) {
      if (auto* str = as<std::string>()) {
        total_size += str->size();
      }
    } else if (tid == TYPE_ID_TOKEN) {
      if (auto* tok = as<value::token>()) {
        total_size += tok->str().size();
      }
    } else if (tid == TYPE_ID_ASSET_PATH) {
      if (auto* path = as<value::AssetPath>()) {
        total_size += path->GetAssetPath().length();
        total_size += path->GetResolvedPath().length();
      }
    } else if (tid == TYPE_ID_DICT || tid == TYPE_ID_CUSTOMDATA) {
      if (auto* dict = as<value::dict>()) {
        total_size += dict->size() * (32 + sizeof(void*) * 4);
        for (const auto& kv : *dict) {
          total_size += kv.first.size();
          total_size += 64;
        }
      }
    }
  }

  return total_size;
}

bool TimeSamples::has_sample_at(const double t) const {
  const auto &samples = get_samples();

  const auto it = std::find_if(samples.begin(), samples.end(), [&t](const Sample &s) {
    return math::is_close(t, s.t);
  });

  return (it != samples.end());
}

bool TimeSamples::get_sample_at(const double t, Sample **dst) {
  if (!dst) {
    return false;
  }

  auto &sample_vec = samples();

  const auto it = std::find_if(sample_vec.begin(), sample_vec.end(), [&t](const Sample &sample) {
    return math::is_close(t, sample.t);
  });

  if (it != sample_vec.end()) {
    (*dst) = const_cast<Sample*>(&(*it));
    return true;  // Found the sample!
  }
  return false;
}

// Floating-point aware equality operators for matrix types
// Use epsilon-based comparison suitable for deduplication
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

// ---------------------------------------------------------------
// Concrete matrix operation implementations (moved from header)
// ---------------------------------------------------------------

namespace {

template <typename MTy, typename STy, size_t N>
MTy MultImpl(const MTy &m, const MTy &n) {
  MTy ret;
  for (size_t j = 0; j < N; j++) {
    for (size_t i = 0; i < N; i++) {
      STy value = static_cast<STy>(0);
      for (size_t k = 0; k < N; k++) {
        value += m.m[j][k] * n.m[k][i];
      }
      ret.m[j][i] = value;
    }
  }
  return ret;
}

template <typename MTy, size_t N>
MTy MatAddImpl(const MTy &m, const MTy &n) {
  MTy ret;
  for (size_t j = 0; j < N; j++) {
    for (size_t i = 0; i < N; i++) {
      ret.m[j][i] = m.m[j][i] + n.m[j][i];
    }
  }
  return ret;
}

template <typename MTy, size_t N>
MTy MatSubImpl(const MTy &m, const MTy &n) {
  MTy ret;
  for (size_t j = 0; j < N; j++) {
    for (size_t i = 0; i < N; i++) {
      ret.m[j][i] = m.m[j][i] - n.m[j][i];
    }
  }
  return ret;
}

}  // namespace

matrix2f Mult(const matrix2f &m, const matrix2f &n) { return MultImpl<matrix2f, float, 2>(m, n); }
matrix3f Mult(const matrix3f &m, const matrix3f &n) { return MultImpl<matrix3f, float, 3>(m, n); }
matrix4f Mult(const matrix4f &m, const matrix4f &n) { return MultImpl<matrix4f, float, 4>(m, n); }
matrix2d Mult(const matrix2d &m, const matrix2d &n) { return MultImpl<matrix2d, double, 2>(m, n); }
matrix3d Mult(const matrix3d &m, const matrix3d &n) { return MultImpl<matrix3d, double, 3>(m, n); }
matrix4d Mult(const matrix4d &m, const matrix4d &n) { return MultImpl<matrix4d, double, 4>(m, n); }

matrix2f MatAdd(const matrix2f &a, const matrix2f &b) { return MatAddImpl<matrix2f, 2>(a, b); }
matrix3f MatAdd(const matrix3f &a, const matrix3f &b) { return MatAddImpl<matrix3f, 3>(a, b); }
matrix4f MatAdd(const matrix4f &a, const matrix4f &b) { return MatAddImpl<matrix4f, 4>(a, b); }
matrix2d MatAdd(const matrix2d &a, const matrix2d &b) { return MatAddImpl<matrix2d, 2>(a, b); }
matrix3d MatAdd(const matrix3d &a, const matrix3d &b) { return MatAddImpl<matrix3d, 3>(a, b); }
matrix4d MatAdd(const matrix4d &a, const matrix4d &b) { return MatAddImpl<matrix4d, 4>(a, b); }

matrix2f MatSub(const matrix2f &a, const matrix2f &b) { return MatSubImpl<matrix2f, 2>(a, b); }
matrix3f MatSub(const matrix3f &a, const matrix3f &b) { return MatSubImpl<matrix3f, 3>(a, b); }
matrix4f MatSub(const matrix4f &a, const matrix4f &b) { return MatSubImpl<matrix4f, 4>(a, b); }
matrix2d MatSub(const matrix2d &a, const matrix2d &b) { return MatSubImpl<matrix2d, 2>(a, b); }
matrix3d MatSub(const matrix3d &a, const matrix3d &b) { return MatSubImpl<matrix3d, 3>(a, b); }
matrix4d MatSub(const matrix4d &a, const matrix4d &b) { return MatSubImpl<matrix4d, 4>(a, b); }

}  // namespace value
}  // namespace tinyusdz
