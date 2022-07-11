#include <string>

#include "simple-variant.hh"

int main(int argc, char **argv) {

  using myvar = tinyusdz::variant<bool, float, std::string>;

  myvar a;
  a.set<bool>(true);

  myvar b;
  
  b = a;

  a.set<float>(1.3f);

  std::cout << "val = " << a.get<float>() << "\n";
  
  if (auto v = a.get_if<float>()) {
    std::cout << "val = " << (*v) << "\n";
  }

  return 0;
}
