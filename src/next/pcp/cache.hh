// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache
//
// Lazy, per-prim composition cache for the `next` module, mirroring the design
// of tinyusdz::pcp::Cache but built entirely on next types (standalone, C++14).
//
// Phase 1 scope: sublayers (L) + references (R), lazy ComputePrimIndex,
// parse-once LayerRegistry, dependency-aware Invalidate, and BuildStage
// materialization. Inherits/specializes/variants/payloads/instancing are later
// phases (see the plan).

#pragma once

#include "prim-index.hh"
#include "layer-registry.hh"
#include "../resolver/asset-resolver.hh"
#include "../stage/stage.hh"

#include <memory>
#include <string>
#include <vector>

#include "nonstd/expected.hpp"

namespace tinyusdz {
namespace next {
namespace pcp {

class Cache {
 public:
  Cache();
  ~Cache();
  Cache(Cache &&) noexcept;
  Cache &operator=(Cache &&) noexcept;
  Cache(const Cache &) = delete;
  Cache &operator=(const Cache &) = delete;

  /// Open a cache over a root layer. Runs the sublayer (L) phase once: builds
  /// the root layer stack [root, sublayers...]. `resolver` is borrowed and must
  /// outlive the cache. `root_identifier` is the root layer's resolved file path
  /// (used as the anchor for resolving its references/sublayers); may be empty.
  static nonstd::expected<Cache, std::string> Open(
      AssetResolver &resolver, std::shared_ptr<Layer> root_layer,
      const std::string &root_identifier = "",
      const CompositionOptions &options = {});

  /// Lazily build (if needed) and cache the PrimIndex for `prim_path`. Returns a
  /// borrowed pointer owned by the cache (stable until Invalidate/destruction),
  /// or nullptr if the prim has no local spec in the root layer stack.
  const PrimIndex *ComputePrimIndex(const Path &prim_path, std::string *warn,
                                    std::string *err);

  /// Build (and cache) the PrimIndices for a batch of paths. Currently
  /// sequential; `CompositionOptions::num_threads` is a forward-compat hint
  /// (lock-free parallel composition is a follow-up that needs per-worker
  /// contexts). Best-effort: a path that fails to build is skipped.
  bool PrewarmPrimIndices(const std::vector<Path> &paths, std::string *warn,
                          std::string *err);

  /// Materialize the fully-composed scene into `stage` (a fresh root Layer).
  bool BuildStage(Stage *stage, std::string *warn, std::string *err);

  // --- instancing ---------------------------------------------------------

  /// Whether `prim_path` is an instance (instanceable, and not the prototype of
  /// its instance group).
  bool IsInstance(const Path &prim_path) const;

  /// The prototype prim path for `prim_path` (itself if it is the prototype, or
  /// empty if not instanceable / not computed).
  Path GetPrototype(const Path &prim_path) const;

  /// All prototype prim paths discovered so far.
  std::vector<Path> GetPrototypePaths() const;

  /// All instances (including the prototype) sharing a prototype.
  std::vector<Path> GetInstancesForPrototype(const Path &prototype) const;

  /// Number of distinct prototypes (instance groups).
  size_t PrototypeCount() const;

  /// Compute the structural instance key for `prim_path` (for diagnostics/tests).
  std::string ComputeInstanceKey(const Path &prim_path, std::string *warn,
                                 std::string *err);

  /// Load a deferred payload on `prim_path` and recompose the affected prims.
  bool LoadPayload(const Path &prim_path, std::string *warn, std::string *err);

  /// Unload (defer) the payload on `prim_path` and recompose.
  bool UnloadPayload(const Path &prim_path);

  /// Whether `prim_path` has an unloaded (deferred) payload.
  bool HasDeferredPayload(const Path &prim_path) const;

  /// All prims that currently have a deferred payload.
  std::vector<Path> GetDeferredPayloadPaths() const;

  /// Drop the cached index for `prim_path` and every index that depended on a
  /// site at/under it.
  void Invalidate(const Path &prim_path);

  /// Drop every cached index that read from `resolved_layer_id`, then drop the
  /// layer from the registry.
  void InvalidateLayer(const std::string &resolved_layer_id);

  bool HasComputedPrimIndex(const Path &prim_path) const;
  size_t ComputedPrimIndexCount() const;
  const LayerRegistry &layer_registry() const;

  /// Pre-register an in-memory layer under `identifier`, so a reference/payload
  /// resolving to that id composes it without disk I/O (embedding helper).
  void PreloadLayer(const std::string &identifier, std::shared_ptr<Layer> layer);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
