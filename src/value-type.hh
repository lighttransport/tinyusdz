/// Copyright 2021-present Syoyo Fujita.
/// MIT license.
///
/// Type-erasure technique for Value, a Value class which can represent USD's mandatory and frequently used types(e.g. `float3`, `token`, `asset`)
/// and its array and compound-types(1D/2D array, dictionary).
/// Neigher std::any nor std::variant is applicable for such usecases, so write our own.
///
#pragma once

#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>
#include <cmath>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// TODO(syoyo): Use C++17 std::optional when compiled with C++-17 compiler

// clang and gcc
#if defined(__EXCEPTIONS) || defined(__cpp_exceptions)

#ifdef nsel_CONFIG_NO_EXCEPTIONS
#undef nsel_CONFIG_NO_EXCEPTIONS
#endif
#ifdef nssv_CONFIG_NO_EXCEPTIONS
#undef nssv_CONFIG_NO_EXCEPTIONS
#endif

#define nsel_CONFIG_NO_EXCEPTIONS 0
#define nssv_CONFIG_NO_EXCEPTIONS 0
#else
// -fno-exceptions
#define nsel_CONFIG_NO_EXCEPTIONS 1
#define nssv_CONFIG_NO_EXCEPTIONS 1
#endif
//#include "../../src/nonstd/expected.hpp"
#include "nonstd/optional.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "token-type.hh"
//#include "external/staticstruct.hh"

namespace tinyusdz {
namespace value {

constexpr char kToken[] = "Token";
constexpr char kString[] = "String";
constexpr char kPath[] = "Path";
constexpr char kAssetPath[] = "AssetPath";
constexpr char kDictionary[] = "Dictionary";
constexpr char kTimeCode[] = "TimeCode";

constexpr char kBool[] = "bool";
constexpr char kUChar[] = "uchar";
constexpr char kHalf[] = "half";
constexpr char kInt[] = "int";
constexpr char kUInt[] = "uint";
constexpr char kInt64[] = "int64";
constexpr char kUInt64[] = "uint64";

constexpr char kInt2[] = "int2";
constexpr char kInt3[] = "int3";
constexpr char kInt4[] = "int4";

constexpr char kUInt2[] = "uint2";
constexpr char kUInt3[] = "uint3";
constexpr char kUInt4[] = "uint4";

constexpr char kHalf2[] = "half2";
constexpr char kHalf3[] = "half3";
constexpr char kHalf4[] = "half4";

constexpr char kMatrix2d[] = "matrix2d";
constexpr char kMatrix3d[] = "matrix3d";
constexpr char kMatrix4d[] = "matrix4d";

constexpr char kFloat[]  = "float";
constexpr char kFloat2[] = "float2";
constexpr char kFloat3[] = "float3";
constexpr char kFloat4[] = "float4";

constexpr char kDouble[]  = "double";
constexpr char kDouble2[] = "double2";
constexpr char kDouble3[] = "double3";
constexpr char kDouble4[] = "double4";

constexpr char kQuath[] = "quath";
constexpr char kQuatf[] = "quatf";
constexpr char kQuatd[] = "quatd";

constexpr char kVector3h[] = "vector3h";
constexpr char kVector3f[] = "vector3f";
constexpr char kVector3d[] = "vector3d";

constexpr char kPoint3h[] = "point3h";
constexpr char kPoint3f[] = "point3f";
constexpr char kPoint3d[] = "point3d";

constexpr char kNormal3h[] = "normal3h";
constexpr char kNormal3f[] = "normal3f";
constexpr char kNormal3d[] = "normal3d";

constexpr char kColor3f[] = "color3f";
constexpr char kColor3d[] = "color3d";
constexpr char kColor4f[] = "color4f";
constexpr char kColor4d[] = "color4d";

constexpr char kFrame4d[] = "frame4d";

constexpr char kTexCoord2h[] = "texcoord2h";
constexpr char kTexCoord2f[] = "texcoord2f";
constexpr char kTexCoord2d[] = "texcoord2d";

constexpr char kTexCoord3h[] = "texcoord3h";
constexpr char kTexCoord3f[] = "texcoord3f";
constexpr char kTexCoord3d[] = "texcoord3d";

using token = tinyusdz::Token;

// SdfAssetPath
class asset_path
{
  public:
    asset_path() = default;
    asset_path(const std::string &a) : asset_path_(a) {}
    asset_path(const std::string &a, const std::string &r) : asset_path_(a), resolved_path_(r) {}

