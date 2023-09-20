// All-in-one TinyUSDZ core
#include "tinyusdz.hh"

// Import to_string() and operator<< features
#include <iostream>
#include "pprinter.hh"
#include "value-pprint.hh"
#include "prim-pprint.hh"

// Tydra is a collection of APIs to access/convert USD Prim data
// (e.g. Can get Attribute by name)
// See <tinyusdz>/examples/tydra_api for more Tydra API examples.
#include "tydra/scene-access.hh"

std::map<std::string, float> g_map;

//
// custom File-format APIs.
//
static bool MyCheck(const tinyusdz::AssetInfo &asset, std::string *warn, std::string *err, void *user_data) {
  return true;
}

static bool MyRead(const  tinyusdz::AssetInfo &asset, tinyusdz::PrimSpec &ps/* inout */, std::string *warn, std::string *err, void *user_data) {
  std::string key = asset.identifier.GetAssetPath();

  if (g_map.count(key)) {
    float val = g_map[key];

    tinyusdz::Attribute attr;
    attr.set_value(val);
    attr.set_name("myval");
    attr.variability() = tinyusdz::Variability::Uniform;

    ps.props()["myval"] = tinyusdz::Property(attr, /* custom */false);

    return true;

  } else {
    if (err) {
      (*err) += "`" + key + "` not found.\n";
    }
    return false;
  }
}

static bool MyWrite(const  tinyusdz::AssetInfo &asset, const tinyusdz::PrimSpec &ps, std::string *err, void *user_data) {
  // TOOD
  return false;
}


int main(int argc, char **argv) {
  g_map["bora"] = 3.14f;
  g_map["dora"] = 6.14f;

  tinyusdz::FileFormatHandler my_handler;
  my_handler.extension = "my";
  my_handler.description = "Custom fileformat  example.";
  my_handler.checker = MyCheck;
  my_handler.reader= MyRead;
  my_handler.writer = MyWrite;
  my_handler.userdata = nullptr;

  tinyusdz::USDLoadOptions options;
  options.fileformats["my"] = my_handler;

  tinyusdz::Stage stage;  // empty scene

  std::string input_usd_filepath = "../data/fileformat_my.usda";
  if (argc > 1) {
    input_usd_filepath = argv[1];
  }

  std::string warn, err;

  tinyusdz::Layer layer;
  bool ret = tinyusdz::LoadLayerFromFile(input_usd_filepath, &layer, &warn, &err, options);

  if (warn.size()) {
    std::cout << "WARN: " << warn << "\n";
  }

  if (!ret) {
    std::cerr << err << "\n";
    exit(-1);
  }

  // dummy Asset resolver.
  tinyusdz::AssetResolutionResolver resolver;

  // Do `references` composition to materialize `references = @***.my@`
  tinyusdz::Layer composited_layer;
  if (!tinyusdz::CompositeReferences(resolver, layer, &composited_layer, &warn, &err)) {
    std::cerr << "Failed to composite `references`: " << err << "\n";
    return -1;
  }
  
 
  // Print USD scene as Ascii.
  std::cout << composited_layer << "\n";

  return EXIT_SUCCESS;
}
