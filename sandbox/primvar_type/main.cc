#include <iostream>
#include <vector>

//
// Simple variant-lile type
//

template<class dtype>
struct TypeTrait;

template<>
struct TypeTrait<float> {
  static constexpr auto type_name = "float";
};

template<>
struct TypeTrait<double> {
  static constexpr auto type_name = "double";
};

template<>
struct TypeTrait<std::vector<double>> {
  static constexpr auto type_name = "double[]";
};

class Value
{
 public:

  std::string type_name;

  union U {
    float _float;
    double _double;
    int _int;
    
    // 1D array type
    std::vector<double> _vdouble;

    // User defined constructor and destructor are required.
    U() {}
    ~U() {}

  } u;

  template<class T>
  Value &operator=(const T &t);

  template<class T>
  T *get_if();
  
};


#define DEFINE_ASSIGN(__type, __varname) \
template<> \
Value &Value::operator=(const __type &v) { \
  type_name = TypeTrait<__type>::type_name; \
  u.__varname = v;  \
 \
  return (*this); \
}

DEFINE_ASSIGN(double, _double);
DEFINE_ASSIGN(float, _float);
DEFINE_ASSIGN(std::vector<double>, _vdouble);

#undef DEFINE_ASSIGN

template<>
double *Value::get_if<double>() {
  if (type_name == TypeTrait<double>::type_name) {
    return &u._double;
  }

  return nullptr;
}

std::ostream &operator<<(std::ostream &os, const Value &v)
{
  
  os << "(type: " << v.type_name << ") ";
  if (v.type_name == "float") {
    os << v.u._float;
  } else if (v.type_name == "double") {
    os << v.u._double;
  } else if (v.type_name == "double[]") {
    std::cout << "n = " << v.u._vdouble.size() << "\n";
    os << "[";
    for (size_t i = 0; i < v.u._vdouble.size(); i++) {
      os << v.u._vdouble[i];
      if (i != v.u._vdouble.size() - 1) {
        os << ", ";
      }
    }
  } else {
    os << "TODO";
  }

  return os;
}

int main(int argc, char **argv)
{
  Value v;

  v = 1.3f;
  v = 1.3;

  std::vector<double> din = {1.0, 2.0};
  v = din;

  std::cout << "val\n";
  std::cout << v << "\n";

  if (v.get_if<double>()) {
    std::cout << "double!" << "\n";
  }

}
