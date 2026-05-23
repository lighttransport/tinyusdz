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
#include "math-util.inc"

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
