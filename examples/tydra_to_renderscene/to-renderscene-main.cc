// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.

//
// Command-line tool to convert USD Stage to RenderScene(glTF-like data
// structure)
//
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#include "io-util.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "str-util.hh"
#include "tinyusdz.hh"
#include "tydra/obj-export.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"
#include "tydra/usd-export.hh"
#include "usdShade.hh"
#include "value-pprint.hh"
#include "value-types.hh"

static int NullARResolve(const char *asset_name,
                       const std::vector<std::string> &search_paths,
                       std::string *resolved_asset_name, std::string *err,
                       void *userdata) {
  (void)err;
  (void)userdata;
  (void)search_paths;
  (void)asset_name;
  (void)resolved_asset_name;
  return -1;
}

static int NullARSize(const char *asset_name, uint64_t *nbytes, std::string *err,
                    void *userdata) {
  (void)userdata;
  (void)asset_name;
  (void)nbytes;
  (void)err;

  return -1;
}

static int NullARRead(const char *asset_name, uint64_t req_nbytes, uint8_t *out_buf,
             uint64_t *nbytes, std::string *err, void *userdata) {
  
  (void)asset_name;
  (void)req_nbytes;
  (void)out_buf;
  (void)nbytes;
  (void)err;
  (void)userdata;

  return -1;
}

static bool SetupNullAssetResolution(
  tinyusdz::AssetResolutionResolver &resolver)
{
  tinyusdz::AssetResolutionHandler handler;
  handler.resolve_fun = NullARResolve;
  handler.size_fun = NullARSize;
  handler.read_fun = NullARRead;
  handler.write_fun = nullptr;
  handler.userdata = nullptr;

  resolver.register_wildcard_asset_resolution_handler(handler);

  return true;
}

static std::string AnimationPathToString(tinyusdz::tydra::AnimationPath path) {
  switch (path) {
    case tinyusdz::tydra::AnimationPath::Translation:
      return "Translation";
    case tinyusdz::tydra::AnimationPath::Rotation:
      return "Rotation";
    case tinyusdz::tydra::AnimationPath::Scale:
      return "Scale";
    case tinyusdz::tydra::AnimationPath::Weights:
      return "Weights";
  }
  return "Unknown";
}

static std::string InterpolationToString(tinyusdz::tydra::AnimationInterpolation interp) {
  switch (interp) {
    case tinyusdz::tydra::AnimationInterpolation::Linear:
      return "Linear";
    case tinyusdz::tydra::AnimationInterpolation::Step:
      return "Step";
    case tinyusdz::tydra::AnimationInterpolation::CubicSpline:
      return "CubicSpline";
  }
  return "Unknown";
}

