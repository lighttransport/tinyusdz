// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// large-scene-loader.cc - see large-scene-loader.hh.

#include "large-scene-loader.hh"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>

#include "asset-resolution.hh"
#include "composition.hh"
#include "composition-graph.hh"
#include "core/composition-types.hh"
#include "io-util.hh"
#include "layer.hh"
#include "pcp/layer-registry.hh"
#include "tinyusdz.hh"

namespace tinyusdz {

namespace {

// Cheap file size without reading the content (used by the Budget policy).
// Returns 0 on failure (treated as "free").
uint64_t FileSizeBytes(const std::string &path) {
  if (path.empty()) return 0;
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if (!ifs) return 0;
  const std::streamoff pos = ifs.tellg();
  return (pos > 0) ? static_cast<uint64_t>(pos) : 0;
}

// Shared state for PayloadMode::Budget. Kept alive by the closure captured into
// CompositionGraphOptions::payload_policy (owned by the graph).
struct BudgetPolicyState {
  std::atomic<uint64_t> loaded_bytes{0};
  uint64_t budget_bytes{0};
  AssetResolutionResolver *resolver{nullptr};
  std::mutex mu;  // serializes resolver re-entrancy (future threaded Compose)
};

std::function<bool(const Path &, const Payload &)> MakePayloadPolicy(
    const LargeSceneLoadOptions &opts, AssetResolutionResolver *resolver) {
  switch (opts.payload_mode) {
    case LargeSceneLoadOptions::PayloadMode::LoadAll:
      return nullptr;  // null => eager load (CompositionGraph default).
    case LargeSceneLoadOptions::PayloadMode::LoadNone:
      return [](const Path &, const Payload &) { return false; };
    case LargeSceneLoadOptions::PayloadMode::Budget: {
      auto st = std::make_shared<BudgetPolicyState>();
      st->budget_bytes = opts.payload_budget_mb * 1024ull * 1024ull;
      st->resolver = resolver;
      return [st](const Path &, const Payload &pl) -> bool {
        const std::string ap = pl.asset_path.GetAssetPath();
        uint64_t sz = 0;
        {
          std::lock_guard<std::mutex> lk(st->mu);
          const std::string rp = st->resolver->resolve(ap);
          sz = FileSizeBytes(rp);
        }
        const uint64_t prev = st->loaded_bytes.fetch_add(sz);
        if (prev + sz > st->budget_bytes) {
          st->loaded_bytes.fetch_sub(sz);
          return false;  // over budget => defer
        }
        return true;
      };
    }
  }
  return [](const Path &, const Payload &) { return false; };
}

}  // namespace

LargeSceneLoader::LargeSceneLoader() = default;
LargeSceneLoader::~LargeSceneLoader() = default;

const Layer *LargeSceneLoader::LoadLayerThunk(void *userdata,
                                              const std::string &asset_path,
                                              const std::string &cwp,
                                              std::string *warn,
                                              std::string *err) {
  auto *self = static_cast<LargeSceneLoader *>(userdata);
  if (!self || !self->_registry || !self->_resolver) return nullptr;
  return self->_registry->GetOrLoad(*self->_resolver, asset_path, cwp, warn,
                                    err);
}

bool LargeSceneLoader::Load(const std::string &filename,
                            const LargeSceneLoadOptions &options,
                            std::string *warn, std::string *err) {
  _loaded = false;

  // 1. Resolver anchored at the scene's base directory.
  _resolver = std::make_unique<AssetResolutionResolver>();
  const std::string base_dir = io::GetBaseDir(filename);
  _resolver->set_current_working_path(base_dir.empty() ? "./" : base_dir);
  std::vector<std::string> search = {base_dir.empty() ? "./" : base_dir};
  for (const auto &p : options.search_paths) search.push_back(p);
  _resolver->set_search_paths(search);
  _resolver->set_max_asset_bytes_in_mb(options.max_asset_bytes_mb);

  // 2. Load the root layer.
  Layer root_layer;
  {
    USDLoadOptions lopts;
    lopts.max_allowed_asset_size_in_mb = static_cast<uint32_t>(
        std::min<size_t>(options.max_asset_bytes_mb, 0xffffffffu));
    if (!LoadLayerFromFile(filename, &root_layer, warn, err, lopts)) {
      return false;
    }
  }

  // 3. Composite subLayers (L phase); CompositionGraph expects the sublayers
  //    already merged into the root layer.
  _flattened = std::make_unique<Layer>();
  {
    SublayersCompositionOptions subopts;
    subopts.allow_parent_relative_paths = options.allow_parent_relative_paths;
    subopts.max_depth = options.max_composition_depth;
    if (!CompositeSublayers(*_resolver, root_layer, _flattened.get(), warn, err,
                            subopts)) {
      return false;
    }
  }

  // 3b. Resolve authored variant selections, flattening the selected variant's
  // CONTENT (child prims + opinions) into the prim tree. CompositionGraph records
  // variant selections but does not compose variant content, so variant-gated
  // child prims (and their nested references/payloads/clips) would otherwise be
  // dropped (e.g. ALab baked_procedurals' GEO behind `alfro=render`). Reuses the
  // LIVRPS variant resolver. This resolves variants whose set is defined in the
  // root layer stack (root + subLayers); variant sets introduced via a reference
  // are resolved later, during composition, by the existing mechanisms.
  {
    VariantSelectorMap vsmap;
    ListVariantSelectionMaps(*_flattened, vsmap);
    if (!vsmap.empty()) {
      Layer variant_resolved;
      std::string vw, ve;
      if (ApplyVariantSelector(*_flattened, vsmap, &variant_resolved, &vw, &ve)) {
        *_flattened = std::move(variant_resolved);
      } else if (warn && !ve.empty()) {
        (*warn) += ve;
      }
      if (warn && !vw.empty()) (*warn) += vw;
    }
  }

  // 4. Parse-once registry for referenced/payload layers.
  if (options.dedup_layers) {
    _registry = std::make_unique<pcp::LayerRegistry>();
  }

  // 5. Build the composition graph (R/V/P phases) with the payload policy.
  CompositionGraphOptions cgopts;
  cgopts.payload_policy = MakePayloadPolicy(options, _resolver.get());
  cgopts.detect_instances = options.detect_instances;
  cgopts.allow_parent_relative_paths = options.allow_parent_relative_paths;
  cgopts.max_depth = options.max_composition_depth;
  if (_registry) {
    cgopts.load_layer_fn = &LargeSceneLoader::LoadLayerThunk;
    cgopts.load_layer_userdata = this;
  }

  auto r = CompositionGraph::Compose(*_resolver, *_flattened, cgopts);
  if (!r) {
    if (err) *err = "CompositionGraph::Compose failed: " + r.error();
    return false;
  }
  _graph = std::make_unique<CompositionGraph>(std::move(*r));

  // 6. Reconstruct the Stage from the graph.
  if (!_graph->BuildStage(&_stage, warn, err)) {
    return false;
  }

  _loaded = true;
  return true;
}

std::vector<Path> LargeSceneLoader::deferred_payload_paths() const {
  if (!_graph) return {};
  return _graph->GetDeferredPayloadPaths();
}

size_t LargeSceneLoader::deferred_count() const {
  return deferred_payload_paths().size();
}

bool LargeSceneLoader::load_payload(const Path &prim_path, std::string *warn,
                                    std::string *err) {
  (void)warn;
  if (!_graph || !_resolver) {
    if (err) *err = "LargeSceneLoader: not loaded.";
    return false;
  }
  auto r = _graph->LoadPayload(prim_path, *_resolver);
  if (!r) {
    if (err) *err = r.error();
    return false;
  }
  return true;
}

bool LargeSceneLoader::unload_payload(const Path &prim_path, std::string *warn,
                                      std::string *err) {
  (void)warn;
  if (!_graph) {
    if (err) *err = "LargeSceneLoader: not loaded.";
    return false;
  }
  auto r = _graph->UnloadPayload(prim_path);
  if (!r) {
    if (err) *err = r.error();
    return false;
  }
  return true;
}

bool LargeSceneLoader::rebuild_stage(std::string *warn, std::string *err) {
  if (!_graph) {
    if (err) *err = "LargeSceneLoader: not loaded.";
    return false;
  }
  Stage rebuilt;
  if (!_graph->BuildStage(&rebuilt, warn, err)) {
    return false;
  }
  _stage = std::move(rebuilt);
  return true;
}

size_t LargeSceneLoader::estimate_stage_memory_bytes() const {
  return _stage.estimate_memory_usage();
}

size_t LargeSceneLoader::layer_parse_count() const {
  return _registry ? _registry->parse_count() : 0;
}

bool LoadStageFromFile(const std::string &filename, Stage *stage,
                       std::string *warn, std::string *err,
                       const LargeSceneLoadOptions &options) {
  if (!stage) {
    if (err) *err = "LoadStageFromFile: null stage.";
    return false;
  }
  LargeSceneLoader loader;
  if (!loader.Load(filename, options, warn, err)) {
    return false;
  }
  (*stage) = std::move(loader.stage());
  return true;
}

}  // namespace tinyusdz
