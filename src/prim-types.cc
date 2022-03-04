#include "prim-types.hh"
//#include "pprinter.hh"

#include <limits>

namespace tinyusdz {

namespace {

// https://www.realtime.bc.ca/articles/endian-safe.html
union HostEndianness
{
  int i;
  char c[sizeof(int)];

  HostEndianness() : i(1) { }

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


float16 float_to_half_full_be(float _f) {
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

  float16 ret = (*reinterpret_cast<const uint16_t *>(&o));
  return ret;
}

float16 float_to_half_full_le(float _f) {
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

  float16 ret = (*reinterpret_cast<const uint16_t *>(&o));
  return ret;
}

} // namespace

float half_to_float(float16 h) {
  // TODO: Compile time detection of endianness
  HostEndianness endian;

  if (endian.isBig()) {
    float16be f;
    f.u = h;
    return half_to_float_be(f);
  } else if (endian.isLittle()) {
    float16le f;
    f.u = h;
    return half_to_float_le(f);
  }

  ///???
  return std::numeric_limits<float>::quiet_NaN();

}


float16 float_to_half_full(float _f) {
  // TODO: Compile time detection of endianness
  HostEndianness endian;

  if (endian.isBig()) {
    return float_to_half_full_be(_f);
  } else if (endian.isLittle()) {
    return float_to_half_full_le(_f);
  }

  ///???
  float16 fp16{0}; // TODO: Raise exception or return NaN
  return fp16;


}

// http://martinecker.com/martincodes/lambda-expression-overloading/
template <class... Fs> struct overload_set;

template <class F1, class... Fs>
struct overload_set<F1, Fs...> : F1, overload_set<Fs...>::type
{
    typedef overload_set type;

    overload_set(F1 head, Fs... tail)
        : F1(head), overload_set<Fs...>::type(tail...)
    {}

    using F1::operator();
    using overload_set<Fs...>::type::operator();
};

template <class F>
struct overload_set<F> : F
{
    typedef F type;
    using F::operator();
};

template <class... Fs>
//auto overloaded(Fs... x) for C++14
typename overload_set<Fs...>::type overloaded(Fs... x)
{
    return overload_set<Fs...>(x...);
}

std::string prim_basic_type_name(const PrimBasicType &v) {

  (void)v;
#if 0
  std::string ty =  nonstd::visit(overloaded (
            [](auto) { return "[[TODO: PrimBasicType. ]]"; },
            [](std::string) { return "string"; },
            [](Token) { return "token"; },
            [](float) { return "float"; },
            [](Vec2f) { return "float2"; },
            [](Vec3f) { return "float3"; },
            [](Vec4f) { return "float4"; },
            [](double) { return "double"; },
            [](Vec2d) { return "double2"; },
            [](Vec3d) { return "double3"; },
            [](Vec4d) { return "double4"; },
            [](Matrix4d) { return "matrix4d"; }
  ), v);

  return ty;
#endif
  return "TODO";
}

namespace primvar {


#if 0 // FIXME
std::string type_name(const TimeSampleType &v) {

  std::string ty =  nonstd::visit(overloaded (
            [](auto) { return "[[TODO: TypeSampleType. ]]"; },
            [](TimeSampledDataDouble) { return "double"; },
            [](TimeSampledDataDouble3) { return "double3"; },
            [](TimeSampledDataFloat) { return "float"; },
            [](TimeSampledDataFloat3) { return "float"; },
            [](TimeSampledDataMatrix4d) { return "matrix4d"; }
  ), v);

  return ty;
}


static std::string get_type_name(const PrimArrayType &v) {

  std::string ty =  nonstd::visit(overloaded (
            [](auto) { return "[[TODO: PrimAarrayType. ]]"; },
            [](const std::vector<std::string>&) { return "string[]"; },
            [](const std::vector<Token>&) { return "token[]"; },
            [](const std::vector<float>&) { return "float[]"; },
            [](const std::vector<Vec2f>&) { return "float2[]"; },
            [](const std::vector<Vec3f>&) { return "float3[]"; },
            [](const std::vector<Vec4f>&) { return "float4[]"; },
            [](const std::vector<double>&) { return "double"; },
            [](const std::vector<Vec2d>&) { return "double2[]"; },
            [](const std::vector<Vec3d>&) { return "double3[]"; },
            [](const std::vector<Vec4d>&) { return "double4[]"; },
            [](const std::vector<Matrix4d>&) { return "matrix4d[]"; }
  ), v);

  return ty;
}
#endif

#if 0
std::string get_type_name(const PrimVar &v) {
  (void)v;

  //if (auto p = nonstd::get_if<None>(&v)) {
  //  return "None";
  //}

  //if (auto p = nonstd::get_if<PrimBasicType>(&v)) {
  //  return type_name(*p);
  //}
  //if (auto p = nonstd::get_if<PrimArrayType>(&v)) {
  //  return type_name(*p);
  //}
  //if (auto p = nonstd::get_if<TimeSampleType>(&v)) {
  //  return type_name(*p);
  //}

  return "[[Invalid PrimVar type]]";
}
#endif

} // namespace primvar

Interpolation InterpolationFromString(const std::string &v)
{
  if ("faceVarying" == v) {
    return Interpolation::FaceVarying;
  } else if ("constant" == v) {
    return Interpolation::Constant;
  } else if ("uniform" == v) {
    return Interpolation::Uniform;
  } else if ("vertex" == v) {
    return Interpolation::Vertex;
  } else if ("varying" == v) {
    return Interpolation::Varying;
  }
  return Interpolation::Invalid;
}

Orientation OrientationFromString(const std::string &v) 
{
  if ("rightHanded" == v) {
    return Orientation::RightHanded;
  } else if ("leftHanded" == v) {
    return Orientation::LeftHanded;
  }
  return Orientation::Invalid;
}


} // namespace tinyusdz
