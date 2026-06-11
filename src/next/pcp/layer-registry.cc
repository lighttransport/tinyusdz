// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP LayerRegistry implementation

#include "layer-registry.hh"

#include "../reader/usda-reader.hh"
#include "../reader/usdc-reader.hh"

#include <algorithm>

namespace tinyusdz {
namespace next {
namespace pcp {

namespace {

std::string ToLowerExt(const std::string &path) {
  auto dot = path.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  return ext;
}

}  // namespace

std::shared_ptr<Layer> LoadLayerFromFile(const std::string &resolved_path,
                                         std::string *warn, std::string *err) {
  const std::string ext = ToLowerExt(resolved_path);

  if (ext == "usda" || ext == "usd") {
    LoadResult r = LoadUSDAFromFile(resolved_path);
    if (!r.success) {
      if (err) *err += "Failed to load USDA layer: " + resolved_path + " : " +
                       r.error_summary + "\n";
      return nullptr;
    }
    for (const auto &w : r.warnings) {
      if (warn) *warn += w + "\n";
    }
    return r.stage.ReleaseRootLayer();
  }

  if (ext == "usdc") {
    USDCLoadResult r = LoadUSDCFromFile(resolved_path);
    if (!r.success) {
      if (err) *err += "Failed to load USDC layer: " + resolved_path + " : " +
                       r.error_summary + "\n";
      return nullptr;
    }
    return r.stage.ReleaseRootLayer();
  }

  // TODO(next-pcp): .usdz package layers.
  if (err) {
    *err += "Unsupported layer file format for: " + resolved_path + "\n";
  }
  return nullptr;
}

std::shared_ptr<Layer> LayerRegistry::GetOrLoad(AssetResolver &resolver,
                                                const std::string &asset_path,
                                                const std::string &anchor,
                                                std::string *warn,
                                                std::string *err) {
  const std::string resolved = resolver.ResolvePath(asset_path, anchor);
  if (resolved.empty()) {
    if (err) *err += "Failed to resolve asset path: " + asset_path + "\n";
    return nullptr;
  }

#if defined(TINYUSDZ_ENABLE_THREAD)
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
  outcome.layer = LoadLayerFromFile(resolved, &outcome.warn, &outcome.err);
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

  std::shared_ptr<Layer> layer = LoadLayerFromFile(resolved, warn, err);
  if (!layer) {
    return nullptr;
  }

  ++parse_count_;
  by_resolved_.emplace(resolved, layer);
  return layer;
#endif
}

void LayerRegistry::Drop(const std::string &resolved_path) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  std::lock_guard<std::mutex> lk(*mu_);
#endif
  by_resolved_.erase(resolved_path);
}

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
