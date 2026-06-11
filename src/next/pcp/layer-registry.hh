// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP LayerRegistry
//
// Parse-once, resolved-path-keyed cache of layers. A referenced/sublayer file
// is parsed exactly once and shared (shared_ptr) across every prim index that
// uses it. Standalone, C++14.

#pragma once

#include "../layer/layer.hh"
#include "../resolver/asset-resolver.hh"

#include <memory>
#include <string>
#include <unordered_map>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <future>
#include <mutex>
#endif

namespace tinyusdz {
namespace next {
namespace pcp {

class LayerRegistry {
 public:
#if defined(TINYUSDZ_ENABLE_THREAD)
  LayerRegistry() : mu_(new std::mutex()) {}
#else
  LayerRegistry() = default;
#endif

  // Move-only (owns shared layers).
  LayerRegistry(LayerRegistry &&) = default;
  LayerRegistry &operator=(LayerRegistry &&) = default;
  LayerRegistry(const LayerRegistry &) = delete;
  LayerRegistry &operator=(const LayerRegistry &) = delete;

  /// Resolve `asset_path` against `anchor` (the referencing file's directory),
  /// parse once, and return shared ownership. Returns nullptr on failure.
  std::shared_ptr<Layer> GetOrLoad(AssetResolver &resolver,
                                   const std::string &asset_path,
                                   const std::string &anchor,
                                   std::string *warn, std::string *err);

  /// Pre-register an in-memory layer under a resolved identifier (so a reference
  /// resolving to that id composes it without touching disk). Does not count as
  /// a parse. Useful for embedding already-loaded layers.
  void Preload(const std::string &resolved_id, std::shared_ptr<Layer> layer) {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::lock_guard<std::mutex> lk(*mu_);
#endif
    by_resolved_[resolved_id] = std::move(layer);
  }

  /// Drop a single cached layer by resolved path. Caller must have already
  /// invalidated every PrimIndex that referenced it.
  void Drop(const std::string &resolved_path);

  void Clear() {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::lock_guard<std::mutex> lk(*mu_);
#endif
    by_resolved_.clear();
  }
  size_t size() const {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::lock_guard<std::mutex> lk(*mu_);
#endif
    return by_resolved_.size();
  }
  size_t parse_count() const {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::lock_guard<std::mutex> lk(*mu_);
#endif
    return parse_count_;
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<Layer>> by_resolved_;
  size_t parse_count_ = 0;
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Heap-allocated so the registry stays movable. Guards by_resolved_/parse_count_
  // and in_flight_.
  std::unique_ptr<std::mutex> mu_;
  struct LoadOutcome {
    std::shared_ptr<Layer> layer;
    std::string warn;
    std::string err;
  };
  // Resolved path -> the in-progress parse for it. A first requester publishes a
  // future here and parses OUTSIDE the lock; concurrent requesters for the same
  // path wait on the future (parse-once) while requesters for OTHER paths parse
  // in parallel (the lock no longer spans the parse).
  std::unordered_map<std::string, std::shared_future<LoadOutcome>> in_flight_;
#endif
};

/// Load a layer from a resolved file path, dispatching by extension to the
/// next USDA / USDC readers. Returns nullptr on failure. (USDZ: TODO.)
std::shared_ptr<Layer> LoadLayerFromFile(const std::string &resolved_path,
                                         std::string *warn, std::string *err);

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