 private:
  std::string asset_path_;
  std::string resolved_path_;
};

//
// Type ID for TypeTrait<T>::type_id.
//
// These type IDs are internally used and can be changed arbitrary.
//
// These ID assignment won't affect Crate binary serialization.
// (See `crate-format.hh` for Type ID used in Crate binary)
//
// TODO(syoyo): Support 3D and 4D?
constexpr uint32_t TYPE_ID_1D_ARRAY_BIT = 1 << 20; // 1024
constexpr uint32_t TYPE_ID_2D_ARRAY_BIT = 1 << 21; // 2048
//constexpr uint32_t TYPE_ID_3D_ARRAY_BIT = 1 << 22;
//constexpr uint32_t TYPE_ID_4D_ARRAY_BIT = 1 << 23;

enum TypeId {
  TYPE_ID_INVALID,  // = 0
  TYPE_ID_NULL,
  TYPE_ID_VOID,
  TYPE_ID_MONOSTATE,

  TYPE_ID_TOKEN,
  TYPE_ID_STRING,

  TYPE_ID_BOOL,

  // TYPE_ID_INT8,
  TYPE_ID_HALF,
  TYPE_ID_INT32,
  TYPE_ID_INT64,

  TYPE_ID_HALF2,
  TYPE_ID_HALF3,
  TYPE_ID_HALF4,

  TYPE_ID_INT2,  // int32 x 2
  TYPE_ID_INT3,
  TYPE_ID_INT4,

  TYPE_ID_UCHAR,  // uint8
  TYPE_ID_UINT32,
  TYPE_ID_UINT64,

  TYPE_ID_UINT2,
  TYPE_ID_UINT3,
  TYPE_ID_UINT4,

  TYPE_ID_FLOAT,
  TYPE_ID_FLOAT2,
  TYPE_ID_FLOAT3,
  TYPE_ID_FLOAT4,

  TYPE_ID_DOUBLE,
  TYPE_ID_DOUBLE2,
  TYPE_ID_DOUBLE3,
  TYPE_ID_DOUBLE4,

  TYPE_ID_QUATH,
  TYPE_ID_QUATF,
  TYPE_ID_QUATD,

  TYPE_ID_MATRIX2D,
  TYPE_ID_MATRIX3D,
  TYPE_ID_MATRIX4D,

  TYPE_ID_COLOR3H,
  TYPE_ID_COLOR3F,
  TYPE_ID_COLOR3D,

  TYPE_ID_COLOR4H,
  TYPE_ID_COLOR4F,
  TYPE_ID_COLOR4D,

  TYPE_ID_POINT3H,
  TYPE_ID_POINT3F,
  TYPE_ID_POINT3D,

  TYPE_ID_NORMAL3H,
  TYPE_ID_NORMAL3F,
  TYPE_ID_NORMAL3D,

  TYPE_ID_VECTOR3H,
  TYPE_ID_VECTOR3F,
  TYPE_ID_VECTOR3D,

  TYPE_ID_FRAME4D,

  TYPE_ID_TEXCOORD2H,
  TYPE_ID_TEXCOORD2F,
  TYPE_ID_TEXCOORD2D,

  TYPE_ID_TEXCOORD3H,
  TYPE_ID_TEXCOORD3F,
  TYPE_ID_TEXCOORD3D,

  TYPE_ID_LAYER_OFFSET,
  TYPE_ID_PAYLOAD,

  TYPE_ID_TIMECODE,
  //TYPE_ID_TIMESAMPLE,

  TYPE_ID_DICT,

  //TYPE_ID_ASSET,
  TYPE_ID_ASSET_PATH,

  // Types in prim-types.hh
  TYPE_ID_REFERENCE,
  TYPE_ID_SPECIFIER,
  TYPE_ID_PERMISSION,
  TYPE_ID_VARIABILITY,
  TYPE_ID_LIST_OP_TOKEN,
  TYPE_ID_LIST_OP_STRING,
  TYPE_ID_LIST_OP_PATH,
  TYPE_ID_LIST_OP_REFERENCE,
  TYPE_ID_LIST_OP_INT,
  TYPE_ID_LIST_OP_INT64,
  TYPE_ID_LIST_OP_UINT,
  TYPE_ID_LIST_OP_UINT64,
  TYPE_ID_LIST_OP_PAYLOAD,

  TYPE_ID_PATH_VECTOR,
  TYPE_ID_TOKEN_VECTOR,

  TYPE_ID_TIMESAMPLES,
  TYPE_ID_VARIANT_SELECION_MAP,

  // Types in crate-format.hh
  TYPE_ID_CRATE_BEGIN = 256,
  TYPE_ID_CRATE_VALUE,
  TYPE_ID_CRATE_UNREGISTERED_VALUE,
  TYPE_ID_CRATE_LIST_OP_UNREGISTERED_VALUE,

  // Types for GPrim and usdGeom
  TYPE_ID_GPRIM = (1 << 10),
  TYPE_ID_GEOM_XFORM,
  TYPE_ID_GEOM_MESH,
  TYPE_ID_GEOM_BASIS_CURVE,
  TYPE_ID_GEOM_SPHERE,
  TYPE_ID_GEOM_CUBE,
  TYPE_ID_GEOM_CYLINDER,
  TYPE_ID_GEOM_CONE,

