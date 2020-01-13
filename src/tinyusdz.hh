#ifndef TINYUSDZ_HH_
#define TINYUSDZ_HH_

#include <string>
#include <vector>
#include <array>
#include <cstring>
#include <map>

namespace tinyusdz {

// TODO(syoyo): Support Big Endian
// https://gist.github.com/rygorous/2156668
union FP32 {
  unsigned int u;
  float f;
  struct {
#if 1
    unsigned int Mantissa : 23;
    unsigned int Exponent : 8;
    unsigned int Sign : 1;
#else
    unsigned int Sign : 1;
    unsigned int Exponent : 8;
    unsigned int Mantissa : 23;
#endif
  } s;
};

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif

// NOTE: usually sizeof(float16) == 4, not 2
union float16 {
  unsigned short u;
  struct {
#if 1
    unsigned int Mantissa : 10;
    unsigned int Exponent : 5;
    unsigned int Sign : 1;
#else
    unsigned int Sign : 1;
    unsigned int Exponent : 5;
    unsigned int Mantissa : 10;
#endif
  } s;
};

float half_to_float(float16 h);
float16 float_to_half_full(float f);


template<typename T, size_t N>
struct Matrix
{
  T m[N][N];
  constexpr static uint32_t n = N;
};

using Matrix2f = Matrix<float, 2>;
using Matrix2d = Matrix<double, 2>;
using Matrix3f = Matrix<float, 3>;
using Matrix3d = Matrix<double, 3>;
using Matrix4f = Matrix<float, 4>;
using Matrix4d = Matrix<double, 4>;

using Vec4i = std::array<int32_t, 4>;
using Vec3i = std::array<int32_t, 3>;
using Vec2i = std::array<int32_t, 2>;

// Use uint16_t for storage class.
// Need to decode/encode value through half converter functions
using Vec4h = std::array<uint16_t, 4>;
using Vec3h = std::array<uint16_t, 3>;
using Vec2h = std::array<uint16_t, 2>;

using Vec4f = std::array<float, 4>;
using Vec3f = std::array<float, 3>;
using Vec2f = std::array<float, 2>;

using Vec4d = std::array<double, 4>;
using Vec3d = std::array<double, 3>;
using Vec2d = std::array<double, 2>;

using Quath = std::array<uint16_t, 4>;
using Quatf = std::array<float, 4>;
using Quatd = std::array<double, 4>;
using Quaternion = std::array<double, 4>; // Storage layout is same with Quadd, so we can delete this

// TODO(syoyo): Range, Interval, Rect2i, Frustum, MultiInterval

/*
#define VT_GFRANGE_VALUE_TYPES                 \
((      GfRange3f,           Range3f        )) \
((      GfRange3d,           Range3d        )) \
((      GfRange2f,           Range2f        )) \
((      GfRange2d,           Range2d        )) \
((      GfRange1f,           Range1f        )) \
((      GfRange1d,           Range1d        ))

#define VT_RANGE_VALUE_TYPES                   \
    VT_GFRANGE_VALUE_TYPES                     \
((      GfInterval,          Interval       )) \
((      GfRect2i,            Rect2i         ))

#define VT_STRING_VALUE_TYPES            \
((      std::string,           String )) \
((      TfToken,               Token  ))

#define VT_QUATERNION_VALUE_TYPES           \
((      GfQuath,             Quath ))       \
((      GfQuatf,             Quatf ))       \
((      GfQuatd,             Quatd ))       \
((      GfQuaternion,        Quaternion ))

#define VT_NONARRAY_VALUE_TYPES                 \
((      GfFrustum,           Frustum))          \
((      GfMultiInterval,     MultiInterval))

*/

struct USDLoadOptions
{


};

struct USDWriteOptions
{


};

template <typename T>
class ListOp
{
 public:
  ListOp() : is_explicit(false) {}

  void ClearAndMakeExplicit() {
    explicit_items.clear();
    added_items.clear();
    prepended_items.clear();
    appended_items.clear();
    deleted_items.clear();
    ordered_items.clear();

    is_explicit = true;
  }

  bool HasExplicitItems() const {
    return explicit_items.size();
  }

  bool HasAddedItems() const {
    return added_items.size();
  }

  bool HasPrependedItems() const {
    return prepended_items.size();
  }

  bool HasAppendedItems() const {
    return appended_items.size();
  }

