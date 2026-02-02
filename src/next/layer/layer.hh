// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Layer
// Container for PrimSpecs with flat storage and path indexing

#pragma once

#include "prim-spec.hh"
#include <string>
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

  std::string doc;
  std::string comment;

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

  /// Finalize layer (build path index, sort properties)
  void finalize();

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
  uint32_t begin_prim(const std::string& name, const std::string& type_name,
                      PrimSpecifier specifier = PrimSpecifier::Def);

  /// End current prim (validates and finalizes)
  void end_prim();

  /// Get current prim being built
  PrimSpec* current();

  /// Add property to current prim
  void add_property(const std::string& name, Value value, uint16_t flags = 0);

  /// Add time sample to current prim
  void add_time_sample(const std::string& prop_name, double time, Value value);

  /// Add relationship to current prim
  void add_relationship(const std::string& name, const Path& target);

  /// Set metadata on current prim
  void set_active(bool active);
  void set_hidden(bool hidden);

  /// Finalize the layer
  void finalize();

private:
  Layer& layer_;
  std::vector<uint32_t> prim_stack_;  // Stack of parent indices
  uint32_t current_index_ = UINT32_MAX;
};

}  // namespace next
}  // namespace tinyusdz
