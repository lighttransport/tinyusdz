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
#include <set>
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

  /// Parse a layer-offset expression "offset:scale" (or "offset").
  static void ParseLayerOffset(const std::string& offset_str, double& offset,
                               double& scale);

  /// Parse variant selection from string "variantSet=selection"
  static VariantSelection ParseVariantSelection(const std::string& str);

  /// Merge local opinions from `source` into `target` (strongest-wins,
  /// fill-absent): type name, properties, time samples, and metadata. Public so
  /// the pcp value-resolution path can reuse it. Does NOT copy relationships or
  /// children (callers handle those, incl. namespace remapping).
  /// Time-sample times are remapped by the layer offset
  /// `t -> time_offset + time_scale*t` (identity by default), baking a
  /// composition arc's layer offset into the flattened result.
  static void CopyLocalOpinions(PrimSpec& target, const PrimSpec& source,
                                double time_offset = 0.0,
                                double time_scale = 1.0);

  // ============================================================
  // Layer cache
  // ============================================================

  /// Get a cached layer (loads if not cached)
  const Layer* GetCachedLayer(const std::string& path);

  /// Get an external layer with its OWN composition arcs (references / payloads
  /// / inherits / specializes) already expanded, variant selection deferred to
  /// the referencing prim. Cached; falls back to the raw layer when it has no
  /// such arcs or a composition cycle is detected.
  const Layer* GetComposedExternalLayer(const std::string& path);

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

  // Cache of external layers with their OWN arcs already expanded (variants
  // deferred), and the set of paths currently being so composed (cross-layer
  // cycle guard). Shared (shared_ptr) with the sub-Compositors that compose
  // referenced layers, so the cache and cycle guard span the whole recursion.
  // Lazily created on first external reference.
  std::shared_ptr<std::map<std::string, std::shared_ptr<Layer>>>
      composed_ext_cache_;
  std::shared_ptr<std::set<std::string>> composing_ext_;

  // Composition state (for cycle detection)
  std::vector<std::string> composition_stack_;

  // Pass-2 arc resolution state: prims whose arcs are resolved/in-progress
  // (prevents infinite recursion + redundant recomposition), and grafted
  // subtree prims to append to the result after the pass (cannot add during
  // iteration). Cleared at the start of each Compose().
  std::set<std::string> arc_resolved_;
  // Grafted subtrees buffered during pass 2 (appended afterwards). The anchor
  // (resolved path of the layer the subtree came from) rides along so a later
  // pass can resolve the grafted prims' OWN arcs — e.g. a referenced mesh file
  // whose material prims reference `../../Materials/x.usd` relative to itself.
  struct PendingGraft {
    PrimSpec prim;
    std::string anchor;
  };
  std::vector<PendingGraft> pending_graft_;
  std::set<std::string> graft_paths_;

  // Internal composition methods
  bool ComposeLayer(Layer& target, const Layer& source_layer,
                    const std::string& anchor_path, int depth);
  bool ComposePrim(PrimSpec& target, const Layer& source_layer,
                   const PrimSpec& source, const std::string& anchor_path, int depth);

  // Arc resolution (pass 2). Operates on the already-merged `layer`, expanding
  // each prim's references/payloads/inherits/specializes/variants in LIVRPS
  // strength order (local opinions kept strongest), grafting referenced
  // subtrees, then clearing the resolved arcs. Recursive (targets resolved
  // before being copied) with cycle/recompute guards via arc_resolved_.
  void ResolveArcsForPrim(Layer& layer, PrimSpec& prim,
                          const std::string& anchor_path, int depth);
  // Resolve one reference/payload arc (internal or external) onto `prim`.
  void ResolveRefArc(Layer& layer, PrimSpec& prim, const CompositionArc& arc,
                     const std::string& anchor_path, int depth);
  // Graft the descendant subtree of `src_root` in `src` under `dst_root`.
  void GraftSubtree(const Layer& src, const std::string& src_anchor,
                    const std::string& src_root,
                    const std::string& dst_root);
  bool ApplyVariants(PrimSpec& prim, const Layer& layer,
                     const std::string& anchor_path, int depth);

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
