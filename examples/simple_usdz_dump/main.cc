#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

#include "tinyusdz.hh"
#include "pprinter.hh"

static std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of('.') != std::string::npos)
    return filename.substr(filename.find_last_of('.') + 1);
  return "";
}

static std::string str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 // static_cast<int(*)(int)>(std::tolower)         // wrong
                 // [](int c){ return std::tolower(c); }           // wrong
                 // [](char c){ return std::tolower(c); }          // wrong
                 [](unsigned char c) { return std::tolower(c); }  // correct
  );
  return s;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage simple_usdz_dump [--flatten] input.usda/usdc/usdz\n" << std::endl;
    std::cout << "\n --flatten (not implemented yet) Do composition(load sublayers, refences, payloads, evaluate `over`, inherit, variants..)\n" << std::endl;
    return EXIT_FAILURE;
  }

  bool has_flatten{false};
  std::string filepath;

  int input_index = -1;

  for (size_t i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg.compare("--flatten") == 0) {
      has_flatten = true;
    } else {
      filepath = arg;
      input_index = i;
    }
  }

  if (filepath.empty() || (input_index < 0)) {
    std::cout << "Input USD filename missing.\n";
    return EXIT_FAILURE;
  }

  std::string warn;
  std::string err;

  std::string ext = str_tolower(GetFileExtension(filepath));

  tinyusdz::Stage stage;

  if (ext.compare("usdc") == 0) {
    tinyusdz::USDLoadOptions options;
    options.do_composition = has_flatten;

    bool ret = tinyusdz::LoadUSDCFromFile(filepath, &stage, &warn, &err, options);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      //return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDC file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else if (ext.compare("usda") == 0) {
    tinyusdz::USDLoadOptions options;
    options.do_composition = has_flatten;

    bool ret = tinyusdz::LoadUSDAFromFile(filepath, &stage, &warn, &err, options);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      //return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDA file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else if (ext.compare("usdz") == 0) {
    if (has_flatten) {
      std::cout << "--flatten is ignored for USDZ model at the moment.\n";
    }
    //std::cout << "usdz\n";
    bool ret = tinyusdz::LoadUSDZFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      //return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDZ file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else {
    tinyusdz::USDLoadOptions options;
    options.do_composition = has_flatten;

    // try to auto detect format.
    bool ret = tinyusdz::LoadUSDFromFile(filepath, &stage, &warn, &err, options);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      //return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USD file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  }

  std::string s = stage.ExportToString();
  std::cout << s << "\n";

  return EXIT_SUCCESS;
}
