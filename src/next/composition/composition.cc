// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Composition Implementation
// Full support: references, payloads, inherits, specializes, variants, layer offsets

#include "composition.hh"
#include "../../external/fast_float/include/fast_float/fast_float.h"
#include <algorithm>
#include <cstring>
#include <system_error>

namespace tinyusdz {
namespace next {

namespace {

// Does the prim author a composition arc that must be expanded (references /
// payloads / inherits / specializes)? Variants are intentionally excluded: their
// selection is deferred to the referencing prim, so a variants-only prim needs
// no pre-composition.
bool PrimHasComposableArcs(const PrimSpec& p) {
  const auto& m = p.meta();
  return !m.references.empty() || !m.payloads.empty() || !m.inherits.empty() ||
         !m.specializes.empty();
}

// Does any prim in `layer` (or a sublayer) author a composable arc?
bool LayerHasComposableArcs(const Layer& layer) {
  for (size_t i = 0; i < layer.prim_count(); ++i) {
    const PrimSpec* p = layer.prim(static_cast<uint32_t>(i));
    if (p && PrimHasComposableArcs(*p)) return true;
  }
  return !layer.meta().subLayers.empty();
}

// Is `path` equal to `root` or a descendant of it?
bool PathInSubtree(const std::string& path, const std::string& root,
                   const std::string& root_slash) {
  return path == root || (path.size() > root_slash.size() &&
                          path.compare(0, root_slash.size(), root_slash) == 0);
}

// Does the prim at `root_path` in `layer`, OR any of its descendants, author a
// composable arc? Lets a reference to an arc-free prim of an otherwise
// arc-bearing library layer skip whole-layer pre-composition (composing an
// arc-free subtree changes nothing, so grafting the raw layer is identical).
bool SubtreeHasComposableArcs(const Layer& layer, const std::string& root_path) {
  const std::string prefix = root_path + "/";
  for (size_t i = 0; i < layer.prim_count(); ++i) {
    const PrimSpec* p = layer.prim(static_cast<uint32_t>(i));
    if (p && PathInSubtree(p->path().str(), root_path, prefix) &&
        PrimHasComposableArcs(*p)) {
      return true;
    }
  }
  return false;
}

// Can the prim at `root_path` (+ descendants) be composed IN ISOLATION — i.e.
// does every composition arc it authors resolve either externally, or to a prim
// WITHIN the subtree? Only then is composing just the subtree identical to
// composing it inside the whole layer, so the unreferenced bulk of a large
// library layer need not be materialized. Conservative: sublayers, any
// inherit/specialize (these target class prims that virtually always live
// outside the subtree), or any INTERNAL reference/payload whose target escapes
// the subtree make it false (→ whole-layer composition, which is always safe).
bool SubtreeIsSelfContained(const Layer& layer, const std::string& root_path) {
  if (!layer.meta().subLayers.empty()) return false;
  const std::string prefix = root_path + "/";
  for (size_t i = 0; i < layer.prim_count(); ++i) {
    const PrimSpec* p = layer.prim(static_cast<uint32_t>(i));
    if (!p || !PathInSubtree(p->path().str(), root_path, prefix)) continue;
    const auto& m = p->meta();
    if (!m.inherits.empty() || !m.specializes.empty()) return false;
    for (const auto& r : m.references) {
      const CompositionArc a = Compositor::ParseReference(r);
      if (a.is_internal && !PathInSubtree(a.prim_path, root_path, prefix)) {
        return false;
      }
    }
    for (const auto& pl : m.payloads) {
      const CompositionArc a = Compositor::ParsePayload(pl);
      if (a.is_internal && !PathInSubtree(a.prim_path, root_path, prefix)) {
        return false;
      }
    }
  }
  return true;
}

// Clone the prim at `root_path` and its descendants into a fresh layer (paths
// kept; `root_path` prim is the layer root). Used to compose a self-contained
// referenced subtree alone. child_indices on the clones reference the source
// layer and are stale, but composition (flat iteration + path lookups, with
// GraftSubtree validating / falling back to a path scan) does not rely on them
// — the same way Compose()'s own output carries stale child_indices.
std::unique_ptr<Layer> ExtractSubtree(const Layer& src,
                                      const std::string& root_path) {
  auto out = std::make_unique<Layer>();
  const std::string prefix = root_path + "/";
  for (size_t i = 0; i < src.prim_count(); ++i) {
    const PrimSpec* p = src.prim(static_cast<uint32_t>(i));
    if (!p) continue;
    const std::string& pp = p->path().str();
    if (!PathInSubtree(pp, root_path, prefix)) continue;
    const uint32_t idx = out->add_prim(p->Clone());
    if (pp == root_path) out->add_root(idx);
  }
  out->build_path_index();
  return out;
}

}  // namespace

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
  // Top-level entry only (Compose re-enters itself for sublayers): reset the
  // stronger-layer arc-delete map, then seed it from this layer so weaker
  // sublayers see the deletes while THEIR arcs resolve.
  const bool nested_sublayer_compose =
      composing_sublayers_ && !composing_sublayers_->empty();
  if (!nested_sublayer_compose) pending_arc_deletes_.clear();
  CollectArcDeletes(root_layer);

  auto result = std::make_unique<Layer>();
  result->meta() = root_layer.meta();

  // Pass 1 — merge local opinions. ComposePrim/CopyLocalOpinions use
  // fill-absent semantics (an opinion already on the target wins), so the
  // STRONGEST layer must be merged FIRST: the root layer, then sublayers in
  // authored order (earlier sublayers are stronger). Arc-free scenes are
  // fully composed by this pass alone.
  ComposeLayer(*result, root_layer, anchor_path, 0);
  if (!root_layer.meta().subLayers.empty()) {
    auto sublayer_base = ComposeSublayers(root_layer, anchor_path);
    if (sublayer_base) {
      ComposeLayer(*result, *sublayer_base, anchor_path, 0);
    }
  }