static void DumpAnimationTimesamples(const tinyusdz::tydra::RenderScene& scene) {
  std::cout << "\n========================================\n";
  std::cout << "Animation Timesamples Dump\n";
  std::cout << "========================================\n";
  std::cout << "Total animations: " << scene.animations.size() << "\n\n";

  for (size_t anim_idx = 0; anim_idx < scene.animations.size(); anim_idx++) {
    const auto& anim = scene.animations[anim_idx];

    std::cout << "Animation [" << anim_idx << "]: " << anim.name << "\n";
    std::cout << "  Prim name: " << anim.prim_name << "\n";
    std::cout << "  Abs path: " << anim.abs_path << "\n";
    std::cout << "  Duration: " << anim.duration << " seconds\n";
    std::cout << "  Channels: " << anim.channels.size() << "\n";
    std::cout << "  Samplers: " << anim.samplers.size() << "\n";

    // Dump each channel
    for (size_t ch_idx = 0; ch_idx < anim.channels.size(); ch_idx++) {
      const auto& channel = anim.channels[ch_idx];

      std::cout << "\n  Channel [" << ch_idx << "]:\n";
      std::cout << "    Path: " << AnimationPathToString(channel.path) << "\n";

      if (channel.target_type == tinyusdz::tydra::ChannelTargetType::SceneNode) {
        std::cout << "    Target type: SceneNode\n";
        std::cout << "    Target node: " << channel.target_node << "\n";
      } else {
        std::cout << "    Target type: SkeletonJoint\n";
        std::cout << "    Skeleton ID: " << channel.skeleton_id << "\n";
        std::cout << "    Joint ID: " << channel.joint_id << "\n";
      }
      std::cout << "    Sampler index: " << channel.sampler << "\n";

      // Dump sampler data
      if (channel.sampler >= 0 &&
          static_cast<size_t>(channel.sampler) < anim.samplers.size()) {
        const auto& sampler = anim.samplers[channel.sampler];

        std::cout << "    Interpolation: " << InterpolationToString(sampler.interpolation) << "\n";
        std::cout << "    Keyframes: " << sampler.num_keyframes() << "\n";

        // Determine components per value based on path
        size_t components = 1;
        switch (channel.path) {
          case tinyusdz::tydra::AnimationPath::Translation:
          case tinyusdz::tydra::AnimationPath::Scale:
            components = 3;  // vec3
            break;
          case tinyusdz::tydra::AnimationPath::Rotation:
            components = 4;  // quat (x, y, z, w)
            break;
          case tinyusdz::tydra::AnimationPath::Weights:
            // Variable, depends on number of morph targets
            if (!sampler.times.empty() && !sampler.values.empty()) {
              components = sampler.values.size() / sampler.times.size();
            }
            break;
        }

        // Dump timesamples
        std::cout << "    Timesamples:\n";
        for (size_t i = 0; i < sampler.times.size(); i++) {
          std::cout << "      [" << i << "] t=" << sampler.times[i] << " : (";

          size_t value_start = i * components;
          for (size_t c = 0; c < components && (value_start + c) < sampler.values.size(); c++) {
            if (c > 0) std::cout << ", ";
            std::cout << sampler.values[value_start + c];
          }
          std::cout << ")\n";
        }
      } else {
        std::cout << "    [Invalid sampler index]\n";
      }
    }
    std::cout << "\n";
  }

  std::cout << "========================================\n\n";
}

