// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

//
// Command-line check tool to convert USD Stage to RenderScene(glTF-like data
// structure)
//
#include <algorithm>
#include <iostream>
#include <sstream>

// Use json.hpp located at <tinyusdz>/src/external
#define TINYGLTF_NO_INCLUDE_JSON
#include "external/jsonhpp/nlohmann/json.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION // this will include <tinyusdz>/src/external/stb_image.h
#include "external/tiny_gltf.h"

#include "tinyusdz.hh"
#include "io-util.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"

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

tinygltf::Material to_gltf_material(const tinyusdz::tydra::RenderMaterial &mat) {
  tinygltf::Material out;

  out.pbrMetallicRoughness.roughnessFactor = mat.surfaceShader.roughness.value;

  return out;
}

bool to_gltf(const tinyusdz::tydra::RenderScene &rscene, const std::string &gltf_filename)
{
  tinygltf::Model model;
  tinygltf::Scene scene;
  std::vector<tinygltf::Mesh> meshes;
  tinygltf::Primitive primitive;

  tinygltf::Asset asset;
  asset.version = "2.0";
  asset.generator = "usd_to_gltf example in TinyUSDZ";

  model.scenes.push_back(scene);

  // model.bufferViews
  // model.buffers
  // model.nodes
  //model.meshes = meshes;
  model.asset = asset;

  // model.materials

  tinygltf::TinyGLTF ctx;
  bool ret = ctx.WriteGltfSceneToFile(&model,
    gltf_filename,
    true, // embedImages
  true, // embedBuffers
  true, // pretty print
  false); // write binary glTF

  return ret;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Need USD file.\n"
              << std::endl;
    return EXIT_FAILURE;
  }

  std::string filepath = argv[1];
  std::string warn;
  std::string err;

  std::string ext = str_tolower(GetFileExtension(filepath));

  tinyusdz::Stage stage;

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

  // Add base directory of .usd file to search path.
  std::string usd_basedir = tinyusdz::io::GetBaseDir(filepath);
  std::cout << "Add seach path: " << usd_basedir << "\n";

  env.set_search_paths({usd_basedir});
  // TODO: Set user-defined AssetResolutionResolver
  // AssetResolutionResolver arr;
  // converter.set_asset_resoluition_resolver(arr);


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

  if (!to_gltf(render_scene, "output.gltf")) {
    std::cerr << "Failed to save scene as glTF\n";
    return EXIT_FAILURE;
  } 

  return EXIT_SUCCESS;
}
