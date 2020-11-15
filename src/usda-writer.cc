#include "usda-writer.hh"

#include <sstream>

namespace tinyusdz {

namespace {

class Writer {
 public:
  Writer(const Scene &scene) : _scene(scene) {}

  std::string Indent(int level) {
    std::stringstream ss;
    for (size_t i = 0; i < level; i++) {
      ss << "  ";
    }

    return ss.str();
  }

  bool WriteXform(std::ostream &ofs, const Xform &xform, int level) {
    ofs << Indent(level) << "def Xform \"" << xform.name << "\"\n";
    // TODO: params
    ofs << Indent(level) << "{\n";
    ofs << Indent(level) << "}\n";

    return true;
  }

  bool WriteNode(std::ostream &ofs, const Node &node, int level) {
    if (node.type == NODE_TYPE_XFORM) {
      if ((node.index < 0) || (node.index >= _scene.xforms.size())) {
        return false;
      }

    }

    for (const auto &child : node.children) {
      if (!WriteNode(ofs, child, level + 1)) {
        return false;
      }
    }
  

    return false;
  }

  const Scene &_scene;

 private:
  Writer() = delete;
  Writer(const Writer &) = delete;
};

}  // namespace

bool SaveAsUSDA(const std::string &filename, const Scene &scene,
                std::string *warn, std::string *err) {
  std::stringstream ss;

  ss << "#usda 1.0\n";
  ss << "(\n";
  ss << "  doc = \"TinyUSDZ v" << tinyusdz::version_major << "."
     << tinyusdz::version_minor << "." << tinyusdz::version_micro << "\"\n";
  ss << "  metersPerUnit = 1\n";
  ss << "  upAxis = \"Z\"\n";
  ss << ")\n";

  // TODO
  Writer writer(scene);

  for (const auto &root : scene.nodes) {
    if (!writer.WriteNode(ss, root, 0)) {
      return false;
    }
  }

  return false;
}

}  // namespace tinyusdz
