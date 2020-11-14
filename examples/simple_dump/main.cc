#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

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

std::string indent(const int val) {
  std::stringstream ss;
  for(int i = 0; i < val; i++) {
    ss << "  ";
  }
  return ss.str();
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

static void DumpGeomMesh(const tinyusdz::GeomMesh &mesh, int level) {
  std::cout << indent(level) << "# of points: " << mesh.GetNumPoints() << "\n";
  const std::vector<float> &points = mesh.points;

  for (size_t i = 0; i < points.size(); i++) {
    std::cout << points[i] << "\n";
  }
}

static void DumpGeomCurves(const tinyusdz::GeomBasisCurves &curves, int level) {
  std::cout << indent(level) << "# of points: " << curves.points.size() << "\n";
  const std::vector<float> &points = curves.points;

  for (size_t i = 0; i < points.size(); i++) {
    std::cout << points[i] << "\n";
  }
}

static void DumpGeomPoints(const tinyusdz::GeomPoints &pts, int level) {
  std::cout << indent(level) << "# of points: " << pts.points.size() << "\n";
  const std::vector<float> &points = pts.points;

  for (size_t i = 0; i < points.size(); i++) {
    std::cout << points[i] << "\n";
  }
}

static void DumpScene(const tinyusdz::Scene &scene)
{

  std::cout << "Scene.name: " << scene.name << "\n";
  std::cout << "Scene.metersPerUnit: " << scene.metersPerUnit << "\n";
  std::cout << "Scene.timeCodesPerSecond: " << scene.timeCodesPerSecond << "\n";
  std::cout << "Scene.defaultPrim: " << scene.defaultPrim << "\n";
  std::cout << "Scene.default_root_node: " << scene.default_root_node << "\n";

  std::cout << "# of nodes: " << scene.nodes.size() << "\n";
  std::cout << "# of xforms: " << scene.xforms.size() << "\n";
  std::cout << "# of geom_meshes: " << scene.geom_meshes.size() << "\n";
  std::cout << "# of geom_basis_curves: " << scene.geom_basis_curves.size() << "\n";
  std::cout << "# of geom_points: " << scene.geom_points.size() << "\n";
  std::cout << "# of materials: " << scene.geom_meshes.size() << "\n";
  std::cout << "# of preview shaders: " << scene.shaders.size() << "\n";
  std::cout << "# of groups: " << scene.groups.size() << "\n";

  if (scene.default_root_node > -1) {
    DumpNode(scene.nodes[scene.default_root_node], 0);
  }

  std::cout << "== Meshes ===\n";
  for (size_t i = 0; i < scene.geom_meshes.size(); i++) {
    DumpGeomMesh(scene.geom_meshes[i], 0);
  }

  std::cout << "== Curves ===\n";
  for (size_t i = 0; i < scene.geom_basis_curves.size(); i++) {
    DumpGeomCurves(scene.geom_basis_curves[i], 0);
  }

  std::cout << "== Points ===\n";
  for (size_t i = 0; i < scene.geom_points.size(); i++) {
    DumpGeomPoints(scene.geom_points[i], 0);
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
