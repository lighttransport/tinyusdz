#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "../../src/nonstd/optional.hpp"
#include "../../src/nonstd/string_view.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

using token = nonstd::string_view;

// TODO(syoyo): Use compile-time string hash?
enum TypeId {
  TYPE_ID_INVALID,  // = 0

  TYPE_ID_TOKEN,  // string literal. represent it as string_view
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

  TYPE_ID_TIMESAMPLE,

  TYPE_ID_DICT,

  TYPE_ID_ALL  // terminator
};

using half = uint16_t;

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
  half3 imag;
  half real;
};

struct quatf {
  float3 imag;
  float real;
};

struct quatd {
  double3 imag;
  double real;
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

//
// Simple variant-lile type
//

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

DEFINE_TYPE_TRAIT(bool, "bool", TYPE_ID_BOOL, 1);
DEFINE_TYPE_TRAIT(uint8_t, "uchar", TYPE_ID_UCHAR, 1);
DEFINE_TYPE_TRAIT(half, "half", TYPE_ID_HALF, 1);

DEFINE_TYPE_TRAIT(int32_t, "int", TYPE_ID_INT32, 1);
DEFINE_TYPE_TRAIT(uint32_t, "uint", TYPE_ID_UINT32, 1);

DEFINE_TYPE_TRAIT(int64_t, "int64", TYPE_ID_INT64, 1);
DEFINE_TYPE_TRAIT(uint64_t, "uint64", TYPE_ID_UINT64, 1);

DEFINE_TYPE_TRAIT(half2, "half2", TYPE_ID_HALF2, 2);
DEFINE_TYPE_TRAIT(half3, "half3", TYPE_ID_HALF3, 3);
DEFINE_TYPE_TRAIT(half4, "half4", TYPE_ID_HALF4, 4);

DEFINE_TYPE_TRAIT(float, "float", TYPE_ID_FLOAT, 1);
DEFINE_TYPE_TRAIT(float2, "float2", TYPE_ID_FLOAT2, 2);
DEFINE_TYPE_TRAIT(float3, "float3", TYPE_ID_FLOAT3, 3);
DEFINE_TYPE_TRAIT(float4, "float4", TYPE_ID_FLOAT4, 4);

DEFINE_TYPE_TRAIT(double, "double", TYPE_ID_DOUBLE, 1);
DEFINE_TYPE_TRAIT(double2, "double2", TYPE_ID_DOUBLE2, 2);
DEFINE_TYPE_TRAIT(double3, "double3", TYPE_ID_DOUBLE3, 3);
DEFINE_TYPE_TRAIT(double4, "double4", TYPE_ID_DOUBLE4, 4);

DEFINE_TYPE_TRAIT(quath, "quath", TYPE_ID_QUATH, 1);
DEFINE_TYPE_TRAIT(quatf, "quatf", TYPE_ID_QUATF, 1);
DEFINE_TYPE_TRAIT(quatd, "quatd", TYPE_ID_QUATD, 1);

DEFINE_TYPE_TRAIT(matrix2d, "matrix2d", TYPE_ID_MATRIX2D, 1);
DEFINE_TYPE_TRAIT(matrix3d, "matrix3d", TYPE_ID_MATRIX3D, 1);
DEFINE_TYPE_TRAIT(matrix4d, "matrix4d", TYPE_ID_MATRIX4D, 1);

//
// Role types
//
DEFINE_ROLE_TYPE_TRAIT(vector3h, "vector3h", TYPE_ID_VECTOR3H, half3);
DEFINE_ROLE_TYPE_TRAIT(vector3f, "vector3f", TYPE_ID_VECTOR3F, float3);
DEFINE_ROLE_TYPE_TRAIT(vector3d, "vector3d", TYPE_ID_VECTOR3D, double3);

DEFINE_ROLE_TYPE_TRAIT(normal3h, "normal3h", TYPE_ID_NORMAL3H, half3);
DEFINE_ROLE_TYPE_TRAIT(normal3f, "normal3f", TYPE_ID_NORMAL3F, float3);
DEFINE_ROLE_TYPE_TRAIT(normal3d, "normal3d", TYPE_ID_NORMAL3D, double3);

DEFINE_ROLE_TYPE_TRAIT(point3h, "point3h", TYPE_ID_POINT3H, half3);
DEFINE_ROLE_TYPE_TRAIT(point3f, "point3f", TYPE_ID_POINT3F, float3);
DEFINE_ROLE_TYPE_TRAIT(point3d, "point3d", TYPE_ID_POINT3D, double3);

DEFINE_ROLE_TYPE_TRAIT(frame4d, "frame4d", TYPE_ID_FRAME4D, matrix4d);

DEFINE_ROLE_TYPE_TRAIT(color3f, "color3f", TYPE_ID_COLOR3F, float3);
DEFINE_ROLE_TYPE_TRAIT(color4f, "color4f", TYPE_ID_COLOR4F, float4);
DEFINE_ROLE_TYPE_TRAIT(color3d, "color3d", TYPE_ID_COLOR3D, double3);
DEFINE_ROLE_TYPE_TRAIT(color4d, "color4d", TYPE_ID_COLOR4D, double4);

DEFINE_ROLE_TYPE_TRAIT(texcoord2h, "texcoord2h", TYPE_ID_TEXCOORD2H, half2);
DEFINE_ROLE_TYPE_TRAIT(texcoord2f, "texcoord2f", TYPE_ID_TEXCOORD2F, float2);
DEFINE_ROLE_TYPE_TRAIT(texcoord2d, "texcoord2d", TYPE_ID_TEXCOORD2D, double2);

DEFINE_ROLE_TYPE_TRAIT(texcoord3h, "texcoord3h", TYPE_ID_TEXCOORD3H, half3);
DEFINE_ROLE_TYPE_TRAIT(texcoord3f, "texcoord3f", TYPE_ID_TEXCOORD3F, float3);
DEFINE_ROLE_TYPE_TRAIT(texcoord3d, "texcoord3d", TYPE_ID_TEXCOORD3D, double3);

//
//
//
DEFINE_TYPE_TRAIT(token, "token", TYPE_ID_TOKEN, 1);
DEFINE_TYPE_TRAIT(std::string, "string", TYPE_ID_STRING, 1);
DEFINE_TYPE_TRAIT(dict, "dictionary", TYPE_ID_DICT, 1);

#undef DEFINE_TYPE_TRAIT


// 1D Array
template <typename T>
struct TypeTrait<std::vector<T>> {
  using value_type = std::vector<T>;
  static constexpr uint32_t ndim = 1; /* array dim */
  static constexpr uint32_t ncomp = TypeTrait<T>::ncomp;
  static constexpr uint32_t type_id =
      TypeTrait<T>::type_id + 1000;  // 1000 = enough amount to hold the number
                                     // of base types(> TYPE_ID_ALL)
  static constexpr uint32_t underlying_type_id =
      TypeTrait<T>::underlying_type_id +
      1000;  // 1000 = enough amount to hold the number
             // of base types(> TYPE_ID_ALL)
  static std::string type_name() { return TypeTrait<T>::type_name() + "[]"; }
  static std::string underlying_type_name() {
    return TypeTrait<T>::underlying_type_name() + "[]";
  }
};

// 2D Array
template <typename T>
struct TypeTrait<std::vector<std::vector<T>>> {
  using value_type = std::vector<std::vector<T>>;
  static constexpr uint32_t ndim = 2; /* array dim */
  static constexpr uint32_t ncomp = TypeTrait<T>::ncomp;
  static constexpr uint32_t type_id = TypeTrait<T>::type_id + 2000;
  static constexpr uint32_t underlying_type_id =
      TypeTrait<T>::underlying_type_id + 2000;
  static std::string type_name() { return TypeTrait<T>::type_name() + "[][]"; }
  static std::string underlying_type_name() {
    return TypeTrait<T>::underlying_type_name() + "[][]";
  }
};

// TODO(syoyo): 3D array?

struct base_value {
  virtual ~base_value();
  virtual const std::string type_name() const = 0;
  virtual const std::string underlying_type_name() const = 0;
  virtual uint32_t type_id() const = 0;
  virtual uint32_t underlying_type_id() const = 0;

