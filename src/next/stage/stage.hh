// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Stage
// Composed scene representation built on top of Layer

#pragma once

#include "../layer/layer.hh"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace tinyusdz {
namespace next {

/// Stage metadata (derived from root layer)
struct StageMeta {
  std::string defaultPrim;
  std::string upAxis = "Y";
  double metersPerUnit = 0.01;
  double timeCodesPerSecond = 24.0;
  double startTimeCode = 0.0;
  double endTimeCode = 0.0;
  std::string doc;
};

/// Prim handle for stage traversal
/// Lightweight reference to a prim in a specific layer
class UsdPrim {
public:
  UsdPrim() = default;
  UsdPrim(const PrimSpec* spec, const Layer* layer, uint32_t index)
      : spec_(spec), layer_(layer), index_(index) {}

  /// Check if this prim handle is valid
  bool IsValid() const { return spec_ != nullptr; }
  explicit operator bool() const { return IsValid(); }

  /// Get prim name
  const std::string& GetName() const;

  /// Get prim type name (e.g., "Mesh", "Xform")
  const std::string& GetTypeName() const;

  /// Get prim path
  const Path& GetPath() const;

  /// Get specifier (Def, Over, Class)
  PrimSpecifier GetSpecifier() const;

  /// Check if prim is active
  bool IsActive() const;

  /// Check if prim is a concrete definition (not over or class)
  bool IsDefined() const;

  // ============================================================
  // Properties
  // ============================================================

  /// Check if prim has a property
  bool HasProperty(const std::string& name) const;

  /// Get property value
  const Value* GetPropertyValue(const std::string& name) const;

  /// Get property value by pre-registered ID (faster)
  const Value* GetPropertyValue(PropNameId name_id) const;

  /// Get all property names
  std::vector<std::string> GetPropertyNames() const;

  // ============================================================
  // TimeSamples
  // ============================================================

  /// Check if property has time samples
  bool HasTimeSamples(const std::string& name) const;

  /// Get all time sample times for a property
  std::vector<double> GetTimeSampleTimes(const std::string& name) const;

  /// Get value at specific time (returns closest sample, no interpolation)
  const Value* GetValueAtTime(const std::string& name, double time) const;

  /// Get interpolated value at time (linear interpolation for numeric types)
  Value GetInterpolatedValue(const std::string& name, double time) const;

  // ============================================================
  // Relationships
  // ============================================================

  /// Get relationship targets
  const std::vector<Path>* GetRelationship(const std::string& name) const;

  /// Get all relationship names
  std::vector<std::string> GetRelationshipNames() const;

  // ============================================================
  // Hierarchy
  // ============================================================

  /// Get parent prim
  UsdPrim GetParent() const;

  /// Get children
  std::vector<UsdPrim> GetChildren() const;

  /// Get child count
  size_t GetChildCount() const;

  /// Get child by name
  UsdPrim GetChild(const std::string& name) const;

  // ============================================================
  // Metadata
  // ============================================================

  /// Get prim metadata
  const PrimSpecMeta& GetMeta() const;

  /// Get underlying PrimSpec (for advanced use)
  const PrimSpec* GetPrimSpec() const { return spec_; }

private:
  const PrimSpec* spec_ = nullptr;
  const Layer* layer_ = nullptr;
  uint32_t index_ = UINT32_MAX;

  friend class Stage;
};

/// Stage - composed scene built from layers
/// Provides a unified view of prims across layer stack
class Stage {
public:
  Stage();
  ~Stage();

  // Move only
  Stage(Stage&&) noexcept;
  Stage& operator=(Stage&&) noexcept;
  Stage(const Stage&) = delete;
  Stage& operator=(const Stage&) = delete;

  // ============================================================
  // Loading
  // ============================================================

  /// Set the root layer (takes ownership)
  void SetRootLayer(Layer&& layer);

  /// Get the root layer
  const Layer* GetRootLayer() const { return root_layer_.get(); }
  Layer* GetRootLayer() { return root_layer_.get(); }

  /// Add a sublayer (for composition)
  void AddSubLayer(Layer&& layer);

  /// Get sublayer count
  size_t GetSubLayerCount() const { return sub_layers_.size(); }

  /// Get sublayer by index
  const Layer* GetSubLayer(size_t index) const;