  // Types for usdLux
  TYPE_ID_LUX_BEGIN = (1 << 10) + (1 << 9),
  TYPE_ID_LUX_SPHERE,
  TYPE_ID_LUX_DOME,
  TYPE_ID_LUX_CYLINDER,
  TYPE_ID_LUX_DISK,
  TYPE_ID_LUX_RECT,

  // Types for usdShader
  TYPE_ID_SHADER_BEGIN = 1 << 11,

  // Types for usdImaging
  // See <pxrUSD>/pxr/usdImaging/usdImaging/tokens.h
  TYPE_ID_IMAGING_BEGIN = (1 << 11) + (1 << 10),
  TYPE_ID_IMAGING_PREVIEWSURFACE,
  TYPE_ID_IMAGING_UVTEXTURE,
  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT,
  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT2,
  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT3,
  TYPE_ID_IMAGING_PRIMVAR_READER_FLOAT4,
  TYPE_ID_IMAGING_PRIMVAR_READER_INT,
  TYPE_ID_IMAGING_TRANSFORM_2D,

  // Ttpes for usdVol
  TYPE_ID_VOL_BEGIN = 1 << 12,

  // Base ID for user data type(less than `TYPE_ID_1D_ARRAY_BIT-1`)
  TYPE_ID_USER_BEGIN = 65536,

  TYPE_ID_ALL = (TYPE_ID_2D_ARRAY_BIT - 1)  // terminator.
};

struct timecode
{
  double value;
};

struct half {
  uint16_t value;
};

using half2 = std::array<half, 2>;
using half3 = std::array<half, 3>;
using half4 = std::array<half, 4>;

using int2 = std::array<int32_t, 2>;
using int3 = std::array<int32_t, 3>;
using int4 = std::array<int32_t, 4>;

using uint2 = std::array<uint32_t, 2>;
using uint3 = std::array<uint32_t, 3>;
using uint4 = std::array<uint32_t, 4>;

using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;
using float4 = std::array<float, 4>;

using double2 = std::array<double, 2>;
using double3 = std::array<double, 3>;
using double4 = std::array<double, 4>;

struct matrix2f {
  matrix2f() {
    m[0][0] = 1.0f;
    m[0][1] = 0.0f;

    m[1][0] = 0.0f;
    m[1][1] = 1.0f;
  }

  float m[2][2];
};

struct matrix3f {
  matrix3f() {
    m[0][0] = 1.0f;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;

    m[1][0] = 0.0f;
    m[1][1] = 1.0f;
    m[1][2] = 0.0f;

    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = 1.0f;
  }

  float m[3][3];
};

struct matrix4f {
  matrix4f() {
    m[0][0] = 1.0f;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[0][3] = 0.0f;

    m[1][0] = 0.0f;
    m[1][1] = 1.0f;
    m[1][2] = 0.0f;
    m[1][3] = 0.0f;

    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[2][2] = 1.0f;
    m[2][3] = 0.0f;

    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;
  }

  float m[4][4];
};

struct matrix2d {
  matrix2d() {
    m[0][0] = 1.0;
    m[0][1] = 0.0;

    m[1][0] = 0.0;
    m[1][1] = 1.0;
  }

  double m[2][2];
};

struct matrix3d {
  matrix3d() {
    m[0][0] = 1.0;
    m[0][1] = 0.0;
    m[0][2] = 0.0;

    m[1][0] = 0.0;
    m[1][1] = 1.0;
    m[1][2] = 0.0;

    m[2][0] = 0.0;
    m[2][1] = 0.0;
    m[2][2] = 1.0;
  }

  double m[3][3];
};

struct matrix4d {
  matrix4d() {
    m[0][0] = 1.0;
    m[0][1] = 0.0;
    m[0][2] = 0.0;
    m[0][3] = 0.0;

    m[1][0] = 0.0;
    m[1][1] = 1.0;
    m[1][2] = 0.0;
    m[1][3] = 0.0;

    m[2][0] = 0.0;
    m[2][1] = 0.0;
    m[2][2] = 1.0;
    m[2][3] = 0.0;

    m[3][0] = 0.0;
    m[3][1] = 0.0;
    m[3][2] = 0.0;
    m[3][3] = 1.0;
  }

