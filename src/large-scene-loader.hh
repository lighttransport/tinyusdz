// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// large-scene-loader.hh - Memory-bounded loading of very large USD scenes.
//
// Production scenes (Disney Moana Island, Animal Logic ALab, Activision
// Caldera) are 10-20 GB on disk across thousands of payloaded files. Loading
// them within a fixed RAM budget requires (a) deferring geometry payloads,
// (b) parsing each referenced file exactly once, and (c) tolerating
// '..'-relative asset paths. This class wires the existing primitives
// (LoadLayerFromFile, CompositeSublayersInPlace, CompositionGraph with its
// lazy-payload API, and pcp::LayerRegistry for parse-once) into one entry point
// and keeps the composition graph alive so geometry payloads can be streamed in
// on demand after the initial (light) load.
//
// See doc/large-scene.md for the scene analyses and the overall strategy.
//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "composition-graph.hh"  // CompositionGraph (+ Layer, AssetResolutionResolver)
#include "pcp/layer-registry.hh"  // pcp::LayerRegistry
#include "stage.hh"

namespace tinyusdz {

struct LargeSceneLoadOptions {
  // How payload arcs are handled during the initial composition.
  enum class PayloadMode {
    LoadAll,   // Eagerly load every payload (may exceed RAM on huge scenes).
    LoadNone,  // Defer every payload (lightest; geometry streamed later).
    Budget,    // Load payloads until `payload_budget_mb` of payload asset bytes
               // have been loaded, then defer the rest.
  };
  PayloadMode payload_mode{PayloadMode::LoadNone};

  // Byte budget (MB) of payload asset file sizes for PayloadMode::Budget.
  size_t payload_budget_mb{12288};

  // Allow '..'-relative reference/payload asset paths (required for scenes such
  // as Caldera). Resolution of the surviving '..' is delegated to the resolver.
  bool allow_parent_relative_paths{true};

  // Parse each referenced/payload file exactly once and share it (the single
  // biggest win for scenes with tens of thousands of files). Backed by
  // pcp::LayerRegistry.
  bool dedup_layers{true};

  // Detect scenegraph instances during composition.
  bool detect_instances{true};

  // Extra resolver search roots (the scene's base directory is always added).
  std::vector<std::string> search_paths;

  // Upper bound on a single asset read (MB). Large crate payloads need this
  // raised above the default.
  size_t max_asset_bytes_mb{8192};

  // Maximum number of concurrently open file descriptors/asset handles used by
  // the resolver during sublayer/reference/payload loading.
  uint32_t max_file_descriptors{1024};

  // Maximum composition recursion depth.
  uint32_t max_composition_depth{1024};
};

// Owns the Stage plus the composition graph, resolver, flattened root layer and
// layer registry that back it, so deferred payloads can be loaded/unloaded
// after Load() returns. Non-copyable and non-movable: a pointer to `this` is
// registered as the layer-loading seam userdata, so the object must not move.
class LargeSceneLoader {
 public:
  LargeSceneLoader();
  ~LargeSceneLoader();
  LargeSceneLoader(const LargeSceneLoader &) = delete;
  LargeSceneLoader &operator=(const LargeSceneLoader &) = delete;
  LargeSceneLoader(LargeSceneLoader &&) = delete;
  LargeSceneLoader &operator=(LargeSceneLoader &&) = delete;

  // Load + compose `filename` (USDA/USDC) under `options`. On success the Stage
  // is populated and (for LoadNone/Budget) some payloads are recorded as
  // deferred. Returns false with *err on failure.
  bool Load(const std::string &filename, const LargeSceneLoadOptions &options,
            std::string *warn, std::string *err);

  Stage &stage() { return _stage; }
  const Stage &stage() const { return _stage; }

  // -- Deferred (unloaded) payload streaming --

  // Prim paths whose payloads were deferred during the initial composition.
  std::vector<Path> deferred_payload_paths() const;
  size_t deferred_count() const;

  // Load / unload a single deferred payload, then call rebuild_stage() to
  // reflect the change in stage(). NOTE: single-threaded only (mutates the
  // graph in place). Returns false with *err on failure.
  bool load_payload(const Path &prim_path, std::string *warn, std::string *err);
  bool unload_payload(const Path &prim_path, std::string *warn,
                      std::string *err);

  // Rebuild stage() from the (possibly mutated) composition graph.
  bool rebuild_stage(std::string *warn, std::string *err);

  // -- Diagnostics --

  // Estimated Stage memory in bytes (composition structure; deferred geometry
  // is NOT counted because it is not in the Stage).
  size_t estimate_stage_memory_bytes() const;

  // Number of unique files actually parsed (cache misses) by the layer
  // registry. With dedup_layers, this is the count of distinct files.
  size_t layer_parse_count() const;

 private:
  // Layer-loading seam trampoline (matches CompositionLoadLayerFn).
  static const Layer *LoadLayerThunk(void *userdata,
                                     const std::string &asset_path,
                                     const std::string &cwp, std::string *warn,
                                     std::string *err);

  Stage _stage;
  std::unique_ptr<AssetResolutionResolver> _resolver;
  std::unique_ptr<pcp::LayerRegistry> _registry;
  std::unique_ptr<Layer> _flattened;  // composed root; outlives the graph
  std::unique_ptr<CompositionGraph> _graph;
  bool _loaded{false};
};

// One-shot convenience: load + compose into `stage`. Does NOT keep a handle for
// on-demand payload streaming (use LargeSceneLoader for that).
bool LoadStageFromFile(const std::string &filename, Stage *stage,
                       std::string *warn, std::string *err,
                       const LargeSceneLoadOptions &options = {});

}  // namespace tinyusdz
