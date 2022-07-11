#include <string>

#include "simple-variant.hh"

int main(int argc, char **argv) {

  using myvar = tinyusdz::variant<bool, float, std::string>;

  myvar a;
  a.set<bool>(true);

  a = 1.4f;

  myvar b;
  
  b = a;

  a.set<float>(1.3f);

  std::cout << "a val = " << a.get<float>() << "\n";
  
  if (auto v = b.get_if<float>()) {
    std::cout << "b val = " << (*v) << "\n";
  }

  return 0;
}