  double m[4][4];
};

// = matrix4d
struct frame4d {
  frame4d() {
    m[0][0] = 1.0;
    m[0][1] = 0.0;
    m[0][2] = 0.0;
    m[0][3] = 0.0;

    m[1][0] = 0.0;
    m[1][1] = 1.0;
    m[1][2] = 0.0;
    m[1][3] = 0.0;

    m[2][0] = 0.0;
    m[2][1] = 0.0;
    m[2][2] = 1.0;
    m[2][3] = 0.0;

    m[3][0] = 0.0;
    m[3][1] = 0.0;
    m[3][2] = 0.0;
    m[3][3] = 1.0;
  }
  double m[4][4];
};

struct quath {
  half real;
  half3 imag;
};

struct quatf {
  float real;
  float3 imag;
};

struct quatd {
  double real;
  double3 imag;
};

struct vector3h {
  half x, y, z;

  half operator[](size_t idx) { return *(&x + idx); }
};

struct vector3f {
  float x, y, z;

  float operator[](size_t idx) { return *(&x + idx); }
};

struct vector3d {
  double x, y, z;

  double operator[](size_t idx) { return *(&x + idx); }
};

struct normal3h {
  half x, y, z;

  half operator[](size_t idx) { return *(&x + idx); }
};

struct normal3f {
  float x, y, z;

  float operator[](size_t idx) { return *(&x + idx); }
};

struct normal3d {
  double x, y, z;

  double operator[](size_t idx) { return *(&x + idx); }
};

struct point3h {
  half x, y, z;

  half operator[](size_t idx) { return *(&x + idx); }
};

struct point3f {
  float x, y, z;

  float operator[](size_t idx) { return *(&x + idx); }
};

struct point3d {
  double x, y, z;

  double operator[](size_t idx) { return *(&x + idx); }
};

struct color3f {
  float r, g, b;

  // C++11 or later, struct is tightly packed, so use the pointer offset is
  // valid.
  float operator[](size_t idx) { return *(&r + idx); }
};

struct color4f {
  float r, g, b, a;

  // C++11 or later, struct is tightly packed, so use the pointer offset is
  // valid.
  float operator[](size_t idx) { return *(&r + idx); }
};

struct color3d {
  double r, g, b;

  // C++11 or later, struct is tightly packed, so use the pointer offset is
  // valid.
  double operator[](size_t idx) { return *(&r + idx); }
};

struct color4d {
  double r, g, b, a;

