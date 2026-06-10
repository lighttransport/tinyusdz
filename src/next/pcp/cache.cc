// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache implementation
// Phase 1: sublayers + references.  Phase 2: ancestral opinions + deferred payloads.

#include "cache.hh"

#include "../composition/composition.hh"  // reuse ParseReference / ParsePayload / CopyLocalOpinions

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <atomic>
#include <mutex>
#include <thread>
// Serializes the engine's shared mutable state so the Cache is safe to use
// (ComputePrimIndex / BuildStage / queries / payload edits) from multiple
// threads. Recursive because some entry points call others (e.g. LoadPayload ->
// ComputePrimIndex). Compiles to nothing in non-threaded builds.
#define NEXT_PCP_LOCK(m) std::lock_guard<std::recursive_mutex> _pcp_lk(m)
#else
#define NEXT_PCP_LOCK(m) (void)0
#endif

namespace tinyusdz {
namespace next {
namespace pcp {

namespace {

// A single composition source (one node's worth of provenance).
struct Src {
  uint32_t stack_idx = 0;
  std::string site;            // prim path within the layer stack
  NamespaceMapping map;        // remap site-namespace -> root-prim namespace
  LayerOffset offset;
  ArcType arc_kind = ArcType::Root;  // arc this source arrived through
  // For Variant sources: the selected variant's inline opinions (lives inside a
  // shared layer's PrimSpecMeta, so the pointer is stable). null otherwise.
  const VariantData *variant = nullptr;
};

// Reverse-dependency key: which (layer, prim-path) an index read from.
struct Site {
  std::string layer_id;
  std::string prim_path;
  bool operator==(const Site &o) const {
    return layer_id == o.layer_id && prim_path == o.prim_path;
  }
};
struct SiteHash {
  size_t operator()(const Site &s) const {
    return std::hash<std::string>()(s.layer_id) ^
           (std::hash<std::string>()(s.prim_path) << 1);
  }
};

// True if `child` == `base` or a namespace descendant of `base`.
bool IsAtOrUnder(const std::string &child, const std::string &base) {
  if (child == base) return true;
  return child.size() > base.size() &&
         child.compare(0, base.size(), base) == 0 && child[base.size()] == '/';
}

// One level of arc expansion, linked up the C++ call stack (OpenUSD
// PcpPrimIndex_StackFrame analogue). Replaces the per-arc copied
// std::set<std::string> cycle keys: cycle detection walks the chain comparing
// (stack_idx, site), and depth gives the max-depth backstop for free. The
// `site` pointer targets a string that outlives the recursion (a Src member or
// a ProcessArc local).
struct ExpansionFrame {
  uint32_t stack_idx;
  const std::string *site;
  const ExpansionFrame *prev;
  uint32_t depth;  // number of frames above the seed (seed == 0)

  bool Contains(uint32_t stack, const std::string &s) const {
    for (const ExpansionFrame *f = this; f; f = f->prev) {
      if (f->stack_idx == stack && *f->site == s) return true;
    }
    return false;
  }
};

}  // namespace

struct Cache::Impl {
  AssetResolver *resolver = nullptr;
  CompositionOptions options;
  LayerRegistry registry;          // owned by this Impl
  LayerRegistry *reg_ = &registry;  // borrowed pointer: worker Impls point this
                                    // at the *main* Impl's registry (parse-once
                                    // shared across the parallel build).
#if defined(TINYUSDZ_ENABLE_THREAD)
  mutable std::recursive_mutex api_mu_;  // guards all shared state below

  // --- parallel-build worker hooks (see PrewarmPrimIndices) ---------------
  // When set, RegisterInstance stashes its (instanceable, key) into
  // pending_instance_ instead of mutating the prototype maps, so the merge can
  // assign prototypes deterministically in input order.
  bool defer_instances_ = false;
  std::unordered_map<std::string, std::pair<bool, std::string>> pending_instance_;
#endif

  std::shared_ptr<Layer> root_layer;  // kept alive (also layer_stacks[0].layers[0])
  std::string root_identifier;

  std::vector<LayerStack> layer_stacks;             // table; [0] == root stack
  std::map<std::string, uint32_t> stack_by_id;      // dedup by resolved identifier