  bool HasDeletedItems() const {
    return deleted_items.size();
  }

  bool HasOrderedItems() const {
    return deleted_items.size();
  }

  const std::vector<T> &GetExplicitItems() const {
    return explicit_items;
  }

  const std::vector<T> &GetAddedItems() const {
    return added_items;
  }

  const std::vector<T> &GetPrependedItems() const {
    return prepended_items;
  }

  const std::vector<T> &GetAppendedItems() const {
    return appended_items;
  }

  const std::vector<T> &GetDeletedItems() const {
    return deleted_items;
  }

  const std::vector<T> &GetOrderedItems() const {
    return ordered_items;
  }

  void SetExplicitItems(const std::vector<T> &v) {
    explicit_items = v;
  }

  void SetAddedItems(const std::vector<T> &v) {
    added_items = v;
  }

  void SetPrependedItems(const std::vector<T> &v) {
    prepended_items = v;
  }

  void SetAppendedItems(const std::vector<T> &v) {
    appended_items = v;
  }

  void SetDeletedItems(const std::vector<T> &v) {
    deleted_items = v;
  }

  void SetOrderedItems(const std::vector<T> &v) {
    ordered_items = v;
  }

 private:

  bool is_explicit{false};
  std::vector<T> explicit_items;
  std::vector<T> added_items;
  std::vector<T> prepended_items;
  std::vector<T> appended_items;
  std::vector<T> deleted_items;
  std::vector<T> ordered_items;
};

struct ListOpHeader {
    enum Bits { IsExplicitBit = 1 << 0,
                 HasExplicitItemsBit = 1 << 1,
                 HasAddedItemsBit = 1 << 2,
                 HasDeletedItemsBit = 1 << 3,
                 HasOrderedItemsBit = 1 << 4,
                 HasPrependedItemsBit = 1 << 5,
                 HasAppendedItemsBit = 1 << 6 };

    ListOpHeader() : bits(0) {}

    explicit ListOpHeader(uint8_t b) : bits(b) { }

    explicit ListOpHeader(ListOpHeader const &op) : bits(0) {
        bits |= op.IsExplicit() ? IsExplicitBit : 0;
        bits |= op.HasExplicitItems() ? HasExplicitItemsBit : 0;
        bits |= op.HasAddedItems() ? HasAddedItemsBit : 0;
        bits |= op.HasPrependedItems() ? HasPrependedItemsBit : 0;
        bits |= op.HasAppendedItems() ? HasAppendedItemsBit : 0;
        bits |= op.HasDeletedItems() ? HasDeletedItemsBit : 0;
        bits |= op.HasOrderedItems() ? HasOrderedItemsBit : 0;
    }

    bool IsExplicit() const { return bits & IsExplicitBit; }

    bool HasExplicitItems() const { return bits & HasExplicitItemsBit; }
    bool HasAddedItems() const { return bits & HasAddedItemsBit; }
    bool HasPrependedItems() const { return bits & HasPrependedItemsBit; }
    bool HasAppendedItems() const { return bits & HasAppendedItemsBit; }
    bool HasDeletedItems() const { return bits & HasDeletedItemsBit; }
    bool HasOrderedItems() const { return bits & HasOrderedItemsBit; }

    uint8_t bits;
};

///
/// We don't need performance and for USDZ, so use naiive implementation
/// to represent Path.
/// Path is something like Unix path, delimited by `/`, ':' and '.'
///
/// Example:
///
/// `/muda/bora.dora` : prim_part is `/muda/bora`, prop_part is `.dora`.
///
/// ':' is a namespce delimiter(example `input:muda`).
///
/// Limitations:
///
/// Relational attribute path(`[` `]`. e.g. `/muda/bora[/ari].dora`) is not
/// supported.
///
/// variant chars('{' '}') is not supported.
/// '..' is not supported
///
/// and have more limitatons.
///
class Path {
 public:
  Path() : valid(true) {}
  Path(const std::string &prim) : prim_part(prim) {}
  Path(const std::string &prim, const std::string &prop)
      : prim_part(prim), prop_part(prop) {}

  std::string name() const {
    std::string s;
    if (!valid) {
      s += "INVALID#";
    }

    s += prim_part;
    if (prop_part.empty()) {
      return s;
    }

    s += "." + prop_part;

    return s;
  }

  bool IsEmpty() { return (prim_part.empty() && prop_part.empty()); }

