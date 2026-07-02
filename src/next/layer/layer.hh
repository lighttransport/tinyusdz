// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Layer
// Container for PrimSpecs with flat storage and path indexing

#pragma once

#include "prim-spec.hh"
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>

namespace tinyusdz {
namespace next {

/// Layer metadata
struct LayerMeta {
  std::string defaultPrim;
  std::string upAxis = "Y";
  double metersPerUnit = 0.01;
  double timeCodesPerSecond = 24.0;
  double startTimeCode = 0.0;
  double endTimeCode = 0.0;

  // Optional stage metadata (parity with the mature reader). The *_set flags
  // distinguish "authored" from "default" so the writer re-emits only authored
  // opinions.
  double framesPerSecond = 24.0;
  bool framesPerSecond_set = false;
  double kilogramsPerUnit = 1.0;
  bool kilogramsPerUnit_set = false;
  std::string colorConfiguration;   // asset path
  std::string colorManagementSystem;  // token

  std::string doc;
  std::string comment;

  // Dictionary-valued stage metadata (Dictionary Value; empty when unauthored).
  Value customLayerData;
  Value expressionVariables;

  // Sublayer paths for composition
  std::vector<std::string> subLayers;
};

/// Layer - owns all PrimSpecs for a USD file
/// Design:
/// - PrimSpecs stored in flat vector (cache-friendly)
/// - Path-to-index map for O(1) lookup
/// - Root prims stored by index
/// - Children stored as indices (no pointer chasing)
class Layer {
public:
  Layer();
  ~Layer();

  // Move only
  Layer(Layer&&) noexcept;
  Layer& operator=(Layer&&) noexcept;
  Layer(const Layer&) = delete;
  Layer& operator=(const Layer&) = delete;

  // ============================================================
  // Building
  // ============================================================

  /// Reserve space for expected prim count
  void reserve(size_t count);

  /// Add a new PrimSpec and return its index
  uint32_t add_prim(PrimSpec&& spec);

  /// Set parent-child relationship
  void set_parent(uint32_t child_index, uint32_t parent_index);

  /// Add a root prim index
  void add_root(uint32_t index);

  /// Finalize layer (build path index, sort properties). When
  /// `path_index_prebuilt` is true the caller already ran build_path_index()
  /// (e.g. overlapped on a worker while properties pre-sorted) and it is
  /// skipped here.
  void finalize(bool path_index_prebuilt = false);

  /// (Re)build only the path->index map from current prim paths. Unlike
  /// finalize() this is not guarded and does not sort properties, so it can be
  /// called mid-mutation (e.g. composition pass 2 needs prim_at_path lookups
  /// before the layer is finalized, and after prims are added/renamed).
  void build_path_index();

  /// Sort prims so every ancestor precedes its descendants and subtrees are
  /// contiguous (lexicographic full-path order). The crate writer's
  /// compressed-paths encoding requires this; composition appends grafted
  /// subtrees out of order. Clears the path index (rebuild after).
  void sort_prims_by_path();

  /// Clone the layer. PrimSpecs are deep-cloned, while Value array payloads keep
  /// their copy-on-write/lazy shared backing, so this is cheap for crate-backed
  /// large arrays.
  Layer Clone() const;

  // ============================================================
  // Parallel-subtree stitch support (USDA parallel prim parse)
  // ============================================================

  /// Placeholder marker bit: root/child index entries carrying this bit refer
  /// to a not-yet-stitched worker fragment (low 31 bits = fragment id). All
  /// placeholders are resolved by resolve_pending_indices() before finalize.
  static constexpr uint32_t kPendingIndexBit = 0x80000000u;

  /// Append a pending root marker (fragment id), preserving authored order.
  void add_root_pending(uint32_t fragment_id);

  /// Move every prim of `fragment` into this layer, rebasing the fragment's
  /// internal child indices. Returns the new absolute index of the fragment's
  /// single root prim (UINT32_MAX if the fragment has no root).
  uint32_t adopt_fragment(Layer&& fragment);