  // Pass 2 — expand composition arcs over the merged layer. Build the path
  // index first so internal-arc / inherit / specialize lookups resolve (the
  // layer is not finalized yet). Iterate by index up to the current count;
  // ResolveArcsForPrim only mutates existing prims (grafted subtrees are
  // buffered in pending_graft_ and appended afterward).
  // The recursive sublayer composition above shares this compositor's state
  // and marked its own prims in arc_resolved_ — those markers must not
  // suppress THIS layer's pass (deferred variants/arcs on merged prims would
  // silently never resolve).
  arc_resolved_.clear();
  result->build_path_index();
  size_t n = result->prim_count();
  for (size_t i = 0; i < n; ++i) {
    PrimSpec* p = result->prim_mutable(static_cast<uint32_t>(i));
    if (p) ResolveArcsForPrim(*result, *p, anchor_path, 0);
  }
  // Append grafted subtree prims brought in by references/payloads, then
  // resolve the grafted prims' OWN arcs (anchored to the layer they came
  // from) — referenced files commonly reference further files relative to
  // themselves (e.g. a mesh asset referencing ../../Materials/x.usd). Each
  // round may graft more subtrees; iterate to a fixed point (depth-capped).
  for (int round = 0; round < 64 && !pending_graft_.empty(); ++round) {
    std::vector<PendingGraft> batch = std::move(pending_graft_);
    pending_graft_.clear();
    std::vector<std::pair<size_t, std::string>> added;  // index in result, anchor
    for (auto& g : batch) {
      std::string gp = g.prim.path().str();
      // Variant-holder prims and unselected variant content keep a "{...}"
      // segment in their path. Drop them only for a final variant-resolving
      // flatten; when composing an external layer with variants deferred, the
      // caller still needs those descendants so its selected holder can be
      // remapped into the host namespace.
      if (options_.resolve_variants && gp.find('{') != std::string::npos) {
        continue;
      }
      if (PrimSpec* existing = result->prim_at_path_mutable(gp)) {
        // A local spec already exists at this path (e.g. the root layer
        // authors `over "LOD1"` material-binding overrides on a prim whose
        // definition arrives via a referenced file's variant). Local opinions
        // win; the graft fills in type/properties/children content.
        CopyLocalOpinions(*existing, g.prim);
        const uint32_t existing_index = result->index_at_path(gp);
        if (existing_index != UINT32_MAX) {
          arc_resolved_.erase(gp);
          added.emplace_back(existing_index, g.anchor);
        }
        continue;
      }
      added.emplace_back(result->prim_count(), g.anchor);
      result->add_prim(std::move(g.prim));
    }
    if (added.empty()) break;
    result->build_path_index();
    for (const auto& a : added) {
      PrimSpec* p = result->prim_mutable(static_cast<uint32_t>(a.first));
      if (p) ResolveArcsForPrim(*result, *p, a.second, 0);
    }
  }
  pending_graft_.clear();
  graft_paths_.clear();

  // The flattened output has all sublayer opinions baked in: keeping the
  // subLayers list would re-apply (now stale) layers on re-read. Fill stage
  // metadata gaps from sublayers first (defaultPrim authored only in a
  // sublayer must survive), then drop the list.
  if (!result->meta().subLayers.empty()) {
    if (result->meta().defaultPrim.empty()) {
      for (const std::string& sl : result->meta().subLayers) {
        std::string resolved = sl;
        if (resolver_) resolved = resolver_->ResolvePath(sl, anchor_path);
        if (const Layer* sub = GetCachedLayer(resolved)) {
          if (!sub->meta().defaultPrim.empty()) {
            result->meta().defaultPrim = sub->meta().defaultPrim;
            break;
          }
        }
      }
    }
    result->meta().subLayers.clear();
  }

  // The crate writer's compressed-paths encoding requires ancestors before
  // descendants with contiguous subtrees; grafted prims were appended at the
  // end, so restore hierarchical order first.
  result->sort_prims_by_path();

  // Finalize the composed layer
  result->finalize();