  // Interned prim-path table shared by all PrimIndex nodes (dedup across indices).
  std::vector<std::string> path_table;
  std::unordered_map<std::string, uint32_t> path_intern;
  uint32_t InternPath(const std::string &p) {
    auto it = path_intern.find(p);
    if (it != path_intern.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(path_table.size());
    path_table.push_back(p);
    path_intern.emplace(p, idx);
    return idx;
  }

  std::unordered_map<std::string, std::unique_ptr<PrimIndex>> index_cache;
  std::unordered_map<std::string, std::vector<Src>> sources_cache;  // path -> expanded sources
  std::unordered_map<Site, std::set<std::string>, SiteHash> site_to_indices;
  std::unordered_map<std::string, std::vector<Site>> index_to_sites;

  // Deferred payload state.
  std::set<std::string> deferred_payload_prims;    // root-space prim paths
  LoadRules load_rules_;  // per-subtree payload overrides (on top of base policy)

  // Instancing: instance key -> prototype prim path; and the groupings.
  std::map<std::string, std::string> prototype_by_key;
  std::unordered_map<std::string, std::string> prototype_of;           // prim -> prototype
  std::unordered_map<std::string, std::vector<std::string>> instances_by_prototype;

  // Relocates: composed source path -> target path (collected from the root
  // layer stack at Open). Applied as a same-parent namespace rename in BuildStage.
  std::map<std::string, std::string> relocates_map;

  void CollectRelocates() {
    relocates_map.clear();
    for (const auto &lp : layer_stacks[0].layers) {
      for (const PrimSpec &ps : lp->prims()) {
        for (const auto &r : ps.meta().relocates) {
          relocates_map[r.first] = r.second;
        }
      }
    }
  }

  // --- layer stacks -------------------------------------------------------

  static constexpr uint32_t kInvalidStack =
      (std::numeric_limits<uint32_t>::max)();

  static std::string DirOf(const std::string &path) {
    return AssetResolver::GetDirectory(path);
  }

  uint32_t InternLayerStack(std::shared_ptr<Layer> layer,
                            const std::string &identifier, std::string *warn,
                            std::string *err) {
    auto it = stack_by_id.find(identifier);
    if (it != stack_by_id.end()) return it->second;

    LayerStack st;
    st.identifier = identifier;
    std::set<std::string> visiting;
    visiting.insert(identifier);
    if (!AppendLayerAndSublayers(st, layer, identifier, &visiting, 0, warn,
                                 err)) {
      return kInvalidStack;
    }

    uint32_t idx = static_cast<uint32_t>(layer_stacks.size());
    layer_stacks.push_back(std::move(st));
    stack_by_id.emplace(identifier, idx);
    return idx;
  }

  bool AppendLayerAndSublayers(LayerStack &st, std::shared_ptr<Layer> layer,
                               const std::string &identifier,
                               std::set<std::string> *visiting,
                               uint32_t depth, std::string *warn,
                               std::string *err) {
    if (!layer) return false;
    if (depth > options.max_depth) {
      if (err) *err += "Sublayer max depth exceeded at: " + identifier + "\n";
      return false;
    }
    st.layers.push_back(layer);
    st.layer_identifiers.push_back(identifier);
    const std::string anchor = DirOf(identifier);
    for (const std::string &sub : layer->meta().subLayers) {
      const std::string sub_id = resolver->ResolvePath(sub, anchor);
      if (sub_id.empty()) {
        if (err) *err += "Failed to resolve sublayer: " + sub + "\n";
        if (options.error_when_asset_not_found) return false;
        continue;
      }
      if (visiting && visiting->count(sub_id)) {
        if (err) *err += "Sublayer cycle detected at: " + sub_id + "\n";
        return false;
      }
      std::shared_ptr<Layer> sl =
          reg_->GetOrLoad(*resolver, sub, anchor, warn, err);
      if (sl) {
        if (visiting) visiting->insert(sub_id);
        if (!AppendLayerAndSublayers(st, sl, sub_id, visiting, depth + 1,
                                     warn, err)) {
          if (visiting) visiting->erase(sub_id);
          return false;
        }
        if (visiting) visiting->erase(sub_id);
      } else if (options.error_when_asset_not_found) {
        return false;
      }
    }
    return true;
  }

  // --- spec lookup --------------------------------------------------------

  struct SpecRef {
    const PrimSpec *spec = nullptr;
    const Layer *layer = nullptr;
    std::string layer_id;
  };

  // Memoized spec resolution. The same (stack, site) is resolved 3-5x per
  // composed prim (ExpandArcs, AnyAuthors, ComposeInto, ComputePrimIndex,
  // instance-key); each call walked every layer and parsed the site Path. The
  // cache keys by (stack_idx, site) and returns a stable reference (unordered_map
  // keeps element addresses valid across rehash, so a recursive insert during
  // expansion never dangles the outer reference). Results depend only on layer
  // contents, which change solely on InvalidateLayer -> spec_cache_ is cleared
  // there (Invalidate(prim) leaves layer stacks intact, so it stays valid).
  struct SpecCacheKey {
    uint32_t stack;
    std::string site;
    bool operator==(const SpecCacheKey &o) const {
      return stack == o.stack && site == o.site;
    }
  };
  struct SpecCacheKeyHash {
    size_t operator()(const SpecCacheKey &k) const {
      return std::hash<std::string>()(k.site) * 1000003u + k.stack;
    }
  };
  mutable std::unordered_map<SpecCacheKey, std::vector<SpecRef>, SpecCacheKeyHash>
      spec_cache_;

  const std::vector<SpecRef> &Specs(uint32_t stack_idx,
                                    const std::string &site) const {
    SpecCacheKey key{stack_idx, site};
    auto it = spec_cache_.find(key);
    if (it != spec_cache_.end()) return it->second;
    std::vector<SpecRef> refs = FindSpecs(layer_stacks[stack_idx], site);
    return spec_cache_.emplace(std::move(key), std::move(refs)).first->second;
  }

  std::vector<SpecRef> FindSpecs(const LayerStack &st,
                                 const std::string &site) const {
    std::vector<SpecRef> refs;
    Path p(site);
    for (size_t i = 0; i < st.layers.size(); ++i) {
      const auto &lp = st.layers[i];
      if (const PrimSpec *s = lp->prim_at_path(p)) {
        SpecRef r;
        r.spec = s;
        r.layer = lp.get();
        if (i < st.layer_identifiers.size()) r.layer_id = st.layer_identifiers[i];
        refs.push_back(std::move(r));
      }
    }
    return refs;
  }

  const PrimSpec *FindSpec(const LayerStack &st, const std::string &site,
                           const Layer **out_layer) const {
    std::vector<SpecRef> refs = FindSpecs(st, site);
    if (!refs.empty()) {
      if (out_layer) *out_layer = refs[0].layer;
      return refs[0].spec;
    }
    return nullptr;
  }

  bool AnyAuthors(const std::vector<Src> &srcs) const {
    for (const Src &s : srcs) {
      if (!Specs(s.stack_idx, s.site).empty()) return true;
    }
    return false;
  }

  // --- payload policy -----------------------------------------------------

  bool ShouldLoadPayload(const std::string &root_prim_path,
                         const std::string &asset) {
    // Explicit load rules win; a path with no governing rule falls back to the
    // base policy (payload_policy callback, else the load_payloads flag).
    switch (load_rules_.GetEffect(root_prim_path)) {
      case LoadRules::Effect::All:
      case LoadRules::Effect::Only:
        return true;
      case LoadRules::Effect::None:
        return false;
      case LoadRules::Effect::Default:
        break;
    }
    if (options.payload_policy) {
      return options.payload_policy(Path(root_prim_path), asset);
    }
    return options.load_payloads;
  }

  // --- variant selection --------------------------------------------------

  // Record `spec`'s variant selections into the accumulator (strong-first wins,
  // so a selection authored on a stronger source overrides a weaker one).
  void RecordSelections(const PrimSpec &spec,
                        std::map<std::string, std::string> *sels) const {
    if (!spec.meta().variantSelection.empty()) {
      VariantSelection s =
          Compositor::ParseVariantSelection(spec.meta().variantSelection);
      if (!s.variant_set.empty()) sels->emplace(s.variant_set, s.variant_name);
    }
    for (const auto &sel : spec.meta().variantSelections) {
      sels->emplace(sel.first, sel.second);
    }
  }

  // A selected variant plus the name of the set it was selected from (the set
  // name is part of the stable variant-content identity used for instancing).
  struct SelectedVariant {
    const std::string *set_name;
    const VariantData *vd;
  };

  // Resolve the selected variants for the variantSets defined on `spec`, using
  // the accumulated cross-source selection map. Returns one entry per selected
  // set.
  std::vector<SelectedVariant> SelectVariants(
      const PrimSpec &spec,
      const std::map<std::string, std::string> &sels) const {
    std::vector<SelectedVariant> out;
    for (const VariantSetData &vss : spec.meta().variantSets) {
      auto sit = sels.find(vss.name);
      if (sit == sels.end()) continue;  // no selection for this set
      for (const VariantData &vd : vss.variants) {
        if (vd.name == sit->second) {
          out.push_back({&vss.name, &vd});
          break;
        }
      }
    }
    return out;
  }

  // --- instancing ---------------------------------------------------------

  // Instanceable iff some contributing spec set instanceable=true AND the prim
  // actually has a composition arc (more than just its own root opinions).
  bool IsInstanceableSources(const std::vector<Src> &srcs) const {
    if (srcs.size() <= 1) return false;
    for (const Src &s : srcs) {
      for (const SpecRef &sr : Specs(s.stack_idx, s.site)) {
        if (sr.spec->meta().instanceable) return true;
      }
    }
    return false;
  }

  // Structural key: composed type + each arc's (kind, layer-id, site, variant).
  // The root node (i==0) is excluded because its site is the instance's own
  // path, which differs between instances of the same asset.
  std::string ComputeInstanceKeyImpl(const std::vector<Src> &srcs) const {
    std::string key;
    for (const Src &s : srcs) {
      bool found_type = false;
      for (const SpecRef &sr : Specs(s.stack_idx, s.site)) {
        if (!sr.spec->type_name().empty()) {
          key += "T:" + sr.spec->type_name() + ";";
          found_type = true;
          break;
        }
      }
      if (found_type) break;
    }
    for (size_t i = 1; i < srcs.size(); ++i) {
      const Src &s = srcs[i];
      key += ArcTypeName(s.arc_kind);
      key += "|" + layer_stacks[s.stack_idx].identifier + "|" + s.site;
      if (s.variant) key += "|v:" + s.variant->name;
      key += ";";
    }
    return key;
  }

  // Prototype bookkeeping for a prim known to be an instance with key `ik`.
  // First prim of a given key becomes the prototype; the rest link to it.
  // Order-sensitive: the caller must invoke this deterministically (the parallel
  // build defers it to an input-order merge).
  void AssignPrototype(const std::string &prim_path, const std::string &ik) {
    auto pit = prototype_by_key.find(ik);
    std::string proto;
    if (pit == prototype_by_key.end()) {
      prototype_by_key[ik] = prim_path;  // first of its key becomes prototype
      proto = prim_path;
    } else {
      proto = pit->second;
    }
    prototype_of[prim_path] = proto;
    auto &vec = instances_by_prototype[proto];
    if (std::find(vec.begin(), vec.end(), prim_path) == vec.end()) {
      vec.push_back(prim_path);
    }
  }

  void RegisterInstance(const std::string &prim_path,
                        const std::vector<Src> &srcs) {
    if (!options.detect_instances || !IsInstanceableSources(srcs)) return;
    const std::string ik = ComputeInstanceKeyImpl(srcs);
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (defer_instances_) {
      // Worker mode: stash for the deterministic input-order merge.
      pending_instance_[prim_path] = {true, ik};
      return;
    }
#endif
    AssignPrototype(prim_path, ik);
  }

  void DropInstancing(const std::string &prim_path) {
    prototype_of.erase(prim_path);
    for (auto it = instances_by_prototype.begin();
         it != instances_by_prototype.end();) {
      auto &v = it->second;
      v.erase(std::remove(v.begin(), v.end(), prim_path), v.end());
      const bool was_prototype = (it->first == prim_path);
      if (was_prototype || v.empty()) {
        for (auto kit = prototype_by_key.begin(); kit != prototype_by_key.end();) {
          if (kit->second == it->first) kit = prototype_by_key.erase(kit);
          else ++kit;
        }
        it = instances_by_prototype.erase(it);
      } else {
        ++it;
      }
    }
  }

  // --- arc expansion (inherits/variants/references/payloads/specializes) ---

  // Resolve+recurse an external/internal arc target from source `src`.
  // `kind` is the arc type; `out` collects ordinary opinions and `spec_out`
  // collects specialize-derived opinions (globally weakest). A specialize arc
  // (and its entire subtree) routes into `spec_out`.
  void ProcessArc(const Src &src, const CompositionArc &arc, ArcType kind,
                  const ExpansionFrame *frame, std::vector<Src> *out,
                  std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err) {
    uint32_t arc_stack_idx;
    std::string arc_site;
    if (arc.is_internal || arc.asset_path.empty()) {
      arc_stack_idx = src.stack_idx;
      arc_site = arc.prim_path.empty() ? src.site : arc.prim_path;
    } else {
      const std::string anchor = DirOf(layer_stacks[src.stack_idx].identifier);
      std::shared_ptr<Layer> arc_layer =
          reg_->GetOrLoad(*resolver, arc.asset_path, anchor, warn, err);
      if (!arc_layer) {
        if (options.error_when_asset_not_found && err) {
          *err += "Arc asset not found: " + arc.asset_path + "\n";
        }
        return;
      }
      const std::string arc_id = resolver->ResolvePath(arc.asset_path, anchor);
      arc_stack_idx = InternLayerStack(arc_layer, arc_id, warn, err);
      if (arc_stack_idx == kInvalidStack) return;
      if (!arc.prim_path.empty()) {
        arc_site = arc.prim_path;
      } else if (!arc_layer->meta().defaultPrim.empty()) {
        arc_site = "/" + arc_layer->meta().defaultPrim;
      } else {
        const auto &roots = arc_layer->root_indices();
        arc_site = roots.empty() ? "/" : "/" + arc_layer->prim(roots[0])->name();
      }
    }

    if (frame && frame->Contains(arc_stack_idx, arc_site)) {
      if (err) {
        *err += "Composition cycle detected at arc: " +
                layer_stacks[arc_stack_idx].identifier + ":" + arc_site + "\n";
      }
      return;
    }
    if (frame && frame->depth + 1 >= options.max_depth) {
      if (err) *err += "Composition max depth exceeded\n";
      return;
    }
    // A same-stack arc that targets a namespace ANCESTOR of its own source site
    // grafts the source under itself: the composed namespace grows without
    // bound (e.g. `def "A" { def "B" (references = </A>) {} }`). The frame
    // chain cannot see this (each child prim starts a fresh expansion), so
    // reject it here.
    if (arc_stack_idx == src.stack_idx && arc_site != src.site &&
        IsAtOrUnder(src.site, arc_site)) {
      if (err) {
        *err += "Composition cycle detected: arc at " + src.site +
                " targets ancestor " + arc_site + "\n";
      }
      return;
    }

    NamespaceMapping local{arc_site, src.site};
    Src arc_src;
    arc_src.stack_idx = arc_stack_idx;
    arc_src.site = arc_site;
    arc_src.map = NamespaceMapping::Compose(src.map, local);
    arc_src.offset = src.offset;
    arc_src.arc_kind = kind;

    // A specialize subtree is globally weakest: it (and everything beneath it)
    // is collected into spec_out.
    std::vector<Src> *target = (kind == ArcType::Specialize) ? spec_out : out;

    // Implied class-arc propagation: a class (inherit/specialize) reached
    // through a reference chain is ALSO expressed in EVERY ancestor layer stack
    // on that chain (root + intermediate references), so an override authored on
    // the same class path at any level composes. Ancestors are pushed first
    // (root strongest) so they outrank the referenced-stack class opinions.
    // Class paths are global, so the site is unchanged across stacks.
    if (IsClassBasedArc(kind)) {
      std::set<uint32_t> seen_stack;
      for (uint32_t as : chain) {
        if (as == arc_stack_idx || !seen_stack.insert(as).second) continue;
        if (Specs(as, arc_site).empty()) continue;
        Src implied = arc_src;
        implied.stack_idx = as;
        std::vector<uint32_t> ichain{as};
        const ExpansionFrame iframe{as, &arc_site, frame,
                                    frame ? frame->depth + 1 : 0};
        ExpandArcs(implied, &iframe, target, spec_out, sels, ichain, warn, err);
      }
    }

    // Reference/payload arcs descend into a new layer stack -> extend the chain.
    std::vector<uint32_t> child_chain = chain;
    if (arc_stack_idx != src.stack_idx) child_chain.push_back(arc_stack_idx);

    const ExpansionFrame cframe{arc_stack_idx, &arc_site, frame,
                                frame ? frame->depth + 1 : 0};
    ExpandArcs(arc_src, &cframe, target, spec_out, sels, child_chain, warn,
               err);
  }

  // Pre-order DFS == strength order. Per source: local (pushed) > inherits >
  // references > payloads; specializes routed to `spec_out` (globally weakest).
  void ExpandArcs(const Src &src, const ExpansionFrame *frame,
                  std::vector<Src> *out, std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err) {
    out->push_back(src);

    const std::vector<SpecRef> &specs = Specs(src.stack_idx, src.site);
    if (specs.empty()) return;

    // Record this source's variant selections (strong-first wins) so a selection
    // authored on a stronger source applies to a variantSet defined on a weaker
    // one (cross-source selection).
    for (const SpecRef &sr : specs) RecordSelections(*sr.spec, sels);

    for (const SpecRef &sr : specs) {
      const PrimSpec *spec = sr.spec;

      // Inherits (stronger than references).
      for (const std::string &s : spec->meta().inherits) {
        ProcessArc(src, Compositor::ParseReference(s), ArcType::Inherit, frame,
                   out, spec_out, sels, chain, warn, err);
      }

      // Variants (weaker than inherits, stronger than references). For each
      // selected variant, graft its inline opinions and/or its content subtree.
      if (!spec->meta().variantSets.empty()) {
        for (const SelectedVariant &sv : SelectVariants(*spec, *sels)) {
          const VariantData *vd = sv.vd;
          // Subtree content: model as a reference-style Variant source into the
          // content layer's "/__self__" root, so child prims compose normally.
          if (vd->content) {
            // Stable variant-content identity: (host layer-stack, host site,
            // variantSet, variantName). The previous pointer-derived id aliased
            // after a VariantData was freed and reallocated (stale instance
            // keys), and never matched across two parses of the same asset.
            const std::string vid = "variant:" +
                                    layer_stacks[src.stack_idx].identifier +
                                    ":" + src.site + ":" + *sv.set_name + ":" +
                                    vd->name;
            uint32_t cstack = InternLayerStack(vd->content, vid, warn, err);
            if (cstack == kInvalidStack) continue;
            Src vsrc;
            vsrc.stack_idx = cstack;
            vsrc.site = "/__self__";
            vsrc.map = NamespaceMapping::Compose(
                src.map, NamespaceMapping{"/__self__", src.site});
            vsrc.offset = src.offset;
            vsrc.arc_kind = ArcType::Variant;
            const ExpansionFrame vframe{cstack, &vsrc.site, frame,
                                        frame ? frame->depth + 1 : 0};
            ExpandArcs(vsrc, &vframe, out, spec_out, sels, chain, warn, err);
          }
          // Inline opinions (properties/relationships on the host prim itself).
          if (!vd->properties.empty() || !vd->relationships.empty()) {
            Src vsrc = src;
            vsrc.arc_kind = ArcType::Variant;
            vsrc.variant = vd;
            out->push_back(std::move(vsrc));
          }
        }
      }

      // References.
      for (const std::string &ref_str : spec->meta().references) {
        ProcessArc(src, Compositor::ParseReference(ref_str), ArcType::Reference,
                   frame, out, spec_out, sels, chain, warn, err);
      }

      // Payloads (deferrable, weaker than references).
      if (!spec->meta().payloads.empty()) {
        const std::string root_prim_path = src.map.Apply(src.site);
        for (const std::string &pl_str : spec->meta().payloads) {
          CompositionArc arc = Compositor::ParsePayload(pl_str);
          if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
            deferred_payload_prims.insert(root_prim_path);
            continue;  // deferred: contributes no opinions.
          }
          deferred_payload_prims.erase(root_prim_path);
          ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                     chain, warn, err);
        }
      }

      // Specializes (globally weakest; routed into spec_out by ProcessArc).
      for (const std::string &s : spec->meta().specializes) {
        ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize,
                   frame, out, spec_out, sels, chain, warn, err);
      }
    }
  }

