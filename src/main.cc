#include <iostream>
#include <algorithm>
#include <cctype>
#include "tinyusdz.hh"

static std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of(".") != std::string::npos)
    return filename.substr(filename.find_last_of(".") + 1);
  return "";
}

static std::string str_tolower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), 
                // static_cast<int(*)(int)>(std::tolower)         // wrong
                // [](int c){ return std::tolower(c); }           // wrong
                // [](char c){ return std::tolower(c); }          // wrong
                   [](unsigned char c){ return std::tolower(c); } // correct
                  );
    return s;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::cout << "Need input.usdz\n" << std::endl;
    return EXIT_FAILURE;
  }

  std::string filepath = argv[1];
  std::string warn;
  std::string err;

  std::string ext = str_tolower(GetFileExtension(filepath));

  if (ext.compare("usdz") == 0) {
    bool ret = tinyusdz::LoadUSDZFromFile(filepath, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
      return EXIT_FAILURE;
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDZ file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else { // assume usdc
    bool ret = tinyusdz::LoadUSDCFromFile(filepath, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
      return EXIT_FAILURE;
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDC file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