static void print_help(const char* prog_name) {
  std::cout << "Usage: " << prog_name << " input.usd [OPTIONS]\n";
  std::cout << "\nConvert USD Stage to RenderScene (glTF-like data structure)\n";
  std::cout << "\nOptions:\n";
  std::cout << "  --help                Show this help message\n";
  std::cout << "  --timecode VALUE      Specify timecode value (e.g. 3.14)\n";
  std::cout << "  --noidxbuild          Do not rebuild vertex indices\n";
  std::cout << "  --notri               Do not triangulate mesh\n";
  std::cout << "  --trifan              Use triangle fan triangulation (instead of earcut)\n";
  std::cout << "  --texload             Load textures\n";
  std::cout << "  --noar                Do not use (default) AssetResolver\n";
  std::cout << "  --usdprint            Print parsed USD\n";
  std::cout << "  --dumpobj             Dump mesh as wavefront .obj (for visual debugging)\n";
  std::cout << "  --dumpusd             Dump scene as USD (USDA Ascii)\n";
  std::cout << "  --dump-timesamples    Dump animation channel timesamples values\n";
  std::cout << "  --nodump              Do not dump RenderScene output\n";
  std::cout << "  --notangent           Do not compute tangents/binormals\n";
  std::cout << "  --calctangent         Force tangent computation even without normal map\n";
  std::cout << "  --tangent-method M    Tangent method: lengyel (default), mikktspace, fast-mikktspace\n";
  std::cout << "  --yaml                Output RenderScene as YAML (human-readable)\n";
  std::cout << "  --json                Output RenderScene as JSON (machine-readable)\n";
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_help(argv[0]);
    return EXIT_FAILURE;
  }

  // When Xform, Mesh, Material, etc. have time-varying values,
  // values are evaluated at `timecode` time(except for animation values in
  // SkelAnimation)
  double timecode = tinyusdz::value::TimeCode::Default();

  bool build_indices = true;
  bool triangulate = true;
  bool use_triangle_fan = false;
  bool export_obj = false;
  bool export_usd = false;
  bool usdprint = false;
  bool texload = false;
  bool no_assetresolver = false;
  bool dump_timesamples = false;
  bool no_dump = false;
  bool no_tangent = false;
  bool force_tangent = false;
  auto tangent_method = tinyusdz::tydra::MeshConverterConfig::TangentComputationMethod::Lengyel;
  std::string output_format = "yaml";  // "yaml" (human-readable), "json" (machine-readable)

  std::string filepath;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      print_help(argv[0]);
      return EXIT_SUCCESS;
    } else if (strcmp(argv[i], "--notri") == 0) {
      triangulate = false;
    } else if (strcmp(argv[i], "--trifan") == 0) {
      use_triangle_fan = true;
    } else if (strcmp(argv[i], "--noidxbuild") == 0) {
      build_indices = false;
    } else if (strcmp(argv[i], "--usdprint") == 0) {
      usdprint = true;
    } else if (strcmp(argv[i], "--texload") == 0) {
      texload = true;
    } else if (strcmp(argv[i], "--noar") == 0) {
      no_assetresolver = true;
    } else if (strcmp(argv[i], "--dumpobj") == 0) {
      export_obj = true;
    } else if (strcmp(argv[i], "--dumpusd") == 0) {
      export_usd = true;
    } else if (strcmp(argv[i], "--dump-timesamples") == 0) {
      dump_timesamples = true;
    } else if (strcmp(argv[i], "--nodump") == 0) {
      no_dump = true;
    } else if (strcmp(argv[i], "--notangent") == 0) {
      no_tangent = true;
    } else if (strcmp(argv[i], "--calctangent") == 0) {
      force_tangent = true;
    } else if (strcmp(argv[i], "--tangent-method") == 0) {
      if ((i + 1) >= argc) {
        std::cerr << "arg is missing for --tangent-method flag.\n";
        return -1;
      }
      std::string tm = argv[i + 1];
      if (tm == "lengyel") {
        tangent_method = tinyusdz::tydra::MeshConverterConfig::TangentComputationMethod::Lengyel;
      } else if (tm == "mikktspace") {
        tangent_method = tinyusdz::tydra::MeshConverterConfig::TangentComputationMethod::MikkTSpace;
      } else if (tm == "fast-mikktspace") {
        tangent_method = tinyusdz::tydra::MeshConverterConfig::TangentComputationMethod::FastMikkTSpace;
      } else {
        std::cerr << "Unknown tangent method: " << tm << ". Use lengyel, mikktspace, or fast-mikktspace.\n";
        return -1;
      }
      i++;
    } else if (strcmp(argv[i], "--timecode") == 0) {
      if ((i + 1) >= argc) {
        std::cerr << "arg is missing for --timecode flag.\n";
        return -1;
      }
      timecode = std::stod(argv[i + 1]);
      std::cout << "Use timecode: " << timecode << "\n";
      i++;
    } else if (strcmp(argv[i], "--yaml") == 0) {
      output_format = "yaml";
    } else if (strcmp(argv[i], "--json") == 0) {
      output_format = "json";
    } else {
      filepath = argv[i];
    }
  }

  std::string warn;
  std::string err;

  tinyusdz::Stage stage;

  if (!tinyusdz::IsUSD(filepath)) {
    std::cerr << "File not found or not a USD format: " << filepath << "\n";
  }

  bool is_usdz = tinyusdz::IsUSDZ(filepath);

  // Collect config info for formatted output
  std::vector<std::pair<std::string, std::string>> config_info;

  // Use mmap if available to save memory (avoids copying entire file)
  tinyusdz::io::MMapFileHandle mmap_handle;
  bool using_mmap = false;
  bool ret = false;

  if (tinyusdz::io::IsMMapSupported()) {
    config_info.push_back({"loading_method", "mmap"});
    if (!tinyusdz::io::MMapFile(filepath, &mmap_handle, /* writable */false, &err)) {
      std::cerr << "Failed to mmap USD file: " << err << "\n";
      return EXIT_FAILURE;
    }
    using_mmap = true;

    // Load USD from mmap'd memory
    ret = tinyusdz::LoadUSDFromMemory(mmap_handle.addr, mmap_handle.size,
                                       filepath, &stage, &warn, &err);
  } else {
    // Fallback to file-based loading
    config_info.push_back({"loading_method", "file"});
    ret = tinyusdz::LoadUSDFromFile(filepath, &stage, &warn, &err);
  }

  if (!warn.empty()) {
    std::cerr << "WARN : " << warn << "\n";
  }

  if (!err.empty()) {
    std::cerr << "ERR : " << err << "\n";
  }

  if (!ret) {
    std::cerr << "Failed to load USD file: " << filepath << "\n";
    if (using_mmap) {
      tinyusdz::io::UnmapFile(mmap_handle, &err);
    }
    return EXIT_FAILURE;
  }

  if (usdprint) {
    std::string s = stage.ExportToString();
    std::cout << s << "\n";
    std::cout << "--------------------------------------"
              << "\n";
  }

  // RenderScene: Scene graph object which is suited for GL/Vulkan renderer
  tinyusdz::tydra::RenderScene render_scene;
  tinyusdz::tydra::RenderSceneConverter converter;
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);

  config_info.push_back({"input_file", filepath});
  config_info.push_back({"is_usdz", is_usdz ? "true" : "false"});
  config_info.push_back({"output_format", output_format});
  config_info.push_back({"triangulate", triangulate ? "true" : "false"});
  env.mesh_config.triangulate = triangulate;
  if (use_triangle_fan) {
    config_info.push_back({"triangulation_method", "TriangleFan"});
    env.mesh_config.triangulation_method = tinyusdz::tydra::MeshConverterConfig::TriangulationMethod::TriangleFan;
  } else {
    config_info.push_back({"triangulation_method", "Earcut"});
    env.mesh_config.triangulation_method = tinyusdz::tydra::MeshConverterConfig::TriangulationMethod::Earcut;
  }
  config_info.push_back({"build_vertex_indices", build_indices ? "true" : "false"});
  env.mesh_config.build_vertex_indices = build_indices;

  if (no_tangent) {
    env.mesh_config.compute_tangents_and_binormals = false;
    config_info.push_back({"compute_tangents", "false"});
  } else {
    config_info.push_back({"compute_tangents", "true"});
  }

  if (force_tangent) {
    // Force tangent computation even without a normal map texture.
    env.mesh_config.compute_tangents_only_with_normal_map = false;
    config_info.push_back({"force_tangent", "true"});
  } else {
    config_info.push_back({"force_tangent", "false"});
  }

  env.mesh_config.tangent_method = tangent_method;
  {
    const char *method_names[] = {"lengyel", "mikktspace", "fast-mikktspace"};
    config_info.push_back({"tangent_method", method_names[int(tangent_method)]});
  }

  config_info.push_back({"load_texture_data", texload ? "true" : "false"});
  env.scene_config.load_texture_assets = texload;

  // Add base directory of .usd file to search path.
  std::string usd_basedir = tinyusdz::io::GetBaseDir(filepath);
  config_info.push_back({"search_path", usd_basedir});

  tinyusdz::USDZAsset usdz_asset;

  if (is_usdz) {
    // Setup AssetResolutionResolver to read assets from USDZ container.
    // Reuse the mmap handle if already mmap'd, otherwise fall back to file-based.
    if (using_mmap) {
      // Use ReadUSDZAssetInfoFromMemory with asset_on_memory=true
      // This avoids copying the USDZ data, just references the mmap'd address
      if (!tinyusdz::ReadUSDZAssetInfoFromMemory(
            mmap_handle.addr, mmap_handle.size,
            /* asset_on_memory */ true,
            &usdz_asset, &warn, &err)) {
        std::cerr << "Failed to read USDZ assetInfo from memory: " << err << "\n";
        tinyusdz::io::UnmapFile(mmap_handle, &err);
        return EXIT_FAILURE;
      }
    } else {
      // Fallback to file-based loading (copies entire file into memory)
      if (!tinyusdz::ReadUSDZAssetInfoFromFile(filepath, &usdz_asset, &warn,
                                               &err)) {
        std::cerr << "Failed to read USDZ assetInfo from file: " << err << "\n";
        return EXIT_FAILURE;
      }
    }
    if (warn.size()) {
      std::cout << warn << "\n";
    }

    tinyusdz::AssetResolutionResolver arr;

    if (no_assetresolver) {
      SetupNullAssetResolution(arr);
      config_info.push_back({"asset_resolver", "null"});
    } else {
      // NOTE: Pointer address of usdz_asset must be valid until the call of
      // RenderSceneConverter::ConvertToRenderScene.
      if (!tinyusdz::SetupUSDZAssetResolution(arr, &usdz_asset)) {
        std::cerr << "Failed to setup AssetResolution for USDZ asset\n";
        exit(-1);
      };
      config_info.push_back({"asset_resolver", "usdz"});
    }

    env.asset_resolver = arr;

  } else {
    env.set_search_paths({usd_basedir});

    if (no_assetresolver) {
      SetupNullAssetResolution(env.asset_resolver);
      config_info.push_back({"asset_resolver", "null"});
    } else {
      config_info.push_back({"asset_resolver", "default"});
    }
  }

  if (!tinyusdz::value::TimeCode(timecode).is_default()) {
    config_info.push_back({"timecode", std::to_string(timecode)});
  } else {
    config_info.push_back({"timecode", "default"});
  }
  env.timecode = timecode;
  ret = converter.ConvertToRenderScene(env, &render_scene);
  if (!ret) {
    std::cerr << "Failed to convert USD Stage to RenderScene: \n"
              << converter.GetError() << "\n";
    return EXIT_FAILURE;
  }

  std::string converter_warn = converter.GetWarning();
  if (!converter_warn.empty()) {
    config_info.push_back({"converter_warning", converter_warn});
  }

  // Dump animation timesamples if requested
  if (dump_timesamples) {
    DumpAnimationTimesamples(render_scene);
  }

  // Helper to escape value for single-line output
  auto escape_for_comment = [](const std::string &s) -> std::string {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
      if (c == '\n') {
        result += "\\n";
      } else if (c == '\r') {
        result += "\\r";
      } else if (c == '\t') {
        result += "\\t";
      } else {
        result += c;
      }
    }
    return result;
  };

  if (!no_dump) {
    // Output config info in appropriate format
    if (output_format == "yaml") {
      // YAML: Output as comments
      std::cout << "# TinyUSDZ tydra_to_renderscene Configuration\n";
      std::cout << "# ==========================================\n";
      for (const auto &kv : config_info) {
        std::cout << "# " << kv.first << ": " << escape_for_comment(kv.second) << "\n";
      }
      std::cout << "#\n";
    } else if (output_format == "json") {
      // JSON: Output config as a separate JSON object before main output
      std::cout << "// TinyUSDZ tydra_to_renderscene Configuration\n";
      std::cout << "// config: {\n";
      for (size_t i = 0; i < config_info.size(); i++) {
        std::cout << "//   \"" << config_info[i].first << "\": \"" << escape_for_comment(config_info[i].second) << "\"";
        if (i < config_info.size() - 1) std::cout << ",";
        std::cout << "\n";
      }
      std::cout << "// }\n";
    } else {
      // KDL or other: output as comments
      for (const auto &kv : config_info) {
        std::cout << "// " << kv.first << ": " << escape_for_comment(kv.second) << "\n";
      }
    }

    std::cout << DumpRenderScene(render_scene, output_format) << "\n";
  }

  if (export_obj) {
    std::cout << "Dump RenderMesh as wavefront .obj\n";
    for (size_t i = 0; i < render_scene.meshes.size(); i++) {
      std::string obj_str;
      std::string mtl_str;
      if (!tinyusdz::tydra::export_to_obj(render_scene, i, obj_str, mtl_str,
                                          &warn, &err)) {
        std::cerr << "obj export error: " << err << "\n";
        exit(-1);
      }

      std::string obj_filename =
          std::to_string(i) + render_scene.meshes[i].prim_name + ".obj";
      std::string mtl_filename =
          std::to_string(i) + render_scene.meshes[i].prim_name + ".mtl";
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
    std::string ext = tinyusdz::io::GetFileExtension(filepath);
    std::string usd_basename = tinyusdz::io::GetBaseFilename(filepath);
    std::string usd_filename =
        tinyusdz::removeSuffix(usd_basename, ext) + "export.usda";

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

  // Cleanup mmap if used
  if (using_mmap) {
    std::string unmap_err;
    if (!tinyusdz::io::UnmapFile(mmap_handle, &unmap_err)) {
      std::cerr << "WARN: Failed to unmap file: " << unmap_err << "\n";
    }
  }

  return EXIT_SUCCESS;
}
