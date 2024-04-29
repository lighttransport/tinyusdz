// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.

//
// Command-line tool to convert USD Stage to RenderScene(glTF-like data
// structure)
//
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>

// clang-format off
// <tinyusdz>/examples/common
#include "nanort.h"

#include "io-util.hh"
#include "str-util.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"
#include "tydra/obj-export.hh"
#include "tydra/usd-export.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
#include "value-types.hh"

// clang-format on

static std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of('.') != std::string::npos)
    return filename.substr(filename.find_last_of('.') + 1);
  return "";
}

static std::string str_tolower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " input.usd [OPTIONS].\n";
    std::cout << "\n\nOptions\n\n";
    std::cout << "  --timecode VALUE: Specify timecode value(e.g. 3.14)\n";
    std::cout << "  --debug: Do some debug logging\n";
    std::cout << "  --notri: Do not triangulate mesh\n";
    return EXIT_FAILURE;
  }

  // When Xform, Mesh, Material, etc. have time-varying values,
  // values are evaluated at `timecode` time(except for animation values in SkelAnimation)
  double timecode = tinyusdz::value::TimeCode::Default();

  bool debug = false;
  bool triangulate = true;
  std::string filepath;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--notri") == 0) {
      triangulate = false;
    } else if (strcmp(argv[i], "--debug") == 0) {
      debug = false;
    } else if (strcmp(argv[i], "--timecode") == 0) {
      if ((i + 1) >= argc) {
        std::cerr << "arg is missing for --timecode flag.\n";
        return -1;
      }
      timecode = std::stod(argv[i+1]);
      std::cout << "Use timecode: " << timecode << "\n";
      i++;
    } else {
      filepath = argv[i];
    }
  }

  std::string warn;
  std::string err;

  std::string ext = str_tolower(GetFileExtension(filepath));

  tinyusdz::Stage stage;
  bool is_usdz = false;

  bool ret = tinyusdz::LoadUSDFromFile(filepath, &stage, &warn, &err);

  if (!warn.empty()) {
    std::cerr << "WARN : " << warn << "\n";
  }

  if (!ret) {
    std::cerr << "Failed to load USD file: " << filepath << "\n";
    std::cerr << "Error: " << err << "\n";
    return EXIT_FAILURE;
  }

  is_usdz = tinyusdz::IsUSDZ(filepath);


  // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
  tinyusdz::tydra::RenderScene render_scene;
  tinyusdz::tydra::RenderSceneConverter converter;
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);

  std::cout << "Triangulate : " << (triangulate ? "true" : "false") << "\n";
  env.mesh_config.triangulate = triangulate;
  env.mesh_config.build_vertex_indices = false;

  // Add base directory of .usd file to search path.
  std::string usd_basedir = tinyusdz::io::GetBaseDir(filepath);
  std::cout << "Add seach path: " << usd_basedir << "\n";


  tinyusdz::USDZAsset usdz_asset;
  if (is_usdz) {
    // Setup AssetResolutionResolver to read a asset(file) from memory.
    if (!tinyusdz::ReadUSDZAssetInfoFromFile(filepath, &usdz_asset, &warn, &err)) {
      std::cerr << "Failed to read USDZ assetInfo from file: " << err << "\n";
      exit(-1);
    }
    if (warn.size()) {
      std::cout << warn << "\n";
    }

    tinyusdz::AssetResolutionResolver arr;

    // NOTE: Pointer address of usdz_asset must be valid until the call of RenderSceneConverter::ConvertToRenderScene.
    if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset)) {
      std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
      exit(-1);
    };

    env.asset_resolver = arr;

  } else {
    env.set_search_paths({usd_basedir});

    // TODO: Add example to set user-defined AssetResolutionResolver
    // AssetResolutionResolver arr;
    // ...
    // env.asset_resolver(arr);
  }

  env.timecode = timecode;
  ret = converter.ConvertToRenderScene(env, &render_scene);
  if (!ret) {
    std::cerr << "Failed to convert USD Stage to RenderScene: \n" << converter.GetError() << "\n";
    return EXIT_FAILURE;
  }

  if (converter.GetWarning().size()) {
    std::cout << "ConvertToRenderScene warn: " << converter.GetWarning() << "\n";
  }

  if (debug) {
    std::cout << DumpRenderScene(render_scene) << "\n";
  }

  // TODO: Do raytracing

  return EXIT_SUCCESS;
}
