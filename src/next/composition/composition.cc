// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Composition Implementation
// Full support: references, payloads, inherits, specializes, variants, layer offsets

#include "composition.hh"
#include <algorithm>
#include <sstream>
#include <cstring>

namespace tinyusdz {
namespace next {

// ============================================================
// Compositor
// ============================================================

Compositor::Compositor() = default;

Compositor::Compositor(AssetResolver* resolver) : resolver_(resolver) {}

Compositor::~Compositor() = default;

std::unique_ptr<Layer> Compositor::Compose(const Layer& root_layer,
                                            const std::string& anchor_path) {
  errors_.clear();
  composition_stack_.clear();
  arc_resolved_.clear();
  pending_graft_.clear();
  graft_paths_.clear();

  auto result = std::make_unique<Layer>();
  result->meta() = root_layer.meta();

  // Pass 1 — merge local opinions (sublayers weakest-first, then the root
  // layer strongest). Arc-free scenes are fully composed by this pass alone.
  if (!root_layer.meta().subLayers.empty()) {
    auto sublayer_base = ComposeSublayers(root_layer, anchor_path);
    if (sublayer_base) {
      ComposeLayer(*result, *sublayer_base, anchor_path, 0);
    }
  }
  ComposeLayer(*result, root_layer, anchor_path, 0);

  // Pass 2 — expand composition arcs over the merged layer. Build the path
  // index first so internal-arc / inherit / specialize lookups resolve (the
  // layer is not finalized yet). Iterate by index up to the current count;
  // ResolveArcsForPrim only mutates existing prims (grafted subtrees are
  // buffered in pending_graft_ and appended afterward).
  result->build_path_index();
  size_t n = result->prim_count();
  for (size_t i = 0; i < n; ++i) {
    PrimSpec* p = result->prim_mutable(static_cast<uint32_t>(i));
    if (p) ResolveArcsForPrim(*result, *p, anchor_path, 0);
  }
  // Append grafted subtree prims brought in by references/payloads.
  for (auto& g : pending_graft_) {
    std::string gp = g.path().str();
    if (result->prim_at_path(gp)) continue;  // a local override already exists
    result->add_prim(std::move(g));
  }
  pending_graft_.clear();
  graft_paths_.clear();

  // Finalize the composed layer
  result->finalize();

  return result;
}

std::unique_ptr<Layer> Compositor::ComposeSublayers(const Layer& root_layer,
                                                      const std::string& anchor_path) {
  const auto& sublayer_paths = root_layer.meta().subLayers;
  if (sublayer_paths.empty()) return nullptr;

  auto result = std::make_unique<Layer>();
  result->meta() = root_layer.meta();

  // Sublayers are ordered strongest-first in the USD spec.
  // We process from the end (weakest) to the beginning (strongest).
  for (auto it = sublayer_paths.rbegin(); it != sublayer_paths.rend(); ++it) {
    const std::string& sublayer_path = *it;

    std::string resolved_path = sublayer_path;
    if (resolver_) {
      resolved_path = resolver_->ResolvePath(sublayer_path, anchor_path);
    }

    if (std::find(options_.muted_layers.begin(),
                  options_.muted_layers.end(),
                  resolved_path) != options_.muted_layers.end()) {
      continue;
    }

    const Layer* sublayer = GetCachedLayer(resolved_path);
    if (!sublayer) {
      AddError("Failed to load sublayer: " + sublayer_path,
               "", sublayer_path, ArcType::Local);
      continue;
    }

    auto composed_sub = Compose(*sublayer, resolved_path);
    if (composed_sub) {
      ComposeLayer(*result, *composed_sub, resolved_path, 0);
    }
  }

  return result;
}

bool Compositor::ComposeLayer(Layer& target, const Layer& source_layer,
                               const std::string& anchor_path, int depth) {
  if (depth > options_.max_depth) {
    AddError("Max composition depth exceeded", "", anchor_path, ArcType::Reference);
    return false;
  }

  // Compose each prim from source_layer into target
  for (size_t i = 0; i < source_layer.prim_count(); ++i) {
    const PrimSpec* src_prim = source_layer.prim(static_cast<uint32_t>(i));
    if (!src_prim) continue;

    std::string prim_path = src_prim->path().str();
    PrimSpec* target_prim = target.prim_at_path_mutable(prim_path);

    if (!target_prim) {
      // Create new prim in target layer via Clone()
      PrimSpec new_prim = src_prim->Clone();
      uint32_t idx = target.add_prim(std::move(new_prim));
      target.add_root(idx);
    } else {
      // Existing prim - compose source onto it
      ComposePrim(*target_prim, source_layer, *src_prim, anchor_path, depth);
    }
  }

  return true;
}

bool Compositor::ComposePrim(PrimSpec& target, const Layer& source_layer,
                              const PrimSpec& source,
                              const std::string& anchor_path, int depth) {
  (void)source_layer;
  (void)anchor_path;
  if (depth > options_.max_depth) return false;
  // Pass 1 only merges local opinions across layers (sublayers + root). The
  // stronger (later) layer's opinions are already in `target`; CopyLocalOpinions
  // fills in only what `target` lacks. Composition arcs are expanded later in
  // pass 2 (ResolveArcsForPrim), once the full local-opinion layer exists.
  CopyLocalOpinions(target, source);
  return true;
}

void Compositor::CopyLocalOpinions(PrimSpec& target, const PrimSpec& source) {
  // Copy type name if target doesn't have one
  if (target.type_name().empty() && !source.type_name().empty()) {
    target.set_type_name(source.type_name());
  }

  // Copy properties (source overrides target for time-sampled props).
  // Preserves valueless slots (connection-only / declared-only attributes),
  // their declared type names, and connection targets for USDC fidelity.
  PropNameTable& name_table = GetPropNameTable();
  for (const auto& slot : source.properties().slots()) {
    const PropSlot* tgt_slot = target.property(slot.name_id);
    if (tgt_slot) continue;  // target opinion wins (incl. time-sampled merge)
    const std::string& pname = name_table.get(slot.name_id);
    const Value* src_val = source.property_value(slot.name_id);
    if (src_val) {
      target.add_property(slot.name_id, *src_val, slot.flags);
    } else {
      // No authored default: carry the typed slot across (connection-only /
      // declared-only attribute).
      target.add_property_slot(slot.name_id,
                               static_cast<TypeId>(slot.value_type), slot.flags);
    }
    if (const std::string* tn = source.property_type_name(pname)) {
      target.set_property_type_name(pname, *tn);
    }
    if (const std::vector<Path>* conns = source.connection(pname)) {
      for (const auto& c : *conns) target.add_connection(pname, c);
    }
  }

  // Copy relationships not already present on the target.
  for (const auto& rel_name : source.relationship_names()) {
    if (target.relationship(rel_name)) continue;
    if (const std::vector<Path>* tgts = source.relationship(rel_name)) {
      for (const auto& t : *tgts) target.add_relationship(rel_name, t);
    }
  }

  // Copy time-sampled properties
  for (auto ts_prop_id : source.time_sampled_properties()) {
    auto* samples = source.time_samples(ts_prop_id);
    if (!samples) continue;

    // Check if target already has time samples for this property
    bool target_has_ts = target.has_time_samples(ts_prop_id);
    if (!target_has_ts) {
      // Copy time samples from source to target
      for (const auto& [time, val_offset] : *samples) {
        const Value* val = source.time_sample_value(val_offset);
        if (val) {
          target.add_time_sample(ts_prop_id, time, *val);
        }
      }
    }
  }

  // Copy metadata fields
  if (!source.meta().doc.empty() && target.meta().doc.empty()) {
    target.meta().doc = source.meta().doc;
  }
  if (source.meta().active != target.meta().active) {
    target.meta().active = source.meta().active;
  }
}

void Compositor::ResolveArcsForPrim(Layer& layer, PrimSpec& prim,
                                    const std::string& anchor_path, int depth) {
  if (depth > options_.max_depth) return;
  const std::string self = prim.path().str();
  // Mark before recursing: prevents infinite recursion on cycles and avoids
  // recomposing a prim referenced by several others.
  if (!arc_resolved_.insert(self).second) return;

  // References (strongest arc) then payloads — both bring in a target prim's
  // opinions plus its descendant subtree.
  for (const auto& ref_str : prim.meta().references) {
    ResolveRefArc(layer, prim, ParseReference(ref_str), anchor_path, depth);
  }
  if (options_.load_payloads) {
    for (const auto& pl_str : prim.meta().payloads) {
      ResolveRefArc(layer, prim, ParsePayload(pl_str), anchor_path, depth);
    }
  }

  // Inherits — copy opinions from same-layer class prims (resolve them first).
  if (options_.resolve_inherits) {
    for (const auto& inh : prim.meta().inherits) {
      PrimSpec* cls = layer.prim_at_path_mutable(inh);
      if (!cls) {
        AddError("Inherited class not found: " + inh, self, inh,
                 ArcType::Inherits);
        continue;
      }
      ResolveArcsForPrim(layer, *cls, anchor_path, depth + 1);
      CopyLocalOpinions(prim, *cls);
      GraftSubtree(layer, inh, self);
    }
  }

  // Variants (if the reader populated variant content).
  if (options_.resolve_variants) {
    ApplyVariants(prim, layer, depth);
  }

  // Specializes (weakest) — same-layer, fill only what is still missing.
  if (options_.resolve_specializes) {
    for (const auto& sp : prim.meta().specializes) {
      PrimSpec* spec = layer.prim_at_path_mutable(sp);
      if (!spec) {
        AddError("Specialized prim not found: " + sp, self, sp,
                 ArcType::Specializes);
        continue;
      }
      ResolveArcsForPrim(layer, *spec, anchor_path, depth + 1);
      CopyLocalOpinions(prim, *spec);
      GraftSubtree(layer, sp, self);
    }
  }

  // Flatten: drop the now-resolved arcs so they are not re-emitted. Variant
  // sets/selection are baked too (the selected opinions are now local).
  prim.meta().references.clear();
  prim.meta().payloads.clear();
  prim.meta().inherits.clear();
  prim.meta().specializes.clear();
  prim.meta().variantSets.clear();
  prim.meta().variantSelection.clear();
}

void Compositor::ResolveRefArc(Layer& layer, PrimSpec& prim,
                               const CompositionArc& arc,
                               const std::string& anchor_path, int depth) {
  const std::string self = prim.path().str();

  if (arc.is_internal) {
    // Internal arc: target prim lives in this same composed layer.
    const std::string& tp = arc.prim_path;
    if (tp.empty()) return;
    PrimSpec* target = layer.prim_at_path_mutable(tp);
    if (!target) {
      AddError("Internal reference target not found: " + tp, self, tp,
               arc.type);
      return;
    }
    if (tp == self) return;  // self-reference
    ResolveArcsForPrim(layer, *target, anchor_path, depth + 1);
    CopyLocalOpinions(prim, *target);
    GraftSubtree(layer, tp, self);
    return;
  }

  // External arc: load the referenced layer via the loader/cache.
  std::string resolved = arc.asset_path;
  if (resolver_) resolved = resolver_->ResolvePath(arc.asset_path, anchor_path);
  if (CheckCycle(resolved)) {
    AddError("Circular reference detected", self, arc.asset_path, arc.type);
    return;
  }
  PushStack(resolved);
  const Layer* ext = GetCachedLayer(resolved);
  if (!ext) {
    AddError("Failed to load reference: " + arc.asset_path, self,
             arc.asset_path, arc.type);
    PopStack();
    return;
  }
  std::string tp = arc.prim_path;
  if (tp.empty()) tp = "/" + ext->meta().defaultPrim;
  const PrimSpec* target = ext->prim_at_path(tp);
  if (!target) {
    AddError("Referenced prim not found: " + tp, self, arc.asset_path, arc.type);
    PopStack();
    return;
  }
  CopyLocalOpinions(prim, *target);
  GraftSubtree(*ext, tp, self);

  // Layer offset (applied at evaluation time via prim metadata).
  if (!arc.layer_offset.empty()) {
    double offset = 0.0, scale = 1.0;
    ParseLayerOffset(arc.layer_offset, offset, scale);
    if (offset != 0.0 || scale != 1.0) {
      prim.meta().layer_offset = std::make_pair(offset, scale);
    }
  }
  PopStack();
}

void Compositor::GraftSubtree(const Layer& src, const std::string& src_root,
                              const std::string& dst_root) {
  // Copy every descendant of src_root in `src` to the matching path under
  // dst_root, buffered in pending_graft_ (added after pass 2). Local overrides
  // already present in the result win (checked when appending).
  //
  // NOTE: scans the whole source layer by path prefix. A child-index DFS from
  // the root would be O(subtree) instead of O(layer_prims x graft_count), but
  // the layers grafted here are composed in place and do not reliably carry
  // child_indices, so the path-prefix scan is the correct form. (CoW Clone in
  // Phase 3 already removed the per-graft array-copy cost.)
  const std::string prefix = src_root + "/";
  for (size_t i = 0; i < src.prim_count(); ++i) {
    const PrimSpec* d = src.prim(static_cast<uint32_t>(i));
    if (!d) continue;
    const std::string& dp = d->path().str();
    if (dp.size() <= prefix.size() ||
        dp.compare(0, prefix.size(), prefix) != 0) {
      continue;  // not a descendant of src_root
    }
    std::string new_path = dst_root + dp.substr(src_root.size());
    if (!graft_paths_.insert(new_path).second) continue;  // already grafted
    PrimSpec g = d->Clone();
    g.set_path(Path(new_path));
    pending_graft_.push_back(std::move(g));
  }
}

bool Compositor::ApplyVariants(PrimSpec& prim, const Layer& layer, int depth) {
  (void)layer;
  (void)depth;
  if (!options_.resolve_variants) return true;

  // Apply EACH variant set's selected variant (a prim may select several sets).
  // Variant opinions are weaker than local opinions already on the prim, so use
  // the dedup-skip-existing copy. The per-set selection is `vs.selected`; fall
  // back to the legacy single-string `variantSelection` if that is unset.
  VariantSelection legacy = ParseVariantSelection(prim.meta().variantSelection);

  for (const auto& vs : prim.meta().variantSets) {
    std::string chosen = vs.selected;
    if (chosen.empty() && vs.name == legacy.variant_set) {
      chosen = legacy.variant_name;
    }
    if (chosen.empty()) continue;

    for (const auto& variant : vs.variants) {
      if (variant.name != chosen) continue;

      for (const auto& [prop_name, prop_val] : variant.properties) {
        if (!prim.property_value(prop_name)) {
          prim.add_property(prop_name, prop_val);
        }
      }
      for (const auto& [rel_name, targets] : variant.relationships) {
        if (!prim.relationship(rel_name)) {
          for (const auto& target : targets) prim.add_relationship(rel_name, target);
        }
      }
      if (!variant.doc.empty() && prim.meta().doc.empty()) {
        prim.meta().doc = variant.doc;
      }
      break;
    }

    // Read the selected variant's holder prim ("<prim>/{vset=sel}"): copy its
    // own opinions (the variant's properties on the owning prim, as authored in
    // the layer) and graft its descendant subtree (variant CHILD prims) under
    // <prim>. (The vs.variants loop above covers in-memory model variants; this
    // covers reader-produced variants whose content lives in the layer.)
    const std::string holder =
        prim.path().str() + "/{" + vs.name + "=" + chosen + "}";
    if (const PrimSpec* h = layer.prim_at_path(holder)) {
      CopyLocalOpinions(prim, *h);
    }
    GraftSubtree(layer, holder, prim.path().str());
  }

  return true;
}

const Layer* Compositor::GetCachedLayer(const std::string& path) {
  auto it = layer_cache_.find(path);
  if (it != layer_cache_.end()) {
    return it->second.get();
  }

  if (!layer_loader_) return nullptr;

  std::string error;
  auto layer = layer_loader_(path, &error);
  if (!layer) {
    AddError("Failed to load layer: " + error, "", path, ArcType::Reference);
    return nullptr;
  }

  Layer* result = layer.get();
  layer_cache_[path] = std::move(layer);
  return result;
}

void Compositor::ClearCache() {
  layer_cache_.clear();
}

void Compositor::AddError(const std::string& msg, const std::string& prim_path,
                           const std::string& arc_path, ArcType type) {
  CompositionError err;
  err.message = msg;
  err.prim_path = prim_path;
  err.arc_path = arc_path;
  err.arc_type = type;
  errors_.push_back(err);
}

bool Compositor::CheckCycle(const std::string& path) {
  return std::find(composition_stack_.begin(), composition_stack_.end(), path)
         != composition_stack_.end();
}

void Compositor::PushStack(const std::string& path) {
  composition_stack_.push_back(path);
}

void Compositor::PopStack() {
  if (!composition_stack_.empty()) {
    composition_stack_.pop_back();
  }
}

// ============================================================
// Static parsing methods
// ============================================================

CompositionArc Compositor::ParseReference(const std::string& ref_str) {
  CompositionArc arc;
  arc.type = ArcType::Reference;

  // Format: @asset_path@</prim/path> or </prim/path> for internal
  // Also supports: @asset_path@?layerOffset=offset:scale
  std::string str = ref_str;

  // Check for asset path
  if (!str.empty() && str[0] == '@') {
    size_t end = str.find('@', 1);
    if (end != std::string::npos) {
      arc.asset_path = str.substr(1, end - 1);
      str = str.substr(end + 1);
    }
  }

  // Check for prim path
  size_t prim_start = str.find('<');
  size_t prim_end = str.find('>');
  if (prim_start != std::string::npos && prim_end != std::string::npos) {
    arc.prim_path = str.substr(prim_start + 1, prim_end - prim_start - 1);
  }

  // Check for layer offset
  size_t offset_pos = str.find("layerOffset=");
  if (offset_pos != std::string::npos) {
    arc.layer_offset = str.substr(offset_pos + 12);
    // Trim trailing content
    size_t end_pos = arc.layer_offset.find_first_of(" \t\n\r)");
    if (end_pos != std::string::npos) {
      arc.layer_offset = arc.layer_offset.substr(0, end_pos);
    }
  }

  arc.is_internal = arc.asset_path.empty();

  return arc;
}

CompositionArc Compositor::ParsePayload(const std::string& payload_str) {
  CompositionArc arc = ParseReference(payload_str);
  arc.type = ArcType::Payload;
  return arc;
}

VariantSelection Compositor::ParseVariantSelection(const std::string& str) {
  VariantSelection sel;
  size_t eq = str.find('=');
  if (eq != std::string::npos) {
    sel.variant_set = str.substr(0, eq);
    sel.variant_name = str.substr(eq + 1);
  }
  return sel;
}

void Compositor::ParseLayerOffset(const std::string& offset_str,
                                   double& offset, double& scale) {
  offset = 0.0;
  scale = 1.0;

  // Format: "offset:scale" or just "offset"
  size_t colon = offset_str.find(':');
  if (colon == std::string::npos) {
    offset = std::atof(offset_str.c_str());
  } else {
    offset = std::atof(offset_str.substr(0, colon).c_str());
    scale = std::atof(offset_str.substr(colon + 1).c_str());
    if (scale == 0.0) scale = 1.0;
  }
}

// ============================================================
// Free utility functions
// ============================================================

void FlattenLayer(Layer& layer) {
  // Remove all composition arc metadata from prims
  for (size_t i = 0; i < layer.prim_count(); ++i) {
    PrimSpec* prim = layer.prim_mutable(static_cast<uint32_t>(i));
    if (!prim) continue;

    auto& meta = prim->meta();
    meta.references.clear();
    meta.payloads.clear();
    meta.inherits.clear();
    meta.specializes.clear();
    meta.variantSelection.clear();
    meta.variantSets.clear();
  }
}

std::vector<CompositionArc> GetExternalReferences(const Layer& layer) {
  std::vector<CompositionArc> result;

  for (size_t i = 0; i < layer.prim_count(); ++i) {
    const PrimSpec* prim = layer.prim(static_cast<uint32_t>(i));
    if (!prim) continue;

    for (const auto& ref_str : prim->meta().references) {
      CompositionArc arc = Compositor::ParseReference(ref_str);
      if (!arc.is_internal) {
        result.push_back(arc);
      }
    }
  }

  return result;
}

std::vector<CompositionArc> GetPayloads(const Layer& layer) {
  std::vector<CompositionArc> result;

  for (size_t i = 0; i < layer.prim_count(); ++i) {
    const PrimSpec* prim = layer.prim(static_cast<uint32_t>(i));
    if (!prim) continue;

    for (const auto& payload_str : prim->meta().payloads) {
      result.push_back(Compositor::ParsePayload(payload_str));
    }
  }

  return result;
}

bool HasCompositionArcs(const PrimSpec& prim) {
  const auto& meta = prim.meta();
  return !meta.references.empty() ||
         !meta.payloads.empty() ||
         !meta.inherits.empty() ||
         !meta.specializes.empty() ||
         !meta.variantSelection.empty() ||
         !meta.variantSets.empty();
}

}  // namespace next
}  // namespace tinyusdz
