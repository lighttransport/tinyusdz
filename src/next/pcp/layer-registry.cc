// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - PCP LayerRegistry implementation

#include "layer-registry.hh"
#include "../safe-file-size.hh"

#include "../layer/asset-anchor.hh"
#include "../layer/layer.hh"
#include "../reader/usda-reader.hh"
#include "../reader/usdc-reader.hh"
#include "../reader/usdz-reader.hh"
#include "../../mtlx-dom.hh"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>

namespace lightusd {
namespace next {
namespace pcp {

void LayerRegistry::RetainImportedAssets(Layer* output) const {
  if (!output) return;
#if defined(LIGHTUSD_ENABLE_THREAD)
  std::lock_guard<std::mutex> lock(*mu_);
#endif
  std::shared_ptr<ImportedAssets> merged;
  auto append = [&](const Layer* layer) {
    if (!layer || !layer->meta().imported_assets) return;
    if (!merged) merged = std::make_shared<ImportedAssets>();
    for (const auto& entry : layer->meta().imported_assets->by_identifier)
      merged->by_identifier.emplace(entry.first, entry.second);
  };
  append(output);
  for (const auto& entry : by_resolved_) append(entry.second.get());
  output->meta().imported_assets = std::move(merged);
}

std::shared_ptr<Layer> LayerRegistry::GetOrLoad(AssetResolver &resolver,
                                                const std::string &asset_path,
                                                const std::string &anchor,
                                                std::string *warn,
                                                std::string *err,
                                                const LayerLoadOptions &options) {
  ResolvedAsset resolved_asset = resolver.Resolve(
      asset_path, anchor, !options.strict_aousd_conformance);
  const std::string resolved = resolved_asset.resolved_path;
  if (resolved.empty()) {
    if (err) *err += "Failed to resolve asset path: " + asset_path + "\n";
    return nullptr;
  }
  const bool native_file = AssetResolver::GetIdentifierScheme(resolved).empty() &&
                           !resolver.HasAssetReader();
  LayerLoadOptions effective = options;
  // Keep dependent resources on the same resolver/context as their OBJ layer.
  if (!native_file) effective.asset_read = [&resolver](
      const std::string& name, const std::string& anchor_name,
      std::vector<uint8_t>* bytes, std::string* id, std::string* error) {
    auto asset = resolver.Resolve(name, anchor_name, false);
    *id = asset.resolved_path;
    return asset.exists && resolver.ReadAsset(asset, bytes, error);
  };
  auto load_resolved = [&](std::string* load_warn,
                           std::string* load_err) -> std::shared_ptr<Layer> {
    if (!AssetResolver::GetIdentifierScheme(resolved).empty() ||
        resolver.HasAssetReader()) {
      std::vector<uint8_t> bytes;
      if (!resolver.ReadAsset(resolved_asset, &bytes, load_err)) return nullptr;
      return LoadLayerFromMemory(resolved, bytes.data(), bytes.size(),
                                 load_warn, load_err, effective);
    }
    std::shared_ptr<Layer> loaded =
        LoadLayerFromFile(resolved, load_warn, load_err, effective);
    if (loaded && !resolver.FileIdentityUnchanged(resolved_asset)) {
      if (load_err) {
        *load_err += "asset changed while loading: " + resolved + "\n";
      }
      return nullptr;
    }
    return loaded;
  };

#if defined(LIGHTUSD_ENABLE_THREAD)
  std::shared_future<LoadOutcome> wait_fut;
  std::shared_ptr<std::promise<LoadOutcome>> my_promise;
  {
    std::lock_guard<std::mutex> lk(*mu_);
    auto it = by_resolved_.find(resolved);
    if (it != by_resolved_.end()) return it->second;  // already parsed

    auto fit = in_flight_.find(resolved);
    if (fit != in_flight_.end()) {
      wait_fut = fit->second;  // another thread is parsing this path
    } else {
      // Become the loader: publish a future, then parse outside the lock.
      my_promise = std::make_shared<std::promise<LoadOutcome>>();
      in_flight_.emplace(resolved, my_promise->get_future().share());
    }
  }

  if (!my_promise) {
    // Someone else is loading this exact path; wait for them (outside the lock).
    LoadOutcome outcome = wait_fut.get();
    if (warn) *warn += outcome.warn;
    if (err) *err += outcome.err;
    return outcome.layer;
  }

  // Parse WITHOUT holding the lock, so other paths load concurrently.
  LoadOutcome outcome;
  outcome.layer = load_resolved(&outcome.warn, &outcome.err);
  {
    std::lock_guard<std::mutex> lk(*mu_);
    if (outcome.layer) {
      ++parse_count_;
      by_resolved_.emplace(resolved, outcome.layer);
    }
    in_flight_.erase(resolved);  // a failed load is retried by the next caller
  }
  if (warn) *warn += outcome.warn;
  if (err) *err += outcome.err;
  my_promise->set_value(outcome);  // unblock any waiters
  return outcome.layer;
#else
  auto it = by_resolved_.find(resolved);
  if (it != by_resolved_.end()) {
    return it->second;  // Cache hit -- no re-parse.
  }

  std::shared_ptr<Layer> layer = load_resolved(warn, err);
  if (!layer) {
    return nullptr;
  }

  ++parse_count_;
  by_resolved_.emplace(resolved, layer);
  return layer;
#endif
}

void LayerRegistry::Drop(const std::string &resolved_path) {
#if defined(LIGHTUSD_ENABLE_THREAD)
  std::lock_guard<std::mutex> lk(*mu_);
#endif
  by_resolved_.erase(resolved_path);
}

}  // namespace pcp
}  // namespace next
}  // namespace lightusd
