// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
#include "value-types.hh"

#include <type_traits>
#include <unordered_set>

#include "str-util.hh"
#include "value-eval-util.hh"

//
#include "common-macros.inc"

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
#define ADD_LERP_ID(__ty) \
    add(TypeTraits<__ty>::type_id(), TypeTraits<__ty>::underlying_type_id());
    APPLY_FUNC_TO_LERP_VALUE_TYPES(ADD_LERP_ID)
#undef ADD_LERP_ID
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

  // Generated from the single lerpable-type list (shared with LerpTraits and
  // GetLerpSupportedTypeIds) so all three agree — fixes the prior drift where
  // matrices were "lerp-supported" but had no Lerp() arm.
  APPLY_FUNC_TO_LERP_VALUE_TYPES(DO_LERP)
  {
    DCOUT("TODO: type " << GetTypeName(tyid));
  }

#undef DO_LERP

  if (ok) {
    (*dst) = result;
  }

  return ok;
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


}  // namespace value
}  // namespace tinyusdz
