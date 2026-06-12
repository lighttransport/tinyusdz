// SPDX-License-Identifier: Apache-2.0
#include "scene_loader.hh"

#include <cctype>
#include <chrono>

#include "composition.hh"
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

bool HasUsdzExtension(const std::string& path) {
  if (path.size() < 5) return false;
  std::string ext = path.substr(path.size() - 5);
  for (char& c : ext) c = static_cast<char>(std::tolower(c));
  return ext == ".usdz";
}

bool Cancelled(LoadControl* ctrl) { return ctrl && ctrl->cancel.load(); }

// True when the layer carries no composition arcs, so plain LayerToStage (or
// the legacy direct Stage load) yields the same result as full composition.
bool LayerHasCompositionArcs(const tinyusdz::Layer& layer) {
  return !layer.metas().subLayers.empty() ||
         layer.check_unresolved_references() ||
         layer.check_unresolved_payload() || layer.check_unresolved_inherits() ||
         layer.check_unresolved_variant() ||
         layer.check_unresolved_specializes();
}

// Compose `src` (post-sublayer) to a fixed point in LIVRPS order, honoring the
// payload policy in `opts`. Deferred payloads are appended to `deferred`.
// Pattern follows ComposeLayerToFixedPoint() in src/usdz-convert.cc, plus the
// PayloadCompositionOptions::load_policy hook for lazy payloads.
bool ComposeToFixedPoint(tinyusdz::AssetResolutionResolver& resolver,
                         tinyusdz::Layer&& src, const LoadOptions& opts,
                         tinyusdz::Layer* composed,
                         std::vector<DeferredArc>* deferred,
                         std::string* warn, std::string* err,
                         LoadControl* ctrl) {
  // One whitelist drives both arc types: a whitelisted prim path loads its
  // payloads and (when deferred) its references.
  const std::set<std::string>* whitelist =
      (opts.payloadPolicy == PayloadPolicy::Whitelist) ? &opts.payloadWhitelist
                                                       : nullptr;

  tinyusdz::PayloadCompositionOptions pl_opts;
  if (opts.payloadPolicy != PayloadPolicy::LoadAll) {
    pl_opts.load_policy = [whitelist, deferred](
                              const tinyusdz::Path& prim_path,
                              const tinyusdz::Payload& payload) -> bool {
      const std::string p = prim_path.full_path_name();
      if (whitelist && whitelist->count(p)) {
        return true;
      }
      deferred->push_back({p, payload.asset_path.GetAssetPath(), "payload"});
      return false;
    };
  }

  tinyusdz::ReferencesCompositionOptions ref_opts;
  if (opts.deferReferences) {
    ref_opts.load_policy = [whitelist, deferred](
                               const tinyusdz::Path& prim_path,
                               const tinyusdz::Reference& reference) -> bool {
      const std::string p = prim_path.full_path_name();
      if (whitelist && whitelist->count(p)) {
        return true;
      }
      deferred->push_back({p, reference.asset_path.GetAssetPath(), "reference"});
      return false;
    };
  }

  tinyusdz::Layer work = std::move(src);

  constexpr int kMaxIteration = 64;
  for (int i = 0; i < kMaxIteration; i++) {
    if (Cancelled(ctrl)) {
      if (err) *err = "Load cancelled.";
      return false;
    }

    bool has_unresolved = false;

    if (work.check_unresolved_references()) {
      has_unresolved = true;
      tinyusdz::Layer tmp;
      if (!tinyusdz::CompositeReferences(resolver, work, &tmp, warn, err,
                                         ref_opts)) {
        return false;
      }
      work = std::move(tmp);
    }

    if (work.check_unresolved_payload()) {
      has_unresolved = true;
      tinyusdz::Layer tmp;
      if (!tinyusdz::CompositePayload(resolver, work, &tmp, warn, err,
                                      pl_opts)) {
        return false;
      }
      work = std::move(tmp);
    }

    if (work.check_unresolved_inherits()) {
      has_unresolved = true;
      tinyusdz::Layer tmp;
      if (!tinyusdz::CompositeInherits(work, &tmp, warn, err)) {
        return false;
      }
      work = std::move(tmp);
    }

    if (work.check_unresolved_variant()) {
      has_unresolved = true;
      tinyusdz::Layer tmp;
      if (!tinyusdz::CompositeVariant(work, &tmp, warn, err)) {
        return false;
      }
      work = std::move(tmp);
    }

    if (work.check_unresolved_specializes()) {
      has_unresolved = true;
      tinyusdz::Layer tmp;
      if (!tinyusdz::CompositeSpecializes(work, &tmp, warn, err)) {
        return false;
      }
      work = std::move(tmp);
    }

    if (!has_unresolved) {
      *composed = std::move(work);
      return true;
    }
  }

  if (err) {
    (*err) += "Composition did not converge before the iteration limit.";
  }
  return false;
}