  // C++11 or later, struct is tightly packed, so use the pointer offset is
  // valid.
  double operator[](size_t idx) { return *(&r + idx); }
};

struct texcoord2h {
  half s, t;
};

struct texcoord2f {
  float s, t;
};

struct texcoord2d {
  double s, t;
};

struct texcoord3h {
  half s, t, r;
};

struct texcoord3f {
  float s, t, r;
};

struct texcoord3d {
  double s, t, r;
};

using double2 = std::array<double, 2>;
using double3 = std::array<double, 3>;
using double4 = std::array<double, 4>;

struct any_value;

using dict = std::map<std::string, any_value>;

template <class dtype>
struct TypeTrait;

#define DEFINE_TYPE_TRAIT(__dty, __name, __tyid, __nc)           \
  template <>                                                    \
  struct TypeTrait<__dty> {                                      \
    using value_type = __dty;                                    \
    using value_underlying_type = __dty;                         \
    static constexpr uint32_t ndim = 0; /* array dim */          \
    static constexpr uint32_t ncomp =                            \
        __nc; /* the number of components(e.g. float3 => 3) */   \
    static constexpr uint32_t type_id = __tyid;                  \
    static constexpr uint32_t underlying_type_id = __tyid;       \
    static std::string type_name() { return __name; }            \
    static std::string underlying_type_name() { return __name; } \
  }

// `role` type. Requies underlying type.
#define DEFINE_ROLE_TYPE_TRAIT(__dty, __name, __tyid, __uty)                  \
  template <>                                                                 \
  struct TypeTrait<__dty> {                                                   \
    using value_type = __dty;                                                 \
    using value_underlying_type = TypeTrait<__uty>::value_type;               \
    static constexpr uint32_t ndim = 0; /* array dim */                       \
    static constexpr uint32_t ncomp = TypeTrait<__uty>::ncomp;                \
    static constexpr uint32_t type_id = __tyid;                               \
    static constexpr uint32_t underlying_type_id = TypeTrait<__uty>::type_id; \
    static std::string type_name() { return __name; }                         \
    static std::string underlying_type_name() {                               \
      return TypeTrait<__uty>::type_name();                                   \
    }                                                                         \
  }

DEFINE_TYPE_TRAIT(std::nullptr_t, "null", TYPE_ID_NULL, 1);
DEFINE_TYPE_TRAIT(void, "void", TYPE_ID_VOID, 1);

DEFINE_TYPE_TRAIT(bool, kBool, TYPE_ID_BOOL, 1);
DEFINE_TYPE_TRAIT(uint8_t, kUChar, TYPE_ID_UCHAR, 1);
DEFINE_TYPE_TRAIT(half, kHalf, TYPE_ID_HALF, 1);

DEFINE_TYPE_TRAIT(int32_t, kInt, TYPE_ID_INT32, 1);
DEFINE_TYPE_TRAIT(uint32_t, kUInt, TYPE_ID_UINT32, 1);

DEFINE_TYPE_TRAIT(int64_t, kInt64, TYPE_ID_INT64, 1);
DEFINE_TYPE_TRAIT(uint64_t, kUInt64, TYPE_ID_UINT64, 1);

DEFINE_TYPE_TRAIT(int2, kInt2, TYPE_ID_INT2, 2);
DEFINE_TYPE_TRAIT(int3, kInt3, TYPE_ID_INT3, 3);
DEFINE_TYPE_TRAIT(int4, kInt4, TYPE_ID_INT4, 4);

DEFINE_TYPE_TRAIT(uint2, kUInt2, TYPE_ID_UINT2, 2);
DEFINE_TYPE_TRAIT(uint3, kUInt3, TYPE_ID_UINT3, 3);
DEFINE_TYPE_TRAIT(uint4, kUInt4, TYPE_ID_UINT4, 4);

DEFINE_TYPE_TRAIT(half2, kHalf2, TYPE_ID_HALF2, 2);
DEFINE_TYPE_TRAIT(half3, kHalf3, TYPE_ID_HALF3, 3);
DEFINE_TYPE_TRAIT(half4, kHalf4, TYPE_ID_HALF4, 4);

DEFINE_TYPE_TRAIT(float, kFloat, TYPE_ID_FLOAT, 1);
DEFINE_TYPE_TRAIT(float2, kFloat2, TYPE_ID_FLOAT2, 2);
DEFINE_TYPE_TRAIT(float3, kFloat3, TYPE_ID_FLOAT3, 3);
DEFINE_TYPE_TRAIT(float4, kFloat4, TYPE_ID_FLOAT4, 4);

DEFINE_TYPE_TRAIT(double, kDouble, TYPE_ID_DOUBLE, 1);
DEFINE_TYPE_TRAIT(double2, kDouble2, TYPE_ID_DOUBLE2, 2);
DEFINE_TYPE_TRAIT(double3, kDouble3, TYPE_ID_DOUBLE3, 3);
DEFINE_TYPE_TRAIT(double4, kDouble4, TYPE_ID_DOUBLE4, 4);


DEFINE_TYPE_TRAIT(quath, kQuath, TYPE_ID_QUATH, 1);
DEFINE_TYPE_TRAIT(quatf, kQuatf, TYPE_ID_QUATF, 1);
DEFINE_TYPE_TRAIT(quatd, kQuatd, TYPE_ID_QUATD, 1);

DEFINE_TYPE_TRAIT(matrix2d, kMatrix2d, TYPE_ID_MATRIX2D, 1);
DEFINE_TYPE_TRAIT(matrix3d, kMatrix3d, TYPE_ID_MATRIX3D, 1);
DEFINE_TYPE_TRAIT(matrix4d, kMatrix4d, TYPE_ID_MATRIX4D, 1);

DEFINE_TYPE_TRAIT(timecode, kTimeCode, TYPE_ID_TIMECODE, 1);

//
// Role types
//
DEFINE_ROLE_TYPE_TRAIT(vector3h, kVector3h, TYPE_ID_VECTOR3H, half3);
DEFINE_ROLE_TYPE_TRAIT(vector3f, kVector3f, TYPE_ID_VECTOR3F, float3);
DEFINE_ROLE_TYPE_TRAIT(vector3d, kVector3d, TYPE_ID_VECTOR3D, double3);

DEFINE_ROLE_TYPE_TRAIT(normal3h, kNormal3h, TYPE_ID_NORMAL3H, half3);
DEFINE_ROLE_TYPE_TRAIT(normal3f, kNormal3f, TYPE_ID_NORMAL3F, float3);
DEFINE_ROLE_TYPE_TRAIT(normal3d, kNormal3d, TYPE_ID_NORMAL3D, double3);

DEFINE_ROLE_TYPE_TRAIT(point3h, kPoint3h, TYPE_ID_POINT3H, half3);
DEFINE_ROLE_TYPE_TRAIT(point3f, kPoint3f, TYPE_ID_POINT3F, float3);
DEFINE_ROLE_TYPE_TRAIT(point3d, kPoint3d, TYPE_ID_POINT3D, double3);

DEFINE_ROLE_TYPE_TRAIT(frame4d, kFrame4d, TYPE_ID_FRAME4D, matrix4d);

DEFINE_ROLE_TYPE_TRAIT(color3f, kColor3f, TYPE_ID_COLOR3F, float3);
DEFINE_ROLE_TYPE_TRAIT(color4f, kColor4f, TYPE_ID_COLOR4F, float4);
DEFINE_ROLE_TYPE_TRAIT(color3d, kColor3d, TYPE_ID_COLOR3D, double3);
DEFINE_ROLE_TYPE_TRAIT(color4d, kColor4d, TYPE_ID_COLOR4D, double4);

DEFINE_ROLE_TYPE_TRAIT(texcoord2h, kTexCoord2h, TYPE_ID_TEXCOORD2H, half2);
DEFINE_ROLE_TYPE_TRAIT(texcoord2f, kTexCoord2f, TYPE_ID_TEXCOORD2F, float2);
DEFINE_ROLE_TYPE_TRAIT(texcoord2d, kTexCoord2d, TYPE_ID_TEXCOORD2D, double2);

DEFINE_ROLE_TYPE_TRAIT(texcoord3h, kTexCoord3h, TYPE_ID_TEXCOORD3H, half3);
DEFINE_ROLE_TYPE_TRAIT(texcoord3f, kTexCoord3f, TYPE_ID_TEXCOORD3F, float3);
DEFINE_ROLE_TYPE_TRAIT(texcoord3d, kTexCoord3d, TYPE_ID_TEXCOORD3D, double3);

//
//
//
DEFINE_TYPE_TRAIT(token, kToken, TYPE_ID_TOKEN, 1);
DEFINE_TYPE_TRAIT(std::string, kString, TYPE_ID_STRING, 1);
DEFINE_TYPE_TRAIT(dict, kDictionary, TYPE_ID_DICT, 1);

DEFINE_TYPE_TRAIT(asset_path, kAssetPath, TYPE_ID_ASSET_PATH, 1);

//
// Other types(e.g. TYPE_ID_REFERENCE) are defined in `prim-types.hh` and `crate-format.hh`(Data types used in Crate data)
//

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

// 1D Array
template <typename T>
struct TypeTrait<std::vector<T>> {
  using value_type = std::vector<T>;
  static constexpr uint32_t ndim = 1; /* array dim */
  static constexpr uint32_t ncomp = TypeTrait<T>::ncomp;
  static constexpr uint32_t type_id =
      TypeTrait<T>::type_id | TYPE_ID_1D_ARRAY_BIT;
  static constexpr uint32_t underlying_type_id =
      TypeTrait<T>::underlying_type_id | TYPE_ID_1D_ARRAY_BIT;
  static std::string type_name() { return TypeTrait<T>::type_name() + "[]"; }
  static std::string underlying_type_name() {
    return TypeTrait<T>::underlying_type_name() + "[]";
  }
};

// 2D Array
// TODO(syoyo): support 3D array?
template <typename T>
struct TypeTrait<std::vector<std::vector<T>>> {
  using value_type = std::vector<std::vector<T>>;
  static constexpr uint32_t ndim = 2; /* array dim */
  static constexpr uint32_t ncomp = TypeTrait<T>::ncomp;
  static constexpr uint32_t type_id =
      TypeTrait<T>::type_id | TYPE_ID_2D_ARRAY_BIT;
  static constexpr uint32_t underlying_type_id =
      TypeTrait<T>::underlying_type_id | TYPE_ID_2D_ARRAY_BIT;
  static std::string type_name() { return TypeTrait<T>::type_name() + "[][]"; }
  static std::string underlying_type_name() {
    return TypeTrait<T>::underlying_type_name() + "[][]";
  }
};

// Lookup TypeTrait<T>::type_name from TypeTrait<T>::type_id
nonstd::optional<std::string> TryGetTypeName(uint32_t tyid);
std::string GetTypeName(uint32_t tyid);

struct base_value {
  virtual ~base_value();
  virtual const std::string type_name() const = 0;
  virtual const std::string underlying_type_name() const = 0;
  virtual uint32_t type_id() const = 0;
  virtual uint32_t underlying_type_id() const = 0;