  virtual uint32_t ndim() const = 0;
  virtual uint32_t ncomp() const = 0;

  virtual const void *value() const = 0;
};

base_value::~base_value() {}

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

  std::shared_ptr<base_value> p;
};

struct TimeSample {
  std::vector<double> times;
  std::vector<any_value> values;
};

// using Object = std::map<std::string, any_value>;

class Value {
 public:
  // using Dict = std::map<std::string, Value>;

  Value() = default;

  template <class T>
  Value(const T &v) : v_(v) {}

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
      // layout is same
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

  friend std::ostream &operator<<(std::ostream &os, const Value &v);

 private:
  any_value v_;
};

// Frequently-used utility function
bool is_float3(const Value &v);
bool is_float4(const Value &v);
bool is_double3(const Value &v);
bool is_double4(const Value &v);

bool is_float3(const Value &v) {
  if (v.underlying_type_name() == "float3") {
    return true;
  }

  return false;
}

bool is_float4(const Value &v) {
  if (v.underlying_type_name() == "float4") {
    return true;
  }

  return false;
}

bool is_double3(const Value &v) {
  if (v.underlying_type_name() == "double3") {
    return true;
  }

  return false;
}

bool is_double4(const Value &v) {
  if (v.underlying_type_name() == "double4") {
    return true;
  }

  return false;
}

