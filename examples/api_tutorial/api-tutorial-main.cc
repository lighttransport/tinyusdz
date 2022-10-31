// All-in-one TinyUSDZ core
#include "tinyusdz.hh"

// Import to_string() and operator<< features
#include <iostream>
#include "pprinter.hh"
#include "value-pprint.hh"

// Tydra is a collection of APIs to access/convert USD Prim data
// (e.g. Can get Attribute by name)
#include "tydra/scene-access.hh"

//
// create a Scene
//
void CreateScene(tinyusdz::Stage *stage) {
  // TinyUSDZ API does not use mutex, smart pointers(e.g. shared_ptr) and C++
  // exception. Also threading is opional in TinyUSDZ and currently multi-threading is not used.
  // (`value::token` does not use mutex by default, see comments in token-types.hh for details)
  //
  // API is not multi-thread safe, thus if you want to manipulate a
  // scene(Stage) in multi-threaded context, The app must take care of resource locks
  // in the app layer.

  //
  // To construct Prim, first create concrete Prim object(e.g. Xform, GeomMesh),
  // then add it to tinyusdz::Prim.
  //
  //
  tinyusdz::Xform xform;
  {
    xform.name = "root";  // Prim's name(elementPath)

    {
      // `xformOp:***` attribute is represented as XformOp class
      tinyusdz::XformOp op;
      op.op_type = tinyusdz::XformOp::OpType::Translate;
      tinyusdz::value::double3 translate;
      translate[0] = 1.0;
      translate[1] = 2.0;
      translate[2] = 3.0;
      op.set_value(translate);

      // `xformOpOrder`(token[]) is represented as std::vector<XformOp>
      xform.xformOps.push_back(op);
    }

    {
      // .suffix will be prepended to `xformOp:translate`
      // 'xformOp:translate:move'
      tinyusdz::XformOp op;
      op.op_type = tinyusdz::XformOp::OpType::Translate;
      op.suffix = "move";
      tinyusdz::value::double3 translate;

      // TimeSamples value can be added with `set_timesample`
      // NOTE: TimeSamples data will be automatically sorted by time when using it.
      translate[0] = 0.0;
      translate[1] = 0.0;
      translate[2] = 0.0;
      op.set_timesample(0.0, translate);

      translate[0] = 1.0;
      translate[1] = 0.1;
      translate[2] = 0.3;
      op.set_timesample(1.0, translate);

      xform.xformOps.push_back(op);
    }

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
        // uvVar.set_value(uvs);
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
        uvIndexVar.set_value(uvIndices);
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
        var.set_value(myvalue);
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

  // Use Stage::root_prims() to retrieve Stage's root Prim array.
  stage->root_prims().emplace_back(std::move(xformPrim));

  // You can add Stage metadatum through Stage::metas()
  tinyusdz::StringData sdata;
  sdata.value = "Generated by TinyUSDZ api_tutorial.";
  stage->metas().comment = sdata;

  {
    // CustomDataType is similar to VtDictionary.
    // it is a map<string, MetaVariable>
    // MetaVariable is similar to Value, but accepts limited variation of types(double, token, string, float3[], ...) 
    tinyusdz::CustomDataType customData;
    tinyusdz::MetaVariable metavar;
    double mycustom = 1.3;
    metavar.set_value("mycustom", mycustom);
    customData.emplace("mycustom", metavar);

    stage->metas().customLayerData = customData;

  }
}

int main(int argc, char **argv) {
  tinyusdz::Stage stage;  // empty scene

  CreateScene(&stage);

  std::cout << stage.ExportToString() << "\n";

  {
    tinyusdz::Path path(/* absolute prim path */ "/root",
                        /* property path */ "");

    const tinyusdz::Prim *prim{nullptr};
    std::string err;
    bool ret = stage.find_prim_at_path(path, prim, &err);
    if (ret) {
      std::cout << "Found Prim at path: " << tinyusdz::to_string(path) << "\n";
    } else {
      std::cerr << err << "\n";
    }

    if (!prim) {
      // This should not be happen though.
      std::cerr << "Prim is null\n";
      return -1;
    }

    if (!prim->is<tinyusdz::Xform>()) {
      std::cerr << "Expected Xform prim." << "\n";
      return -1;
    }

    // Cast to Xform
    const tinyusdz::Xform *xform = prim->as<tinyusdz::Xform>();
    if (!xform) {
      std::cerr << "Expected Xform prim." << "\n";
      return -1;
    }

  }

  {
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

    if (!prim) {
      // This should not be happen though.
      std::cerr << "Prim is null\n";
      return -1;
    }

    tinyusdz::Attribute attr;
    if (tinyusdz::tydra::GetAttribute(*prim, "points", &attr, &err)) {
      std::cout << "point attribute type = " << attr.type_name() << "\n";

      // Ensure Attribute has a value(not Attribute connection)
      if (attr.is_value()) {
        std::vector<tinyusdz::value::point3f> pts;
        if (attr.is_timesamples()) {
          // TODO: timesamples
        } else {
          if (attr.get_value(&pts)) {
            std::cout << "point attribute value = " << pts << "\n";
          }
        }
      }

    } else {
      std::cerr << err << "\n";
    }
  }


  return EXIT_SUCCESS;
}
