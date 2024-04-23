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

// key = Full absolute prim path(e.g. `/bora/dora`)
using XformMap = std::map<std::string, const tinyusdz::Xform *>;
using MeshMap = std::map<std::string, const tinyusdz::GeomMesh *>;
using MaterialMap = std::map<std::string, const tinyusdz::Material *>;
using PreviewSurfaceMap =
    std::map<std::string, std::pair<const tinyusdz::Shader *,
                                    const tinyusdz::UsdPreviewSurface *>>;
using UVTextureMap =
    std::map<std::string, std::pair<const tinyusdz::Shader *,
                                    const tinyusdz::UsdUVTexture *>>;
using PrimvarReader_float2Map =
    std::map<std::string, std::pair<const tinyusdz::Shader *,
                                    const tinyusdz::UsdPrimvarReader_float2 *>>;

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " input.usd [OPTIONS].\n";
    std::cout << "\n\nOptions\n\n";
    std::cout << "  --noidxbuild: Do not rebuild vertex indices\n";
    std::cout << "  --notri: Do not triangulate mesh\n";
    std::cout << "  --dumpobj: Dump mesh as wavefront .obj(for visual debugging)\n";
    std::cout << "  --dumpusd: Dump scene as USD(USDA Ascii)\n";
    return EXIT_FAILURE;
  }

  bool build_indices = true;
  bool triangulate = true;
  bool export_obj = false;
  bool export_usd = false;
  std::string filepath;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--notri") == 0) {
      triangulate = false;
    } else if (strcmp(argv[i], "--noidxbuild") == 0) {
      build_indices = false;
    } else if (strcmp(argv[i], "--dumpobj") == 0) {
      export_obj = true;
    } else if (strcmp(argv[i], "--dumpusd") == 0) {
      export_usd = true;
    } else {
      filepath = argv[i];
    }
  }

  std::string warn;
  std::string err;

  std::string ext = str_tolower(GetFileExtension(filepath));

  tinyusdz::Stage stage;
  bool is_usdz = false;

  if (ext.compare("usdc") == 0) {
    bool ret = tinyusdz::LoadUSDCFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDC file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else if (ext.compare("usda") == 0) {
    bool ret = tinyusdz::LoadUSDAFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    if (!ret) {
      std::cerr << "Failed to load USDA file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  } else if (ext.compare("usdz") == 0) {
    // std::cout << "usdz\n";
    bool ret = tinyusdz::LoadUSDZFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    is_usdz = true;

    if (!ret) {
      std::cerr << "Failed to load USDZ file: " << filepath << "\n";
      return EXIT_FAILURE;
    }

  } else {
    // try to auto detect format.
    bool ret = tinyusdz::LoadUSDFromFile(filepath, &stage, &warn, &err);
    if (!warn.empty()) {
      std::cerr << "WARN : " << warn << "\n";
    }
    if (!err.empty()) {
      std::cerr << "ERR : " << err << "\n";
      // return EXIT_FAILURE;
    }

    is_usdz = tinyusdz::IsUSDZ(filepath);

    if (!ret) {
      std::cerr << "Failed to load USD file: " << filepath << "\n";
      return EXIT_FAILURE;
    }
  }

  std::string s = stage.ExportToString();
  std::cout << s << "\n";
  std::cout << "--------------------------------------"
            << "\n";

  // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
  tinyusdz::tydra::RenderScene render_scene;
  tinyusdz::tydra::RenderSceneConverter converter;
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);

  std::cout << "Triangulate : " << (triangulate ? "true" : "false") << "\n";
  env.mesh_config.triangulate = triangulate;
  std::cout << "Rebuild vertex indices : " << (build_indices ? "true" : "false") << "\n";
  env.mesh_config.build_vertex_indices = build_indices; 

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

  double timecode = tinyusdz::value::TimeCode::Default();
  bool ret = converter.ConvertToRenderScene(env, &render_scene);
  if (!ret) {
    std::cerr << "Failed to convert USD Stage to RenderScene: \n" << converter.GetError() << "\n";
    return EXIT_FAILURE;
  }

  if (converter.GetWarning().size()) {
    std::cout << "ConvertToRenderScene warn: " << converter.GetWarning() << "\n";
  }

  std::cout << DumpRenderScene(render_scene) << "\n";

  if (export_obj) {
    std::cout << "Dump RenderMesh as wavefront .obj\n";
    for (size_t i = 0; i < render_scene.meshes.size(); i++) {
      std::string obj_str;
      std::string mtl_str;
      if (!tinyusdz::tydra::export_to_obj(render_scene, i, obj_str, mtl_str, &warn, &err)) {
        std::cerr << "obj export error: " << err << "\n";
        exit(-1);
      }

      std::string obj_filename = std::to_string(i) + render_scene.meshes[i].prim_name + ".obj";
      std::string mtl_filename = std::to_string(i) + render_scene.meshes[i].prim_name + ".mtl";
      {
        std::ofstream obj_ofs(obj_filename);
        obj_ofs << obj_str;
      }

      {
        std::ofstream mtl_ofs(mtl_filename);
        mtl_ofs << mtl_str;
      }
      std::cout << "  Wrote " << obj_filename << "\n";
    }
  }

  if (export_usd) {
    std::string usd_basename = tinyusdz::io::GetBaseFilename(filepath);
    std::string usd_filename = tinyusdz::removeSuffix(usd_basename, ext) + "export.usda";
    
    std::string usda_str;
    if (!tinyusdz::tydra::export_to_usda(render_scene, usda_str, &warn, &err)) {
      std::cerr << "Failed to export RenderScene to USDA: " << err << "\n";
    }
    if (warn.size()) {
      std::cout << "WARN: " << warn << "\n";
    }

    {
      std::ofstream ofs(usd_filename);
      ofs << usda_str;
    }
    std::cout << "Exported RenderScene as USDA: " << usd_filename << "\n";
  } 

  return EXIT_SUCCESS;
}
