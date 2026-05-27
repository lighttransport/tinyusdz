// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Composition Arcs
//
// USD composition: references, payloads, inherits, specializes, variants

#pragma once

#include "../layer/layer.hh"
#include "../resolver/asset-resolver.hh"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

namespace tinyusdz {
namespace next {

/// Composition arc types (in LIVRPS strength order)
enum class ArcType : uint8_t {
  Local = 0,      // Direct opinions in the layer
  Inherits,       // Class inheritance
  VariantSet,     // Variant selection
  Reference,      // Reference to another prim
  Payload,        // Lazy-loaded reference
  Specializes     // Specialization (weaker than local)
};

/// Reference/Payload target
struct CompositionArc {
  ArcType type = ArcType::Reference;
  std::string asset_path;     // External asset (empty for internal)
  std::string prim_path;      // Target prim path (default prim if empty)
  std::string layer_offset;   // Optional layer offset expression
  bool is_internal = false;   // Internal reference (same layer)

  bool operator==(const CompositionArc& other) const {
    return type == other.type &&
           asset_path == other.asset_path &&
           prim_path == other.prim_path;
  }
};

/// Variant selection
struct VariantSelection {
  std::string variant_set;
  std::string variant_name;
};

/// Composition options
struct CompositionOptions {
  bool load_payloads = true;              // Load payloads (false = unloaded)
  bool resolve_inherits = true;           // Resolve inherits
  bool resolve_specializes = true;        // Resolve specializes
  bool resolve_variants = true;           // Apply variant selections
  int max_depth = 100;                    // Max composition recursion depth
  std::vector<std::string> muted_layers;  // Layers to skip
};

/// Composition error
struct CompositionError {
  std::string message;
  std::string prim_path;
  std::string arc_path;
  ArcType arc_type;
};

/// Layer loader callback - used to load external layers
using LayerLoader = std::function<std::unique_ptr<Layer>(
    const std::string& resolved_path, std::string* error)>;

/// Compositor - composes layers into a flattened view
class Compositor {
public:
  Compositor();
  explicit Compositor(AssetResolver* resolver);
  ~Compositor();

  /// Set the asset resolver
  void SetResolver(AssetResolver* resolver) { resolver_ = resolver; }

  /// Set layer loader callback
  void SetLayerLoader(LayerLoader loader) { layer_loader_ = std::move(loader); }

  /// Set composition options
  void SetOptions(const CompositionOptions& options) { options_ = options; }
  const CompositionOptions& GetOptions() const { return options_; }

  // ============================================================
  // Main composition API
  // ============================================================

  /// Compose a root layer into a flattened layer
  /// @param root_layer The root layer to compose
  /// @param anchor_path Path of the root layer (for resolving references)
  /// @return Composed layer, or nullptr on error
  std::unique_ptr<Layer> Compose(const Layer& root_layer,
                                  const std::string& anchor_path = "");

  /// Compose sublayers into a single layer
  std::unique_ptr<Layer> ComposeSublayers(const Layer& root_layer,
                                           const std::string& anchor_path = "");

  /// Get errors from last composition
  const std::vector<CompositionError>& GetErrors() const { return errors_; }

  /// Clear errors
  void ClearErrors() { errors_.clear(); }

  // ============================================================
  // Arc resolution
  // ============================================================

  /// Parse a reference string into a CompositionArc
  static CompositionArc ParseReference(const std::string& ref_str);

  /// Parse a payload string
  static CompositionArc ParsePayload(const std::string& payload_str);

  /// Parse variant selection from string "variantSet=selection"
  static VariantSelection ParseVariantSelection(const std::string& str);

  // ============================================================
  // Layer cache
  // ============================================================

  /// Get a cached layer (loads if not cached)
  const Layer* GetCachedLayer(const std::string& path);

  /// Clear the layer cache
  void ClearCache();

  /// Get cache size
  size_t GetCacheSize() const { return layer_cache_.size(); }

private:
  AssetResolver* resolver_ = nullptr;
  LayerLoader layer_loader_;
  CompositionOptions options_;
  std::vector<CompositionError> errors_;

  // Layer cache: resolved path -> layer
  std::map<std::string, std::unique_ptr<Layer>> layer_cache_;

  // Composition state (for cycle detection)
  std::vector<std::string> composition_stack_;

  // Internal composition methods
  bool ComposeLayer(Layer& target, const Layer& source_layer,
                    const std::string& anchor_path, int depth);
  bool ComposePrim(PrimSpec& target, const Layer& source_layer,
                   const PrimSpec& source, const std::string& anchor_path, int depth);

  // Arc handling
  bool ApplyReferences(PrimSpec& prim, const std::string& anchor_path, int depth);
  bool ApplyPayloads(PrimSpec& prim, const std::string& anchor_path, int depth);
  bool ApplyInherits(PrimSpec& prim, const Layer& layer, int depth);
  bool ApplySpecializes(PrimSpec& prim, const Layer& layer, int depth);
  bool ApplyVariants(PrimSpec& prim, const Layer& layer, int depth);

  // Copy local opinions from source to target (strongest strength)
  static void CopyLocalOpinions(PrimSpec& target, const PrimSpec& source);

  // Parse layer offset string into offset and scale
  static void ParseLayerOffset(const std::string& offset_str,
                                double& offset, double& scale);

  // Helper methods
  void AddError(const std::string& msg, const std::string& prim_path,
                const std::string& arc_path, ArcType type);
  bool CheckCycle(const std::string& path);
  void PushStack(const std::string& path);
  void PopStack();
};

// ============================================================
// Utility functions
// ============================================================

/// Flatten a composed layer (remove composition arcs, keep only final opinions)
void FlattenLayer(Layer& layer);

/// Get all external references from a layer
std::vector<CompositionArc> GetExternalReferences(const Layer& layer);

/// Get all payloads from a layer
std::vector<CompositionArc> GetPayloads(const Layer& layer);

/// Check if a prim has any composition arcs
bool HasCompositionArcs(const PrimSpec& prim);

}  // namespace next
}  // namespace tinyusdz