  static Path AbsoluteRootPath() { return Path("/"); }

  Path AppendProperty(const std::string &elem) {
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
      //std::cerr << "???. elem[0] is '.'\n";
      // For a while, make this valid.
      p.valid = false;
      return p;
    } else {
      p.prop_part = elem;
      return p;
    }
  }

  Path AppendElement(const std::string &elem) {
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
      //std::cerr << "???. elem[0] is '.'\n";
      // For a while, make this valid.
      p.valid = false;
      return p;
    } else {
      //std::cout << "elem " << elem << "\n";
      if ((p.prim_part.size() == 1) && (p.prim_part[0] == '/')) {
        p.prim_part += elem;
      } else {
        p.prim_part += '/' + elem;
      }
      return p;
    }
  }

  bool IsValid() const { return valid; }

 private:
  std::string prim_part;
  std::string prop_part;
  bool valid{true};
};


enum ValueTypeId
{
  VALUE_TYPE_INVALID = 0,

  VALUE_TYPE_BOOL = 1,
  VALUE_TYPE_UCHAR = 2,
  VALUE_TYPE_INT = 3,
  VALUE_TYPE_UINT = 4,
  VALUE_TYPE_INT64 = 5,
  VALUE_TYPE_UINT64 = 6,

  VALUE_TYPE_HALF = 7,
  VALUE_TYPE_FLOAT = 8,
  VALUE_TYPE_DOUBLE = 9,

  VALUE_TYPE_STRING = 10,
  VALUE_TYPE_TOKEN = 11,
  VALUE_TYPE_ASSET_PATH = 12,

  VALUE_TYPE_MATRIX2D = 13,
  VALUE_TYPE_MATRIX3D = 14,
  VALUE_TYPE_MATRIX4D = 15,

  VALUE_TYPE_QUATD = 16,
  VALUE_TYPE_QUATF = 17,
  VALUE_TYPE_QUATH = 18,

  VALUE_TYPE_VEC2D = 19,
  VALUE_TYPE_VEC2F = 20,
  VALUE_TYPE_VEC2H = 21,
  VALUE_TYPE_VEC2I = 22,

  VALUE_TYPE_VEC3D = 23,
  VALUE_TYPE_VEC3F = 24,
  VALUE_TYPE_VEC3H = 25,
  VALUE_TYPE_VEC3I = 26,

  VALUE_TYPE_VEC4D = 27,
  VALUE_TYPE_VEC4F = 28,
  VALUE_TYPE_VEC4H = 29,
  VALUE_TYPE_VEC4I = 30,

  VALUE_TYPE_DICTIONARY = 31,
  VALUE_TYPE_TOKEN_LIST_OP = 32,
  VALUE_TYPE_STRING_LIST_OP = 33,
  VALUE_TYPE_PATH_LIST_OP = 34,
  VALUE_TYPE_REFERENCE_LIST_OP = 35,
  VALUE_TYPE_INT_LIST_OP = 36,
  VALUE_TYPE_INT64_LIST_OP = 37,
  VALUE_TYPE_UINT_LIST_OP = 38,
  VALUE_TYPE_UINT64_LIST_OP = 39,

  VALUE_TYPE_PATH_VECTOR = 40,
  VALUE_TYPE_TOKEN_VECTOR = 41,

  VALUE_TYPE_SPECIFIER = 42,
  VALUE_TYPE_PERMISSION = 43,
  VALUE_TYPE_VARIABILITY = 44,

  VALUE_TYPE_VARIANT_SELECTION_MAP = 45,
  VALUE_TYPE_TIME_SAMPLES = 46,
  VALUE_TYPE_PAYLOAD = 47,
  VALUE_TYPE_DOUBLE_VECTOR = 48,
  VALUE_TYPE_LAYER_OFFSET_VECTOR = 49,
  VALUE_TYPE_STRING_VECTOR = 50,
  VALUE_TYPE_VALUE_BLOCK = 51,
  VALUE_TYPE_VALUE = 52,
  VALUE_TYPE_UNREGISTERED_VALUE = 53,
  VALUE_TYPE_UNREGISTERED_VALUE_LIST_OP = 54,
  VALUE_TYPE_PAYLOAD_LIST_OP = 55,
  VALUE_TYPE_TIME_CODE = 56,
};