  virtual uint32_t ndim() const = 0;
  virtual uint32_t ncomp() const = 0;

  virtual const void *value() const = 0;
  virtual void *value() = 0;
};

template <typename T>
struct value_impl : public base_value {
  using type = typename TypeTrait<T>::value_type;

  value_impl(const T &v) : _value(v) {}

  const std::string type_name() const override {
    return TypeTrait<T>::type_name();
  }

  const std::string underlying_type_name() const override {
    return TypeTrait<T>::underlying_type_name();
  }

  uint32_t type_id() const override { return TypeTrait<T>::type_id; }
  uint32_t underlying_type_id() const override {
    return TypeTrait<T>::underlying_type_id;
  }

  const void *value() const override {
    return reinterpret_cast<const void *>(&_value);
  }

  void *value() override { return reinterpret_cast<void *>(&_value); }

  uint32_t ndim() const override { return TypeTrait<T>::ndim; }
  uint32_t ncomp() const override { return TypeTrait<T>::ncomp; }

  T _value;
};

struct any_value {
  any_value() = default;

  template <typename T>
  any_value(const T &v) {
    p.reset(new value_impl<T>(v));
  }

  const std::string type_name() const {
    if (p) {
      return p->type_name();
    }
    return std::string();
  }

  const std::string underlying_type_name() const {
    if (p) {
      return p->underlying_type_name();
    }
    return std::string();
  }

