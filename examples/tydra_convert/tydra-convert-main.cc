#include <algorithm>
#include <iostream>
#include <sstream>

#include "tinyusdz.hh"
#include "usdShade.hh"
#include "tydra/render-data.hh"

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

using MaterialMap = std::map<std::string, const tinyusdz::Material *>;


static bool TraverseMaterialRec(const std::string &path_prefix, const tinyusdz::Prim &prim, uint32_t depth, MaterialMap &matmap) {
  if (depth > 1024*128) {
    // Too deep
    return false;
  }

  (void)matmap;
  (void)prim;

  std::string prim_abs_path = path_prefix + "/" + prim.path.full_path_name();

  if (prim.data.type_id() == tinyusdz::value::TYPE_ID_MATERIAL) {
    if (const tinyusdz::Material *pv = prim.data.as<tinyusdz::Material>()) {
      std::cout << "Path : <" << prim_abs_path << "> is Material.\n";
      matmap[prim_abs_path] = pv;
    }
  }

  for (const auto &child : prim.children) {
    if (!TraverseMaterialRec(prim_abs_path, child, depth+1, matmap)) {
      return false;
    }
  } 

  return true;
}

static void TraverseMaterial(const tinyusdz::Stage &stage)
{
  MaterialMap matmap;

  for (const auto &prim : stage.GetRootPrims()) {
    TraverseMaterialRec(/* root */"", prim, 0, matmap);    
  } 

}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Need input.usdz\n" << std::endl;
    return EXIT_FAILURE;
  }

  std::string filepath = argv[1];
  std::string warn;
  std::string err;

  std::string ext = str_tolower(GetFileExtension(filepath));

  tinyusdz::Stage stage;

  if (ext.compare("usdc") == 0) {
    bool ret = tinyusdz::LoadUSDCFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDC file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else if (ext.compare("usda") == 0) {
    bool ret = tinyusdz::LoadUSDAFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDA file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else if (ext.compare("usdz") == 0) {
    // std::cout << "usdz\n";
    bool ret = tinyusdz::LoadUSDZFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDZ file: " << filepath << "\n";
      return EXIT_FAILURE;
    }

  } else {
    // try to auto detect format.
    bool ret = tinyusdz::LoadUSDFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USD file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  }

  std::string s = stage.ExportToString();
  std::cout << s << "\n";

  TraverseMaterial(stage);

  return 0;
}
