#include "usda-writer.hh"

#include <iostream>

//
// create dummy scene.
//
void DummyScene(tinyusdz::HighLevelScene *scene)
{
  //
  // tinyusdz currently does not provide scene graph API yet, so edit parameters directly.
  //
  tinyusdz::Xform xform;
  xform.name = "root";

  tinyusdz::XformOp op;
  op.op = tinyusdz::XformOp::TRANSLATE;
  tinyusdz::value::double3 translate;
  translate[0] = 1.0;
  translate[1] = 2.0;
  translate[2] = 3.0;
  op.value = translate;

  xform.xformOps.push_back(op);

  tinyusdz::GeomMesh mesh;
  mesh.name = "quad";

  tinyusdz::NodeIndex mesh_node_id;
  mesh_node_id.type_id = tinyusdz::value::TYPE_ID_GEOM_MESH;
  mesh_node_id.index = 0; // geom_meshes[0]

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

  tinyusdz::NodeIndex xform_node_id;
  xform_node_id.type_id = tinyusdz::value::TYPE_ID_GEOM_XFORM;
  xform_node_id.index = 0; // nodes[0]


#if 0
  scene->xforms.push_back(xform);
  scene->geom_meshes.push_back(std::move(mesh));

  {
    // Node graph 
    auto xform_node = tinyusdz::PrimNode();
    xform_node.data = xform;

    auto geom_node = tinyusdz::PrimNode();
    geom_node.data = mesh;

    xform_node.children.push_back(geom_node);

    scene->prim_nodes.push_back(std::move(xform_node));

  }



  // Index-based node graph
  tinyusdz::Node mesh_node;
  tinyusdz::Node xform_node;

  xform_node.index = scene->node_indices.size();
  scene->node_indices.emplace_back(xform_node_id);

  mesh_node.index = scene->node_indices.size();
  scene->node_indices.emplace_back(mesh_node_id);

  mesh_node.parent = 0; // xform_node_id[0]
  xform_node.children.push_back(mesh_node);

  scene->nodes.push_back(xform_node);
  scene->nodes.push_back(mesh_node);
#endif
}

int main(int argc, char **argv)
{
  tinyusdz::HighLevelScene scene; // empty scene

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
