#include "usda-writer.hh"
#include "pprinter.hh"
#include "value-pprint.hh"

#include <fstream>
#include <iostream>
#include <sstream>

namespace tinyusdz {
namespace usda {

namespace {

inline std::string GetTypeName(XformOpValueType const &v) {
  if (auto pval = nonstd::get_if<float>(&v)) {
    (void)pval;
    return "float";
  }

  if (auto pval = nonstd::get_if<double>(&v)) {
    (void)pval;
    return "double";
  }

  if (auto pval = nonstd::get_if<value::float3>(&v)) {
    (void)pval;
    return "float3";
  }
  if (auto pval = nonstd::get_if<value::double3>(&v)) {
    (void)pval;
    return "double3";
  }
  if (auto pval = nonstd::get_if<value::matrix4d>(&v)) {
    (void)pval;
    return "matrix4d";
  }
  if (auto pval = nonstd::get_if<value::quatf>(&v)) {
    (void)pval;
    return "quatf";
  }
  if (auto pval = nonstd::get_if<value::quatd>(&v)) {
    (void)pval;
    return "quatd";
  }

  return "TypeName(XformOpValueType) = ???";
}

class Writer {
 public:
  Writer(const Scene &scene) : _scene(scene) {}

  std::string Indent(size_t level) {
    std::stringstream ss;
    for (size_t i = 0; i < level; i++) {
      ss << "  ";
    }

    return ss.str();
  }

  bool WriteGeomMesh(std::ostream &ofs, const GeomMesh &mesh, uint32_t level) {

    ofs << to_string(mesh, level, /* closing brace */false);

    ofs << Indent(level) << "}\n";

    return true;
  }

  bool WriteXform(std::ostream &ofs, const Xform &xform, uint32_t level) {
    std::cout << "Writing Xform: " << xform.name << " ...\n";

    ofs << Indent(level) << "\n";
    ofs << Indent(level) << "def Xform \"" << xform.name << "\"\n";
    ofs << Indent(level) << "{\n";

    if (xform.xformOps.size()) {
      // xformOpOrder
      ofs << Indent(level + 1) << "uniform token[] xformOpOrder = [";

      for (size_t i = 0; i < xform.xformOps.size(); i++) {
        ofs << "\"" << XformOp::GetOpTypeName(xform.xformOps[i].op) << "\"";

        if (i != (xform.xformOps.size() - 1)) {
          ofs << ", ";
        }
      }

      ofs << "]\n";

      for (size_t i = 0; i < xform.xformOps.size(); i++) {
        ofs << Indent(level + 1);

        ofs << GetTypeName(xform.xformOps[i].value);

        ofs << " " << XformOp::GetOpTypeName(xform.xformOps[i].op) << " = ";

#if 0 // TODO
        nonstd::visit([&ofs](XformOpValueType &&arg) { ofs << arg; },
                      xform.xformOps[i].value);
#endif
        ofs << "\n";
      }
    }

    return true;
  }

  bool WriteNode(std::ostream &ofs, const Node &node, uint32_t level) {
    if (node.type == NODE_TYPE_XFORM) {
      if ((node.index < 0) || (size_t(node.index) >= _scene.xforms.size())) {
        // invalid index
        return false;
      }

      if (!WriteXform(ofs, _scene.xforms.at(size_t(node.index)), level)) {
        return false;
      }

    } else if (node.type == NODE_TYPE_GEOM_MESH) {
      if ((node.index < 0) ||
          (size_t(node.index) >= _scene.geom_meshes.size())) {
        // invalid index
        return false;
      }

      if (!WriteGeomMesh(ofs, _scene.geom_meshes.at(size_t(node.index)),
                         level)) {
        return false;
      }

    } else {
      // unimplemented/unsupported node.
      _err += "TODO: Unimplemnted node type.\n";
      return false;
    }

    for (const auto &child : node.children) {
      if (!WriteNode(ofs, child, level + 1)) {
        return false;
      }
    }

    ofs << Indent(level) << "}\n";

    return true;
  }

  const Scene &_scene;

  const std::string &Error() const { return _err; }
  const std::string &Warn() const { return _warn; }

 private:
  Writer() = delete;
  Writer(const Writer &) = delete;

  std::string _err;
  std::string _warn;
};

}  // namespace

bool SaveAsUSDA(const std::string &filename, const Scene &scene,
                std::string *warn, std::string *err) {

  std::stringstream ss;

  ss << "#usda 1.0\n";
  ss << "(\n";
  if (scene.doc.empty()) {
    ss << "  doc = \"TinyUSDZ v" << tinyusdz::version_major << "."
       << tinyusdz::version_minor << "." << tinyusdz::version_micro << "\"\n";
  } else {
    ss << "  doc = \"" << scene.doc << "\"\n";
  }
  ss << "  metersPerUnit = " << scene.metersPerUnit << "\n";
  ss << "  upAxis = \"" << scene.upAxis << "\"\n";
  ss << "  timeCodesPerSecond = \"" << scene.timeCodesPerSecond << "\"\n";
  // TODO: write other header data.
  ss << ")\n";

  // TODO
  Writer writer(scene);

  std::cout << "# of nodes: " << scene.nodes.size() << "\n";

  for (const auto &root : scene.nodes) {
    if (!writer.WriteNode(ss, root, 0)) {
      if (err && writer.Error().size()) {
        (*err) += writer.Error();
      }

      if (warn && writer.Warn().size()) {
        (*warn) += writer.Warn();
      }

      return false;
    }
  }

  std::ofstream ofs(filename);
  if (!ofs) {
    if (err) {
      (*err) += "Failed to open file [" + filename + "] to write.\n";
    }
    return false;
  }

  ofs << ss.str();

  std::cout << "Wrote to [" << filename << "]\n";

  return true;
}

} // namespace usda
}  // namespace tinyusdz