  uint32_t type_id() const {
    if (p) {
      return p->type_id();
    }

    return TYPE_ID_INVALID;
  }

  uint32_t underlying_type_id() const {
    if (p) {
      return p->underlying_type_id();
    }

    return TYPE_ID_INVALID;
  }

  int32_t ndim() const {
    if (p) {
      return int32_t(p->ndim());
    }

    return -1;  // invalid
  }

  uint32_t ncomp() const {
    if (p) {
      return p->ncomp();
    }

    return 0;  // empty
  }

  const void *value() const {
    if (p) {
      return p->value();
    }
    return nullptr;
  }

  void *value() {
    if (p) {
      return p->value();
    }
    return nullptr;
  }

  template <class T>
  operator T() const {
    assert(TypeTrait<T>::type_id == p->type_id());

    return *(reinterpret_cast<const T *>(p->value()));
  }

  template <class T>
  operator std::vector<T>() const {
    assert(TypeTrait<std::vector<T>>::type_id == p->type_id());

    return *(reinterpret_cast<const std::vector<T> *>(p->value()));
  }

  std::shared_ptr<base_value> p;
};

struct TimeSamples {
  std::vector<double> times;
  std::vector<any_value> values; // Could contain 'None'

  bool Valid() {
    if (times.size() > 0) {
      return true;
    }
    return false;
  }
};

// simple linear interpolator
template<typename T>
struct LinearInterpolator
{
  static T interpolate(const T *values, const size_t n, const double _t) {
    if (n == 0) {
      return static_cast<T>(0);
    } else if (n == 1) {
      return values[0];
    }

    // [0.0, 1.0]
    double t = std::fmin(0.0, std::fmax(_t, 1.0));

    size_t idx0 = std::max(n-1, size_t(t * double(n)));
    size_t idx1 = std::max(n-1, idx0+1);

    return (1.0 - t) * values[idx0] + t * values[idx1];
  }
};

// Explicitly typed version of `TimeSamples`
//
// `None` value and `deleted` items are omitted in this data struct.
// e.g.
//
// double radius.timeSamples = { 0: 1.0, 1: None, 2: 3.0 }
//
// in .usd(or `TimeSamples` class), are stored as
//
// radius = { 0: 1.0, 2: 3.0 }
//
template<typename T>
struct AnimatableValue
{
  std::vector<double> times; // Assume sorted
  std::vector<T> values;

  bool is_scalar() const {
    return (times.size() == 0) && (values.size() == 1);
  }

  bool is_timesample() const {
    return (times.size() > 0) && (times.size() == values.size());
  }

  template<class Interpolator>
  T Get(double time = 0.0) {
    std::vector<double>::iterator it = std::lower_bound(times.begin(), times.end(), time);

    size_t idx0, idx1;
    if (it != times.end()) {
      idx0 = std::distance(times.begin(), it);
      idx1 = std::min(idx0 + 1, times.size() - 1);
    } else {
      idx0 = idx1 = times.size() - 1;
    }
    double slope = times[idx1] - times[idx0];
    if (slope < std::numeric_limits<double>::epsilon()) {
      slope = 1.0;
    }

    const double t = (times[idx1] - time) / slope;

    T val = Interpolator::interpolate(values.data(), values.size(), t);
    return val;
  }
};

struct PrimVar {
  // For scalar value, times.size() == 0, and values.size() == 1
  TimeSamples var;

  bool is_scalar() const {
    return (var.times.size() == 0) && (var.values.size() == 1);
  }

  bool is_timesample() const {
    return (var.times.size() > 0) && (var.times.size() == var.values.size());
  }

  bool is_valid() const { return is_scalar() || is_timesample(); }

  std::string type_name() const {
    if (!is_valid()) {
      return std::string();
    }
    return var.values[0].type_name();
  }

  uint32_t type_id() const {
    if (!is_valid()) {
      return TYPE_ID_INVALID;
    }

    return var.values[0].type_id();
  }

  // Type-safe way to get concrete value.
  template <class T>
  nonstd::optional<T> get_value() const {

    if (!is_scalar()) {
      return nonstd::nullopt;
    }

    if (TypeTrait<T>::type_id == var.values[0].type_id()) {
      return std::move(*reinterpret_cast<const T *>(var.values[0].value()));
    } else if (TypeTrait<T>::underlying_type_id == var.values[0].underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      return std::move(*reinterpret_cast<const T *>(var.values[0].value()));
    }
    return nonstd::nullopt;
  }

