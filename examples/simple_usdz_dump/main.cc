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

std::string indent(const int val) {
  std::stringstream ss;
  for(int i = 0; i < val; i++) {
    ss << "  ";
  }
  return ss.str();
}

#if 0
static std::string PrintNodeType(tinyusdz::NodeType ty)
{
  if (ty == tinyusdz::NODE_TYPE_XFORM) {
    return "node:xform";
  } else if (ty == tinyusdz::NODE_TYPE_SCOPE) {
    return "node:scope";
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
#endif

//static void DumpNode(const tinyusdz::Node &node, int level) {
//  for (const auto &child : node.children) {
//    //child.GetNodeType();
//  }
//}

static void DumpGeomMesh(const tinyusdz::GeomMesh &mesh, int level) {
  std::cout << to_string(mesh, level);
}

static void DumpGeomCurves(const tinyusdz::GeomBasisCurves &curves, int level) {
  std::cout << to_string(curves, level);
}

static void DumpGeomPoints(const tinyusdz::GeomPoints &pts, int level) {
  std::cout << to_string(pts, level);
}

static void DumpScene(const tinyusdz::Stage &stage)
{

  std::cout << "Scene.name: " << stage.name << "\n";
  std::cout << "Scene.metersPerUnit: " << stage.stage_metas.metersPerUnit << "\n";
  std::cout << "Scene.timeCodesPerSecond: " << stage.stage_metas.timeCodesPerSecond << "\n";
  std::cout << "Scene.defaultPrim: " << stage.stage_metas.defaultPrim << "\n";
  std::cout << "Scene.default_root_node: " << stage.default_root_node << "\n";

#if 0
  std::cout << "# of nodes: " << stage.node_indices.size() << "\n";
  std::cout << "# of xforms: " << stage.xforms.size() << "\n";
  std::cout << "# of geom_meshes: " << stage.geom_meshes.size() << "\n";
  std::cout << "# of geom_basis_curves: " << stage.geom_basis_curves.size() << "\n";
  std::cout << "# of geom_points: " << stage.geom_points.size() << "\n";
  std::cout << "# of materials: " << stage.geom_meshes.size() << "\n";
  std::cout << "# of preview shaders: " << stage.shaders.size() << "\n";
  std::cout << "# of scopes: " << stage.scopes.size() << "\n";

  //if (stage.default_root_node > -1) {
  //  DumpNode(stage.nodes[size_t(stage.default_root_node)], 0);
  //}

  std::cout << "== Meshes ===\n";
  for (size_t i = 0; i < stage.geom_meshes.size(); i++) {
    DumpGeomMesh(stage.geom_meshes[i], 0);
  }

  std::cout << "== Curves ===\n";
  for (size_t i = 0; i < stage.geom_basis_curves.size(); i++) {
    DumpGeomCurves(stage.geom_basis_curves[i], 0);
  }

  std::cout << "== Points ===\n";
  for (size_t i = 0; i < stage.geom_points.size(); i++) {
    DumpGeomPoints(stage.geom_points[i], 0);
  }
#endif
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

  if (ext.compare("usdz") == 0) {
    std::cout << "usdz\n";
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
  } else {  // assume usdc
    bool ret = tinyusdz::LoadUSDCFromFile(filepath, &stage, &warn, &err);
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
  }

  DumpScene(stage);

  return EXIT_SUCCESS;
}
