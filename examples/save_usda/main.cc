#include "usda-writer.hh"

#include <iostream>

//
// create simple scene composed of Xform and Mesh.
//
void SimpleScene(tinyusdz::Stage *stage)
{
  //
  // tinyusdz currently does not provide scene construction API yet, so edit parameters directly.
  //
  tinyusdz::Xform xform;
  xform.name = "root";

  tinyusdz::XformOp op;
  op.op = tinyusdz::XformOp::OpType::Translate;
  tinyusdz::value::double3 translate;
  translate[0] = 1.0;
  translate[1] = 2.0;
  translate[2] = 3.0;
  op.set_scalar(translate);

  xform.xformOps.push_back(op);

  tinyusdz::GeomMesh mesh;
  mesh.name = "quad";

  {
    std::vector<tinyusdz::value::point3f> pts;
    pts.push_back({0.0f, 0.0f, 0.0f});

    pts.push_back({1.0f, 0.0f, 0.0f});

    pts.push_back({1.0f, 1.0f, 0.0f});

    pts.push_back({0.0f, 1.0f, 0.0f});

    mesh.points.SetValue(pts);
  }

  {
    // quad plane composed of 2 triangles.
    std::vector<int> indices;
    std::vector<int> counts;
    counts.push_back(3);
    counts.push_back(3);
    mesh.faceVertexCounts.SetValue(counts);

    indices.push_back(0);
    indices.push_back(1);
    indices.push_back(2);

    indices.push_back(0);
    indices.push_back(2);
    indices.push_back(3);

    mesh.faceVertexIndices.SetValue(indices);
  }

  tinyusdz::Prim meshPrim(mesh);
  tinyusdz::Prim xformPrim(xform);

  // [Xform]
  //  |
  //  +- [Mesh]
  //
  xformPrim.children.emplace_back(std::move(meshPrim));

  stage->GetRootPrims().emplace_back(std::move(xformPrim));
}

int main(int argc, char **argv)
{
  tinyusdz::Stage stage; // empty scene

  SimpleScene(&stage);

  std::string warn;
  std::string err;
  bool ret = tinyusdz::usda::SaveAsUSDA("output.udsa", stage, &warn, &err);

  if (warn.size()) {
    std::cout << "WARN: " << warn << "\n";
  }

  if (err.size()) {
    std::cerr << "ERR: " << err << "\n";
  }

  return ret ? EXIT_SUCCESS : -1;
}