struct ValueType {
  ValueType() : name("Invalid"), id(VALUE_TYPE_INVALID), supports_array(false) {}
  ValueType(const std::string &n, uint32_t i, bool a)
      : name(n), id(ValueTypeId(i)), supports_array(a) {}

  std::string name;
  ValueTypeId id{VALUE_TYPE_INVALID};
  bool supports_array{false};
};

///
/// Represent value.
/// multi-dimensional type is not supported(e.g. float[][])
///
class Value {
 public:
  typedef std::map<std::string, Value> Dictionary;

  Value() = default;

  Value(const ValueType &_dtype, const std::vector<uint8_t> &_data) :
    dtype(_dtype), data(_data), array_length(-1) {}
  Value(const ValueType &_dtype, const std::vector<uint8_t> &_data, uint64_t _array_length) :
    dtype(_dtype), data(_data), array_length(int64_t(_array_length)) {}

  bool IsArray() {
    if ((array_length > 0) || string_array.size() || (dtype.id == VALUE_TYPE_PATH_LIST_OP)) {
      return true;
    }
    return false;
  }

  // Setter for primitive types.
  void SetBool(const bool d) {
    dtype.name = "Bool";   
    dtype.id = VALUE_TYPE_BOOL;

    uint8_t value = d ? 1 : 0;
    data.resize(1);
    data[0] = value;
  }

  void SetUChar(const unsigned char d) {
    dtype.name = "UChar";   
    dtype.id = VALUE_TYPE_UCHAR;

    data.resize(1);
    data[0] = d;
  }

  void SetInt(const int32_t i) {
    static_assert(sizeof(int32_t) == 4, "");
    dtype.name = "Int";   
    dtype.id = VALUE_TYPE_INT;
    data.resize(sizeof(int32_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(int32_t));
  }

  void SetUInt(const uint32_t i) {
    static_assert(sizeof(uint32_t) == 4, "");
    dtype.name = "UInt";   
    dtype.id = VALUE_TYPE_UINT;
    data.resize(sizeof(uint32_t));
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(uint32_t));
  }

  void SetInt64(const int64_t i) {
    static_assert(sizeof(int64_t) == 8, "");
    dtype.name = "Int64";   
    dtype.id = VALUE_TYPE_INT64;
    data.resize(sizeof(int64_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(int64_t));
  }

  void SetUInt64(const uint64_t i) {
    static_assert(sizeof(uint64_t) == 8, "");
    dtype.name = "UInt64";   
    dtype.id = VALUE_TYPE_UINT64;
    data.resize(sizeof(uint64_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&i), sizeof(uint64_t));
  }

  void SetDouble(const double d) {
    static_assert(sizeof(double) == 8, "");
    dtype.name = "Double";   
    dtype.id = VALUE_TYPE_DOUBLE;
    data.resize(sizeof(double)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(double));
  }

  void SetFloat(const float d) {
    static_assert(sizeof(float) == 4, "");
    dtype.name = "Float";   
    dtype.id = VALUE_TYPE_FLOAT;
    data.resize(sizeof(float));
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(float));
  }

  void SetHalf(const float16 d) {
    dtype.name = "Half";   
    dtype.id = VALUE_TYPE_HALF;
    data.resize(sizeof(uint16_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&d.u), sizeof(uint16_t));
  }

