// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.

#include "value-pprint.hh"

#include <sstream>

#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "value-types.hh"


// For fast int/float to ascii
// Disabled for a while(need to write a test).
//#include "external/jeaiii_to_text.h" 

#include "external/dtoa_milo.h"

namespace tinyusdz {

namespace {

#if 0
void itoa(uint32_t n, char* b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int32_t n, char* b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(uint64_t n, char* b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int64_t n, char* b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
#endif

}


} // namespace tinyusdz

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half &v) {
  os << tinyusdz::value::half_to_float(v);
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3h &v) {
  os << "(" << tinyusdz::value::half_to_float(v.r) << ", "
     << tinyusdz::value::half_to_float(v.g) << ", " << tinyusdz::value::half_to_float(v.b)
     << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3f &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3d &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4h &v) {
  os << "(" << tinyusdz::value::half_to_float(v.r) << ", "
     << tinyusdz::value::half_to_float(v.g) << ", " << tinyusdz::value::half_to_float(v.b)
     << ", " << tinyusdz::value::half_to_float(v.a) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4f &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4d &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << ")";
  return os;
}

// pxrUSD prints quateron in [w, x, y, z] order
// https://github.com/PixarAnimationStudios/USD/blob/3abc46452b1271df7650e9948fef9f0ce602e3b2/pxr/base/gf/quatf.h#L287
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quath &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatf &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatd &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2h &v) {
  os << "(" << v.s << ", " << v.t << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2f &v) {
  os << "(" << v.s << ", " << v.t << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2d &v) {
  os << "(" << v.s << ", " << v.t << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3h &v) {
  os << "(" << v.s << ", " << v.t << ", " << v.r << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3f &v) {
  os << "(" << v.s << ", " << v.t << ", " << v.r << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3d &v) {
  os << "(" << v.s << ", " << v.t << ", " << v.r << ")";
  return os;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix2d &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix3d &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << ", " << m.m[0][2] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ", " << m.m[1][2] << "), ";
  ofs << "(" << m.m[2][0] << ", " << m.m[2][1] << ", " << m.m[2][2] << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix4d &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << ", " << m.m[0][2] << ", "
      << m.m[0][3] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ", " << m.m[1][2] << ", "
      << m.m[1][3] << "), ";
  ofs << "(" << m.m[2][0] << ", " << m.m[2][1] << ", " << m.m[2][2] << ", "
      << m.m[2][3] << "), ";
  ofs << "(" << m.m[3][0] << ", " << m.m[3][1] << ", " << m.m[3][2] << ", "
      << m.m[3][3] << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::token &tok) {
  ofs << tinyusdz::quote(tok.str());

  return ofs;
}

#if 0
std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::dict &m) {
  ofs << "{\n";
  for (const auto &item : m) {
    ofs << item.first << " = " << tinyusdz::value::pprint_any(item.second)
        << "\n";
  }
  ofs << "}";

  return ofs;
}
#endif

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::AssetPath &asset) {
  std::string in_s = asset.GetAssetPath();

  std::string quote_str = "@";

  std::string s;

  if (tinyusdz::contains(in_s, '@')) {
    // Escape '@@@'(to '\@@@') if the input path contains '@@@'
    for (size_t i = 0; i < in_s.length(); i++) {
      if ((i + 2) < in_s.length()) {
        if (in_s[i] == '@' && in_s[i + 1] == '@' && in_s[i + 2] == '@') {
          s += "\\@@@";
          i += 2;
        } else {
          s += in_s[i];
        }
      }
    }

    quote_str = "@@@";
  } else {
    s = in_s;
  }

  ofs << quote_str << s << quote_str;

  return ofs;
}

template<>
std::ostream &operator<<(std::ostream &ofs, const std::vector<double> &v) {

  // Not sure what is the HARD-LIMT buffer length for dtoa_milo,
  // but according to std::numeric_limits<double>::digits10(=15),
  // 256 should be sufficient
  char buf[256];

  // TODO: multi-threading for further performance gain?

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    dtoa_milo(v[i], buf);
    ofs << std::string(buf);
  }
  ofs << "]";

  return ofs;
}

template<>
std::ostream &operator<<(std::ostream &ofs, const std::vector<float> &v) {

  // Use dtoa
  char buf[256];

  // TODO: multi-threading for further performance gain?

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    dtoa_milo(double(v[i]), buf);
    ofs << std::string(buf);
  }
  ofs << "]";

  return ofs;
}


template<>
std::ostream &operator<<(std::ostream &ofs, const std::vector<int32_t> &v) {

  // numeric_limits<uint64_t>::digits10 is 19, so 32 should suffice.
  //char buf[32];

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    //tinyusdz::itoa(v[i], buf);
    //ofs << buf;
    ofs << v[i];
  }
  ofs << "]";

  return ofs;
}

template<>
std::ostream &operator<<(std::ostream &ofs, const std::vector<uint32_t> &v) {

  //char buf[32];

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    //tinyusdz::itoa(v[i], buf);
    //ofs << buf;
    ofs << v[i];
  }
  ofs << "]";

  return ofs;
}

