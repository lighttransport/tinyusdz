#include "usda-writer.hh"

#include <sstream>

namespace tinyusdz {

namespace {

// TODO

bool WriteNode(std::ostream &ofs, const Node &node, int indent) {
  return false;
}


}

bool SaveAsUSDA(const std::string &filename, const Scene &scene, std::string *warn, std::string *err) {
  std::stringstream ss;

  ss << "#usda 1.0\n";
  ss << "(\n";
  ss << "  doc = \"TinyUSDZ v" << tinyusdz::version_major << "." << tinyusdz::version_minor << "." << tinyusdz::version_micro << "\"\n";
  ss << "  metersPerUnit = 1\n";
  ss << "  upAxis = \"Z\"\n";
  ss << ")\n";

  // TODO

  for (const auto &node : scene.nodes) {
    if (!WriteNode(ss, node, 0)) {
      return false;
    }
  }

  return false;
}

} // namespace tinyusdz
