#include "value-pprint.hh"
#include "prim-types.hh"

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half &v) {
  os << tinyusdz::half_to_float(v);
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

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::vector3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::vector3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::normal3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::normal3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::normal3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::point3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::point3f &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::point3d &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::color3f &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::color3d &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::color4f &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::color4d &v) {
  os << "(" << v.r << ", " << v.g << ", " << v.b << ", " << v.a << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::quath &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::quatf &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::quatd &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const  tinyusdz::value::texcoord2f &v) {
  os << "(" << v.s << ", " << v.t << ")";
  return os;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::matrix2d &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::matrix3d &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << ", " << m.m[0][2] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ", " << m.m[1][2] << "), ";
  ofs << "(" << m.m[2][0] << ", " << m.m[2][1] << ", " << m.m[2][2] << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::matrix4d &m) {
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
  ofs << tok.str();

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::dict &m) {
  (void)m;
  ofs << "[TODO] dict type[/TODO]";

  return ofs;
}

} // namespace std

namespace tinyusdz {
namespace value {

inline std::ostream &operator<<(std::ostream &os, const any_value &v) {
    // Simple brute-force way..
    // TODO: Use std::function or some template technique?

#define BASETYPE_CASE_EXPR(__ty)                      \
  case TypeTrait<__ty>::type_id: {                    \
    os << *reinterpret_cast<const __ty *>(v.value()); \
    break;                                            \
  }

#define ARRAY1DTYPE_CASE_EXPR(__ty)                                \
  case TypeTrait<std::vector<__ty>>::type_id: {                    \
    os << *reinterpret_cast<const std::vector<__ty> *>(v.value()); \
    break;                                                         \
  }

#define ARRAY2DTYPE_CASE_EXPR(__ty)                                  \
  case TypeTrait<std::vector<std::vector<__ty>>>::type_id: {         \
    os << *reinterpret_cast<const std::vector<std::vector<__ty>> *>( \
        v.value());                                                  \
    break;                                                           \
  }

#define CASE_EXR_LIST(__FUNC) \
  __FUNC(token)               \
  __FUNC(std::string)         \
  __FUNC(half)                \
  __FUNC(half2)               \
  __FUNC(half3)               \
  __FUNC(half4)               \
  __FUNC(int32_t)             \
  __FUNC(uint32_t)            \
  __FUNC(int2)                \
  __FUNC(int3)                \
  __FUNC(int4)                \
  __FUNC(uint2)               \
  __FUNC(uint3)               \
  __FUNC(uint4)               \
  __FUNC(int64_t)             \
  __FUNC(uint64_t)            \
  __FUNC(float)               \
  __FUNC(float2)              \
  __FUNC(float3)              \
  __FUNC(float4)              \
  __FUNC(double)              \
  __FUNC(double2)             \
  __FUNC(double3)             \
  __FUNC(double4)             \
  __FUNC(matrix2d)            \
  __FUNC(matrix3d)            \
  __FUNC(matrix4d)            \
  __FUNC(quath)               \
  __FUNC(quatf)               \
  __FUNC(quatd)               \
  __FUNC(normal3h)            \
  __FUNC(normal3f)            \
  __FUNC(normal3d)            \
  __FUNC(vector3h)            \
  __FUNC(vector3f)            \
  __FUNC(vector3d)            \
  __FUNC(point3h)             \
  __FUNC(point3f)             \
  __FUNC(point3d)             \
  __FUNC(color3f)             \
  __FUNC(color3d)             \
  __FUNC(color4f)             \
  __FUNC(color4d)

    switch (v.type_id()) {
      // no `bool` type for 1D and 2D array
      BASETYPE_CASE_EXPR(bool)

      // no std::vector<dict> and std::vector<std::vector<dict>>, ...
      BASETYPE_CASE_EXPR(dict)

      // base type
      CASE_EXR_LIST(BASETYPE_CASE_EXPR)

      // 1D array
      CASE_EXR_LIST(ARRAY1DTYPE_CASE_EXPR)

      // 2D array
      CASE_EXR_LIST(ARRAY2DTYPE_CASE_EXPR)

      // TODO: List-up all case and remove `default` clause.
      default: {
        os << "PPRINT: TODO: (type: " << v.type_name() << ") ";
      }
    }

#undef BASETYPE_CASE_EXPR
#undef ARRAY1DTYPE_CASE_EXPR
#undef ARRAY2DTYPE_CASE_EXPR
#undef CASE_EXPR_LIST

    return os;
  }

//std::ostream &operator<<(std::ostream &os, const Value &v) {
//  os << v.get_raw();  // delegate to operator<<(os, any_value)
//  return os;
//}



} // namespace value
} // namespace tinyusdz