  // ============================================================
  // Prim Access
  // ============================================================

  /// Get the pseudo-root prim (parent of all root prims)
  UsdPrim GetPseudoRoot() const;

  /// Get prim by path
  UsdPrim GetPrimAtPath(const Path& path) const;
  UsdPrim GetPrimAtPath(const std::string& path) const;

  /// Get the default prim (if set)
  UsdPrim GetDefaultPrim() const;

  /// Get all root prims
  std::vector<UsdPrim> GetRootPrims() const;

  /// Check if a prim exists at path
  bool HasPrimAtPath(const Path& path) const;
  bool HasPrimAtPath(const std::string& path) const;

  // ============================================================
  // Traversal
  // ============================================================

  /// Traverse all prims in depth-first order
  /// Callback signature: bool(const UsdPrim& prim)
  /// Return false from callback to stop traversal
  template<typename Fn>
  void Traverse(Fn&& callback) const;

  /// Traverse prims matching a predicate
  template<typename Fn, typename Pred>
  void TraverseIf(Fn&& callback, Pred&& predicate) const;

  /// Get all prims of a specific type
  std::vector<UsdPrim> GetPrimsOfType(const std::string& typeName) const;

  /// Flatten to a single layer (apply all composition)
  Layer Flatten() const;

  // ============================================================
  // Time
  // ============================================================

  /// Get start time code
  double GetStartTimeCode() const;

  /// Get end time code
  double GetEndTimeCode() const;

  /// Get time codes per second
  double GetTimeCodesPerSecond() const;

  /// Check if stage has time samples
  bool HasTimeSamples() const;

  // ============================================================
  // Metadata
  // ============================================================

  /// Get stage metadata
  const StageMeta& GetMeta() const { return meta_; }
  StageMeta& GetMeta() { return meta_; }

  /// Get up axis ("Y" or "Z")
  const std::string& GetUpAxis() const { return meta_.upAxis; }

  /// Get meters per unit
  double GetMetersPerUnit() const { return meta_.metersPerUnit; }

  // ============================================================
  // Statistics
  // ============================================================

  /// Get total prim count
  size_t GetPrimCount() const;

  /// Get memory usage
  size_t GetMemoryUsage() const;

  struct Stats {
    size_t prim_count;
    size_t layer_count;
    size_t total_properties;
    size_t memory_bytes;
  };
  Stats GetStats() const;

private:
  std::unique_ptr<Layer> root_layer_;
  std::vector<std::unique_ptr<Layer>> sub_layers_;
  StageMeta meta_;

  // Internal helpers
  void UpdateMetaFromRootLayer();
  bool TraverseImpl(uint32_t prim_index, const Layer* layer,
                    const std::function<bool(const UsdPrim&)>& callback) const;
};

// ============================================================
// Template implementations
// ============================================================

template<typename Fn>
void Stage::Traverse(Fn&& callback) const {
  if (!root_layer_) return;

  std::function<bool(const UsdPrim&)> fn = std::forward<Fn>(callback);
  for (uint32_t idx : root_layer_->root_indices()) {
    TraverseImpl(idx, root_layer_.get(), fn);
  }
}

template<typename Fn, typename Pred>
void Stage::TraverseIf(Fn&& callback, Pred&& predicate) const {
  Traverse([&](const UsdPrim& prim) {
    if (predicate(prim)) {
      return callback(prim);
    }
    return true;  // Continue traversal
  });
}

// ============================================================
// Stage Builder - helper for constructing stages
// ============================================================

class StageBuilder {
public:
  StageBuilder();
  ~StageBuilder();

  /// Set stage metadata
  void SetDefaultPrim(const std::string& primName);
  void SetUpAxis(const std::string& axis);
  void SetMetersPerUnit(double value);
  void SetTimeCodesPerSecond(double fps);
  void SetStartTimeCode(double time);
  void SetEndTimeCode(double time);

  /// Get the layer builder for constructing prims
  LayerBuilder& GetLayerBuilder() { return *layer_builder_; }

  /// Build the stage (moves ownership of layer to stage)
  Stage Build();

private:
  std::unique_ptr<Layer> layer_;
  std::unique_ptr<LayerBuilder> layer_builder_;
  StageMeta meta_;
};

}  // namespace next
}  // namespace tinyusdz