  std::vector<Src> ExpandList(const std::vector<Src> &base, std::string *warn,
                              std::string *err) {
    std::vector<Src> main;
    std::vector<Src> spec;  // specialize-derived: globally weakest
    // Variant selections accumulated across the prim's sources (cross-source).
    std::map<std::string, std::string> sels;
    for (const Src &s : base) {
      // Carry a base source's own arc kind so a specialize re-rooted onto a
      // child stays globally weakest.
      std::vector<Src> *tgt = (s.arc_kind == ArcType::Specialize) ? &spec : &main;
      std::vector<uint32_t> chain{s.stack_idx};
      const ExpansionFrame seed{s.stack_idx, &s.site, nullptr, 0};
      ExpandArcs(s, &seed, tgt, &spec, &sels, chain, warn, err);
    }
    main.insert(main.end(), spec.begin(), spec.end());
    return main;
  }

  // --- ancestral source resolution (cached) -------------------------------

  // The expanded composition sources for `path`. A root-level prim seeds from
  // the root layer stack; a descendant re-roots each of its parent's expanded
  // sources at the child name (so arcs authored on an ancestor reach it), then
  // expands that child's own arcs. Cached per path.
  const std::vector<Src> &SourcesForPath(const Path &path, std::string *warn,
                                         std::string *err) {
    {
      auto it = sources_cache.find(path.str());
      if (it != sources_cache.end()) return it->second;
    }

    // Collect the uncached ancestor chain (leaf -> topmost uncached), then
    // expand top-down, so a pathologically deep query path walks a loop instead
    // of the C++ stack.
    std::vector<Path> pending;
    {
      Path cur = path;
      for (;;) {
        pending.push_back(cur);
        Path parent = cur.parent();
        if (parent.is_root() || parent.empty() || parent.str() == "/") break;
        if (sources_cache.count(parent.str())) break;
        cur = parent;
      }
    }

    for (size_t i = pending.size(); i-- > 0;) {
      const Path &p = pending[i];
      const std::string key = p.str();

      std::vector<Src> base;
      Path parent = p.parent();
      const bool parent_is_root =
          parent.is_root() || parent.empty() || parent.str() == "/";
      if (parent_is_root) {
        Src s;
        s.stack_idx = 0;
        s.site = key;
        base.push_back(std::move(s));
      } else {
        const std::vector<Src> &psrc = sources_cache[parent.str()];
        const std::string cn = p.name();
        base.reserve(psrc.size());
        for (const Src &ps : psrc) {
          // Variant sources are inline opinions on the prim itself; they do not
          // (yet) carry child prims, so they don't propagate to children.
          if (ps.variant) continue;
          Src c;
          c.stack_idx = ps.stack_idx;
          c.site = ps.site + "/" + cn;
          c.map = ps.map;
          c.offset = ps.offset;
          c.arc_kind = ps.arc_kind;
          base.push_back(std::move(c));
        }
      }

      std::vector<Src> expanded = ExpandList(base, warn, err);
      sources_cache[key] = std::move(expanded);
    }
    return sources_cache[path.str()];
  }

