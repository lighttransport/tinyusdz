#include <string>

#include "simple-variant.hh"


struct mystruct {
  using myvar = tinyusdz::variant<uint8_t, float, std::string>;
  myvar data;

  mystruct() : data(myvar::create<0>(0)) {}
};

int main(int argc, char **argv) {
  mystruct s;

  using myvar = tinyusdz::variant<uint8_t, float, std::string>;

  myvar a = myvar::create<0>(1);

  if (auto v = a.get_if<0>()) {
    std::cout << "var = " << std::to_string((*v)) << "\n";
  }

  a.set<2>("bora");

  if (auto v = a.get_if<2>()) {
    std::cout << "var2 = " << (*v) << "\n";
  }

  a.set<1>(3.14f);

  if (auto v = a.get_if<2>()) {
    std::cout << "var2 = " << (*v) << "\n";
  }

  if (auto v = a.get_if<1>()) {
    std::cout << "var1 = " << std::to_string((*v)) << "\n";
  }

  return 0;
}
