#include "usda-writer.hh"

#include <sstream>
#include <fstream>
#include <iostream>

namespace tinyusdz {

namespace {

// TODO: Use ryu print for precise floating point output?

std::ostream &operator<<(std::ostream &ofs, const Matrix4d &m) {
  ofs << "( ";

  ofs << "(" << m.m[0][0] << ", " << m.m[0][1] << ", " << m.m[0][2] << ", " << m.m[0][3] << "), ";
  ofs << "(" << m.m[1][0] << ", " << m.m[1][1] << ", " << m.m[1][2] << ", " << m.m[1][3] << "), ";
  ofs << "(" << m.m[2][0] << ", " << m.m[2][1] << ", " << m.m[2][2] << ", " << m.m[2][3] << "), ";
  ofs << "(" << m.m[3][0] << ", " << m.m[3][1] << ", " << m.m[3][2] << ", " << m.m[3][3] << ")";

  ofs << " )";

  return ofs;
}

std::string PrintIntArray(const std::vector<int32_t> &data) 
{
  std::stringstream ofs;

  ofs << "[";
  for (size_t i = 0; i < data.size(); i++) {
    
    ofs << data[i];

    if (i != (data.size() - 1)) {
      ofs << ", ";
    }
  }
  ofs << "]";

  return ofs.str();
}

std::string PrintVec3fArray(const std::vector<Vec3f> &data) 
{
  std::stringstream ofs;

  ofs << "[";
  for (size_t i = 0; i < data.size(); i++) {
    
    ofs << "(" << data[i][0] << ", " << data[i][1] << ", " << data[i][2] << ")";

    if (i != (data.size() - 1)) {
      ofs << ", ";
    }
  }
  ofs << "]";

  return ofs.str();

}

std::string PrintVec3fArray(const std::vector<float> &data) 
{
  std::stringstream ofs;

  if ((data.size() % 3) != 0) {
    throw std::runtime_error("data is not a valid array of float3");
  }

  ofs << "[";
  // TODO: Use ryu print?
  for (size_t i = 0; i < data.size() / 3; i++) {
    
    ofs << "(" << data[3 * i + 0] << ", " << data[3 * i + 1] << ", " << data[3 * i + 2] << ")";

    if (i != ((data.size() / 3) - 1)) {
      ofs << ", ";
    }
  }
  ofs << "]";

  return ofs.str();
}

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

  bool WriteGeomMesh(std::ostream &ofs, const GeomMesh &mesh, int level) {
    std::cout << "Writing GeomMesh: " << mesh.name << " ...\n";

    ofs << Indent(level) << "\n";
    ofs << Indent(level) << "def GeomMesh \"" << mesh.name << "\"\n";
    ofs << Indent(level) << "{\n";

    // params
    ofs << Indent(level+1) << "int[] faceVertexCounts = " << PrintIntArray(mesh.faceVertexCounts) << "\n";
    ofs << Indent(level+1) << "int[] faceVertexIndices = " << PrintIntArray(mesh.faceVertexIndices) << "\n";
    ofs << Indent(level+1) << "point3f[] points = " << PrintVec3fArray(mesh.points) << "\n";

    {
      std::vector<float> normals = mesh.normals.buffer.GetAsVec3fArray();

      if (normals.size()) {
        ofs << Indent(level+1) << "normal3f[] normals = " << PrintVec3fArray(normals);

        // Currently we only support `facevarying` for interpolation
        if (mesh.normals.facevarying) {
          ofs << Indent(level+2) << "(\n";
          ofs << Indent(level+3) << "interpolation = \"faceVarying\"\n";
          ofs << Indent(level+2) << ")\n";
        } else {
          ofs << "\n";
        }
      }
    }

    // primvars

    // uniforms
    // TODO
    ofs << Indent(level+1) << "uniform token subdivisionScheme = \"none\"\n";


    return true;
  }

  bool WriteXform(std::ostream &ofs, const Xform &xform, int level) {
    std::cout << "Writing Xform: " << xform.name << " ...\n";

    ofs << Indent(level) << "\n";
    ofs << Indent(level) << "def Xform \"" << xform.name << "\"\n";
    ofs << Indent(level) << "{\n";

    // TODO: translate, rot, scale, ...
    ofs << Indent(level + 1) << "matrix4d xformOp:transform = " << xform.transform << "\n";
    ofs << Indent(level + 1) << "uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";


    return true;
  }

  bool WriteNode(std::ostream &ofs, const Node &node, int level) {
    if (node.type == NODE_TYPE_XFORM) {

      if ((node.index < 0) || (node.index >= _scene.xforms.size())) {
        // invalid index
        return false;
      }

      if (!WriteXform(ofs, _scene.xforms.at(node.index), level)) {
        return false;
      }

    } else if (node.type == NODE_TYPE_GEOM_MESH) {

      if ((node.index < 0) || (node.index >= _scene.geom_meshes.size())) {
        // invalid index
        return false;
      }

      if (!WriteGeomMesh(ofs, _scene.geom_meshes.at(node.index), level)) {
        return false;
      }

    } else {
      // unsupported node.
      _err += "Unsupported node type.\n";
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

  const std::string &Error() const {
    return _err;
  }

 private:
  Writer() = delete;
  Writer(const Writer &) = delete;

  std::string _err;
};

}  // namespace

bool SaveAsUSDA(const std::string &filename, const Scene &scene,
                std::string *warn, std::string *err) {
  std::stringstream ss;

  ss << "#usda 1.0\n";
  ss << "(\n";
  ss << "  doc = \"TinyUSDZ v" << tinyusdz::version_major << "."
     << tinyusdz::version_minor << "." << tinyusdz::version_micro << "\"\n";
  ss << "  metersPerUnit = " << scene.metersPerUnit << "\n";
  ss << "  upAxis = \"" << scene.upAxis << "\"\n";
  ss << "  timeCodesPerSecond = \"" << scene.timeCodesPerSecond << "\"\n";
  ss << ")\n";

  // TODO
  Writer writer(scene);

  std::cout << "# of nodes: " << scene.nodes.size() << "\n";

  for (const auto &root : scene.nodes) {
    if (!writer.WriteNode(ss, root, 0)) {
      if (err && writer.Error().size()) {
        (*err) += writer.Error();
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

}  // namespace tinyusdz
