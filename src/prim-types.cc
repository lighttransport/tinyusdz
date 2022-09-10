#include "prim-types.hh"
//#include "pprinter.hh"

#include <algorithm>
#include <limits>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/pystring.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

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

value::half float_to_half_full_be(float _f) {
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

  value::half ret;
  ret.value = (*reinterpret_cast<const uint16_t *>(&o));

  return ret;
}

value::half float_to_half_full_le(float _f) {
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

  value::half ret;
  ret.value = (*reinterpret_cast<const uint16_t *>(&o));
  return ret;
}

}  // namespace

float half_to_float(value::half h) {
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

value::half float_to_half_full(float _f) {
  // TODO: Compile time detection of endianness
  HostEndianness endian;

  if (endian.isBig()) {
    return float_to_half_full_be(_f);
  } else if (endian.isLittle()) {
    return float_to_half_full_le(_f);
  }

  ///???
  value::half fp16{0};  // TODO: Raise exception or return NaN
  return fp16;
}

nonstd::optional<Interpolation> InterpolationFromString(const std::string &v) {
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
  return nonstd::nullopt;
}

nonstd::optional<Orientation> OrientationFromString(const std::string &v) {
  if ("rightHanded" == v) {
    return Orientation::RightHanded;
  } else if ("leftHanded" == v) {
    return Orientation::LeftHanded;
  }
  return nonstd::nullopt;
}

bool operator==(const Path &lhs, const Path &rhs) {
  if (!lhs.IsValid()) {
    return false;
  }

  if (!rhs.IsValid()) {
    return false;
  }

  // Currently simply compare string.
  // FIXME: Better Path identity check.
  return (lhs.full_path_name() == rhs.full_path_name());
}

//
// -- Path
//

Path::Path(const std::string &p, const std::string &prop) {
  //
  // For absolute path, starts with '/' and no other '/' exists.
  // For property part, '.' exists only once.
  (void)prop;

  if (p.size() < 1) {
    valid = false;
    return;
  }

  auto slash_fun = [](const char c) { return c == '/'; };
  auto dot_fun = [](const char c) { return c == '.'; };

  // TODO: More checks('{', '[', ...)

  if (p[0] == '/') {
    // absolute path

    auto ndots = std::count_if(p.begin(), p.end(), dot_fun);

    if (ndots == 0) {
      // absolute prim.
      prim_part = p;
      valid = true;
    } else if (ndots == 1) {
      if (p.size() < 3) {
        // "/."
        valid = false;
        return;
      }

      auto loc = p.find_first_of('.');
      if (loc == std::string::npos) {
        // ?
        valid = false;
        return;
      }

      if (loc <= 0) {
        // this should not happen though.
        valid = false;
      }

      // split
      std::string prop_name = p.substr(size_t(loc));

      // Check if No '/' in prop_part
      if (std::count_if(prop_name.begin(), prop_name.end(), slash_fun) > 0) {
        valid = false;
        return;
      }

      prop_part = prop_name.erase(0, 1);  // remove '.'
      prim_part = p.substr(0, size_t(loc));

      valid = true;

    } else {
      valid = false;
      return;
    }

  } else if (p[0] == '.') {
    // property
    auto nslashes = std::count_if(p.begin(), p.end(), slash_fun);
    if (nslashes > 0) {
      valid = false;
      return;
    }

    prop_part = p;
    prop_part = prop_part.erase(0, 1);
    valid = true;

  } else {
    // prim.prop

    auto ndots = std::count_if(p.begin(), p.end(), dot_fun);
    if (ndots == 0) {
      // relative prim.
      prim_part = p;
      valid = true;
    } else if (ndots == 1) {
      if (p.size() < 3) {
        // "/."
        valid = false;
        return;
      }

      auto loc = p.find_first_of('.');
      if (loc == std::string::npos) {
        // ?
        valid = false;
        return;
      }

      if (loc <= 0) {
        // this should not happen though.
        valid = false;
      }

      // split
      std::string prop_name = p.substr(size_t(loc));

      // Check if No '/' in prop_part
      if (std::count_if(prop_name.begin(), prop_name.end(), slash_fun) > 0) {
        valid = false;
        return;
      }

      prim_part = p.substr(0, size_t(loc));
      prop_part = prop_name.erase(0, 1);  // remove '.'

      valid = true;

    } else {
      valid = false;
      return;
    }
  }
}

Path Path::AppendProperty(const std::string &elem) {
  Path p = (*this);

  if (elem.empty()) {
    p.valid = false;
    return p;
  }

  if (elem[0] == '{') {
    // variant chars are not supported
    p.valid = false;
    return p;
  } else if (elem[0] == '[') {
    // relational attrib are not supported
    p.valid = false;
    return p;
  } else if (elem[0] == '.') {
    // std::cerr << "???. elem[0] is '.'\n";
    // For a while, make this valid.
    p.valid = false;
    return p;
  } else {
    p.prop_part = elem;

    return p;
  }
}

std::pair<Path, Path> Path::SplitAtRoot() const {
  if (IsAbsolutePath()) {
    if (IsRootPath()) {
      return std::make_pair(Path("/", ""), Path());
    }

    std::string p = full_path_name();

    if (p.size() < 2) {
      // Never should reach here. just in case
      return std::make_pair(*this, Path());
    }

    // Fine 2nd '/'
    auto ret =
        std::find_if(p.begin() + 1, p.end(), [](char c) { return c == '/'; });

    if (ret != p.end()) {
      auto ndist = std::distance(p.begin(), ret);  // distance from str[0]
      if (ndist < 1) {
        // This should not happen though.
        return std::make_pair(*this, Path());
      }
      size_t n = size_t(ndist);
      std::string root = p.substr(0, n);
      std::string siblings = p.substr(n);

      Path rP(root, "");
      Path sP(siblings, "");

      return std::make_pair(rP, sP);
    }

    return std::make_pair(*this, Path());
  } else {
    return std::make_pair(Path(), *this);
  }
}

Path Path::AppendElement(const std::string &elem) {
  Path p = (*this);

  if (elem.empty()) {
    p.valid = false;
    return p;
  }

  if (elem[0] == '{') {
    // variant chars are not supported
    p.valid = false;
    return p;
  } else if (elem[0] == '[') {
    // relational attrib are not supported
    p.valid = false;
    return p;
  } else if (elem[0] == '.') {
    // std::cerr << "???. elem[0] is '.'\n";
    // For a while, make this valid.
    p.valid = false;
    return p;
  } else {
    // std::cout << "elem " << elem << "\n";
    if ((p.prim_part.size() == 1) && (p.prim_part[0] == '/')) {
      p.prim_part += elem;
    } else {
      p.prim_part += '/' + elem;
    }

    return p;
  }
}

Path Path::GetParentPrim() const {
  if (!valid) {
    return Path();
  }

  if (IsRootPrim()) {
    return *this;
  }

  size_t n = prim_part.find_last_of('/');
  if (n == std::string::npos) {
    // this should never happen though.
    return Path();
  }

  if (n == 0) {
    // return root
    return Path("/", "");
  }

  return Path(prim_part.substr(0, n), "");
}

bool MetaVariable::IsObject() const {
  return (value.type_id() ==
          value::TypeTrait<tinyusdz::CustomDataType>::type_id);
}

nonstd::optional<Kind> KindFromString(const std::string &str) {
  if (str == "model") {
    return Kind::Model;
  } else if (str == "group") {
    return Kind::Group;
  } else if (str == "assembly") {
    return Kind::Assembly;
  } else if (str == "component") {
    return Kind::Component;
  } else if (str == "subcomponent") {
    return Kind::Subcomponent;
  }
  return nonstd::nullopt;
}

}  // namespace tinyusdz