  // --- value resolution ---------------------------------------------------

  // Merge all sources (strong->weak) into a fresh PrimSpec; return child names.
  std::vector<std::string> ComposeInto(const std::vector<Src> &srcs,
                                        PrimSpec *out) {
    std::vector<std::string> child_names;
    std::set<std::string> seen_child;
    bool specifier_set = false;

    for (const Src &s : srcs) {
      // Variant source: graft the selected variant's inline opinions
      // (properties + relationships) with fill-absent semantics. Variant child
      // prims are not yet modeled (VariantData has no child storage).
      if (s.variant) {
        for (const auto &pr : s.variant->properties) {
          if (!out->property(pr.first)) out->add_property(pr.first, pr.second);
        }
        for (const auto &rp : s.variant->relationships) {
          if (out->relationship(rp.first)) continue;
          for (const Path &t : rp.second) {
            out->add_relationship(rp.first, Path(s.map.Apply(t.str())));
          }
        }
        continue;
      }

      const std::vector<SpecRef> &specs = Specs(s.stack_idx, s.site);
      if (specs.empty()) continue;

      for (const SpecRef &sr : specs) {
        const PrimSpec *spec = sr.spec;
        const Layer *layer = sr.layer;

        if (!specifier_set) {
          out->set_specifier(spec->specifier());
          specifier_set = true;
        }

        Compositor::CopyLocalOpinions(*out, *spec);

        for (const std::string &rn : spec->relationship_names()) {
          if (out->relationship(rn)) continue;
          const std::vector<Path> *tgts = spec->relationship(rn);
          if (!tgts) continue;
          for (const Path &t : *tgts) {
            out->add_relationship(rn, Path(s.map.Apply(t.str())));
          }
        }

        for (uint32_t ci : spec->child_indices()) {
          const PrimSpec *cs = layer->prim(ci);
          if (!cs) continue;
          if (seen_child.insert(cs->name()).second) {
            child_names.push_back(cs->name());
          }
        }
      }
    }
    return child_names;
  }

