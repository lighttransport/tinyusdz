// All-in-one TinyUSDZ core
#include "tinyusdz.hh"

// Import to_string() and operator<< features
#include <iostream>
#include "pprinter.hh"
#include "value-pprint.hh"
#include "prim-pprint.hh"

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
      tinyusdz::XformOp op;
      op.op_type = tinyusdz::XformOp::OpType::Transform;
      tinyusdz::value::matrix4d a0;
      tinyusdz::value::matrix4d b0;

      tinyusdz::Identity(&a0);
      tinyusdz::Identity(&b0);

      a0.m[1][1] = 2.1;

      // column major, so [3][0], [3][1], [3][2] = translate X, Y, Z
      b0.m[3][0] = 1.0;
      b0.m[3][1] = 3.1;
      b0.m[3][2] = 5.1;

      tinyusdz::value::matrix4d transform = a0 * b0;

      op.set_value(transform);

      // `xformOpOrder`(token[]) is represented as std::vector<XformOp>
      xform.xformOps.push_back(op);

    }

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

        tinyusdz::Property uvProp(uvAttr);

        mesh.props.emplace("primvars:uv", uvProp);

        // ----------------------

        tinyusdz::Attribute uvIndexAttr;
        std::vector<int> uvIndices;

        // FIXME: Validate
        uvIndices.push_back(0);
        uvIndices.push_back(1);
        uvIndices.push_back(3);
        uvIndices.push_back(2);

        tinyusdz::primvar::PrimVar uvIndexVar;
        uvIndexVar.set_value(uvIndices);
        uvIndexAttr.set_var(std::move(uvIndexVar));
        // Or you can use this approach(if you want to keep a copy of PrimVar
        // data)
        // uvIndexAttr.set_var(uvIndexVar);

        tinyusdz::Property uvIndexProp(uvIndexAttr);
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

      // Add Primvar through GeomPrimvar;
      {
        tinyusdz::GeomPrimvar uvPrimvar;

        uvPrimvar.set_name("my_uv");
        std::vector<tinyusdz::value::texcoord2f> uvs;

        uvs.push_back({0.0f, 0.0f});
        uvs.push_back({1.0f, 0.0f});
        uvs.push_back({1.0f, 1.0f});
        uvs.push_back({0.0f, 1.0f});

        uvPrimvar.set_value(uvs);
        uvPrimvar.set_interpolation(tinyusdz::Interpolation::Vertex);

        std::vector<int> uvIndices;
        uvIndices.push_back(0);
        uvIndices.push_back(1);
        uvIndices.push_back(3);
        uvIndices.push_back(2);

        uvPrimvar.set_indices(uvIndices);
      
        // primvar name is extracted from Primvar::name
        std::string err;
        if (!mesh.set_primvar(uvPrimvar, &err)) {
          std::cerr << "Failed to add Primar: " << err << "\n";
        }
      }
    }


    // Access GeomPrimvar
    {
      std::cout << "uv is primvar? " << mesh.has_primvar("uv") << "\n";
      tinyusdz::GeomPrimvar primvar;
      std::string err;
      if (mesh.get_primvar("uv", &primvar, &err)) {
        std::cout << "uv primvar is Indexed Primvar? " << primvar.has_indices() << "\n";
      } else {
        std::cerr << "get_primvar(\"uv\") failed. err = " << err << "\n";
      }

      // Equivalent to pxr::UsdGeomPrimvar::ComputeFlattened().
      // elems[i] = values[indices[i]]
      tinyusdz::value::Value value;
      if (primvar.flatten_with_indices(&value, &err)) {
        // value;:Value can contain any types, but value.array_size() should work well only for primvar types(e.g. `float[]`, `color3f[]`)
        // It would report 0 for non-primvar types(e.g.`std::vector<Xform>`)
        std::cout << "uv primvars. array size = " << value.array_size() << "\n";
        std::cout << "uv primvars. expand_by_indices result = " << tinyusdz::value::pprint_value(value) << "\n";
      } else {
        std::cerr << "expand_by_indices failed. err = " << err << "\n";
      }

      // Typed version
      std::vector<tinyusdz::value::texcoord2f> uvs;
      if (primvar.flatten_with_indices(&uvs, &err)) {
        // value;:Value can contain any types, but value.array_size() should work well only for primvar types(e.g. `float[]`, `color3f[]`)
        // It would report 0 for non-primvar types(e.g.`std::vector<Xform>`)
        std::cout << "uv primvars. array size = " << uvs.size() << "\n";
        std::cout << "uv primvars. expand_by_indices result = " << tinyusdz::value::pprint_value(uvs) << "\n";
      } else {
        std::cerr << "expand_by_indices failed. err = " << err << "\n";
      }

      std::vector<tinyusdz::GeomPrimvar> gpvars = mesh.get_primvars();
      std::cout << "# of primvars = " << gpvars.size();
      for (const auto &item : gpvars) {
        std::cout << "  primvar = " << item.name() << "\n";
      }
    }
  }

  tinyusdz::GeomSphere sphere;
  {
    sphere.name = "sphere0";

    sphere.radius = 3.14;

  }

  //
  // Create Scene(Stage) hierarchy.
  // You can add child Prim to parent Prim's `children()`.
  //
  // [Xform]
  //  |
  //  +- [Mesh]
  //  +- [Sphere]
  //
  


  // Prim's elementName is read from concrete Prim class(GeomMesh::name,
  // Xform::name, ...)
  tinyusdz::Prim meshPrim(mesh);
  tinyusdz::Prim spherePrim(sphere);
  tinyusdz::Prim xformPrim(xform);

  //
  // Prim::prim_id is optional, but it'd be better to have it when you construct Prim hierarchy manually,
  // since Prim itself cannot have absolute Path information at the moment.
  // (USDA reader and USDC reader assigns unique Prim id in another way)
  //
  // You can use Stage::allocate_prim_id() to assign unique Prim id(Starts with 1. zero is reserved)
  // NOTE: You can release prim_id through Stage::release_prim_id(prim_id)
  uint64_t meshPrimId;
  uint64_t spherePrimId;
  uint64_t xformPrimId;

  stage->allocate_prim_id(&meshPrimId);
  stage->allocate_prim_id(&spherePrimId);
  stage->allocate_prim_id(&xformPrimId);

  std::cout << "meshPrimId = " << meshPrimId << "\n";
  std::cout << "spherePrimId = " << spherePrimId << "\n";
  std::cout << "xformPrimId = " << xformPrimId << "\n";

  meshPrim.prim_id() = meshPrimId;
  spherePrim.prim_id() = spherePrimId;
  xformPrim.prim_id() = xformPrimId;

  xformPrim.children().emplace_back(std::move(meshPrim));
  xformPrim.children().emplace_back(std::move(spherePrim));

  // If you want to specify the appearance/traversal order of child Prim(e.g. Showing Prim tree in GUI, Ascii output), set "primChildren"(token[]) metadata
  // xfromPrim.metas().primChildren.size() must be identical to xformPrim.children().size()
  xformPrim.metas().primChildren.push_back(tinyusdz::value::token(spherePrim.element_name()));
  xformPrim.metas().primChildren.push_back(tinyusdz::value::token(meshPrim.element_name()));
  std::cout << "sphere.element_name = " << spherePrim.element_name() << "\n";
  std::cout << "mesh.element_name = " << meshPrim.element_name() << "\n";

  // Use Stage::root_prims() to retrieve Stage's root Prim array.
  stage->root_prims().emplace_back(std::move(xformPrim));

  // You can add Stage metadatum through Stage::metas()
  stage->metas().comment = "Generated by TinyUSDZ api_tutorial.";

  {
    // CustomDataType is similar to VtDictionary.
    // it is a map<string, MetaVariable>
    // MetaVariable is similar to Value, but accepts limited variation of types(double, token, string, float3[], ...)
    tinyusdz::CustomDataType customData;

    tinyusdz::MetaVariable metavar;
    double mycustom = 1.3;
    metavar.set_value("mycustom", mycustom);

    std::string mystring = "hello";
    tinyusdz::MetaVariable metavar2("mystring", mystring);

    customData.emplace("mycustom", metavar);
    customData.emplace("mystring", metavar2);
    customData.emplace("myvalue", 2.45); // Construct MetaVariables implicitly


    // You can also use SetCustomDataByKey to set custom value with key having namespaces(':')
    
    tinyusdz::MetaVariable intval = int(5);
    tinyusdz::SetCustomDataByKey("mydict:myval", intval, /* inout */customData);

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

  // GeomPrimvar
  {

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
