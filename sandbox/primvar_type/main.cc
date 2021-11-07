#include <iostream>
#include <vector>
#include <array>
#include <memory>
#include <type_traits>
#include <map>

// TODO(syoyo): Use compile-time string hash?
enum TypeId {
  TYPE_ID_INVALID,  // = 0
  TYPE_ID_FLOAT,
  TYPE_ID_FLOAT2,
  TYPE_ID_FLOAT3,
  TYPE_ID_FLOAT4,
  TYPE_ID_DOUBLE,
  TYPE_ID_DOUBLE2,
  TYPE_ID_DOUBLE3,
  TYPE_ID_DOUBLE4,
  TYPE_ID_VFLOAT,
  TYPE_ID_VFLOAT2,
  TYPE_ID_VFLOAT3,
  TYPE_ID_VFLOAT4,
  TYPE_ID_VDOUBLE,
  TYPE_ID_VDOUBLE2,
  TYPE_ID_VDOUBLE3,
  TYPE_ID_VDOUBLE4,

  TYPE_ID_DICT,
};

using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;
using float4 = std::array<float, 4>;

using double2 = std::array<double, 2>;
using double3 = std::array<double, 3>;
using double4 = std::array<double, 4>;

//
// Simple variant-lile type
//

template<class dtype>
struct TypeTrait;

#define DEFINE_TYPE_TRAIT(__dty, __name, __tyid) \
template<> \
struct TypeTrait<__dty> { \
  using value_type = __dty; \
  static constexpr uint32_t type_id = __tyid; \
  static constexpr auto type_name = __name; \
};

DEFINE_TYPE_TRAIT(float, "float",   TYPE_ID_FLOAT);
DEFINE_TYPE_TRAIT(float2, "float2", TYPE_ID_FLOAT2);
DEFINE_TYPE_TRAIT(float3, "float3", TYPE_ID_FLOAT3);
DEFINE_TYPE_TRAIT(float4, "float4", TYPE_ID_FLOAT4);

DEFINE_TYPE_TRAIT(double, "double",   TYPE_ID_DOUBLE);
DEFINE_TYPE_TRAIT(double2, "double2", TYPE_ID_DOUBLE2);
DEFINE_TYPE_TRAIT(double3, "double3", TYPE_ID_DOUBLE3);
DEFINE_TYPE_TRAIT(double4, "double4", TYPE_ID_DOUBLE4);

DEFINE_TYPE_TRAIT(std::vector<float> , "float[]" , TYPE_ID_VFLOAT);
DEFINE_TYPE_TRAIT(std::vector<float2>, "float2[]", TYPE_ID_VFLOAT2);
DEFINE_TYPE_TRAIT(std::vector<float3>, "float3[]", TYPE_ID_VFLOAT3);
DEFINE_TYPE_TRAIT(std::vector<float4>, "float4[]", TYPE_ID_VFLOAT4);

DEFINE_TYPE_TRAIT(std::vector<double>, "double[]"  , TYPE_ID_VDOUBLE);
DEFINE_TYPE_TRAIT(std::vector<double2>, "double2[]", TYPE_ID_VDOUBLE2);
DEFINE_TYPE_TRAIT(std::vector<double3>, "double3[]", TYPE_ID_VDOUBLE3);
DEFINE_TYPE_TRAIT(std::vector<double4>, "double4[]", TYPE_ID_VDOUBLE4);

#undef DEFINE_TYPE_TRAIT

struct base_value
{
  virtual const std::string type_name() const = 0;
  virtual const uint32_t type_id() const = 0;

  virtual const void *value() const = 0;

};

template<typename T>
struct value_impl : public base_value
{
  value_impl(const T &v) : _value(v) {}

  const std::string type_name() const override {
    return TypeTrait<T>::type_name;
  }

  const uint32_t type_id() const override {
    return TypeTrait<T>::type_id;
  }

  const void *value() const override {
    return reinterpret_cast<const void *>(&_value);
  }

#if 0
  explicit operator T() const {
    return _value;
  }
#endif

  T _value;
};

struct vfloat {
  std::vector<float> v;
};

template<typename T>
struct TimeSample {
  std::vector<double> times;
  std::vector<T> values;
};


class Value
{
 public:

  using Dict = std::map<std::string, Value>;

  Value() = default;

  template<class T> Value(const T &v) {
    v_ptr.reset(new value_impl<T>(v));
  }
    
  
  std::string type_name() const {
    return v_ptr->type_name();
  }

  // Return nullptr when type conversion failed.
  template<class T> const T *as() const {
    if (TypeTrait<T>::type_id == v_ptr->type_id()) {
      return reinterpret_cast<const T *>(v_ptr->value());
    } else {
      return nullptr;
    }
  }

  // Useful function to retrieve concrete value with type T.
  // Undefined behavior(usually will triger segmentation fault) when type-mismatch.
  // (We don't throw exception)
  template<class T> const T &value() const {
    return (*reinterpret_cast<const T *>(v_ptr->value()));
  }

  template<class T>
  Value &operator=(const T &v) {
    v_ptr.reset(new value_impl<T>(v));
    return (*this);
  }

  //template<class T>
  //T *get_if();
  
 private:
  std::shared_ptr<base_value> v_ptr; // basic type, array

  Dict dict_; // dictionary
};


#if 0
#define VALUE_DEFINE_ASSIGN(__dtype, __varname) \
template<> \
Value &Value::operator=(const __dtype &v) { \
  v_ptr.reset(new value<__dtype>(
  u.__varname = v;  \
 \
  return (*this); \
}

#define VALUE_DEFINE_VASSIGN(__type, __varname) \
template<> \
Value &Value::operator=(const __type &v) { \
  type_name = TypeTrait<__type>::type_name; \
  new (&u._ ##__varname) __type;  /* placement new */ \
  u._ ##__varname.v =v; \
 \
  return (*this); \
}

VALUE_DEFINE_ASSIGN(double, _double);
VALUE_DEFINE_ASSIGN(float, _float);
VALUE_DEFINE_VASSIGN(std::vector<float>, vfloat);
//VALUE_DEFINE_ASSIGN(std::vector<float3>, _vfloat3);
//VALUE_DEFINE_ASSIGN(std::vector<double>, _vdouble);

#undef VALUE_DEFINE_ASSIGN
#endif

#if 0
template<>
double *Value::get_if<double>() {
  if (type_name == TypeTrait<double>::type_name) {
    return &u._double;
  }

  return nullptr;
}
#endif

std::ostream &operator<<(std::ostream &os, const Value &v)
{
  
  os << "(type: " << v.type_name() << ") ";
  if (v.type_name() == "float") {
    os << v.value<float>();
  } else if (v.type_name() == "double") {
    os << v.value<double>();
  } else if (v.type_name() == "float[]") {
    auto val = v.value<std::vector<float>>();
    std::cout << "n = " << val.size() << "\n";
    os << "[";
    for (size_t i = 0; i < val.size(); i++) {
      os << val[i];
      if (i != val.size() - 1) {
        os << ", ";
      }
    }
    os << "]";
  } else {
    os << "TODO";
  }

  return os;
}

int main(int argc, char **argv)
{
  //std::cout << "sizeof(U) = " << sizeof(Value::U) << "\n";

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

#if 0
  if (v.get_if<double>()) {
    std::cout << "double!" << "\n";
  }
#endif


}