  // --- ComputePrimIndex ---------------------------------------------------

  const PrimIndex *ComputePrimIndex(const Path &prim_path, std::string *warn,
                                    std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    const std::string key = prim_path.str();
    auto it = index_cache.find(key);
    if (it != index_cache.end()) return it->second.get();

    const std::vector<Src> &srcs = SourcesForPath(prim_path, warn, err);
    if (!AnyAuthors(srcs)) {
      return nullptr;  // prim does not exist in the composed namespace.
    }

    auto index = std::unique_ptr<PrimIndex>(new PrimIndex());
    index->SetPath(prim_path);
    index->SetLayerStacks(&layer_stacks);
    index->SetPathTable(&path_table);

    std::vector<uint16_t> order;
    std::vector<Site> sites;
    for (size_t i = 0; i < srcs.size(); ++i) {
      const Src &s = srcs[i];
      CompNode n;
      n.arc_type = (i == 0) ? ArcType::Root : s.arc_kind;
      n.parent = (i == 0) ? 0xFFFF : 0;
      n.layer_stack_idx = s.stack_idx;
      n.site_path_idx = InternPath(s.site);
      n.map_to_root = s.map;
      n.offset = s.offset;
      const std::vector<SpecRef> &specs = Specs(s.stack_idx, s.site);
      if (!specs.empty()) {
        n.flags |= NodeFlags::HasSpecs;
        for (const SpecRef &sr : specs) {
          sites.push_back(Site{sr.layer_id.empty()
                                   ? layer_stacks[s.stack_idx].identifier
                                   : sr.layer_id,
                               s.site});
        }
      }
      uint16_t ni = index->AddNode(std::move(n));
      if (ni == PrimIndex::kInvalidNode) {
        if (err) {
          *err += "PrimIndex node count exceeds uint16 capacity for " + key +
                  "\n";
        }
        return nullptr;
      }
      if (i != 0) index->MutableNode(0).children.push_back(ni);
      order.push_back(ni);
    }
    index->SetStrengthOrder(std::move(order));

    RegisterInstance(key, srcs);

    for (const Site &site : sites) site_to_indices[site].insert(key);
    index_to_sites[key] = std::move(sites);

    const PrimIndex *ret = index.get();
    index_cache.emplace(key, std::move(index));
    return ret;
  }

  // --- BuildStage ---------------------------------------------------------