  return result;
}

std::unique_ptr<Layer> Compositor::ComposeSublayers(const Layer& root_layer,
                                                      const std::string& anchor_path) {
  const auto& sublayer_paths = root_layer.meta().subLayers;
  if (sublayer_paths.empty()) return nullptr;

  // Sublayer cycle guard: a.usda <-> b.usda used to recurse Compose ->
  // ComposeSublayers unbounded (stack-overflow SEGV). Track visiting resolved
  // paths across the recursion (shared like composing_ext_) and skip repeats
  // like pxr does (warn + break the cycle).
  if (!composing_sublayers_) {
    composing_sublayers_ = std::make_shared<std::set<std::string>>();
  }

  auto result = std::make_unique<Layer>();
  result->meta() = root_layer.meta();

  // Sublayers are ordered strongest-first in the USD spec, and the merge
  // below is fill-absent (first opinion wins) — so process in AUTHORED order
  // (strongest first). The previous weakest-first iteration inverted
  // sublayer strength.
  const auto& sublayer_offsets = root_layer.meta().subLayerOffsets;
  for (auto it = sublayer_paths.begin(); it != sublayer_paths.end(); ++it) {
    const size_t sub_index = static_cast<size_t>(it - sublayer_paths.begin());
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

    if (composing_sublayers_->count(resolved_path)) {
      AddError("Sublayer cycle detected (skipped): " + resolved_path,
               "", sublayer_path, ArcType::Local);
      continue;
    }

    const Layer* sublayer = GetCachedLayer(resolved_path);
    if (!sublayer) {
      AddError("Failed to load sublayer: " + sublayer_path,
               "", sublayer_path, ArcType::Local);
      continue;
    }

    composing_sublayers_->insert(resolved_path);
    // Defer VARIANT resolution inside the sublayer: a variant set and its
    // selection may live in DIFFERENT layers of the stack (set in root,
    // selection in a sublayer or vice versa). Resolving per-sublayer consumed
    // the set before the cross-layer merge, so they never met. The merged
    // top-level pass applies variants once, over the full stack.
    const bool saved_rv = options_.resolve_variants;
    options_.resolve_variants = false;
    auto composed_sub = Compose(*sublayer, resolved_path);
    options_.resolve_variants = saved_rv;
    composing_sublayers_->erase(resolved_path);
    if (composed_sub) {
      // Apply the authored per-sublayer layer offset: every time sample the
      // sublayer contributes maps t -> t*scale + offset in the root's time
      // space. (Nested sublayer offsets were already baked by the recursive
      // Compose above, so this composes correctly through nesting.)
      if (sub_index < sublayer_offsets.size()) {
        const double off = sublayer_offsets[sub_index].first;
        const double scl = sublayer_offsets[sub_index].second;
        if (off != 0.0 || scl != 1.0) {
          for (size_t pi = 0; pi < composed_sub->prim_count(); ++pi) {
            if (PrimSpec* p = composed_sub->prim_mutable(static_cast<uint32_t>(pi))) {
              p->remap_time_sample_times(off, scl);
            }
          }
        }
      }
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

  // The dedup lookup below needs a current path index. Without it every
  // source prim was cloned AGAIN into the target (one duplicate spec per
  // contributing layer), and the crate round-trip of a sublayered flatten
  // produced multiple same-path specs whose values collapsed into husks.
  target.build_path_index();

  // Compose each prim from source_layer into target.
  std::vector<uint32_t> added;  // indices of freshly cloned prims
  for (size_t i = 0; i < source_layer.prim_count(); ++i) {
    const PrimSpec* src_prim = source_layer.prim(static_cast<uint32_t>(i));
    if (!src_prim) continue;

    std::string prim_path = src_prim->path().str();
    PrimSpec* target_prim = target.prim_at_path_mutable(prim_path);

    if (!target_prim) {
      // Create new prim in target layer via Clone(). The clone's
      // child_indices reference the SOURCE layer and are meaningless here;
      // links are re-established from paths below.
      PrimSpec new_prim = src_prim->Clone();
      new_prim.clear_child_indices();
      uint32_t idx = target.add_prim(std::move(new_prim));
      added.push_back(idx);
    } else {
      // Existing prim - compose source onto it
      ComposePrim(*target_prim, source_layer, *src_prim, anchor_path, depth);
    }
  }

  // Link freshly added prims into the hierarchy: child of an existing (or
  // just-added) parent, or a root prim. (Previously every clone was added as
  // a ROOT, so sublayer children leaked to the stage root.) One index rebuild
  // covers all adds; a well-formed source has no duplicate paths, so the
  // in-loop lookups never needed the additions.
  if (!added.empty()) target.build_path_index();
  for (uint32_t idx : added) {
    const std::string path = target.prim(idx)->path().str();
    size_t slash = path.find_last_of('/');
    if (slash == 0 || slash == std::string::npos) {
      target.add_root(idx);
      continue;
    }
    uint32_t pidx = target.index_at_path(path.substr(0, slash));
    if (pidx != UINT32_MAX) {
      target.set_parent(idx, pidx);
    } else {
      target.add_root(idx);  // orphan without a composed parent
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
  // Pass 1 only merges local opinions across layers (root strongest first,
  // then sublayers). CopyLocalOpinions fills in only what `target` lacks.
  // Composition arcs are expanded later in pass 2 (ResolveArcsForPrim), once
  // the full local-opinion layer exists — so the ARCS themselves must merge
  // here (append weaker after stronger; stronger arcs resolve first and win
  // under fill-absent), or a sublayer-defined prim's references vanish when
  // a stronger layer also has a spec for it.
  CopyLocalOpinions(target, source);
  // A STRONGER layer's `delete <arc>` list-edit removes matching arcs coming
  // from weaker layers (previously the union ignored ArcEdits entirely, so
  // `delete references = @a@` in the root still composed @a@ from a sublayer).
  auto deleted_by = [](const ArcEdit* e, const std::string& a) {
    return e && e->authored &&
           std::find(e->deleted.begin(), e->deleted.end(), a) != e->deleted.end();
  };
  auto merge_arcs = [&](std::vector<std::string>& dst,
                        const std::vector<std::string>& src,
                        const ArcEdit* tgt_edit) {
    for (const std::string& a : src) {
      if (deleted_by(tgt_edit, a)) continue;
      if (std::find(dst.begin(), dst.end(), a) == dst.end()) {
        dst.push_back(a);
      }
    }
  };
  const ArcListOpEdits* te = target.meta().arc_edits();
  merge_arcs(target.meta().references, source.meta().references,
             te ? &te->references : nullptr);
  merge_arcs(target.meta().payloads, source.meta().payloads,
             te ? &te->payloads : nullptr);
  merge_arcs(target.meta().inherits, source.meta().inherits,
             te ? &te->inherits : nullptr);
  merge_arcs(target.meta().specializes, source.meta().specializes,
             te ? &te->specializes : nullptr);
  // Propagate the source's own delete edits down the stack (they must also
  // remove arcs authored in still-weaker layers merged later).
  if (const ArcListOpEdits* se = source.meta().arc_edits()) {
    auto merge_deleted = [&](const ArcEdit& from, ArcEdit& into) {
      if (!from.authored || from.deleted.empty()) return;
      into.authored = true;
      if (into.deleted.empty() && into.prepended.empty() &&
          into.appended.empty() && into.ordered.empty()) {
        into.is_explicit = false;
      }
      for (const std::string& d : from.deleted) {
        if (std::find(into.deleted.begin(), into.deleted.end(), d) ==
            into.deleted.end()) {
          into.deleted.push_back(d);
        }
      }
    };
    ArcListOpEdits& tedits = target.meta().ensure_arc_edits();
    merge_deleted(se->references, tedits.references);
    merge_deleted(se->payloads, tedits.payloads);
    merge_deleted(se->inherits, tedits.inherits);
    merge_deleted(se->specializes, tedits.specializes);
  }
  // Variant sets/selections: fill-absent by set name.
  if (!source.meta().variantSets().empty()) {
    for (const VariantSetData& svs : source.meta().variantSets()) {
      bool have = false;
      for (const VariantSetData& tvs : target.meta().variantSets()) {
        if (tvs.name == svs.name) {
          have = true;
          break;
        }
      }
      if (!have) target.meta().variantSets().push_back(svs);
    }
  }
  if (!source.meta().variantSelections().empty()) {
    for (const auto& sel : source.meta().variantSelections()) {
      bool have = false;
      for (const auto& t : target.meta().variantSelections()) {
        if (t.first == sel.first) {
          have = true;
          break;
        }
      }
      if (!have) target.meta().variantSelections().push_back(sel);
    }
  }
  if (target.meta().variantSelection.empty() &&
      !source.meta().variantSelection.empty()) {
    target.meta().variantSelection = source.meta().variantSelection;
  }
  return true;
}

void Compositor::CopyLocalOpinions(
    PrimSpec& target, const PrimSpec& source, double time_offset,
    double time_scale,
    const std::function<std::string(const std::string&)>& remap_path) {
  // Remap a relationship/connection target. An empty remap result means the
  // target is not expressible in the composed namespace (outside the arc's
  // scope) and must be DROPPED, not copied verbatim.
  auto map_target = [&](const Path& p) -> Path {
    return remap_path ? Path(remap_path(p.str())) : p;
  };
  auto target_mappable = [&](const Path& p) -> bool {
    if (!remap_path) return true;
    return p.str().empty() || !remap_path(p.str()).empty();
  };
  // Copy type name if target doesn't have one
  if (target.type_name().empty() && !source.type_name().empty()) {
    target.set_type_name(source.type_name());
  }

  // Specifier: composed prim existence is the union of opinions — an `over`
  // backed by a `def` from any arc (reference / variant content) flattens to
  // `def`, matching pxr (an over alone defines nothing; the def makes the
  // prim real).
  if (target.specifier() == PrimSpecifier::Over &&
      source.specifier() == PrimSpecifier::Def) {
    target.set_specifier(PrimSpecifier::Def);
  }

  // Copy properties (source overrides target for time-sampled props).
  // Preserves valueless slots (connection-only / declared-only attributes),
  // their declared type names, and connection targets for USDC fidelity.
  PropNameTable& name_table = GetPropNameTable();
  for (const auto& slot : source.properties().slots()) {
    const std::string& pname = name_table.get(slot.name_id);
    const PropSlot* tgt_slot = target.property(slot.name_id);
    if (tgt_slot) {
      // Field-level fill-absent: pxr composes a property's default VALUE and its
      // CONNECTIONS as INDEPENDENT fields. A stronger source may author only one
      // of them; fill the other from this weaker source rather than dropping it.
      // (e.g. a value-only override `metallic = 0` over a base
      // `metallic = 0 (+ .connect)` must keep the connection; and the mirror.)
      if (tgt_slot->value_offset == UINT32_MAX) {
        if (const Value* sv = source.property_value(slot.name_id)) {
          target.fill_property_value_if_absent(slot.name_id, *sv);
        }
      }
      const std::vector<Path>* tconns = target.connection(pname);
      if (!tconns || tconns->empty()) {
        if (const std::vector<Path>* sconns = source.connection(pname)) {
          for (const auto& c : *sconns) {
            if (!target_mappable(c)) continue;
            target.add_connection(pname, map_target(c));
          }
        }
      }
      // Property METADATA is likewise per-field: a stronger value-only
      // override must not drop the weaker layer's interpolation / elementSize
      // / customData (render-breaking for primvars). Fill absent fields.
      if (const PropMeta* spm = source.property_meta(slot.name_id)) {
        const PropMeta* tpm = target.property_meta(slot.name_id);
        if (!tpm || tpm->authored == 0) {
          target.ensure_property_meta(pname) = *spm;
        } else if ((spm->authored & ~tpm->authored) != 0) {
          PropMeta& dst = target.ensure_property_meta(pname);
          const uint32_t missing = spm->authored & ~dst.authored;
          if (missing & PropMeta::kInterpolation) dst.interpolation = spm->interpolation;
          if (missing & PropMeta::kElementSize) dst.elementSize = spm->elementSize;
          if (missing & PropMeta::kColorSpace) dst.colorSpace = spm->colorSpace;
          if (missing & PropMeta::kCustomData) dst.customData = spm->customData;
          if (missing & PropMeta::kDoc) dst.doc = spm->doc;
          if (missing & PropMeta::kDisplayName) dst.displayName = spm->displayName;
          dst.authored |= missing;
        }
      }
      // Variability (uniform) fills from the weaker source too.
      if ((slot.flags & PropSlot::kFlagUniform) &&
          !(tgt_slot->flags & PropSlot::kFlagUniform)) {
        if (PropSlot* ms = target.property_mutable(slot.name_id)) {
          ms->flags |= PropSlot::kFlagUniform;
        }
      }
      continue;  // target opinion otherwise wins (incl. time-sampled merge)
    }
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
      for (const auto& c : *conns) {
        if (!target_mappable(c)) continue;
        target.add_connection(pname, map_target(c));
      }
    }
    if (const PropMeta* pm = source.property_meta(slot.name_id)) {
      target.ensure_property_meta(pname) = *pm;
    }
  }

  // Copy relationships not already present on the target.
  for (const auto& rel_name : source.relationship_names()) {
    if (target.relationship(rel_name)) continue;
    if (const std::vector<Path>* tgts = source.relationship(rel_name)) {
      for (const auto& t : *tgts) {
        if (!target_mappable(t)) continue;
        target.add_relationship(rel_name, map_target(t));
      }
    }
    if (const PropMeta* pm = source.property_meta(rel_name)) {
      target.ensure_property_meta(rel_name) = *pm;
    }
  }

  // Copy time-sampled properties
  for (auto ts_prop_id : source.time_sampled_properties()) {
    auto* samples = source.time_samples(ts_prop_id);
    if (!samples) continue;

    // Check if target already has time samples for this property. A stronger
    // authored DEFAULT also blocks weaker samples: pxr resolves an attribute
    // from the strongest spec with any value opinion, so samples from a weaker
    // layer must not ride along under a stronger default (they would win at
    // evaluation time — samples beat defaults only WITHIN one spec).
    bool target_has_ts = target.has_time_samples(ts_prop_id);
    if (!target_has_ts) {
      if (const PropSlot* tslot = target.property(ts_prop_id)) {
        if (tslot->value_offset != UINT32_MAX &&
            !source.property(ts_prop_id)) {
          // The default came from a STRONGER spec (this weaker source's own
          // default, if any, was already skipped by fill-absent): block.
          continue;
        }
        if (tslot->value_offset != UINT32_MAX && source.property(ts_prop_id)) {
          // Both target and source author this property; if the target's
          // default was filled FROM this source the samples belong with it,
          // otherwise a stronger spec authored it. Distinguish via pointer
          // identity of the values.
          const Value* tv = target.property_value(ts_prop_id);
          const Value* sv = source.property_value(ts_prop_id);
          if (tv && sv && !(*tv == *sv)) continue;
        }
      }
      // Copy time samples from source to target, remapping the sample time by
      // the layer offset (t -> time_offset + time_scale*t).
      for (const auto& [time, val_offset] : *samples) {
        const Value* val = source.time_sample_value(val_offset);
        if (val) {
          target.add_time_sample(ts_prop_id, time_offset + time_scale * time,
                                 *val);
        }
      }
    }
  }

  // Copy metadata fields (fill-absent: a stronger/earlier opinion already on the
  // target wins; weaker opinions only fill gaps).
  if (!source.meta().doc().empty() && target.meta().doc().empty()) {
    target.meta().doc() = source.meta().doc();
  }
  // active/hidden: fill-absent on AUTHORED opinions only. The previous
  // "copy on difference" let a weaker spec's DEFAULT (active = true) clobber
  // a stronger authored `active = false` — the inverse of LIVRPS (pxr prunes
  // such prims; we un-deactivated them).
  if (source.meta().active_authored && !target.meta().active_authored) {
    target.meta().active = source.meta().active;
    target.meta().active_authored = true;
  }
  if (source.meta().hidden_authored && !target.meta().hidden_authored) {
    target.meta().hidden = source.meta().hidden;
    target.meta().hidden_authored = true;
  }
  if ((source.meta().instanceable || source.meta().instanceable_authored) &&
      !target.meta().instanceable_authored && !target.meta().instanceable) {
    target.meta().instanceable = source.meta().instanceable;
    target.meta().instanceable_authored =
        source.meta().instanceable_authored || source.meta().instanceable;
  }
  if (!source.meta().comment().empty() && target.meta().comment().empty()) {
    target.meta().comment() = source.meta().comment();
  }
  if (!source.meta().kind().empty() && target.meta().kind().empty()) {
    target.meta().kind() = source.meta().kind();
  }
  if (!source.meta().displayName().empty() &&
      target.meta().displayName().empty()) {
    target.meta().displayName() = source.meta().displayName();
  }
  if (target.meta().apiSchemas().empty() &&
      !source.meta().apiSchemas().empty()) {
    target.meta().apiSchemas() = source.meta().apiSchemas();
  }
  // Dictionary-valued metadata (fill-absent). `ct` binds a const view so the
  // gap check never allocates the target's metadata ext.
  {
    const PrimSpecMeta& cs = source.meta();
    const PrimSpecMeta& ct = target.meta();
    if (cs.customData().is_dictionary() && !ct.customData().is_dictionary())
      target.meta().customData() = cs.customData();
    if (cs.assetInfo().is_dictionary() && !ct.assetInfo().is_dictionary())
      target.meta().assetInfo() = cs.assetInfo();
    if (cs.sdrMetadata().is_dictionary() && !ct.sdrMetadata().is_dictionary())
      target.meta().sdrMetadata() = cs.sdrMetadata();
    if (cs.clips().is_dictionary() && !ct.clips().is_dictionary())
      target.meta().clips() = cs.clips();
  }

  // Variant sets + selections ride along from the weaker `source`, MERGED
  // per-set / per-variant (not all-or-nothing). The target (stronger) keeps its
  // own opinions; the source fills in what is absent. This matters because a
  // stronger layer may DECLARE a variantSet with empty variant blocks while the
  // actual variant CONTENT is authored in a weaker layer reached via
  // payload/reference (e.g. Pixar Kitchen_set: Chair.usd declares empty
  // ChairA/ChairB blocks, the geometry lives in Chair.geom.usd two arcs deeper).
  // An all-or-nothing copy would drop that content because the target "already
  // has" the (empty) set. The selection itself is left to the referencing prim
  // (ApplyVariants runs after arc resolution and is strongest).
  if (!source.meta().variantSets().empty()) {
    std::vector<VariantSetData>& tsets = target.meta().variantSets();
    // The host (target) may express its selection as the legacy variantSelection
    // STRING rather than a per-set `selected`. The crate reader sets both, but a
    // host carrying only the string must still win over a referenced asset's
    // (weaker) per-set `selected`, which would otherwise ride along this copy and
    // take precedence in ApplyVariants. Suppress the source `selected` for the
    // set the host's string already selects (ApplyVariants then falls back to the
    // host's string).
    const VariantSelection target_sel =
        ParseVariantSelection(target.meta().variantSelection);
    for (const auto& ssvs : source.meta().variantSets()) {
      const bool host_string_selects =
          !target_sel.variant_set.empty() && ssvs.name == target_sel.variant_set;
      VariantSetData* tvs = nullptr;
      for (auto& t : tsets) {
        if (t.name == ssvs.name) { tvs = &t; break; }
      }
      if (!tvs) {
        tsets.push_back(ssvs);  // whole set is new to the target
        if (host_string_selects) tsets.back().selected.clear();  // host wins
        continue;
      }
      if (tvs->selected.empty() && !host_string_selects) {
        tvs->selected = ssvs.selected;
      }
      for (const auto& svar : ssvs.variants) {
        VariantData* tvar = nullptr;
        for (auto& v : tvs->variants) {
          if (v.name == svar.name) { tvar = &v; break; }
        }
        if (!tvar) {
          tvs->variants.push_back(svar);  // variant option new to the target
          continue;
        }
        // Shared variant option: fill-absent its content.
        for (const auto& sp : svar.properties) {
          bool have = false;
          for (const auto& tp : tvar->properties) {
            if (tp.name == sp.name) { have = true; break; }
          }
          if (!have) tvar->properties.push_back(sp);
        }
        for (const auto& sr : svar.relationships) {
          if (!tvar->relationships.count(sr.first)) {
            tvar->relationships[sr.first] = sr.second;
          }
        }
        if (!tvar->content && svar.content) tvar->content = svar.content;
        if (tvar->doc.empty() && !svar.doc.empty()) tvar->doc = svar.doc;
        if (tvar->variantSets.empty() && !svar.variantSets.empty()) {
          tvar->variantSets = svar.variantSets;
        }
      }
    }
  }
  if (target.meta().variantSelection.empty() &&
      !source.meta().variantSelection.empty()) {
    target.meta().variantSelection = source.meta().variantSelection;
  }
  if (!source.meta().variantSelections().empty() &&
      target.meta().variantSelections().empty()) {
    target.meta().variantSelections() = source.meta().variantSelections();
  }
}

void Compositor::CollectArcDeletes(const Layer& layer) {
  for (const PrimSpec& prim : layer.prims()) {
    const ArcListOpEdits* e = prim.meta().arc_edits();
    if (!e) continue;
    auto take = [&](const ArcEdit& a) {
      if (!a.authored || a.deleted.empty()) return;
      auto& set = pending_arc_deletes_[prim.path().str()];
      set.insert(a.deleted.begin(), a.deleted.end());
    };
    take(e->references);
    take(e->payloads);
    take(e->inherits);
    take(e->specializes);
  }
}

bool Compositor::ArcDeletedByStronger(const std::string& prim_path,
                                      const std::string& arc) const {
  auto it = pending_arc_deletes_.find(prim_path);
  return it != pending_arc_deletes_.end() && it->second.count(arc) > 0;
}

void Compositor::ResolveArcsForPrim(Layer& layer, PrimSpec& prim,
                                    const std::string& anchor_path, int depth) {
  if (depth > options_.max_depth) return;
  const std::string self = prim.path().str();
  // Mark before recursing: prevents infinite recursion on cycles and avoids
  // recomposing a prim referenced by several others.
  if (!arc_resolved_.insert(self).second) return;

  // Opinion merging is fill-absent (an opinion already on the prim wins), so
  // arcs must be applied in LIVRPS strength order: local (already present) >
  // inherits > variants > references > payloads > specializes. References
  // were previously applied FIRST, making them stronger than inherits and
  // variants — the inverse of USD semantics.

  // Inherits — copy opinions from same-layer class prims (resolve them
  // first). Arc strings use the canonical "</path>" (or "@asset@</path>")
  // encoding; decode before the path lookup (raw strings never matched, so
  // every inherit failed with "class not found").
  if (options_.resolve_inherits) {
    for (const auto& inh : prim.meta().inherits) {
      CompositionArc arc = ParseReference(inh);
      // Programmatically-built layers store a bare prim path; the USDA
      // parser stores the canonical "</path>" form. Accept both.
      if (arc.prim_path.empty() && arc.asset_path.empty() && !inh.empty() &&
          inh[0] == '/') {
        arc.prim_path = inh;
      }
      if (!arc.is_internal) {
        AddError("External inherits are not supported by the flatten "
                 "compositor: " + inh, self, inh, ArcType::Inherits);
        continue;
      }
      PrimSpec* cls = layer.prim_at_path_mutable(arc.prim_path);
      if (!cls) {
        AddError("Inherited class not found: " + arc.prim_path, self, inh,
                 ArcType::Inherits);
        continue;
      }
      ResolveArcsForPrim(layer, *cls, anchor_path, depth + 1);
      CopyLocalOpinions(prim, *cls);
      GraftSubtree(layer, anchor_path, arc.prim_path, self);
    }
  }

  // Variants (if the reader populated variant content).
  if (options_.resolve_variants) {
    ApplyVariants(prim, layer, anchor_path, depth);
  }

  // References then payloads — both bring in a target prim's opinions plus
  // its descendant subtree. Arcs deleted by a STRONGER layer are skipped
  // (visible here via pending_arc_deletes_ during sublayer composition).
  for (const auto& ref_str : prim.meta().references) {
    if (ArcDeletedByStronger(self, ref_str)) continue;
    ResolveRefArc(layer, prim, ParseReference(ref_str), anchor_path, depth);
  }
  if (options_.load_payloads) {
    for (const auto& pl_str : prim.meta().payloads) {
      if (ArcDeletedByStronger(self, pl_str)) continue;
      ResolveRefArc(layer, prim, ParsePayload(pl_str), anchor_path, depth);
    }
  }

  // Variants again: references/payloads may have merged variant SETS defined
  // in the referenced asset (host selects a referenced set). Their opinions
  // compose at reference strength, which fill-absent gives us here; sets
  // already applied in the first pass are idempotent (fill-absent no-ops).
  if (options_.resolve_variants) {
    ApplyVariants(prim, layer, anchor_path, depth);
  }

  // Specializes (weakest) — same-layer, fill only what is still missing.
  if (options_.resolve_specializes) {
    for (const auto& sp : prim.meta().specializes) {
      CompositionArc sarc = ParseReference(sp);
      if (sarc.prim_path.empty() && sarc.asset_path.empty() && !sp.empty() &&
          sp[0] == '/') {
        sarc.prim_path = sp;
      }
      PrimSpec* spec = sarc.is_internal
                           ? layer.prim_at_path_mutable(sarc.prim_path)
                           : nullptr;
      if (!spec) {
        AddError("Specialized prim not found: " + sarc.prim_path, self, sp,
                 ArcType::Specializes);
        continue;
      }
      ResolveArcsForPrim(layer, *spec, anchor_path, depth + 1);
      CopyLocalOpinions(prim, *spec);
      GraftSubtree(layer, anchor_path, sarc.prim_path, self);
    }
  }

  // Flatten: drop the now-resolved arcs so they are not re-emitted.
  prim.meta().references.clear();
  prim.meta().payloads.clear();
  prim.meta().inherits.clear();
  prim.meta().specializes.clear();
  // Also drop the list-op EDITS: the writers re-emit arcs from surviving
  // qualifiers, so a flattened layer would otherwise carry phantom
  // `prepend references = ...` metadata (pxr then errors on it and would
  // re-compose on top of the baked content).
  prim.meta().clear_arc_edits();
  // Variant sets/selection are consumed ONLY when variants were actually baked
  // (resolve_variants). When variant resolution is DEFERRED (a referenced layer
  // composed by GetComposedExternalLayer, whose variant selection belongs to the
  // referencing prim), the variant metadata must survive so it can be merged
  // into — and baked by — the host. variantSets lives in the lazily-allocated
  // ext; only touch it (the mutable accessor would otherwise allocate an empty
  // ext for every ext-free prim) when an ext already exists.
  if (options_.resolve_variants) {
    if (prim.meta().has_ext()) {
      prim.meta().variantSets().clear();
      prim.meta().variantSelections().clear();
    }
    prim.meta().variantSelection.clear();
  }
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
    // Retarget relationship/connection paths from the referenced prim's
    // namespace into the host's (targets outside the referenced subtree are
    // unmappable and dropped, matching pxr).
    auto remap = [&](const std::string& p) -> std::string {
      if (p == tp) return self;
      if (p.size() > tp.size() && p.compare(0, tp.size(), tp) == 0 &&
          (p[tp.size()] == '/' || p[tp.size()] == '.')) {
        return self + p.substr(tp.size());
      }
      return std::string();
    };
    CopyLocalOpinions(prim, *target, 0.0, 1.0, remap);
    GraftSubtree(layer, anchor_path, tp, self);
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
  const Layer* raw = GetCachedLayer(resolved);
  if (!raw) {
    AddError("Failed to load reference: " + arc.asset_path, self,
             arc.asset_path, arc.type);
    PopStack();
    return;
  }
  std::string tp = arc.prim_path;
  if (tp.empty()) tp = "/" + raw->meta().defaultPrim;

  // Compose the external target's OWN arcs first (references / payloads /
  // inherits / specializes), variants deferred, so a referenced asset whose
  // content arrives through its own payload or nested references is fully
  // expanded before its opinions are merged here (mirrors the internal-arc path
  // above). Cost-scaled to what is actually needed:
  //   - subtree has NO arcs        → graft the raw layer (compose is identical);
  //   - subtree is self-contained  → compose ONLY that subtree (a big library
  //                                  layer is not materialized for one prim);
  //   - otherwise                  → compose the whole layer (always safe).
  const Layer* ext = raw;
  if (SubtreeHasComposableArcs(*raw, tp)) {
    ext = SubtreeIsSelfContained(*raw, tp)
              ? GetComposedExternalLayer(resolved, tp)
              : GetComposedExternalLayer(resolved, std::string());
    if (!ext) ext = raw;  // fall back to raw on composition failure
  }
  const PrimSpec* target = ext->prim_at_path(tp);
  if (!target) {
    AddError("Referenced prim not found: " + tp, self, arc.asset_path, arc.type);
    PopStack();
    return;
  }
  // Layer offset: BAKE it into the copied/grafted time samples (nothing ever
  // consumed the previous meta().layer_offset stash, so offsets were a no-op).
  // Chains accumulate naturally: each hop remaps the already-remapped samples
  // of its own graft.
  double t_offset = 0.0, t_scale = 1.0;
  if (!arc.layer_offset.empty()) {
    ParseLayerOffset(arc.layer_offset, t_offset, t_scale);
  }
  // Retarget relationship/connection paths from the referenced namespace into
  // the host's; out-of-scope targets are dropped (pxr behavior).
  auto remap = [&](const std::string& p) -> std::string {
    if (p == tp) return self;
    if (p.size() > tp.size() && p.compare(0, tp.size(), tp) == 0 &&
        (p[tp.size()] == '/' || p[tp.size()] == '.')) {
      return self + p.substr(tp.size());
    }
    return std::string();
  };
  CopyLocalOpinions(prim, *target, t_offset, t_scale, remap);
  GraftSubtree(*ext, resolved, tp, self, t_offset, t_scale);
  PopStack();
}

void Compositor::GraftSubtree(const Layer& src, const std::string& src_anchor,
                              const std::string& src_root,
                              const std::string& dst_root, double t_offset,
                              double t_scale) {
  // Copy every descendant of src_root in `src` to the matching path under
  // dst_root, buffered in pending_graft_ (added after pass 2). Local overrides
  // already present in the result win (checked when appending).
  //
  // Prefer the child-index hierarchy when it proves valid for this subtree. Some
  // composed/intermediate layers can carry stale child indices after cloned or
  // renamed prims, so fall back to the path-prefix scan when validation fails.
  const std::string prefix = src_root + "/";
  auto graft_descendant = [&](const PrimSpec& d) {
    const std::string& dp = d.path().str();
    if (dp.size() <= prefix.size() ||
        dp.compare(0, prefix.size(), prefix) != 0) {
      return;  // not a descendant of src_root
    }
    std::string new_path = dst_root + dp.substr(src_root.size());
    if (!graft_paths_.insert(new_path).second) return;  // already grafted
    PrimSpec g = d.Clone();
    g.set_path(Path(new_path));
    // Retarget relationship/connection paths that point inside the grafted
    // subtree; bake the arc's layer offset into the clone's time samples.
    g.remap_target_prefix(src_root, dst_root);
    if (t_offset != 0.0 || t_scale != 1.0) {
      g.remap_time_sample_times(t_offset, t_scale);
    }
    pending_graft_.push_back(PendingGraft{std::move(g), src_anchor});
  };

  const PrimSpec* root = src.prim_at_path(src_root);
  if (root && root->child_count() > 0) {
    bool valid_child_links = true;
    std::vector<uint32_t> stack(root->child_indices().begin(),
                                root->child_indices().end());
    std::vector<uint32_t> order;
    order.reserve(stack.size());
    std::vector<uint8_t> seen(src.prim_count(), uint8_t{0});
    while (!stack.empty()) {
      uint32_t idx = stack.back();
      stack.pop_back();
      if (idx >= src.prim_count() || seen[idx]) {
        valid_child_links = false;
        break;
      }
      seen[idx] = 1;
      const PrimSpec* d = src.prim(idx);
      if (!d) {
        valid_child_links = false;
        break;
      }
      const std::string& dp = d->path().str();
      if (dp.size() <= prefix.size() ||
          dp.compare(0, prefix.size(), prefix) != 0) {
        valid_child_links = false;
        break;
      }
      order.push_back(idx);
      const auto& children = d->child_indices();
      stack.insert(stack.end(), children.begin(), children.end());
    }
    if (valid_child_links) {
      for (uint32_t idx : order) {
        if (const PrimSpec* d = src.prim(idx)) {
          graft_descendant(*d);
        }
      }
      return;
    }
  }

  for (size_t i = 0; i < src.prim_count(); ++i) {
    const PrimSpec* d = src.prim(static_cast<uint32_t>(i));
    if (!d) continue;
    graft_descendant(*d);
  }
}

// Apply one SELECTED variant option's opinions onto `prim` (fill-absent):
// inline properties/relationships/doc/state, the content sub-layer (its
// "__self__" root merges onto the prim; descendants graft as children),
// composition arcs authored on the option, and nested variant sets
// (recursive, selection from the nested set's own `selected`).
void Compositor::ApplyOneVariant(PrimSpec& prim, const Layer& layer,
                                 const std::string& anchor_path, int depth,
                                 const VariantData& variant) {
  if (depth > options_.max_depth) return;

  for (const auto& vp : variant.properties) {
    if (!prim.property_value(vp.name)) {
      prim.add_property(vp.name, vp.value, vp.flags);
    }
  }
  for (const auto& [rel_name, targets] : variant.relationships) {
    if (!prim.relationship(rel_name)) {
      for (const auto& target : targets) prim.add_relationship(rel_name, target);
    }
  }
  if (!variant.doc.empty() && prim.meta().doc().empty()) {
    prim.meta().doc() = variant.doc;
  }
  if (!variant.active) {
    prim.meta().active = false;
    prim.meta().active_authored = true;
  }
  if (variant.hidden) {
    prim.meta().hidden = true;
    prim.meta().hidden_authored = true;
  }

  // Content sub-layer (USDA representation): "__self__" opinions merge onto
  // the prim; its descendants become the prim's children.
  if (variant.content) {
    const Layer& content = *variant.content;
    if (const PrimSpec* self_spec = content.prim_at_path("/__self__")) {
      CopyLocalOpinions(prim, *self_spec);
    }
    GraftSubtree(content, anchor_path, "/__self__", prim.path().str());
  }

  // Composition arcs authored on the option (XGen-style geometry payloads).
  // Resolve at the variant's strength, anchored like the host's own arcs.
  for (const auto& r : variant.references) {
    ResolveRefArc(const_cast<Layer&>(layer), prim, ParseReference(r),
                  anchor_path, depth + 1);
  }
  if (options_.load_payloads) {
    for (const auto& pl : variant.payloads) {
      ResolveRefArc(const_cast<Layer&>(layer), prim, ParsePayload(pl),
                    anchor_path, depth + 1);
    }
  }

  // Nested variant sets on this option (selection authored in the option's
  // metadata is stored in the nested set's `selected`).
  for (const auto& nvs : variant.variantSets) {
    if (nvs.selected.empty()) continue;
    for (const auto& nested : nvs.variants) {
      if (nested.name == nvs.selected) {
        ApplyOneVariant(prim, layer, anchor_path, depth + 1, nested);
        break;
      }
    }
  }
}

bool Compositor::ApplyVariants(PrimSpec& prim, const Layer& layer,
                               const std::string& anchor_path, int depth) {
  if (!options_.resolve_variants) return true;

  // Apply EACH variant set's selected variant (a prim may select several sets).
  // Variant opinions are weaker than local opinions already on the prim, so use
  // the dedup-skip-existing copy. The per-set selection is `vs.selected`,
  // falling back to the plural variantSelections() list (the USDA parser
  // stores ALL selections there and only the FIRST in the legacy single
  // string — consulting just the legacy string dropped every set after the
  // first when flattening multi-set USDA), then the legacy single string.
  VariantSelection legacy = ParseVariantSelection(prim.meta().variantSelection);

  for (const auto& vs : prim.meta().variantSets()) {
    std::string chosen = vs.selected;
    auto override_it = options_.variant_overrides.find(vs.name);
    if (override_it != options_.variant_overrides.end()) {
      chosen = override_it->second;
    }
    if (chosen.empty()) {
      for (const auto& sel : prim.meta().variantSelections()) {
        if (sel.first == vs.name) {
          chosen = sel.second;
          break;
        }
      }
    }
    if (chosen.empty() && vs.name == legacy.variant_set) {
      chosen = legacy.variant_name;
    }
    if (chosen.empty()) continue;

    for (const auto& variant : vs.variants) {
      if (variant.name != chosen) continue;
      ApplyOneVariant(prim, layer, anchor_path, depth, variant);
      break;
    }

    // Read the selected variant's holder prim ("<prim>/{vset=sel}"): copy its
    // own opinions (the variant's properties on the owning prim, as authored in
    // the layer) and graft its descendant subtree (variant CHILD prims) under
    // <prim>. (The vs.variants loop above covers in-memory model variants; this
    // covers reader-produced variants whose content lives in the layer.)
    const std::string dst = prim.path().str();
    const std::string holder =
        dst + "/{" + vs.name + "=" + chosen + "}";
    const std::string holder_legacy =
        dst + "/" + prim.name() + "{" + vs.name + "=" + chosen + "}";
    std::vector<std::string> holders{holder};
    if (holder_legacy != holder) holders.push_back(holder_legacy);
    for (const std::string& hp : holders) {
      if (const PrimSpec* h = layer.prim_at_path(hp)) {
        CopyLocalOpinions(prim, *h);
      }
      GraftSubtree(layer, anchor_path, hp, dst);
    }

    // The holder (and its children) may have JUST been grafted from a
    // referenced layer in this same pass and still sit in the pending buffer
    // (e.g. a UE mesh asset whose LOD variant content arrives via an external
    // reference). Apply from the pending entries: copy the holder's own
    // opinions and re-root its descendants under the prim, dropping the
    // "{vset=sel}" path segment. Unselected variant content keeps its brace
    // path and is filtered out at append time.
    const std::string holder_alt =
        dst + "{" + vs.name + "=" + chosen + "}";
    for (auto& pg : pending_graft_) {
      const std::string& pp = pg.prim.path().str();
      for (const std::string& hp : holders) {
        const std::string hprefix = hp + "/";
        if (pp == hp) {
          pg.prim.remap_target_prefix(hp, dst);
          pg.prim.remap_target_prefix(holder_alt, dst);
          CopyLocalOpinions(prim, pg.prim);
          break;
        }
        if (pp.size() > hprefix.size() &&
            pp.compare(0, hprefix.size(), hprefix) == 0) {
          std::string new_path = dst + "/" + pp.substr(hprefix.size());
          graft_paths_.insert(new_path);
          pg.prim.remap_target_prefix(hp, dst);
          pg.prim.remap_target_prefix(holder_alt, dst);
          pg.prim.set_path(Path(new_path));
          break;
        }
      }
    }
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

const Layer* Compositor::GetComposedExternalLayer(
    const std::string& resolved_path, const std::string& subtree_root) {
  if (!composed_ext_cache_) {
    composed_ext_cache_ =
        std::make_shared<std::map<std::string, std::shared_ptr<Layer>>>();
  }
  if (!composing_ext_) {
    composing_ext_ = std::make_shared<std::set<std::string>>();
  }

  // Cache/cycle key: whole-layer composes share one entry; a self-contained
  // subtree compose is keyed per (layer, subtree) since it materializes only
  // that subtree. ('\x1f' = unit separator, never a valid path/asset char.)
  const std::string key = subtree_root.empty()
                              ? resolved_path
                              : resolved_path + '\x1f' + subtree_root;

  auto cit = composed_ext_cache_->find(key);
  if (cit != composed_ext_cache_->end()) return cit->second.get();

  const Layer* raw = GetCachedLayer(resolved_path);
  if (!raw) return nullptr;

  // Nothing to expand → graft the raw layer directly (no clone/compose cost).
  // (Subtree callers already verified the subtree has arcs.)
  if (subtree_root.empty() && !LayerHasComposableArcs(*raw)) return raw;

  // Cross-layer cycle guard: already composing this key → fall back to raw so
  // the recursion terminates (the in-progress layer's own arcs still resolve in
  // the outer composition that owns it).
  if (composing_ext_->count(key)) return raw;
  composing_ext_->insert(key);

  // Compose the external content in its OWN anchor context so its references /
  // payloads resolve relative to itself, but DEFER variant selection: the
  // referencing prim's selection is stronger and is applied by the host's
  // ApplyVariants pass after grafting. A fresh sub-Compositor is used because
  // Compose() resets per-run state (arc_resolved_/pending_graft_/...) that the
  // outer composition is still mid-pass on; the composed-layer cache and the
  // cycle guard are shared so the whole recursion sees them.
  //
  // When `subtree_root` is set the input is just that (self-contained) subtree,
  // so a large multi-prim library is not materialized to pull one prim from it.
  std::unique_ptr<Layer> extracted;
  const Layer* input = raw;
  if (!subtree_root.empty()) {
    extracted = ExtractSubtree(*raw, subtree_root);
    input = extracted.get();
  }

  Compositor sub;
  sub.resolver_ = resolver_;
  sub.layer_loader_ = layer_loader_;
  CompositionOptions opts = options_;
  opts.resolve_variants = false;
  sub.options_ = opts;
  sub.composed_ext_cache_ = composed_ext_cache_;
  sub.composing_ext_ = composing_ext_;

  std::unique_ptr<Layer> composed = sub.Compose(*input, resolved_path);
  for (const auto& e : sub.errors_) errors_.push_back(e);
  composing_ext_->erase(key);

  if (!composed) return raw;  // fall back to raw on failure
  std::shared_ptr<Layer> shared(std::move(composed));
  const Layer* ptr = shared.get();
  (*composed_ext_cache_)[key] = std::move(shared);
  return ptr;
}

void Compositor::ClearCache() {
  layer_cache_.clear();
  if (composed_ext_cache_) composed_ext_cache_->clear();
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

// Locale-independent double parse (replaces std::atof, which honors the C locale
// and could mis-parse under a comma-decimal locale). fast_float is correctly
// rounded and already the parser's value producer. Returns 0.0 on a malformed
// field, matching atof. Tolerates leading whitespace / a single '+' the way atof
// did, since these query fields are not pre-trimmed.
static double ParseOffsetField(const char* first, const char* last) {
  while (first < last && (*first == ' ' || *first == '\t')) ++first;
  const char* p = (first < last && *first == '+') ? first + 1 : first;
  double v = 0.0;
  auto r = fast_float::from_chars(p, last, v);
  return (r.ec == std::errc{}) ? v : 0.0;
}

void Compositor::ParseLayerOffset(const std::string& offset_str,
                                   double& offset, double& scale) {
  offset = 0.0;
  scale = 1.0;

  // Format: "offset:scale" or just "offset"
  const char* b = offset_str.data();
  const char* e = b + offset_str.size();
  size_t colon = offset_str.find(':');
  if (colon == std::string::npos) {
    offset = ParseOffsetField(b, e);
  } else {
    offset = ParseOffsetField(b, b + colon);
    scale = ParseOffsetField(b + colon + 1, e);
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
    meta.clear_arc_edits();
    meta.variantSelection.clear();
    // See note above: avoid allocating an empty ext just to clear variantSets.
    if (meta.has_ext()) {
      meta.variantSets().clear();
      meta.variantSelections().clear();
    }
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
         !meta.variantSets().empty();
}

}  // namespace next
}  // namespace tinyusdz
