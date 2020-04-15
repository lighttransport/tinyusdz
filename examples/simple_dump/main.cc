#include <algorithm>
#include <cctype>
#include <iostream>

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
                 [](unsigned char c) { return std::tolower(c); }  // correct
  );
  return s;
}

static void PrintValue(const tinyusdz::Value &v) {

  std::cout << "data type = " << v.GetTypeName() << "\n";

}

static std::string PrintNodeType(tinyusdz::NodeType ty)
{
  if (ty == tinyusdz::NODE_TYPE_XFORM) {
    return "node:xform";
  } else if (ty == tinyusdz::NODE_TYPE_GROUP) {
    return "node:group";
  } else if (ty == tinyusdz::NODE_TYPE_GEOM_MESH) {
    return "node:geom_mesh";
  } else if (ty == tinyusdz::NODE_TYPE_MATERIAL) {
    return "node:material";
  } else if (ty == tinyusdz::NODE_TYPE_SHADER) {
    return "node:shader";
  } else if (ty == tinyusdz::NODE_TYPE_CUSTOM) {
    return "node:custom";
  } else {
    return "node:???";
  }
}

static void DumpNode(const tinyusdz::Node &node, int level) {
  for (const auto &child : node.children) {
    //child.GetNodeType();
  }
}

static void DumpScene(const tinyusdz::Scene &scene)
{

  std::cout << "Scene.name: " << scene.name << "\n";
  std::cout << "Scene.metersPerUnit: " << scene.metersPerUnit << "\n";
  std::cout << "Scene.timeCodesPerSecond: " << scene.timeCodesPerSecond << "\n";
  std::cout << "Scene.defaultPrim: " << scene.defaultPrim << "\n";
  std::cout << "Scene.root_node: " << scene.root_node << "\n";

  std::cout << "# of nodes: " << scene.nodes.size() << "\n";
  std::cout << "# of xforms: " << scene.xforms.size() << "\n";
  std::cout << "# of geom_meshes: " << scene.geom_meshes.size() << "\n";
  std::cout << "# of materials: " << scene.geom_meshes.size() << "\n";
  std::cout << "# of preview shaders: " << scene.shaders.size() << "\n";
  std::cout << "# of groups: " << scene.groups.size() << "\n";

  if (scene.root_node > -1) {
    DumpNode(scene.nodes[scene.root_node], 0);
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

  tinyusdz::Scene scene;

  if (ext.compare("usdz") == 0) {
    std::cout << "usdz\n";
    bool ret = tinyusdz::LoadUSDZFromFile(filepath, &scene, &warn, &err);
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
  } else {  // assume usdc
    bool ret = tinyusdz::LoadUSDCFromFile(filepath, &scene, &warn, &err);
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

  DumpScene(scene);

  return EXIT_SUCCESS;
}