  // `src_path` is where composition opinions are gathered; `out_path` is where
  // the composed prim is placed (differs when a relocate renames it).
  void BuildStageRec(const Path &src_path, const Path &out_path, Layer *out,
                     uint32_t parent_idx, bool is_root, uint32_t depth,
                     std::string *warn, std::string *err) {
    // Namespace-depth backstop: an arc-induced namespace that keeps growing
    // (e.g. an ancestor cycle a stronger check missed) must surface as an
    // error, never as C++ stack exhaustion.
    if (depth > options.max_namespace_depth) {
      if (err) {
        *err += "BuildStage max namespace depth exceeded at: " +
                out_path.str() + "\n";
      }
      return;
    }
    const std::vector<Src> &srcs = SourcesForPath(src_path, warn, err);

    PrimSpec spec(out_path.name());
    spec.set_path(out_path);
    std::vector<std::string> children = ComposeInto(srcs, &spec);

    // Instancing: register this prim. If it is an instance (not the prototype of
    // its group), link it to the prototype and do NOT duplicate its subtree --
    // children are provided by the prototype (UsdPrim follows instance_prototype).
    RegisterInstance(out_path.str(), srcs);
    bool is_instance = false;
    {
      auto pit = prototype_of.find(out_path.str());
      if (pit != prototype_of.end() && pit->second != out_path.str()) {
        spec.meta().instance_prototype = pit->second;
        is_instance = true;
      }
    }

    uint32_t idx = out->add_prim(std::move(spec));
    if (is_root) {
      out->add_root(idx);
    } else {
      out->set_parent(idx, parent_idx);
    }

    if (is_instance) return;  // prototype provides the subtree

    for (const std::string &cn : children) {
      Path child_src = src_path.append_child(cn);
      Path child_out;
      auto rit = relocates_map.find(child_src.str());
      if (rit != relocates_map.end() &&
          Path(rit->second).parent().str() == out_path.str()) {
        // Same-parent relocate: keep the source opinions, rename in output.
        child_out = Path(rit->second);
      } else {
        child_out = out_path.append_child(cn);
      }
      BuildStageRec(child_src, child_out, out, idx, /*is_root=*/false,
                    depth + 1, warn, err);
    }
  }

  bool BuildStage(Stage *stage, std::string *warn, std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    auto out = std::unique_ptr<Layer>(new Layer());

    // BuildStage re-registers every prim's instancing from scratch below, so
    // reset the prototype maps first. They are otherwise only pruned via
    // index_cache-driven Invalidate, which BuildStage's direct RegisterInstance
    // path doesn't populate -- leaving stale groupings across recomposition
    // (e.g. after Load/UnloadPayload changes a prim's arc set and instance key).
    prototype_by_key.clear();
    prototype_of.clear();
    instances_by_prototype.clear();

    const Layer *root = layer_stacks[0].layers[0].get();
    std::set<std::string> seen_root;
    std::vector<std::string> root_names;
    for (const auto &lp : layer_stacks[0].layers) {
      for (uint32_t ri : lp->root_indices()) {
        const PrimSpec *ps = lp->prim(ri);
        if (!ps) continue;
        if (seen_root.insert(ps->name()).second) root_names.push_back(ps->name());
      }
    }
    for (const std::string &nm : root_names) {
      BuildStageRec(Path("/" + nm), Path("/" + nm), out.get(), 0,
                    /*is_root=*/true, /*depth=*/1, warn, err);
    }

    out->finalize();
    out->meta() = root->meta();
    stage->SetRootLayer(std::move(*out));
    return true;
  }

#if defined(TINYUSDZ_ENABLE_THREAD)
  // --- parallel-build merge helpers (main thread only) --------------------

