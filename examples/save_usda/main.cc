#include "usda-writer.hh"

#include <iostream>

//
// create dummy scene.
//
void DummyScene(tinyusdz::Scene *scene)
{
  //
  // tinyusdz currently does not provide scene graph API yet, so edit parameters directly.
  //
  tinyusdz::Xform xform;
  xform.name = "root";

  tinyusdz::XformOp op;
  op.op = tinyusdz::XformOp::TRANSLATE;
  op.precision = tinyusdz::XformOp::PRECISION_DOUBLE;
  tinyusdz::value::double3 translate;
  translate[0] = 1.0;
  translate[1] = 2.0;
  translate[2] = 3.0;
  op.value = translate;

  xform.xformOps.push_back(op);

  tinyusdz::GeomMesh mesh;
  mesh.name = "quad";

  tinyusdz::Node mesh_node;
  mesh_node.type = tinyusdz::NODE_TYPE_GEOM_MESH;
  mesh_node.index = 0; // geom_meshes[0]

  mesh.points.push_back({0.0f, 0.0f, 0.0f});

  mesh.points.push_back({1.0f, 0.0f, 0.0f});

  mesh.points.push_back({1.0f, 1.0f, 0.0f});

  mesh.points.push_back({0.0f, 1.0f, 0.0f});

  // quad plane composed of 2 triangles.
  mesh.faceVertexCounts.push_back(3);
  mesh.faceVertexCounts.push_back(3);

  mesh.faceVertexIndices.push_back(0);
  mesh.faceVertexIndices.push_back(1);
  mesh.faceVertexIndices.push_back(2);

  mesh.faceVertexIndices.push_back(0);
  mesh.faceVertexIndices.push_back(2);
  mesh.faceVertexIndices.push_back(3);

  tinyusdz::Node node;
  node.type = tinyusdz::NODE_TYPE_XFORM;
  node.index = 0; // nodes[0]
  node.children.push_back(mesh_node);

  scene->xforms.emplace_back(xform);
  scene->geom_meshes.emplace_back(mesh);
  scene->nodes.emplace_back(node);

}

int main(int argc, char **argv)
{
  tinyusdz::Scene scene; // empty scene

  DummyScene(&scene);

  std::string warn;
  std::string err;
  bool ret = tinyusdz::usda::SaveAsUSDA("output.udsa", scene, &warn, &err);

  if (warn.size()) {
    std::cout << "WARN: " << warn << "\n";
  }

  if (err.size()) {
    std::cerr << "ERR: " << err << "\n";
  }

  return ret ? EXIT_SUCCESS : -1;
}
