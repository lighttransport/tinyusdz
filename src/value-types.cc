// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#include "value-types.hh"

#include "str-util.hh"
#include "value-pprint.hh"

// For compile-time map
// Another candidate is frozen: https://github.com/serge-sans-paille/frozen
//
#include "external/mapbox/eternal/include/mapbox/eternal.hpp"

namespace tinyusdz {
namespace value {

#if 0  // TODO: Remove
bool Reconstructor::reconstruct(AttribMap &amap) {
  err_.clear();

  staticstruct::Reader r;

#define CONVERT_TYPE_SCALAR(__ty, __value)       \
  case TypeTrait<__ty>::type_id: {               \
    __ty *p = reinterpret_cast<__ty *>(__value); \
    staticstruct::Handler<__ty> _h(p);           \
    return _h.write(&handler);                   \
  }

#define CONVERT_TYPE_1D(__ty, __value)                                     \
  case (TypeTrait<__ty>::type_id | TYPE_ID_1D_ARRAY_BIT): {                \
    std::vector<__ty> *p = reinterpret_cast<std::vector<__ty> *>(__value); \
    staticstruct::Handler<std::vector<__ty>> _h(p);                        \
    return _h.write(&handler);                                             \
  }

#define CONVERT_TYPE_2D(__ty, __value)                               \
  case (TypeTrait<__ty>::type_id | TYPE_ID_2D_ARRAY_BIT): {          \
    std::vector<std::vector<__ty>> *p =                              \
        reinterpret_cast<std::vector<std::vector<__ty>> *>(__value); \
    staticstruct::Handler<std::vector<std::vector<__ty>>> _h(p);     \
    return _h.write(&handler);                                       \
  }

#define CONVERT_TYPE_LIST(__FUNC) \
  __FUNC(half, v)                 \
  __FUNC(half2, v)                \
  __FUNC(half3, v)                \
  __FUNC(half4, v)                \
  __FUNC(int32_t, v)              \
  __FUNC(uint32_t, v)             \
  __FUNC(int2, v)                 \
  __FUNC(int3, v)                 \
  __FUNC(int4, v)                 \
  __FUNC(uint2, v)                \
  __FUNC(uint3, v)                \
  __FUNC(uint4, v)                \
  __FUNC(int64_t, v)              \
  __FUNC(uint64_t, v)             \
  __FUNC(float, v)                \
  __FUNC(float2, v)               \
  __FUNC(float3, v)               \
  __FUNC(float4, v)               \
  __FUNC(double, v)               \
  __FUNC(double2, v)              \
  __FUNC(double3, v)              \
  __FUNC(double4, v)              \
  __FUNC(quath, v)                \
  __FUNC(quatf, v)                \
  __FUNC(quatd, v)                \
  __FUNC(vector3h, v)             \
  __FUNC(vector3f, v)             \
  __FUNC(vector3d, v)             \
  __FUNC(normal3h, v)             \
  __FUNC(normal3f, v)             \
  __FUNC(normal3d, v)             \
  __FUNC(point3h, v)              \
  __FUNC(point3f, v)              \
  __FUNC(point3d, v)              \
  __FUNC(color3f, v)              \
  __FUNC(color3d, v)              \
  __FUNC(color4f, v)              \
  __FUNC(color4d, v)              \
  __FUNC(matrix2d, v)             \
  __FUNC(matrix3d, v)             \
  __FUNC(matrix4d, v)

  bool ret = r.ParseStruct(
      &h,
      [&amap](std::string key, uint32_t flags, uint32_t user_type_id,
              staticstruct::BaseHandler &handler) -> bool {
        std::cout << "key = " << key << ", count = " << amap.attribs.count(key)
                  << "\n";

        if (!amap.attribs.count(key)) {
          if (flags & staticstruct::Flags::Optional) {
            return true;
          } else {
            return false;
          }
        }

        auto &value = amap.attribs[key];
        if (amap.attribs[key].type_id() == user_type_id) {
          void *v = value.value();

          switch (user_type_id) {
            CONVERT_TYPE_SCALAR(bool, v)

            CONVERT_TYPE_LIST(CONVERT_TYPE_SCALAR)
            CONVERT_TYPE_LIST(CONVERT_TYPE_1D)
            CONVERT_TYPE_LIST(CONVERT_TYPE_2D)

            default: {
              std::cerr << "Unsupported type: " << GetTypeName(user_type_id)
                        << "\n";
              return false;
            }
          }
        } else {
          std::cerr << "type: " << amap.attribs[key].type_name() << "(a.k.a "
                    << amap.attribs[key].underlying_type_name()
                    << ") expected but got " << GetTypeName(user_type_id)
                    << " for attribute \"" << key << "\"\n";
          return false;
        }
      },
      &err_);

  return ret;

#undef CONVERT_TYPE_SCALAR
#undef CONVERT_TYPE_1D
#undef CONVERT_TYPE_2D
#undef CONVERT_TYPE_LIST
}
#endif

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


}  // namespace value
}  // namespace tinyusdz