  void SetVec2i(const Vec2i v) {
    static_assert(sizeof(Vec2i) == 8, "");
    dtype.name = "Vec2i";   
    dtype.id = VALUE_TYPE_VEC2I;
    data.resize(sizeof(Vec2i)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2i));
  }

  void SetVec2f(const Vec2f v) {
    static_assert(sizeof(Vec2f) == 8, "");
    dtype.name = "Vec2f";   
    dtype.id = VALUE_TYPE_VEC2F;
    data.resize(sizeof(Vec2f)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2f));
  }

  void SetVec2d(const Vec2d v) {
    static_assert(sizeof(Vec2d) == 16, "");
    dtype.name = "Vec2d";   
    dtype.id = VALUE_TYPE_VEC2D;
    data.resize(sizeof(Vec2d)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2d));
  }

  void SetVec2h(const Vec2h v) {
    static_assert(sizeof(Vec2h) == 4, "");
    dtype.name = "Vec2h";   
    dtype.id = VALUE_TYPE_VEC2H;
    data.resize(sizeof(Vec2h)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec2h));
  }

  void SetVec3i(const Vec3i v) {
    static_assert(sizeof(Vec3i) == 12, "");
    dtype.name = "Vec3i";   
    dtype.id = VALUE_TYPE_VEC3I;
    data.resize(sizeof(Vec3i)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3i));
  }

  void SetVec3f(const Vec3f v) {
    static_assert(sizeof(Vec3f) == 12, "");
    dtype.name = "Vec3f";   
    dtype.id = VALUE_TYPE_VEC3F;
    data.resize(sizeof(Vec3f)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3f));
  }

  void SetVec3d(const Vec3d v) {
    static_assert(sizeof(Vec3d) == 24, "");
    dtype.name = "Vec3d";   
    dtype.id = VALUE_TYPE_VEC3D;
    data.resize(sizeof(Vec3d)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3d));
  }

  void SetVec3h(const Vec3h v) {
    static_assert(sizeof(Vec3h) == 6, "");
    dtype.name = "Vec3h";   
    dtype.id = VALUE_TYPE_VEC3H;
    data.resize(sizeof(Vec3h)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3h));
  }

  void SetVec4i(const Vec4i v) {
    static_assert(sizeof(Vec4i) == 16, "");
    dtype.name = "Vec4i";   
    dtype.id = VALUE_TYPE_VEC4I;
    data.resize(sizeof(Vec4i)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4i));
  }

  void SetVec4f(const Vec4f v) {
    static_assert(sizeof(Vec4f) == 16, "");
    dtype.name = "Vec4f";   
    dtype.id = VALUE_TYPE_VEC4F;
    data.resize(sizeof(Vec4f)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4f));
  }

  void SetVec4d(const Vec4d v) {
    static_assert(sizeof(Vec4d) == 32, "");
    dtype.name = "Vec4d";   
    dtype.id = VALUE_TYPE_VEC4D;
    data.resize(sizeof(Vec4d)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec4d));
  }

  void SetVec4h(const Vec4h v) {
    static_assert(sizeof(Vec4h) == 8, "");
    dtype.name = "Vec4h";   
    dtype.id = VALUE_TYPE_VEC4H;
    data.resize(sizeof(Vec4h)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&v), sizeof(Vec3h));
  }

  void SetToken(const std::string &s) {
    dtype.name = "Token";   
    dtype.id = VALUE_TYPE_TOKEN;
    data.resize(s.size()); // No '\0' 
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetString(const std::string &s) {
    dtype.name = "String";   
    dtype.id = VALUE_TYPE_STRING; // we treat String as std::string, not StringIndex 
    data.resize(s.size()); // No '\0' 
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetAssetPath(const std::string &s) {
    dtype.name = "AssetPath";
    dtype.id = VALUE_TYPE_ASSET_PATH; // we treat AssetPath as std::string, not TokenIndex 
    data.resize(s.size()); // No '\0' 
    memcpy(data.data(), reinterpret_cast<const void *>(&s[0]), s.size());
  }

  void SetPermission(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Permission";   
    dtype.id = VALUE_TYPE_PERMISSION;
    data.resize(sizeof(uint32_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetSpecifier(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Specifier";   
    dtype.id = VALUE_TYPE_SPECIFIER;
    data.resize(sizeof(uint32_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetVariability(const uint32_t d) {
    // TODO(syoyo): range check
    dtype.name = "Variability";   
    dtype.id = VALUE_TYPE_VARIABILITY;
    data.resize(sizeof(uint32_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(&d), sizeof(uint32_t));
  }

  void SetIntArray(const int *d, const size_t n) {
    dtype.name = "IntArray";
    dtype.id = VALUE_TYPE_INT;
    array_length = n;
    data.resize(n * sizeof(uint32_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(uint32_t));
  }

  void SetHalfArray(const uint16_t *d, const size_t n) {
    dtype.name = "HalfArray";
    dtype.id = VALUE_TYPE_HALF;
    array_length = n;
    data.resize(n * sizeof(uint16_t)); 
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(uint16_t));
  }

  void SetFloatArray(const float *d, const size_t n) {
    dtype.name = "FloatArray";
    dtype.id = VALUE_TYPE_FLOAT;
    array_length = n;
    data.resize(n * sizeof(float)); 
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(float));
  }

  void SetDoubleArray(const double *d, const size_t n) {
    dtype.name = "DoubleArray";
    dtype.id = VALUE_TYPE_DOUBLE;
    array_length = n;
    data.resize(n * sizeof(double)); 
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(double));
  }

  void SetVec2fArray(const Vec2f *d, const size_t n) {
    static_assert(sizeof(Vec2f) == 8, "");
    dtype.name = "Vec2fArray";   
    dtype.id = VALUE_TYPE_VEC2F;
    array_length = n;
    data.resize(n * sizeof(Vec2f)); 
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec2f));
  }

  void SetVec3fArray(const Vec3f *d, const size_t n) {
    static_assert(sizeof(Vec3f) == 12, "");
    dtype.name = "Vec3fArray";   
    dtype.id = VALUE_TYPE_VEC3F;
    array_length = n;
    data.resize(n * sizeof(Vec3f)); 
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec3f));
  }

  void SetVec4fArray(const Vec4f *d, const size_t n) {
    static_assert(sizeof(Vec4f) == 16, "");
    dtype.name = "Vec4fArray";   
    dtype.id = VALUE_TYPE_VEC4F;
    array_length = n;
    data.resize(n * sizeof(Vec4f)); 
    memcpy(data.data(), reinterpret_cast<const void *>(d), n * sizeof(Vec4f));
  }

  void SetTokenArray(const std::vector<std::string> &d) {
    dtype.name = "TokenArray";   
    dtype.id = VALUE_TYPE_TOKEN;
    string_array = d;
  }

  void SetPathListOp(const ListOp<Path> &d) {
    dtype.name = "PathListOp";
    dtype.id = VALUE_TYPE_PATH_LIST_OP;
    path_list_op = d;
  }

  // Getter for frequently used types.
  std::string GetToken() {
    if (dtype.id == VALUE_TYPE_TOKEN) {
      std::string s(reinterpret_cast<const char *>(data.data()), data.size());
      return s;
    }
    return std::string();
  }

  std::string GetString() {
    if (dtype.id == VALUE_TYPE_STRING) {
      std::string s(reinterpret_cast<const char *>(data.data()), data.size());
      return s;
    }
    return std::string();
  }

  size_t GetArrayLength() const {
    return array_length;
  }

  const std::vector<uint8_t> &GetData() const {
    // TODO(syoyo): Report error for Dictionary type. 
    return data;
  }

  const std::string &GetTypeName() const {
    return dtype.name;
  }

  const ValueTypeId &GetTypeId() const {
    return dtype.id;
  }

  bool IsDictionary() const {
    return dtype.id == VALUE_TYPE_DICTIONARY;
  }

  void SetDictionary(const Dictionary &d) {
    // Dictonary has separated storage
    dict = d;
  }

  const Dictionary &GetDictionary() const {
    return dict;
  }
    

 private:
  ValueType dtype;
  std::string string_value;
  std::vector<uint8_t> data; // value as opaque binary data.
  int64_t array_length{-1};

  // Dictonary, ListOp and array of string has separated storage
  std::vector<std::string> string_array;
  Dictionary dict;
  ListOp<Path> path_list_op;
  ListOp<std::string> token_list_op;
  // TODO(syoyo): Reference

  // TODO(syoyo): Use single representation for integral types
  ListOp<int32_t> int_list_op;
  ListOp<uint32_t> int64_list_op;
  ListOp<int64_t> uint_list_op;
  ListOp<uint64_t> uint64_list_op;
};

///
/// Load USDZ(zip) from a file.
///
/// @param[in] filename USDZ filename
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDZFromFile(const std::string &filename, std::string *warn, std::string *err, const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDC(binary) from a file.
///
/// @param[in] filename USDC filename
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromFile(const std::string &filename, std::string *warn, std::string *err, const USDLoadOptions &options = USDLoadOptions());

///
/// Load USDC(binary) from a memory.
///
/// @param[in] addr Memory address of USDC data
/// @param[in] length Byte length of USDC data
/// @param[out] warn Warning message.
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromMemory(const uint8_t *addr, const size_t length, std::string *warn, std::string *err, const USDLoadOptions &options = USDLoadOptions());

#if 0 // TODO
///
/// Write scene as USDC to a file.
///
/// @param[in] filename USDC filename
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Write options(optional)
///
/// @return true upon success
///
bool WriteAsUSDCToFile(const std::string &filename, std::string *err, const USDCWriteOptions &options = USDCWriteOptions());

#endif

} // namespace tinyusdz

#endif // TINYUSDZ_HH_