  // Adopt a worker's prebuilt LayerStack into the main table (dedup by resolved
  // identifier). Returns the main-table index. The LayerStack is copied, which
  // just bumps the shared_ptr<Layer> refcounts -- the layers stay shared.
  uint32_t AdoptStack(const LayerStack &ws) {
    auto it = stack_by_id.find(ws.identifier);
    if (it != stack_by_id.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(layer_stacks.size());
    layer_stacks.push_back(ws);
    stack_by_id.emplace(ws.identifier, idx);
    return idx;
  }

  // Fold the worker-built PrimIndex for `key` into the main cache, remapping its
  // private layer-stack / path-table indices onto the shared tables. Sites use
  // identifier strings, so they need no remap. Instancing is assigned in the
  // caller's (input) order so the prototype choice matches a serial build.
  void MergeWorkerIndex(Impl &w, const std::string &key) {
    auto wit = w.index_cache.find(key);
    if (wit == w.index_cache.end()) return;  // worker didn't build it (nullptr)
    if (index_cache.count(key)) return;      // already merged (duplicate path)
    std::unique_ptr<PrimIndex> idx = std::move(wit->second);

    std::unordered_map<uint32_t, uint32_t> stack_remap, path_remap;
    const size_t n = idx->GetNodes().size();
    for (size_t i = 0; i < n; ++i) {
      CompNode &node = idx->MutableNode(static_cast<uint16_t>(i));
      auto sr = stack_remap.find(node.layer_stack_idx);
      if (sr == stack_remap.end()) {
        uint32_t m = AdoptStack(w.layer_stacks[node.layer_stack_idx]);
        stack_remap.emplace(node.layer_stack_idx, m);
        node.layer_stack_idx = m;
      } else {
        node.layer_stack_idx = sr->second;
      }
      auto pr = path_remap.find(node.site_path_idx);
      if (pr == path_remap.end()) {
        uint32_t m = InternPath(w.path_table[node.site_path_idx]);
        path_remap.emplace(node.site_path_idx, m);
        node.site_path_idx = m;
      } else {
        node.site_path_idx = pr->second;
      }
    }
    idx->SetLayerStacks(&layer_stacks);
    idx->SetPathTable(&path_table);

    auto sit = w.index_to_sites.find(key);
    if (sit != w.index_to_sites.end()) {
      for (const Site &s : sit->second) site_to_indices[s].insert(key);
      index_to_sites[key] = sit->second;
    }

    index_cache.emplace(key, std::move(idx));

    if (options.detect_instances) {
      auto piit = w.pending_instance_.find(key);
      if (piit != w.pending_instance_.end() && piit->second.first) {
        AssignPrototype(key, piit->second.second);
      }
    }
  }
#endif  // TINYUSDZ_ENABLE_THREAD

  // Batch build. With threads enabled and num_threads != 1, this first prefetches
  // first-level reference/payload layers into the (thread-safe) registry in
  // parallel, then builds the per-prim indices CONCURRENTLY: each worker runs the
  // ordinary build on its own private Impl (borrowing the shared, parse-once
  // registry), and a deterministic input-order merge folds the results into the
  // cache. Composed values are identical to a serial run. Non-threaded builds (or
  // num_threads <= 1) use the plain serial loop.
  bool PrewarmPrimIndices(const std::vector<Path> &paths, std::string *warn,
                          std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
#if defined(TINYUSDZ_ENABLE_THREAD)
    int nt = options.num_threads;
    if (nt < 0) nt = static_cast<int>(std::thread::hardware_concurrency());
    if (nt > 1) {
      // (a) Prefetch root-level reference/payload assets to warm the shared
      // registry (reduces redundant double-parses across workers).
      const std::string anchor = DirOf(layer_stacks[0].identifier);
      std::vector<std::pair<std::string, std::string>> assets;  // (asset, anchor)
      std::set<std::string> seen;
      for (const auto &lp : layer_stacks[0].layers) {
        for (const PrimSpec &ps : lp->prims()) {
          for (const std::string &r : ps.meta().references) {
            CompositionArc a = Compositor::ParseReference(r);
            if (!a.asset_path.empty() && seen.insert(a.asset_path).second)
              assets.emplace_back(a.asset_path, anchor);
          }
          for (const std::string &r : ps.meta().payloads) {
            CompositionArc a = Compositor::ParsePayload(r);
            if (!a.asset_path.empty() && seen.insert(a.asset_path).second)
              assets.emplace_back(a.asset_path, anchor);
          }
        }
      }
      if (!assets.empty()) {
        std::atomic<size_t> next_idx{0};
        int pf = std::min<int>(nt, static_cast<int>(assets.size()));
        std::vector<std::thread> ts;
        auto fn = [&]() {
          for (;;) {
            size_t i = next_idx.fetch_add(1);
            if (i >= assets.size()) break;
            std::string w, e;
            reg_->GetOrLoad(*resolver, assets[i].first, assets[i].second, &w, &e);
          }
        };
        ts.reserve(static_cast<size_t>(pf));
        for (int t = 0; t < pf; ++t) ts.emplace_back(fn);
        for (auto &t : ts) t.join();
      }

      // (b) Parallel per-prim index build into private worker Impls, then a
      // deterministic input-order merge.
      if (paths.size() > 1) {
        const int W = std::min<int>(nt, static_cast<int>(paths.size()));
        auto chunk_start = [&](int t) -> size_t {
          return static_cast<size_t>(t) * paths.size() / static_cast<size_t>(W);
        };

        std::vector<std::unique_ptr<Impl>> workers;
        workers.reserve(static_cast<size_t>(W));
        std::vector<std::string> wwarn(static_cast<size_t>(W));
        std::vector<std::string> werr(static_cast<size_t>(W));
        for (int t = 0; t < W; ++t) {
          std::unique_ptr<Impl> wp(new Impl());
          wp->resolver = resolver;
          wp->options = options;
          wp->load_rules_ = load_rules_;
          wp->reg_ = reg_;  // borrow the shared, parse-once registry
          wp->root_layer = root_layer;
          wp->root_identifier = root_identifier;
          wp->defer_instances_ = true;
          std::string sw, se;
          wp->InternLayerStack(root_layer, root_identifier, &sw, &se);  // root @0
          workers.push_back(std::move(wp));
        }

        std::vector<std::thread> bts;
        bts.reserve(static_cast<size_t>(W));
        for (int t = 0; t < W; ++t) {
          Impl *wk = workers[static_cast<size_t>(t)].get();
          size_t s = chunk_start(t), e = chunk_start(t + 1);
          std::string *pw = &wwarn[static_cast<size_t>(t)];
          std::string *pe = &werr[static_cast<size_t>(t)];
          bts.emplace_back([wk, s, e, &paths, pw, pe]() {
            for (size_t i = s; i < e; ++i) {
              wk->ComputePrimIndex(paths[i], pw, pe);
            }
          });
        }
        for (auto &t : bts) t.join();

        // Merge in input order (chunks are contiguous and ascending).
        for (int t = 0; t < W; ++t) {
          size_t s = chunk_start(t), e = chunk_start(t + 1);
          for (size_t i = s; i < e; ++i) {
            MergeWorkerIndex(*workers[static_cast<size_t>(t)], paths[i].str());
          }
        }
        for (int t = 0; t < W; ++t) {
          if (warn) *warn += wwarn[static_cast<size_t>(t)];
          if (err) *err += werr[static_cast<size_t>(t)];
          for (const std::string &dp :
               workers[static_cast<size_t>(t)]->deferred_payload_prims) {
            deferred_payload_prims.insert(dp);
          }
        }
        return true;
      }
    }
#endif
    for (const Path &p : paths) ComputePrimIndex(p, warn, err);
    return true;
  }

  std::string ComputeInstanceKey(const Path &path, std::string *warn,
                                 std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    const std::vector<Src> &srcs = SourcesForPath(path, warn, err);
    return ComputeInstanceKeyImpl(srcs);
  }

  // --- invalidation -------------------------------------------------------

  void DropIndex(const std::string &key) {
    auto it = index_to_sites.find(key);
    if (it != index_to_sites.end()) {
      for (const Site &s : it->second) {
        auto sit = site_to_indices.find(s);
        if (sit != site_to_indices.end()) sit->second.erase(key);
      }
      index_to_sites.erase(it);
    }
    index_cache.erase(key);
    sources_cache.erase(key);
    DropInstancing(key);
  }

  void Invalidate(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    const std::string base = prim_path.str();
    std::set<std::string> to_drop;

    for (const auto &kv : index_cache) {
      if (IsAtOrUnder(kv.first, base)) to_drop.insert(kv.first);
    }
    for (const auto &kv : site_to_indices) {
      if (IsAtOrUnder(kv.first.prim_path, base)) {
        for (const std::string &dep : kv.second) to_drop.insert(dep);
      }
    }
    for (const std::string &k : to_drop) DropIndex(k);

    // sources_cache + deferred state may have descendants not in index_cache.
    for (auto it = sources_cache.begin(); it != sources_cache.end();) {
      if (IsAtOrUnder(it->first, base)) it = sources_cache.erase(it);
      else ++it;
    }
    for (auto it = deferred_payload_prims.begin();
         it != deferred_payload_prims.end();) {
      if (IsAtOrUnder(*it, base)) it = deferred_payload_prims.erase(it);
      else ++it;
    }
  }

  void InvalidateLayer(const std::string &layer_id) {
    NEXT_PCP_LOCK(api_mu_);
    std::set<std::string> to_drop;
    for (const auto &kv : site_to_indices) {
      if (kv.first.layer_id == layer_id) {
        for (const std::string &dep : kv.second) to_drop.insert(dep);
      }
    }
    for (const std::string &k : to_drop) DropIndex(k);
    // Conservative: a referenced layer feeds many prims' sources, and its spec
    // contents just changed.
    sources_cache.clear();
    spec_cache_.clear();
    reg_->Drop(layer_id);
  }

  // --- payloads -----------------------------------------------------------

  bool LoadPayload(const Path &prim_path, bool with_descendants,
                   std::string *warn, std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    if (with_descendants) {
      load_rules_.LoadWithDescendants(prim_path.str());
    } else {
      load_rules_.LoadWithoutDescendants(prim_path.str());
    }
    Invalidate(prim_path);
    ComputePrimIndex(prim_path, warn, err);  // recompose now.
    return true;
  }

  bool UnloadPayload(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    load_rules_.Unload(prim_path.str());
    Invalidate(prim_path);
    // Recompose so the deferred-payload set (and HasDeferredPayload) reflects
    // the unload immediately -- Invalidate() drops the deferred entries under
    // the path, and ExpandArcs repopulates them on the next composition.
    std::string warn, err;
    ComputePrimIndex(prim_path, &warn, &err);
    return true;
  }

  void SetLoadRules(const LoadRules &rules) {
    NEXT_PCP_LOCK(api_mu_);
    load_rules_ = rules;
    // A wholesale rule change can affect any prim; drop everything (lazy
    // rebuild). Deferred state is recomputed as prims recompose.
    index_cache.clear();
    sources_cache.clear();
    site_to_indices.clear();
    index_to_sites.clear();
    deferred_payload_prims.clear();
    prototype_by_key.clear();
    prototype_of.clear();
    instances_by_prototype.clear();
  }
};

// ---------------------------------------------------------------------------
// Cache (thin forwarding shell)
// ---------------------------------------------------------------------------

Cache::Cache() : impl_(new Impl()) {}
Cache::~Cache() = default;
Cache::Cache(Cache &&) noexcept = default;
Cache &Cache::operator=(Cache &&) noexcept = default;

nonstd::expected<Cache, std::string> Cache::Open(
    AssetResolver &resolver, std::shared_ptr<Layer> root_layer,
    const std::string &root_identifier, const CompositionOptions &options) {
  if (!root_layer) {
    return nonstd::make_unexpected(std::string("pcp::Cache::Open: null root layer"));
  }
  Cache cache;
  cache.impl_->resolver = &resolver;
  cache.impl_->options = options;
  cache.impl_->root_layer = root_layer;
  cache.impl_->root_identifier = root_identifier;

  std::string warn, err;
  uint32_t root_stack =
      cache.impl_->InternLayerStack(root_layer, root_identifier, &warn, &err);
  if (root_stack == Impl::kInvalidStack) {
    return nonstd::make_unexpected(err.empty()
                                       ? std::string("pcp::Cache::Open: failed "
                                                     "to build root layer stack")
                                       : err);
  }
  cache.impl_->CollectRelocates();
  return cache;
}

const PrimIndex *Cache::ComputePrimIndex(const Path &prim_path,
                                         std::string *warn, std::string *err) {
  return impl_->ComputePrimIndex(prim_path, warn, err);
}

bool Cache::PrewarmPrimIndices(const std::vector<Path> &paths, std::string *warn,
                               std::string *err) {
  return impl_->PrewarmPrimIndices(paths, warn, err);
}

bool Cache::BuildStage(Stage *stage, std::string *warn, std::string *err) {
  if (!stage) return false;
  return impl_->BuildStage(stage, warn, err);
}

bool Cache::IsInstance(const Path &p) const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  auto it = impl_->prototype_of.find(p.str());
  return it != impl_->prototype_of.end() && it->second != p.str();
}
Path Cache::GetPrototype(const Path &p) const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  auto it = impl_->prototype_of.find(p.str());
  return it != impl_->prototype_of.end() ? Path(it->second) : Path();
}
std::vector<Path> Cache::GetPrototypePaths() const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  std::vector<Path> out;
  out.reserve(impl_->instances_by_prototype.size());
  for (const auto &kv : impl_->instances_by_prototype) out.push_back(Path(kv.first));
  return out;
}
std::vector<Path> Cache::GetInstancesForPrototype(const Path &proto) const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  std::vector<Path> out;
  auto it = impl_->instances_by_prototype.find(proto.str());
  if (it != impl_->instances_by_prototype.end()) {
    for (const std::string &s : it->second) out.push_back(Path(s));
  }
  return out;
}
size_t Cache::PrototypeCount() const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  return impl_->instances_by_prototype.size();
}
std::string Cache::ComputeInstanceKey(const Path &p, std::string *warn,
                                      std::string *err) {
  return impl_->ComputeInstanceKey(p, warn, err);
}