std::ostream &operator<<(std::ostream &os, const float2 &v);
std::ostream &operator<<(std::ostream &os, const float3 &v);
std::ostream &operator<<(std::ostream &os, const float4 &v);
std::ostream &operator<<(std::ostream &os, const double2 &v);
std::ostream &operator<<(std::ostream &os, const double3 &v);
std::ostream &operator<<(std::ostream &os, const double4 &v);

std::ostream &operator<<(std::ostream &os, const float2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const float3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const float4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const double2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const double3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const double4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
  os << "[";
  for (size_t i = 0; i < v.size(); i++) {
    os << v[i];
    if (i != (v.size() - 1)) {
      os << ", ";
    }
  }
  os << "]";
  return os;
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<std::vector<T>> &v) {
  os << "[";
  for (size_t i = 0; i < v.size(); i++) {
    os << v[i];
    if (i != (v.size() - 1)) {
      os << ", ";
    }
  }
  os << "]";
  return os;
}

std::ostream &operator<<(std::ostream &os, const Value &v) {
  os << "(type: " << v.type_name() << ") ";

  // List up all possible datatypes.
  // TODO: Use std::function or some template technique?
  if (v.type_id() == TYPE_ID_INT32) {
     os << v.value<int32_t>();
  } else if (v.type_name() == "float") {
    os << v.value<float>();
  } else if (v.type_name() == "double") {
    os << v.value<double>();
  } else if (v.type_name() == "float[]") {
    os << v.value<std::vector<float>>();
  } else if (v.type_name() == "float2[]") {
    os << v.value<std::vector<float2>>();
  } else if (v.type_name() == "float3[]") {
    os << v.value<std::vector<float3>>();
  } else if (v.type_name() == "float4[]") {
    os << v.value<std::vector<float4>>();
  } else if (v.type_name() == "double[]") {
    os << v.value<std::vector<double>>();
  } else if (v.type_name() == "dictionary") {
    auto val = v.value<dict>();
    std::cout << "n = " << val.size() << "\n";
    os << "{";
    for (const auto &item : val) {
      static uint32_t i{0};
      os << " \"" << item.first << "\": ";
      os << item.second;
      if (i != val.size() - 1) {
        os << ", ";
      }
    }
    os << "}";

  } else {
    os << "TODO: type: " << v.type_name() << "\n";
  }

  return os;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // std::cout << "sizeof(U) = " << sizeof(Value::U) << "\n";

  dict o;
  o["muda"] = 1.3;

  Value v;

  v = 1.3f;

  std::cout << "val\n";
  std::cout << v << "\n";

  v = 1.3;

  std::cout << "val\n";
  std::cout << v << "\n";

  std::vector<float> din = {1.0, 2.0};
  v = din;

  std::cout << "val\n";
  std::cout << v << "\n";

  std::vector<std::vector<float>> din2 = {{1.0, 2.0}, {3.0, 4.0}};
  v = din2;
  std::cout << "val\n";
  std::cout << v << "\n";

  std::vector<int> vids = {1, 2, 3};
  v = vids;

  v = o;

  std::cout << "val\n";
  std::cout << v << "\n";

  auto ret = v.get_value<double>();
  if (ret) {
    std::cout << "double!\n";
  }

  v = 1.2;
  ret = v.get_value<double>();
  if (ret) {
    std::cout << "double!\n";
  }

#if 0
  if (v.get_if<double>()) {
    std::cout << "double!" << "\n";
  }
#endif
}

static_assert(sizeof(half) == 2, "sizeof(half) must be 2");
static_assert(sizeof(float3) == 12, "sizeof(float3) must be 12");
static_assert(sizeof(color3f) == 12, "sizeof(color3f) must be 12");
static_assert(sizeof(color4f) == 16, "sizeof(color4f) must be 16");
