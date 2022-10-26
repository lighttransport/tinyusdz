// All-in-one TinyUSDZ core
#include "tinyusdz.hh"

// Import to_string() and operator<< features
#include "pprinter.hh"
#include "value-pprint.hh"

// Tydra is a collection of APIs to access/convert USD Prim data
// (e.g. Can get attribute by name)
#include <iostream>

#include "tydra/scene-access.hh"

//
// create a Scene
//
void CreateScene(tinyusdz::Stage *stage) {
  // TinyUSDZ does not use mutex, smart pointers(e.g. shared_ptr) and C++
  // exception. API is not multi-thread safe, thus if you need to manipulate a
  // scene in multi-threaded context, The app must take care of resource locks
  // in the app layer.

  //
  // To construct Prim, first create concrete Prim object(e.g. Xform, GeomMesh),
  // then add it to tinyusdz::Prim.
  //
  //
  tinyusdz::Xform xform;
  {
    xform.name = "root";  // Prim's name(elementPath)

    // `xformOp:***` attribute is represented as XformOp class
    tinyusdz::XformOp op;
    op.op = tinyusdz::XformOp::OpType::Translate;
    tinyusdz::value::double3 translate;
    translate[0] = 1.0;
    translate[1] = 2.0;
    translate[2] = 3.0;
    op.set_scalar(translate);

    // `xformOpOrder`(token[]) is represented as std::vector<XformOp>
    xform.xformOps.push_back(op);
  }

  tinyusdz::GeomMesh mesh;
  {
    mesh.name = "quad";

    {
      std::vector<tinyusdz::value::point3f> pts;
      pts.push_back({0.0f, 0.0f, 0.0f});

      pts.push_back({1.0f, 0.0f, 0.0f});

      pts.push_back({1.0f, 1.0f, 0.0f});

      pts.push_back({0.0f, 1.0f, 0.0f});

      mesh.points.set_value(pts);
    }

    {
      // quad plane composed of 2 triangles.
      std::vector<int> indices;
      std::vector<int> counts;
      counts.push_back(3);
      counts.push_back(3);
      mesh.faceVertexCounts.set_value(counts);

      indices.push_back(0);
      indices.push_back(1);
      indices.push_back(2);

      indices.push_back(0);
      indices.push_back(2);
      indices.push_back(3);

      mesh.faceVertexIndices.set_value(indices);
    }

    // primvar and custom attribute can be added to generic Property container
    // `props`(map<std::string, Property>)
    {
      // primvar is simply an attribute with prefix `primvars:`
      //
      // texCoord2f[] primvars:uv = [ ... ] ( interpolation = "vertex" )
      // int[] primvars:uv:indices = [ ... ]
      //
      {
        tinyusdz::Attribute uvAttr;
        std::vector<tinyusdz::value::texcoord2f> uvs;

        uvs.push_back({0.0f, 0.0f});
        uvs.push_back({1.0f, 0.0f});
        uvs.push_back({1.0f, 1.0f});
        uvs.push_back({0.0f, 1.0f});

        // Fast path. Set the value directly to Attribute.
        uvAttr.set_value(uvs);

        // or we can first build primvar::PrimVar
        // tinyusdz::primvar::PrimVar uvVar;
        // uvVar.set_scalar(uvs);
        // uvAttr.set_var(std::move(uvVar));

        // Currently `interpolation` is described in Attribute metadataum.
        // You can set builtin(predefined) Attribute Metadatum(e.g.
        // `interpolation`, `hidden`) through `metas()`.
        uvAttr.metas().interpolation = tinyusdz::Interpolation::Vertex;

        tinyusdz::Property uvProp(uvAttr, /* custom*/ false);

        mesh.props.emplace("primvars:uv", uvProp);

        // ----------------------

        tinyusdz::Attribute uvIndexAttr;
        std::vector<int> uvIndices;

        // FIXME: Validate
        uvIndices.push_back(0);
        uvIndices.push_back(1);
        uvIndices.push_back(2);
        uvIndices.push_back(3);

        tinyusdz::primvar::PrimVar uvIndexVar;
        uvIndexVar.set_scalar(uvIndices);
        uvIndexAttr.set_var(std::move(uvIndexVar));
        // Or you can use this approach(if you want to keep a copy of PrimVar
        // data)
        // uvIndexAttr.set_var(uvIndexVar);

        tinyusdz::Property uvIndexProp(uvIndexAttr, /* custom*/ false);
        mesh.props.emplace("primvars:uv:indices", uvIndexProp);
      }

      // `custom uniform double myvalue = 3.0 ( hidden = 0 )`
      {
        tinyusdz::Attribute attrib;
        double myvalue = 3.0;
        tinyusdz::primvar::PrimVar var;
        var.set_scalar(myvalue);
        attrib.set_var(std::move(var));
        attrib.variability() = tinyusdz::Variability::Uniform;

        attrib.metas().hidden = false;

        // NOTE: `custom` keyword would be deprecated in the future USD syntax,
        // so you can set it false.
        tinyusdz::Property prop(attrib, /* custom*/ true);

        mesh.props.emplace("myvalue", prop);
      }
    }
  }

  //
  // Create Scene(Stage) hierarchy.
  // You can add child Prim to parent Prim's `children()`.
  //
  // [Xform]
  //  |
  //  +- [Mesh]
  //

  // Prim's elementName is read from concrete Prim class(GeomMesh::name,
  // Xform::name, ...)
  tinyusdz::Prim meshPrim(mesh);
  tinyusdz::Prim xformPrim(xform);

  xformPrim.children().emplace_back(std::move(meshPrim));

  // Use GetRootPrims() to retrieve Stage's root Prim array.
  stage->root_prims().emplace_back(std::move(xformPrim));

  // You can add Stage metadatum through
  tinyusdz::StringData sdata;
  sdata.value = "Generated by TinyUSDZ api_tutorial.";
  stage->metas().comment = sdata;
}

int main(int argc, char **argv) {
  tinyusdz::Stage stage;  // empty scene

  CreateScene(&stage);

  std::cout << stage.ExportToString() << "\n";

  tinyusdz::Path path(/* absolute prim path */ "/root/quad",
                      /* property path */ "");

  const tinyusdz::Prim *prim{nullptr};
  std::string err;
  bool ret = stage.find_prim_at_path(path, prim, &err);
  if (ret) {
    std::cout << "Found Prim at path: " << tinyusdz::to_string(path) << "\n";
  } else {
    std::cerr << err << "\n";
  }

  tinyusdz::Attribute attr;
  if (tinyusdz::tydra::GetAttribute(*prim, "points", &attr, &err)) {
    std::cout << "point attribute type = " << attr.type_name() << "\n";

    // Ensure Attribute has a value(not Attribute connection)
    if (attr.is_value()) {
      std::vector<tinyusdz::value::point3f> pts;
      // TODO: timesamples
      if (attr.get_value(&pts)) {
        std::cout << "point attribute value = " << pts << "\n";
      }
    }

  } else {
    std::cerr << err << "\n";
  }


  return EXIT_SUCCESS;
}