bool Cache::LoadPayload(const Path &p, std::string *warn, std::string *err) {
  return impl_->LoadPayload(p, /*with_descendants=*/true, warn, err);
}
bool Cache::LoadPayload(const Path &p, LoadPolicy policy, std::string *warn,
                        std::string *err) {
  return impl_->LoadPayload(p, policy == LoadPolicy::WithDescendants, warn, err);
}
bool Cache::UnloadPayload(const Path &p) { return impl_->UnloadPayload(p); }
void Cache::SetLoadRules(const LoadRules &rules) { impl_->SetLoadRules(rules); }
LoadRules Cache::GetLoadRules() const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  return impl_->load_rules_;
}
bool Cache::HasDeferredPayload(const Path &p) const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  return impl_->deferred_payload_prims.count(p.str()) != 0;
}
std::vector<Path> Cache::GetDeferredPayloadPaths() const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  std::vector<Path> out;
  out.reserve(impl_->deferred_payload_prims.size());
  for (const std::string &s : impl_->deferred_payload_prims) out.push_back(Path(s));
  return out;
}

void Cache::Invalidate(const Path &prim_path) { impl_->Invalidate(prim_path); }
void Cache::InvalidateLayer(const std::string &id) { impl_->InvalidateLayer(id); }

bool Cache::HasComputedPrimIndex(const Path &prim_path) const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  return impl_->index_cache.count(prim_path.str()) != 0;
}
size_t Cache::ComputedPrimIndexCount() const {
  NEXT_PCP_LOCK(impl_->api_mu_);
  return impl_->index_cache.size();
}
const LayerRegistry &Cache::layer_registry() const { return impl_->registry; }
void Cache::PreloadLayer(const std::string &identifier,
                         std::shared_ptr<Layer> layer) {
  impl_->registry.Preload(identifier, std::move(layer));
}

// --- one-call composition helpers ------------------------------------------

bool ComposeStageFromLayer(std::shared_ptr<Layer> root_layer,
                           AssetResolver &resolver, Stage *out_stage,
                           const std::string &root_identifier,
                           const CompositionOptions &options, std::string *warn,
                           std::string *err) {
  if (!root_layer || !out_stage) return false;
  auto opened = Cache::Open(resolver, std::move(root_layer), root_identifier,
                            options);
  if (!opened) {
    if (err) *err += opened.error() + "\n";
    return false;
  }
  Cache cache = std::move(*opened);
  return cache.BuildStage(out_stage, warn, err);
}

bool ComposeStageFromFile(const std::string &filename, AssetResolver &resolver,
                          Stage *out_stage, const CompositionOptions &options,
                          std::string *warn, std::string *err) {
  std::shared_ptr<Layer> root = LoadLayerFromFile(filename, warn, err);
  if (!root) return false;
  return ComposeStageFromLayer(std::move(root), resolver, out_stage, filename,
                               options, warn, err);
}

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