// Build the Stage from `out->comp.rootLayer` (post-sublayer snapshot) with the
// given payload policy, refreshing comp.deferred/loadedPayloads.
bool ComposeStage(const LoadOptions& opts, LoadedScene* out,
                  LoadControl* ctrl) {
  tinyusdz::AssetResolutionResolver resolver;
  resolver.set_search_paths(out->comp.searchPaths);

  tinyusdz::Layer work = *out->comp.rootLayer;  // compose from a copy

  out->comp.deferred.clear();
  tinyusdz::Layer composed;
  if (!ComposeToFixedPoint(resolver, std::move(work), opts, &composed,
                           &out->comp.deferred, &out->warn, &out->err, ctrl)) {
    return false;
  }
  out->comp.loadedPayloads = (opts.payloadPolicy == PayloadPolicy::Whitelist)
                                 ? opts.payloadWhitelist
                                 : std::set<std::string>{};
  out->comp.composed = true;

  return tinyusdz::LayerToStage(std::move(composed), &out->stage, &out->warn,
                                &out->err);
}

// Acquire `out->stage` without composition: mmap zero-copy load of the root
// layer only (legacy path; also used for .usdz).
bool LoadStageDirect(const std::string& path, LoadedScene* out) {
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
  if (!ok && out->err.empty()) {
    out->err = "Failed to load USD file: " + path;
  }
  return ok;
}

// Acquire `out->stage` with composition (non-.usdz). Falls back to plain
// LayerToStage when the file has no composition arcs. Retains the
// post-sublayer layer in out->comp for later payload recompose.
bool LoadStageComposed(const std::string& path, const LoadOptions& opts,
                       LoadedScene* out, LoadControl* ctrl) {
  tinyusdz::Layer root;
  if (!tinyusdz::LoadLayerFromFile(path, &root, &out->warn, &out->err)) {
    if (out->err.empty()) {
      out->err = "Failed to load USD layer: " + path;
    }
    return false;
  }

  if (Cancelled(ctrl)) {
    out->err = "Load cancelled.";
    return false;
  }

  if (!LayerHasCompositionArcs(root)) {
    return tinyusdz::LayerToStage(std::move(root), &out->stage, &out->warn,
                                  &out->err);
  }

  out->comp.searchPaths = {DirName(path)};

  tinyusdz::AssetResolutionResolver resolver;
  resolver.set_search_paths(out->comp.searchPaths);

  // Flatten sublayers first, then snapshot: payload recompose restarts from
  // this layer (CompositePayload strips payload metadata even for deferred
  // arcs, so the composed result alone cannot load payloads later).
  if (!root.metas().subLayers.empty()) {
    tinyusdz::Layer tmp;
    if (!tinyusdz::CompositeSublayers(resolver, root, &tmp, &out->warn,
                                      &out->err)) {
      return false;
    }
    root = std::move(tmp);
  }
  out->comp.rootLayer = std::make_shared<tinyusdz::Layer>(std::move(root));

  return ComposeStage(opts, out, ctrl);
}

// Convert out->stage to RenderScene + DrawScene (shared by load and
// recompose). Returns false with *err set on failure. Reads `stage` (const) +
// `mmap`/`path` for USDZ asset resolution; writes geometry to `draw` and the
// full RenderScene to `render`. `timecode` selects the evaluation time (NaN =
// static default). `loadTextures=false` skips texture image decode (used by
// playback re-evaluation, which keeps the initial load's textures).
bool ConvertStageToSceneImpl(const tinyusdz::Stage& stage,
                             const std::string& path,
                             const std::shared_ptr<tinyusdz::io::MMapFileHandle>& mmap,
                             double timecode, bool rtPath, bool loadTextures,
                             tinyusdz::tydra::RenderScene* render, DrawScene* draw,
                             std::string* warn, std::string* err,
                             LoadControl* ctrl) {
  tinyusdz::tydra::RenderSceneConverterEnv env(stage);
  env.usd_filename = path;
  env.set_search_paths({DirName(path)});
  env.timecode = timecode;
  env.scene_config.load_texture_assets = loadTextures;

  // USDZ assets (textures, audio, ...) live *inside* the .usdz archive. Register
  // the archive's internal asset map with the resolver so embedded textures
  // resolve; without this only assets that happen to exist on disk next to the
  // .usdz would load. `usdzAsset` must outlive the RenderScene conversion below
  // (the resolver retains a pointer to it); it is a local here, and conversion
  // happens before this function returns.
  tinyusdz::USDZAsset usdzAsset;
  if (loadTextures && HasUsdzExtension(path)) {
    std::string uwarn, uerr;
    bool gotInfo = false;
    if (mmap) {
      // Zero-copy: reference the mmap (kept alive by the caller through the
      // conversion below).
      gotInfo = tinyusdz::ReadUSDZAssetInfoFromMemory(
          mmap->addr, static_cast<size_t>(mmap->size),
          /*asset_on_memory=*/true, &usdzAsset, &uwarn, &uerr);
    } else {
      gotInfo = tinyusdz::ReadUSDZAssetInfoFromFile(path, &usdzAsset, &uwarn, &uerr);
    }
    if (gotInfo) {
      if (!tinyusdz::SetupUSDZAssetResolution(env.asset_resolver, &usdzAsset)) {
        *warn += "Failed to set up USDZ asset resolution; embedded textures "
                 "may not load.\n";
      }
    } else {
      *warn += "Failed to read USDZ asset info" +
               (uerr.empty() ? std::string() : ": " + uerr) + "\n";
    }
  }

  // Configure for a single-index OpenGL/Vulkan renderer.
  auto& mc = env.mesh_config;
  mc.triangulate = true;
  // Ray tracing only needs triangulated positions + a uint32 index buffer for the
  // BLAS, so skip the rasterization-only single-index dedup on the RT path.
  mc.build_vertex_indices = !rtPath;
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
  // are produced (and fully populates *render). Falls back to the monolithic
  // path if no DrawScene sink was requested.
  const bool converted =
      draw ? BuildDrawSceneStreaming(converter, env, render, draw, ctrl)
           : converter.ConvertToRenderScene(env, render);
  if (!converted) {
    if (ctrl && ctrl->cancel.load()) {
      *err = "Load cancelled.";
    } else if (budgetHit) {
      *err = "Aborted: conversion exceeded the time budget (scene too large).";
    } else {
      *err = converter.GetError();
    }
    if (!converter.GetWarning().empty()) {
      *warn += converter.GetWarning();
    }
    if (err->empty()) {
      *err = "RenderScene conversion failed for: " + path;
    }
    return false;
  }
  if (!converter.GetWarning().empty()) {
    *warn += converter.GetWarning();
  }
  return true;
}

