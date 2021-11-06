#include <iostream>
#include <vector>
#include <array>
#include <memory>

using float3 = std::array<float, 3>;

//
// Simple variant-lile type
//

template<class dtype>
struct TypeTrait;

#define DEFINE_TYPE_TRAIT(__dty, __name) \
template<> \
struct TypeTrait<__dty> { \
  static constexpr auto type_name = __name; \
};

DEFINE_TYPE_TRAIT(float, "float");
DEFINE_TYPE_TRAIT(double, "double");
DEFINE_TYPE_TRAIT(float3, "float3");
DEFINE_TYPE_TRAIT(std::vector<float>, "float[]");
DEFINE_TYPE_TRAIT(std::vector<float3>, "float3[]");
DEFINE_TYPE_TRAIT(std::vector<double>, "double[]");

#undef DEFINE_TYPE_TRAIT

struct base_value
{
  virtual const std::string type_name() const = 0;

  virtual const void *value() const = 0;
};

template<class T>
struct value_impl : public base_value
{
  value_impl(const T &v) : _value(v) {}

  const std::string type_name() const override {
    return TypeTrait<T>::type_name;
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

class Value
{
 public:
  Value() = default;

  template<class T> Value(const T &v) {
    v_ptr.reset(new value_impl<T>(v));
  }
    
  
  std::string type_name() const {
    return v_ptr->type_name();
  }

  template<class T> const T val() const {
    return (*reinterpret_cast<const T *>(v_ptr->value()));
  }

#if 0
  union U {
    float _float;
    double _double;
    int _int;
    
    // 1D array type
    vfloat _vfloat;

    // User defined constructor and destructor are required,
    // since the union contains non-POD datatype(e.g. std::vector)
    U() {}
    ~U() {}

  } u;
#endif

  template<class T>
  Value &operator=(const T &v) {
    v_ptr.reset(new value_impl<T>(v));
    return (*this);
  }

  //template<class T>
  //T *get_if();
  
 private:
  std::shared_ptr<base_value> v_ptr;
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
    os << v.val<float>();
  } else if (v.type_name() == "double") {
    os << v.val<double>();
  } else if (v.type_name() == "float[]") {
    auto val = v.val<std::vector<float>>();
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
