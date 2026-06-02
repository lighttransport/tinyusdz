// SPDX-License-Identifier: Apache-2.0
#include "scene_loader.hh"

#include <chrono>

#include "mesh_build.hh"
#include "tinyusdz.hh"
#include "tydra/render-data-converter.hh"

namespace tusdview {

namespace {

std::string DirName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) {
    return ".";
  }
  if (slash == 0) {
    return path.substr(0, 1);
  }
  return path.substr(0, slash);
}

}  // namespace

bool LoadUSD(const std::string& path, LoadedScene* out, DrawScene* draw,
             LoadControl* ctrl) {
  out->filepath = path;
  out->ok = false;
  out->warn.clear();
  out->err.clear();
  if (draw) *draw = DrawScene{};

  if (ctrl && ctrl->cancel.load()) {
    out->err = "Load cancelled.";
    return false;
  }

  // mmap-based load: memory-map the file ourselves and parse from it. We keep
  // the mapping alive in LoadedScene so zero-copy USDC arrays (which reference
  // offsets into the mapping) stay valid for the Stage's lifetime.
  tinyusdz::USDLoadOptions opts;
  opts.mmap_zero_copy = true;

  auto mh = std::shared_ptr<tinyusdz::io::MMapFileHandle>(
      new tinyusdz::io::MMapFileHandle(),
      [](tinyusdz::io::MMapFileHandle* h) {
        std::string e;
        tinyusdz::io::UnmapFile(*h, &e);
        delete h;
      });
  std::string merr;
  const bool mapped = tinyusdz::io::MMapFile(path, mh.get(), /*writable=*/false, &merr) &&
                      mh->addr && mh->size > 0;

  bool ok = false;
  if (mapped) {
    out->mmap = mh;  // keep the mapping alive alongside the Stage
    ok = tinyusdz::LoadUSDFromMemory(mh->addr, static_cast<size_t>(mh->size), path,
                                     &out->stage, &out->warn, &out->err, opts);
  } else {
    // Fall back to a regular (copying) file load. Disable zero-copy: it requires
    // a persistent mapping which we don't have on this path.
    opts.mmap_zero_copy = false;
    ok = tinyusdz::LoadUSDFromFile(path, &out->stage, &out->warn, &out->err, opts);
  }
  if (!ok) {
    if (out->err.empty()) {
      out->err = "Failed to load USD file: " + path;
    }
    return false;
  }

  tinyusdz::tydra::RenderSceneConverterEnv env(out->stage);
  env.usd_filename = path;
  env.set_search_paths({DirName(path)});

  // Configure for a single-index OpenGL/Vulkan renderer.
  auto& mc = env.mesh_config;
  mc.triangulate = true;
  mc.build_vertex_indices = true;
  mc.compute_normals = true;
  // Keep normals as plain float3 (native default) and skip tangents: the simple
  // light3d shaders don't read tangents, and the native tangent default is a
  // packed fp16 format we don't want to decode.
  mc.normal_storage =
      tinyusdz::tydra::MeshConverterConfig::NormalStorageFormat::Float3;
  mc.compute_tangents_and_binormals = false;

  // Keep texels 8-bit (avoids float-image conversion) and let UDIM collapse to
  // an atlas so the renderer never sees a raw UDIM texture.
  auto& matc = env.material_config;
  matc.preserve_texel_bitdepth = true;
  matc.linearize_color_space = false;
  matc.combine_udim_tiles = true;
  // Graceful skip for missing/failed textures (these are already defaults).
  matc.allow_texture_load_failure = true;
  matc.allow_missing_asset = true;

  // RenderSceneConverter is non-copyable / non-movable: keep it local.
  tinyusdz::tydra::RenderSceneConverter converter;

  // Cancellation + progress + conversion time budget (worker-thread friendly).
  bool budgetHit = false;
  if (ctrl) {
    const auto start = std::chrono::steady_clock::now();
    converter.SetDetailedProgressCallback(
        [ctrl, start, &budgetHit](const tinyusdz::tydra::DetailedProgressInfo& info,
                                   void*) -> bool {
          ctrl->stage.store(static_cast<int>(info.stage));
          ctrl->meshesDone.store(static_cast<long long>(info.meshes_processed));
          ctrl->meshesTotal.store(static_cast<long long>(info.meshes_total));
          if (ctrl->cancel.load()) return false;
          if (ctrl->convertTimeBudgetSec > 0.0) {
            const double el =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                    .count();
            if (el > ctrl->convertTimeBudgetSec) {
              budgetHit = true;
              return false;
            }
          }
          return true;  // continue
        },
        nullptr);
  }

  // Streaming convert: builds `draw` incrementally as meshes/materials/textures
  // are produced (and fully populates out->render). Falls back to the monolithic
  // path if no DrawScene sink was requested.
  const bool converted =
      draw ? BuildDrawSceneStreaming(converter, env, &out->render, draw, ctrl)
           : converter.ConvertToRenderScene(env, &out->render);
  if (!converted) {
    if (ctrl && ctrl->cancel.load()) {
      out->err = "Load cancelled.";
    } else if (budgetHit) {
      out->err = "Aborted: conversion exceeded the time budget (scene too large).";
    } else {
      out->err = converter.GetError();
    }
    if (!converter.GetWarning().empty()) {
      out->warn += converter.GetWarning();
    }
    if (out->err.empty()) {
      out->err = "RenderScene conversion failed for: " + path;
    }
    return false;
  }
  if (!converter.GetWarning().empty()) {
    out->warn += converter.GetWarning();
  }

  out->ok = true;
  return true;
}

}  // namespace tusdview
