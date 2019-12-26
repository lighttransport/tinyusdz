#include <iostream>
#include "tinyusdz.hh"

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::cout << "Need ainput.usdz\n" << std::endl;
    return EXIT_FAILURE;
  }

  std::string filepath = argv[1];
  std::string err;

  bool ret = tinyusdz::LoadUSDCFromFile(filepath, &err);
  if (!err.empty()) {
    std::cerr << "ERR : " << err << "\n";
    return EXIT_FAILURE;
  }

  if (!ret) {
    std::cerr << "Failed to load USDC file: " << filepath << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