  template <class T>
  void set_scalar(const T &v) {
    var.times.clear();
    var.values.clear();

    var.values.push_back(v);
  }

};

// using Object = std::map<std::string, any_value>;

class Value {
 public:
  // using Dict = std::map<std::string, Value>;

  Value() = default;

  template <class T>
  Value(const T &v) : v_(v) {}

  //template <class T>
  //Value(T &&v) : v_(v) {}

  std::string type_name() const { return v_.type_name(); }
  std::string underlying_type_name() const { return v_.underlying_type_name(); }

  uint32_t type_id() const { return v_.type_id(); }
  uint32_t underlying_type_id() const { return v_.underlying_type_id(); }

  // Return nullptr when type conversion failed.
  template <class T>
  const T *as() const {
    if (TypeTrait<T>::type_id == v_.type_id()) {
      return reinterpret_cast<const T *>(v_.value());
    } else {
      return nullptr;
    }
  }

  // Useful function to retrieve concrete value with type T.
  // Undefined behavior(usually will triger segmentation fault) when
  // type-mismatch. (We don't throw exception)
  template <class T>
  const T &value() const {
    return (*reinterpret_cast<const T *>(v_.value()));
  }

  // Type-safe way to get concrete value.
  template <class T>
  nonstd::optional<T> get_value() const {
    if (TypeTrait<T>::type_id == v_.type_id()) {
      return std::move(value<T>());
    } else if (TypeTrait<T>::underlying_type_id == v_.underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      return std::move(value<T>());
    }
    return nonstd::nullopt;
  }

  template <class T>
  Value &operator=(const T &v) {
    v_ = v;
    return (*this);
  }

  bool is_array() const { return v_.ndim() > 0; }
  int32_t ndim() const { return v_.ndim(); }

  uint32_t ncomp() const { return v_.ncomp(); }

  bool is_vector_type() const { return v_.ncomp() > 1; }

  //friend std::ostream &operator<<(std::ostream &os, const Value &v);
  const any_value& get_raw() const {
    return v_;
  }

 private:
  any_value v_;
};

bool is_float(const any_value &v);
bool is_double(const any_value &v);

// Frequently-used utility function
bool is_float(const Value &v);
bool is_float2(const Value &v);
bool is_float3(const Value &v);
bool is_float4(const Value &v);
bool is_double(const Value &v);
bool is_double2(const Value &v);
bool is_double3(const Value &v);
bool is_double4(const Value &v);


//
// typecast from type_id
// It does not throw exception.
//
template <uint32_t tid>
struct typecast {};

#define TYPECAST_BASETYPE(__tid, __ty)                   \
  template <>                                            \
  struct typecast<__tid> {                               \
    static __ty to(const any_value &v) {                 \
      return *reinterpret_cast<const __ty *>(v.value()); \
    }                                                    \
  }

TYPECAST_BASETYPE(TYPE_ID_BOOL, bool);
TYPECAST_BASETYPE(TYPE_ID_UCHAR, uint8_t);
TYPECAST_BASETYPE(TYPE_ID_HALF, half);
TYPECAST_BASETYPE(TYPE_ID_HALF2, half2);
TYPECAST_BASETYPE(TYPE_ID_HALF3, half3);
TYPECAST_BASETYPE(TYPE_ID_HALF4, half4);

TYPECAST_BASETYPE(TYPE_ID_UINT32, uint32_t);
TYPECAST_BASETYPE(TYPE_ID_FLOAT, float);
TYPECAST_BASETYPE(TYPE_ID_DOUBLE, double);

TYPECAST_BASETYPE(TYPE_ID_FLOAT | TYPE_ID_1D_ARRAY_BIT, std::vector<float>);

// TODO(syoyo): Implement more types...

#undef TYPECAST_BASETYPE

//
// Type checker
//
template <typename T>
struct is_type {
  bool operator()(const any_value &v) {
    return TypeTrait<T>::type_id == v.type_id();
  }
};

struct AttribMap {
  std::map<std::string, any_value> attribs;
};

} // namespace value
} // namespace tinyusdz

namespace tinyusdz {
namespace value {

static_assert(sizeof(quath) == 8, "sizeof(quath) must be 8");
static_assert(sizeof(quatf) == 16, "sizeof(quatf) must be 16");
static_assert(sizeof(half) == 2, "sizeof(half) must be 2");
static_assert(sizeof(half2) == 4, "sizeof(half2) must be 4");
static_assert(sizeof(half3) == 6, "sizeof(half3) must be 6");
static_assert(sizeof(half4) == 8, "sizeof(half4) must be 8");
static_assert(sizeof(float3) == 12, "sizeof(float3) must be 12");
static_assert(sizeof(color3f) == 12, "sizeof(color3f) must be 12");
static_assert(sizeof(color4f) == 16, "sizeof(color4f) must be 16");

} // namespace value
} // namespace tinyusdz