  /// Replace every pending index in root and child lists with
  /// resolved[fragment_id].
  void resolve_pending_indices(const std::vector<uint32_t>& resolved);

  // ============================================================
  // Access
  // ============================================================

  /// Get prim by index
  const PrimSpec* prim(uint32_t index) const;
  PrimSpec* prim(uint32_t index);

  /// Get prim by path (O(1) after finalize)
  const PrimSpec* prim_at_path(const Path& path) const;
  const PrimSpec* prim_at_path(const std::string& path) const;
  PrimSpec* prim_at_path_mutable(const std::string& path);

  /// Index of the prim at `path`, or UINT32_MAX if absent (O(1), from the path
  /// index). Lets a caller build a UsdPrim without re-scanning for the index.
  uint32_t index_at_path(const std::string& path) const;

  /// Get prim by index (mutable)
  PrimSpec* prim_mutable(uint32_t index);

  /// Get root prim indices
  const std::vector<uint32_t>& root_indices() const { return root_indices_; }

  /// Get all prims (flat array)
  const std::vector<PrimSpec>& prims() const { return prims_; }

  /// Get prim count
  size_t prim_count() const { return prims_.size(); }

  /// Get children of a prim
  std::vector<const PrimSpec*> children(uint32_t prim_index) const;

  // ============================================================
  // Metadata
  // ============================================================

  const LayerMeta& meta() const { return meta_; }
  LayerMeta& meta() { return meta_; }

  // ============================================================
  // Memory
  // ============================================================

  /// Get total memory usage
  size_t memory_usage() const;

  /// Get statistics
  struct Stats {
    size_t prim_count;
    size_t root_count;
    size_t total_properties;
    size_t total_time_samples;
    size_t memory_bytes;
  };
  Stats stats() const;

private:
  std::vector<PrimSpec> prims_;
  std::vector<uint32_t> root_indices_;
  std::unordered_map<std::string, uint32_t> path_to_index_;
  LayerMeta meta_;
  bool finalized_ = false;
};

/// Layer builder - helper for constructing layers from parsed data
class LayerBuilder {
public:
  explicit LayerBuilder(Layer& layer);

  /// Start a new prim at the given path
  /// Returns the prim index
  uint32_t begin_prim(std::string_view name, std::string_view type_name,
                      PrimSpecifier specifier = PrimSpecifier::Def);

  /// Start a new prim with an explicit absolute path.
  /// Returns the prim index.
  uint32_t begin_prim(std::string_view name, std::string_view type_name,
                      PrimSpecifier specifier, std::string_view full_path);

  /// End current prim (validates and finalizes)
  void end_prim();

  /// Get current prim being built
  PrimSpec* current();

  /// Add property to current prim
  void add_property(std::string_view name, Value value, uint16_t flags = 0);

  /// Add time sample to current prim. `dedup=false` skips content-hash dedup
  /// (deferred-fill values from the async USDA array parse; see PrimSpec).
  void add_time_sample(std::string_view prop_name, double time, Value value,
                       bool dedup = true);

  /// Add relationship to current prim
  void add_relationship(std::string_view name, const Path& target);

  /// Set metadata on current prim
  void set_active(bool active);
  void set_hidden(bool hidden);

  /// Finalize the layer
  void finalize(bool path_index_prebuilt = false);

  /// Absolute-path prefix applied to prims begun with an EMPTY parent stack
  /// (path = prefix + "/" + name). Used by parallel subtree sub-parsers whose
  /// fragment root is not a real layer root. Empty (default) keeps the normal
  /// root behavior.
  void set_path_prefix(std::string prefix) { path_prefix_ = std::move(prefix); }

private:
  Layer& layer_;
  std::vector<uint32_t> prim_stack_;  // Stack of parent indices
  uint32_t current_index_ = UINT32_MAX;
  std::string path_prefix_;
};

}  // namespace next
}  // namespace tinyusdz