// Convert out->stage to RenderScene + DrawScene at `timecode` (out->stage,
// out->mmap, out->render, out->warn/err are the load targets).
bool ConvertStageToScene(const std::string& path, double timecode,
                         LoadedScene* out, DrawScene* draw, bool rtPath,
                         LoadControl* ctrl) {
  if (!ConvertStageToSceneImpl(out->stage, path, out->mmap, timecode, rtPath,
                               /*loadTextures=*/true, &out->render, draw,
                               &out->warn, &out->err, ctrl)) {
    return false;
  }
  out->ok = true;
  return true;
}

}  // namespace

bool LoadUSD(const std::string& path, const LoadOptions& opts, LoadedScene* out,
             DrawScene* draw, bool rtPath, LoadControl* ctrl) {
  out->filepath = path;
  out->ok = false;
  out->warn.clear();
  out->err.clear();
  if (draw) *draw = DrawScene{};

  if (Cancelled(ctrl)) {
    out->err = "Load cancelled.";
    return false;
  }

  // .usdz archives keep the direct (non-composition) path for now: composing
  // over archive-internal layers needs LoadLayerFromAsset wiring.
  const bool compose = opts.composition && !HasUsdzExtension(path);

  bool ok = false;
  if (compose) {
    ok = LoadStageComposed(path, opts, out, ctrl);
  } else {
    ok = LoadStageDirect(path, out);
  }
  if (!ok) {
    return false;
  }

  return ConvertStageToScene(path, opts.timecode, out, draw, rtPath, ctrl);
}

bool RecomposeWithPayloads(const std::string& path, const CompositionInfo& prev,
                           const LoadOptions& opts, LoadedScene* out,
                           DrawScene* draw, bool rtPath, LoadControl* ctrl) {
  out->filepath = path;
  out->ok = false;
  out->warn.clear();
  out->err.clear();
  if (draw) *draw = DrawScene{};

  if (!prev.rootLayer) {
    out->err = "No retained composition layer to recompose from.";
    return false;
  }

  if (Cancelled(ctrl)) {
    out->err = "Load cancelled.";
    return false;
  }

  out->comp.rootLayer = prev.rootLayer;
  out->comp.searchPaths = prev.searchPaths;

  if (!ComposeStage(opts, out, ctrl)) {
    return false;
  }

  return ConvertStageToScene(path, opts.timecode, out, draw, rtPath, ctrl);
}

bool RenderSceneAtTime(const LoadedScene& src, double timecode, bool rtPath,
                       DrawScene* draw, std::string* warn, std::string* err,
                       LoadControl* ctrl) {
  if (draw) *draw = DrawScene{};
  // Discard the rebuilt RenderScene (geometry flows through `draw`); textures
  // are not re-decoded (loadTextures=false), so the caller keeps the initial
  // load's textures/materials.
  tinyusdz::tydra::RenderScene scratch;
  return ConvertStageToSceneImpl(src.stage, src.filepath, src.mmap, timecode,
                                 rtPath, /*loadTextures=*/false, &scratch, draw,
                                 warn, err, ctrl);
}

}  // namespace tusdview