template<>
std::ostream &operator<<(std::ostream &ofs, const std::vector<int64_t> &v) {

  // numeric_limits<uint64_t>::digits10 is 19, so 32 should suffice.
  //char buf[32];

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    //tinyusdz::itoa(v[i], buf);
    //ofs << buf;
    ofs << v[i];
  }
  ofs << "]";

  return ofs;
}

template<>
std::ostream &operator<<(std::ostream &ofs, const std::vector<uint64_t> &v) {

  //char buf[32];

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    //tinyusdz::itoa(v[i], buf);
    //ofs << buf;
    ofs << v[i];
  }
  ofs << "]";

  return ofs;
}


}  // namespace std

namespace tinyusdz {
namespace value {

// Simple brute-force way..
// TODO: Use std::function or some template technique?

#define CASE_EXPR_LIST(__FUNC) \
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
  __FUNC(matrix2d)             \
  __FUNC(matrix3d)             \
  __FUNC(matrix4d)             \
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
  __FUNC(color4f)              \
  __FUNC(color4d)              \
  __FUNC(texcoord2h)           \
  __FUNC(texcoord2f)           \
  __FUNC(texcoord2d)           \
  __FUNC(texcoord3h)           \
  __FUNC(texcoord3f)           \
  __FUNC(texcoord3d)

#define CASE_GPRIM_LIST(__FUNC) \
  __FUNC(Model)                 \
  __FUNC(Scope)                 \
  __FUNC(Xform)                 \
  __FUNC(GeomMesh)              \
  __FUNC(GeomSphere)            \
  __FUNC(GeomPoints)            \
  __FUNC(GeomCube)              \
  __FUNC(GeomCylinder)          \
  __FUNC(GeomCapsule)           \
  __FUNC(GeomCone)              \
  __FUNC(GeomBasisCurves)       \
  __FUNC(GeomCamera)            \
  __FUNC(LuxSphereLight)        \
  __FUNC(LuxDomeLight)          \
  __FUNC(LuxDiskLight)          \
  __FUNC(LuxDistantLight)          \
  __FUNC(LuxCylinderLight)          \
  __FUNC(SkelRoot)              \
  __FUNC(Skeleton)              \
  __FUNC(SkelAnimation)         \
  __FUNC(BlendShape)            \
  __FUNC(Material)              \
  __FUNC(Shader)

#if 0 // remove
// std::ostream &operator<<(std::ostream &os, const any_value &v) {
// std::ostream &operator<<(std::ostream &os, const linb::any &v) {
std::string pprint_any(const linb::any &v, const uint32_t indent,
                       bool closing_brace) {
#define BASETYPE_CASE_EXPR(__ty)         \
  case TypeTrait<__ty>::type_id: {       \
    os << linb::any_cast<const __ty>(v); \
    break;                               \
  }

#define PRIMTYPE_CASE_EXPR(__ty)                                           \
  case TypeTrait<__ty>::type_id: {                                         \
    os << to_string(linb::any_cast<const __ty>(v), indent, closing_brace); \
    break;                                                                 \
  }

#define ARRAY1DTYPE_CASE_EXPR(__ty)                   \
  case TypeTrait<std::vector<__ty>>::type_id: {       \
    os << linb::any_cast<const std::vector<__ty>>(v); \
    break;                                            \
  }

#define ARRAY2DTYPE_CASE_EXPR(__ty)                                \
  case TypeTrait<std::vector<std::vector<__ty>>>::type_id: {       \
    os << linb::any_cast<const std::vector<std::vector<__ty>>>(v); \
    break;                                                         \
  }

  std::stringstream os;

  switch (v.type_id()) {
    // no `bool` type for 1D and 2D array
    BASETYPE_CASE_EXPR(bool)

    // no std::vector<dict> and std::vector<std::vector<dict>>, ...
    BASETYPE_CASE_EXPR(dict)

    // base type
    CASE_EXPR_LIST(BASETYPE_CASE_EXPR)

    // 1D array
    CASE_EXPR_LIST(ARRAY1DTYPE_CASE_EXPR)

    // 2D array
    CASE_EXPR_LIST(ARRAY2DTYPE_CASE_EXPR)
    // Assume no 2D array of string-like data.

    // GPrim
    CASE_GPRIM_LIST(PRIMTYPE_CASE_EXPR)

    // token, str: wrap with '"'
    case TypeTrait<value::token>::type_id: {
      os << quote(linb::any_cast<const value::token>(v).str());
      break;
    }
    case TypeTrait<std::vector<value::token>>::type_id: {
      const std::vector<value::token> &lst =
          linb::any_cast<const std::vector<value::token>>(v);
      std::vector<std::string> vs;
      std::transform(lst.begin(), lst.end(), std::back_inserter(vs),
                     [](const value::token &tok) { return tok.str(); });

      os << quote(vs);
      break;
    }
    case TypeTrait<std::string>::type_id: {
      os << quote(linb::any_cast<const std::string>(v));
      break;
    }
    case TypeTrait<std::vector<std::string>>::type_id: {
      const std::vector<std::string> &vs =
          linb::any_cast<const std::vector<std::string>>(v);
      os << quote(vs);
      break;
    }
    case TypeTrait<value::ValueBlock>::type_id: {
      os << "None";
      break;
    }

    // TODO: List-up all case and remove `default` clause.
    default: {
      os << "ANY_PPRINT: TODO: (type: " << v.type_name() << ") ";
    }
  }

#undef BASETYPE_CASE_EXPR
#undef PRIMTYPE_CASE_EXPR
#undef ARRAY1DTYPE_CASE_EXPR
#undef ARRAY2DTYPE_CASE_EXPR

  return os.str();
}
#endif

std::string pprint_value(const value::Value &v, const uint32_t indent,
                         bool closing_brace) {
#define BASETYPE_CASE_EXPR(__ty)   \
  case TypeTrait<__ty>::type_id: { \
    auto ret = v.get_value<__ty>(); \
    if (ret) { \
      os << ret.value();         \
    } else { \
      os << "[InternalError: Base type TypeId mismatch.]"; \
    } \
    break;                         \
  }

#define PRIMTYPE_CASE_EXPR(__ty)                             \
  case TypeTrait<__ty>::type_id: {                           \
    auto ret = v.get_value<__ty>(); \
    if (ret) { \
      os << to_string(ret.value(), indent, closing_brace);     \
    } else { \
      os << "[InternalError: Prim type TypeId mismatch.]"; \
    } \
    break;                                                   \
  }

#define ARRAY1DTYPE_CASE_EXPR(__ty)             \
  case TypeTrait<std::vector<__ty>>::type_id: { \
    auto ret = v.get_value<std::vector<__ty>>(); \
    if (ret) { \
      os << ret.value(); \
    } else { \
      os << "[InternalError: 1D type TypeId mismatch.]"; \
    } \
    break;                                      \
  }



  std::stringstream os;

  switch (v.type_id()) {
    // TODO: `bool` type for 1D?
    BASETYPE_CASE_EXPR(bool)


    // base type
    CASE_EXPR_LIST(BASETYPE_CASE_EXPR)

    // 1D array
    CASE_EXPR_LIST(ARRAY1DTYPE_CASE_EXPR)

    // 2D array
    //CASE_EXPR_LIST(ARRAY2DTYPE_CASE_EXPR)

    // GPrim
    CASE_GPRIM_LIST(PRIMTYPE_CASE_EXPR)

    // dict and customData
    case TypeTrait<CustomDataType>::type_id: {
      auto ret = v.get_value<CustomDataType>();
      if (ret) {
        os << print_customData(ret.value(), "", indent);
      } else {
        os << "[InternalError: Dict type TypeId mismatch.]";
      }
      break;
    }

    case TypeTrait<value::token>::type_id: {
      if (auto ret = v.get_value<value::token>()) {
        os << quote(ret.value().str());
      } else {
        os << "[InternalError: Token type TypeId mismatch.]";
      }
      break;
    }
    case TypeTrait<std::vector<value::token>>::type_id: {
      auto ret = v.get_value<std::vector<value::token>>();
      if (ret) {
        const std::vector<value::token> &lst = ret.value();
        std::vector<std::string> vs;
        std::transform(lst.begin(), lst.end(), std::back_inserter(vs),
                       [](const value::token &tok) { return tok.str(); });

        os << quote(vs);
      } else {
        os << "[InternalError: `token[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTrait<std::string>::type_id: {
      if (auto ret = v.get_value<std::string>()) {
        os << quote(ret.value());
      } else {
        os << "[InternalError: `string` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTrait<StringData>::type_id: {
      if (auto ret = v.get_value<StringData>()) {
        os << ret.value();
      } else {
        os << "[InternalError: `string` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTrait<std::vector<std::string>>::type_id: {
      if (auto ret = v.get_value<std::vector<std::string>>()) {
        const std::vector<std::string> &vs = ret.value();
        os << quote(vs);
      } else {
        os << "[InternalError: `string[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTrait<std::vector<StringData>>::type_id: {
      if (auto ret = v.get_value<std::vector<StringData>>()) {
        const std::vector<StringData> &vs = ret.value();
        os << vs;
      } else {
        os << "[InternalError: `string[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTrait<value::ValueBlock>::type_id: {
      if (auto ret = v.get_value<value::ValueBlock>()) {
        os << "None";
      } else {
        os << "[InternalError: ValueBlock type TypeId mismatch.]";
      }
      break;
    }
    // TODO: List-up all case and remove `default` clause.
    default: {
      os << "VALUE_PPRINT: TODO: (type: " << v.type_name() << ") ";
    }
  }

#undef BASETYPE_CASE_EXPR
#undef PRIMTYPE_CASE_EXPR
#undef ARRAY1DTYPE_CASE_EXPR
#undef ARRAY2DTYPE_CASE_EXPR

  return os.str();
}

#undef CASE_EXPR_LIST
#undef CASE_GPRIM_LIST

}  // namespace value
}  // namespace tinyusdz
