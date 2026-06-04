// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache implementation
// Phase 1: sublayers + references.  Phase 2: ancestral opinions + deferred payloads.

#include "cache.hh"

#include "../composition/composition.hh"  // reuse ParseReference / ParsePayload / CopyLocalOpinions

#include <algorithm>
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

}  // namespace

struct Cache::Impl {
  AssetResolver *resolver = nullptr;
  CompositionOptions options;
  LayerRegistry registry;
#if defined(TINYUSDZ_ENABLE_THREAD)
  mutable std::recursive_mutex api_mu_;  // guards all shared state below
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
  std::set<std::string> payloads_force_loaded;     // explicit LoadPayload overrides
  std::set<std::string> payloads_force_unloaded;   // explicit UnloadPayload overrides

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
    AppendLayerAndSublayers(st, layer, identifier, warn, err);

    uint32_t idx = static_cast<uint32_t>(layer_stacks.size());
    layer_stacks.push_back(std::move(st));
    stack_by_id.emplace(identifier, idx);
    return idx;
  }

  void AppendLayerAndSublayers(LayerStack &st, std::shared_ptr<Layer> layer,
                               const std::string &identifier, std::string *warn,
                               std::string *err) {
    st.layers.push_back(layer);
    const std::string anchor = DirOf(identifier);
    for (const std::string &sub : layer->meta().subLayers) {
      std::shared_ptr<Layer> sl =
          registry.GetOrLoad(*resolver, sub, anchor, warn, err);
      if (sl) {
        const std::string sub_id = resolver->ResolvePath(sub, anchor);
        AppendLayerAndSublayers(st, sl, sub_id, warn, err);
      }
    }
  }

  // --- spec lookup --------------------------------------------------------

  const PrimSpec *FindSpec(const LayerStack &st, const std::string &site,
                           const Layer **out_layer) const {
    Path p(site);
    for (const auto &lp : st.layers) {
      if (const PrimSpec *s = lp->prim_at_path(p)) {
        if (out_layer) *out_layer = lp.get();
        return s;
      }
    }
    return nullptr;
  }

  bool AnyAuthors(const std::vector<Src> &srcs) const {
    for (const Src &s : srcs) {
      if (FindSpec(layer_stacks[s.stack_idx], s.site, nullptr)) return true;
    }
    return false;
  }

  // --- payload policy -----------------------------------------------------

  bool ShouldLoadPayload(const std::string &root_prim_path,
                         const std::string &asset) {
    if (payloads_force_unloaded.count(root_prim_path)) return false;
    if (payloads_force_loaded.count(root_prim_path)) return true;
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

  // Resolve the selected variants for the variantSets defined on `spec`, using
  // the accumulated cross-source selection map. Returns one VariantData per
  // selected set.
  std::vector<const VariantData *> SelectVariants(
      const PrimSpec &spec,
      const std::map<std::string, std::string> &sels) const {
    std::vector<const VariantData *> out;
    for (const VariantSetData &vss : spec.meta().variantSets) {
      auto sit = sels.find(vss.name);
      if (sit == sels.end()) continue;  // no selection for this set
      for (const VariantData &vd : vss.variants) {
        if (vd.name == sit->second) {
          out.push_back(&vd);
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
      const PrimSpec *sp = FindSpec(layer_stacks[s.stack_idx], s.site, nullptr);
      if (sp && sp->meta().instanceable) return true;
    }
    return false;
  }

  // Structural key: composed type + each arc's (kind, layer-id, site, variant).
  // The root node (i==0) is excluded because its site is the instance's own
  // path, which differs between instances of the same asset.
  std::string ComputeInstanceKeyImpl(const std::vector<Src> &srcs) const {
    std::string key;
    for (const Src &s : srcs) {
      const PrimSpec *sp = FindSpec(layer_stacks[s.stack_idx], s.site, nullptr);
      if (sp && !sp->type_name().empty()) {
        key += "T:" + sp->type_name() + ";";
        break;
      }
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

  void RegisterInstance(const std::string &prim_path,
                        const std::vector<Src> &srcs) {
    if (!options.detect_instances || !IsInstanceableSources(srcs)) return;
    const std::string ik = ComputeInstanceKeyImpl(srcs);
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
                  const std::set<std::string> &cycle, std::vector<Src> *out,
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
          registry.GetOrLoad(*resolver, arc.asset_path, anchor, warn, err);
      if (!arc_layer) {
        if (options.error_when_asset_not_found && err) {
          *err += "Arc asset not found: " + arc.asset_path + "\n";
        }
        return;
      }
      const std::string arc_id = resolver->ResolvePath(arc.asset_path, anchor);
      arc_stack_idx = InternLayerStack(arc_layer, arc_id, warn, err);
      if (!arc.prim_path.empty()) {
        arc_site = arc.prim_path;
      } else if (!arc_layer->meta().defaultPrim.empty()) {
        arc_site = "/" + arc_layer->meta().defaultPrim;
      } else {
        const auto &roots = arc_layer->root_indices();
        arc_site = roots.empty() ? "/" : "/" + arc_layer->prim(roots[0])->name();
      }
    }

    const std::string key = layer_stacks[arc_stack_idx].identifier + ":" + arc_site;
    if (cycle.count(key)) {
      if (err) *err += "Composition cycle detected at arc: " + key + "\n";
      return;
    }
    if (cycle.size() >= options.max_depth) {
      if (err) *err += "Composition max depth exceeded\n";
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
        if (!FindSpec(layer_stacks[as], arc_site, nullptr)) continue;
        Src implied = arc_src;
        implied.stack_idx = as;
        std::set<std::string> ic = cycle;
        ic.insert(layer_stacks[as].identifier + ":" + arc_site);
        std::vector<uint32_t> ichain{as};
        ExpandArcs(implied, std::move(ic), target, spec_out, sels, ichain, warn,
                   err);
      }
    }

    // Reference/payload arcs descend into a new layer stack -> extend the chain.
    std::vector<uint32_t> child_chain = chain;
    if (arc_stack_idx != src.stack_idx) child_chain.push_back(arc_stack_idx);

    std::set<std::string> child_cycle = cycle;
    child_cycle.insert(key);
    ExpandArcs(arc_src, std::move(child_cycle), target, spec_out, sels,
               child_chain, warn, err);
  }

  // Pre-order DFS == strength order. Per source: local (pushed) > inherits >
  // references > payloads; specializes routed to `spec_out` (globally weakest).
  void ExpandArcs(const Src &src, std::set<std::string> cycle,
                  std::vector<Src> *out, std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err) {
    out->push_back(src);

    const Layer *src_layer = nullptr;
    const PrimSpec *spec =
        FindSpec(layer_stacks[src.stack_idx], src.site, &src_layer);
    if (!spec) return;

    // Record this source's variant selections (strong-first wins) so a selection
    // authored on a stronger source applies to a variantSet defined on a weaker
    // one (cross-source selection).
    RecordSelections(*spec, sels);

    // Inherits (stronger than references).
    for (const std::string &s : spec->meta().inherits) {
      ProcessArc(src, Compositor::ParseReference(s), ArcType::Inherit, cycle,
                 out, spec_out, sels, chain, warn, err);
    }

    // Variants (weaker than inherits, stronger than references). For each
    // selected variant, graft its inline opinions and/or its content subtree.
    if (!spec->meta().variantSets.empty()) {
      for (const VariantData *vd : SelectVariants(*spec, *sels)) {
        // Subtree content: model as a reference-style Variant source into the
        // content layer's "/__self__" root, so child prims compose normally.
        if (vd->content) {
          const std::string vid =
              "variant:" + std::to_string(reinterpret_cast<uintptr_t>(vd));
          uint32_t cstack = InternLayerStack(vd->content, vid, warn, err);
          Src vsrc;
          vsrc.stack_idx = cstack;
          vsrc.site = "/__self__";
          vsrc.map = NamespaceMapping::Compose(
              src.map, NamespaceMapping{"/__self__", src.site});
          vsrc.offset = src.offset;
          vsrc.arc_kind = ArcType::Variant;
          std::set<std::string> vc = cycle;
          vc.insert(vid + ":/__self__");
          ExpandArcs(vsrc, std::move(vc), out, spec_out, sels, chain, warn, err);
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
                 cycle, out, spec_out, sels, chain, warn, err);
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
        ProcessArc(src, arc, ArcType::Payload, cycle, out, spec_out, sels, chain, warn,
                   err);
      }
    }

    // Specializes (globally weakest; routed into spec_out by ProcessArc).
    for (const std::string &s : spec->meta().specializes) {
      ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize, cycle,
                 out, spec_out, sels, chain, warn, err);
    }
  }

  std::vector<Src> ExpandList(const std::vector<Src> &base, std::string *warn,
                              std::string *err) {
    std::vector<Src> main;
    std::vector<Src> spec;  // specialize-derived: globally weakest
    // Variant selections accumulated across the prim's sources (cross-source).
    std::map<std::string, std::string> sels;
    for (const Src &s : base) {
      std::set<std::string> cycle;
      cycle.insert(layer_stacks[s.stack_idx].identifier + ":" + s.site);
      // Carry a base source's own arc kind so a specialize re-rooted onto a
      // child stays globally weakest.
      std::vector<Src> *tgt = (s.arc_kind == ArcType::Specialize) ? &spec : &main;
      std::vector<uint32_t> chain{s.stack_idx};
      ExpandArcs(s, std::move(cycle), tgt, &spec, &sels, chain, warn, err);
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
    const std::string key = path.str();
    auto it = sources_cache.find(key);
    if (it != sources_cache.end()) return it->second;

    std::vector<Src> base;
    Path parent = path.parent();
    const bool parent_is_root =
        parent.is_root() || parent.empty() || parent.str() == "/";
    if (parent_is_root) {
      Src s;
      s.stack_idx = 0;
      s.site = key;
      base.push_back(std::move(s));
    } else {
      const std::vector<Src> &psrc = SourcesForPath(parent, warn, err);
      const std::string cn = path.name();
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
    auto res = sources_cache.emplace(key, std::move(expanded));
    return res.first->second;
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

      const Layer *layer = nullptr;
      const PrimSpec *spec = FindSpec(layer_stacks[s.stack_idx], s.site, &layer);
      if (!spec) continue;

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

    RegisterInstance(key, srcs);

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
      if (FindSpec(layer_stacks[s.stack_idx], s.site, nullptr)) {
        n.flags |= NodeFlags::HasSpecs;
        sites.push_back(Site{layer_stacks[s.stack_idx].identifier, s.site});
      }
      uint16_t ni = index->AddNode(std::move(n));
      if (i != 0) index->MutableNode(0).children.push_back(ni);
      order.push_back(static_cast<uint16_t>(i));
    }
    index->SetStrengthOrder(std::move(order));

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
                     uint32_t parent_idx, bool is_root, std::string *warn,
                     std::string *err) {
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
      BuildStageRec(child_src, child_out, out, idx, /*is_root=*/false, warn, err);
    }
  }

  bool BuildStage(Stage *stage, std::string *warn, std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    auto out = std::unique_ptr<Layer>(new Layer());

    const Layer *root = layer_stacks[0].layers[0].get();
    for (uint32_t ri : root->root_indices()) {
      const std::string nm = root->prim(ri)->name();
      BuildStageRec(Path("/" + nm), Path("/" + nm), out.get(), 0,
                    /*is_root=*/true, warn, err);
    }

    out->finalize();
    out->meta() = root->meta();
    stage->SetRootLayer(std::move(*out));
    return true;
  }

  // Batch build. When threads are enabled and num_threads != 1, first-level
  // reference/payload layers are prefetched into the (thread-safe) registry in
  // parallel, then indices are built serially (the shared Impl state -- caches,
  // dependency maps -- is mutated single-threaded). This parallelizes the
  // I/O-bound parsing; per-prim parallel index building (per-worker contexts) is
  // a deeper refactor left as future work.
  bool PrewarmPrimIndices(const std::vector<Path> &paths, std::string *warn,
                          std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
#if defined(TINYUSDZ_ENABLE_THREAD)
    int nt = options.num_threads;
    if (nt < 0) nt = static_cast<int>(std::thread::hardware_concurrency());
    if (nt > 1) {
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
        int workers = std::min<int>(nt, static_cast<int>(assets.size()));
        std::vector<std::thread> ts;
        auto fn = [&]() {
          for (;;) {
            size_t i = next_idx.fetch_add(1);
            if (i >= assets.size()) break;
            std::string w, e;
            registry.GetOrLoad(*resolver, assets[i].first, assets[i].second, &w, &e);
          }
        };
        ts.reserve(static_cast<size_t>(workers));
        for (int t = 0; t < workers; ++t) ts.emplace_back(fn);
        for (auto &t : ts) t.join();
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
    // Conservative: a referenced layer feeds many prims' sources.
    sources_cache.clear();
    registry.Drop(layer_id);
  }

  // --- payloads -----------------------------------------------------------

  bool LoadPayload(const Path &prim_path, std::string *warn, std::string *err) {
    NEXT_PCP_LOCK(api_mu_);
    payloads_force_loaded.insert(prim_path.str());
    payloads_force_unloaded.erase(prim_path.str());
    Invalidate(prim_path);
    ComputePrimIndex(prim_path, warn, err);  // recompose now.
    return true;
  }

  bool UnloadPayload(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    payloads_force_unloaded.insert(prim_path.str());
    payloads_force_loaded.erase(prim_path.str());
    Invalidate(prim_path);
    return true;
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
  cache.impl_->InternLayerStack(root_layer, root_identifier, &warn, &err);
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
  return impl_->LoadPayload(p, warn, err);
}
bool Cache::UnloadPayload(const Path &p) { return impl_->UnloadPayload(p); }
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
