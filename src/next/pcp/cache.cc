// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache implementation
// Phase 1: sublayers + references.  Phase 2: ancestral opinions + deferred payloads.

#include "cache.hh"

#include "../composition/composition.hh"  // reuse ParseReference / ParsePayload / CopyLocalOpinions

#include <algorithm>
#include <deque>
#include <limits>
#include <chrono>
#include <cstdlib>
#include <map>
#include <set>
#include <unordered_map>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <atomic>
#include <mutex>
#include <thread>
#if defined(TINYUSDZ_NEXT_FINE_LOCKS)
#include <shared_mutex>
#endif
#endif

namespace tinyusdz {
namespace next {
namespace pcp {

#if defined(TINYUSDZ_ENABLE_THREAD)
// Serializes the Cache's shared mutable state so it is safe to use
// (ComputePrimIndex / BuildStage / queries / payload edits) from multiple
// threads. Compiles to nothing in non-threaded builds.
//
// Three lock policies, selected at compile time:
//
//  * default (Phase 9 F6): a single non-recursive std::mutex. Public entry
//    points take the lock exactly once and delegate to lock-free `*_locked`
//    internals, so no public method re-enters the lock. READ and WRITE locks
//    are the same exclusive lock.
//
//  * TINYUSDZ_NEXT_FINE_LOCKS (Phase 9 F4): a std::shared_timed_mutex. Pure
//    reads (cache hits in ComputePrimIndex, and the read-only query methods)
//    take a shared lock and run concurrently; builds and writers take the
//    exclusive lock. ComputePrimIndex tries the shared fast path first and,
//    on a cache miss, re-acquires exclusively and double-checks (so the same
//    prim is built once even under contention).
//
//  * TINYUSDZ_NEXT_RECURSIVE_LOCK: the original re-entrant recursive mutex --
//    a bring-up escape hatch should a new re-entrant path be introduced.
#if defined(TINYUSDZ_NEXT_FINE_LOCKS)
#include <shared_mutex>
using PcpMutex = std::shared_timed_mutex;
// std::shared_lock lives in <shared_mutex> (included at file scope above).
#define NEXT_PCP_READ_LOCK(m) std::shared_lock<PcpMutex> _pcp_rlk(m)
#define NEXT_PCP_WRITE_LOCK(m) std::unique_lock<PcpMutex> _pcp_wlk(m)
#elif defined(TINYUSDZ_NEXT_RECURSIVE_LOCK)
using PcpMutex = std::recursive_mutex;
#define NEXT_PCP_READ_LOCK(m) std::lock_guard<PcpMutex> _pcp_lk(m)
#define NEXT_PCP_WRITE_LOCK(m) std::lock_guard<PcpMutex> _pcp_lk(m)
#else
using PcpMutex = std::mutex;
#define NEXT_PCP_READ_LOCK(m) std::lock_guard<PcpMutex> _pcp_lk(m)
#define NEXT_PCP_WRITE_LOCK(m) std::lock_guard<PcpMutex> _pcp_lk(m)
#endif
// Legacy alias: existing exclusive sites use NEXT_PCP_LOCK == write lock.
#define NEXT_PCP_LOCK(m) NEXT_PCP_WRITE_LOCK(m)
#else
#define NEXT_PCP_READ_LOCK(m) (void)0
#define NEXT_PCP_WRITE_LOCK(m) (void)0
#define NEXT_PCP_LOCK(m) (void)0
#endif

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
  mutable PcpMutex api_mu_;  // guards all shared state below (F6: non-recursive)

  // --- parallel-build worker hooks (see PrewarmPrimIndices) ---------------
  // When set, RegisterInstance stashes its (instanceable, key) into
  // pending_instance_ instead of mutating the prototype maps, so the merge can
  // assign prototypes deterministically in input order.
  bool defer_instances_ = false;
  std::unordered_map<std::string, std::pair<bool, std::string>> pending_instance_;
#endif

  std::shared_ptr<Layer> root_layer;  // kept alive (also layer_stacks[0].layers[0])
  std::string root_identifier;

  // Append-only shared tables backing every PrimIndex (Phase 9 F3). std::deque
  // gives stable element addresses: a PrimIndex handed to another thread keeps
  // resolving its nodes' layer-stacks/sites even as a concurrent build appends
  // new entries here (a std::vector would reallocate and dangle them).
  std::deque<LayerStack> layer_stacks;              // table; [0] == root stack
  std::map<std::string, uint32_t> stack_by_id;      // dedup by resolved identifier

  // Interned prim-path table shared by all PrimIndex nodes (dedup across indices).
  std::deque<std::string> path_table;
  std::unordered_map<std::string, uint32_t> path_intern;
  uint32_t InternPath(const std::string &p) {
    auto it = path_intern.find(p);
    if (it != path_intern.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(path_table.size());
    path_table.push_back(p);
    path_intern.emplace(p, idx);
    return idx;
  }

  std::map<std::string, std::unique_ptr<PrimIndex>> index_cache;
  std::unordered_map<std::string, std::vector<Src>> sources_cache;  // path -> expanded sources

  // Phase 10: lazily-composed per-prim specs + their composed child names,
  // keyed by prim path. Populated by ComposePrim, dropped by Invalidate.
  std::unordered_map<std::string, std::unique_ptr<PrimSpec>> composed_cache_;
  std::unordered_map<std::string, std::vector<std::string>> composed_children_;
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
        for (const auto &r : ps.meta().relocates()) {
          relocates_map[r.first] = r.second;
        }
      }
    }
  }

  // --- typed composition diagnostics (Phase 7 E4) -------------------------
  // Accumulated typed issues. Recorded regardless of whether a caller passed an
  // `err` pointer; when present, the rendered message is mirrored into *err so
  // the existing string-based API is unchanged.
  std::vector<CompositionIssue> issues_;

  void AddIssue(ErrorCode code, const std::string &site,
                const std::string &message, std::string *err) {
    issues_.push_back(CompositionIssue{code, site, message});
    if (err) *err += message + "\n";
  }

  // --- layer stacks -------------------------------------------------------

  static constexpr uint32_t kInvalidStack =
      (std::numeric_limits<uint32_t>::max)();

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
      AddIssue(ErrorCode::MaxDepthExceeded, identifier,
               "Sublayer max depth exceeded at: " + identifier, err);
      return false;
    }
    st.layers.push_back(layer);
    st.layer_identifiers.push_back(identifier);
    // Anchor = the referencing layer's FILE path; AssetResolver::Resolve takes
    // its directory internally. (Passing a pre-stripped dir double-strips and
    // breaks deep relative paths like `../../Materials/x.usd`.)
    const std::string &anchor = identifier;
    for (const std::string &sub : layer->meta().subLayers) {
      const std::string sub_id = resolver->ResolvePath(sub, anchor);
      if (sub_id.empty()) {
        AddIssue(ErrorCode::InvalidAssetPath, sub,
                 "Failed to resolve sublayer: " + sub, err);
        if (options.error_when_asset_not_found) return false;
        continue;
      }
      if (visiting && visiting->count(sub_id)) {
        AddIssue(ErrorCode::SublayerCycle, sub_id,
                 "Sublayer cycle detected at: " + sub_id, err);
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
    // Per-set `selected` field FIRST: it is the authoritative per-set form (what
    // the crate reader sets for EVERY selected set, and what the Compositor's
    // ApplyVariants reads first). The legacy variantSelection string carries only
    // ONE set, so without this a MULTI-set selection loaded from USDC loses every
    // set after the first. (emplace keeps the first/strongest opinion per set.)
    for (const VariantSetData &vss : spec.meta().variantSets()) {
      if (!vss.selected.empty()) sels->emplace(vss.name, vss.selected);
    }
    if (!spec.meta().variantSelection.empty()) {
      VariantSelection s =
          Compositor::ParseVariantSelection(spec.meta().variantSelection);
      if (!s.variant_set.empty()) sels->emplace(s.variant_set, s.variant_name);
    }
    for (const auto &sel : spec.meta().variantSelections()) {
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
    for (const VariantSetData &vss : spec.meta().variantSets()) {
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

  // Structural key: composed type + each MASTER-DEFINING arc's (kind, layer-id,
  // site, variant). Must be instance-INDEPENDENT so two instances of the same
  // asset share a prototype (pxr PcpInstanceKey).
  //
  // The root node (i==0) is excluded -- its site is the instance's own path.
  // ANCESTRAL / positional sources are also excluded: a deeply-referenced
  // instance accumulates sources whose `site` is the instance's path WITHIN an
  // ancestor asset (e.g. `/root/set/.../leaf_0069` vs `_0070`), carrying the
  // instance-LOCAL opinions (transform/placement) that live outside the
  // instancing boundary. Those have `site != map.source_prefix` (the arc has a
  // positional offset beyond where it was introduced). A master-defining arc
  // targets the prim directly, so `site == map.source_prefix` (asset-relative,
  // identical across instances). Keep only the latter.
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
      if (s.site != s.map.source_prefix) continue;  // ancestral/positional
      key += ArcTypeName(s.arc_kind);
      key += "|" + layer_stacks[s.stack_idx].identifier + "|" + s.site;
      if (s.variant) key += "|v:" + s.variant->name;
      // A non-identity layer offset bakes different sample times, so instances
      // with different offsets must not share a prototype.
      if (!s.offset.is_identity()) {
        key += "|o:" + std::to_string(s.offset.offset) + ":" +
               std::to_string(s.offset.scale);
      }
      key += ";";
    }
    return key;
  }

  // Prototype bookkeeping for a prim that is an instance with key `ik`.
  //
  // prefer_min == false (BuildStage): the first prim registered for a key
  // becomes its prototype. BuildStage visits prims in deterministic namespace
  // order and decides instance-vs-prototype in a single pass, so "first wins"
  // is already order-independent there.
  //
  // prefer_min == true (per-prim ComputePrimIndex cache and the parallel
  // merge): the prototype is the lexicographically-smallest member path, so the
  // cached prototype/instance grouping is identical regardless of the order --
  // or the threads -- in which prims were computed (Phase 9 F5). A smaller path
  // arriving later demotes the previous prototype and re-points the group.
  void AssignPrototype(const std::string &prim_path, const std::string &ik,
                       bool prefer_min = false) {
    auto pit = prototype_by_key.find(ik);
    if (pit == prototype_by_key.end()) {
      prototype_by_key[ik] = prim_path;  // first of its key seeds the prototype
      prototype_of[prim_path] = prim_path;
      instances_by_prototype[prim_path].push_back(prim_path);
      return;
    }
    const std::string old_proto = pit->second;
    if (prim_path == old_proto) return;  // re-registering the prototype itself

    if (!prefer_min || old_proto < prim_path) {
      // prim_path is a (non-prototype) instance of the existing prototype.
      prototype_of[prim_path] = old_proto;
      auto &vec = instances_by_prototype[old_proto];
      if (std::find(vec.begin(), vec.end(), prim_path) == vec.end()) {
        vec.push_back(prim_path);
      }
      return;
    }

    // prefer_min && prim_path < old_proto: prim_path becomes the new prototype.
    // Demote old_proto and re-point the whole group under the new prototype.
    pit->second = prim_path;
    std::vector<std::string> members;
    auto oit = instances_by_prototype.find(old_proto);
    if (oit != instances_by_prototype.end()) {
      members = std::move(oit->second);
      instances_by_prototype.erase(oit);
    }
    if (std::find(members.begin(), members.end(), prim_path) == members.end()) {
      members.push_back(prim_path);
    }
    for (const std::string &m : members) prototype_of[m] = prim_path;
    instances_by_prototype[prim_path] = std::move(members);
  }

  void RegisterInstance(const std::string &prim_path,
                        const std::vector<Src> &srcs, bool prefer_min = false) {
    if (!options.detect_instances || !IsInstanceableSources(srcs)) return;
    const std::string ik = ComputeInstanceKeyImpl(srcs);
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (defer_instances_) {
      // Worker mode: stash for the deterministic input-order merge.
      pending_instance_[prim_path] = {true, ik};
      return;
    }
#endif
    AssignPrototype(prim_path, ik, prefer_min);
  }

  void DropInstancing(const std::string &prim_path) {
    prototype_of.erase(prim_path);
    for (auto it = instances_by_prototype.begin();
         it != instances_by_prototype.end();) {
      auto &v = it->second;
      v.erase(std::remove(v.begin(), v.end(), prim_path), v.end());
      const bool was_prototype = (it->first == prim_path);
      if (was_prototype || v.empty()) {
        // The group's prototype is gone: orphan the remaining siblings so their
        // prototype_of doesn't dangle to a dropped prototype. They re-group on
        // the next recomposition. (Without this, IsInstance/GetPrototype could
        // report a removed prototype after invalidating its subtree.)
        for (const std::string &m : v) prototype_of.erase(m);
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
  // Unwrap a synthetic variant-content layer identifier
  // ("variant:<hostfile>:<site>:<vset>:<vname>", see the vid built in ExpandArcs)
  // back to the host layer's real FILE path, so a relative reference/payload
  // authored inside a variant anchors to the file that contains the variant.
  // Recurses for nested variants; a real file path passes through unchanged.
  static std::string RealAnchorOf(std::string id) {
    while (id.compare(0, 8, "variant:") == 0) {
      std::string s = id.substr(8);
      bool ok = true;
      for (int k = 0; k < 3; ++k) {  // strip trailing :site:vset:vname
        auto p = s.rfind(':');
        if (p == std::string::npos) { ok = false; break; }
        s.resize(p);
      }
      if (!ok) break;
      id.swap(s);
    }
    return id;
  }

  void ProcessArc(const Src &src, const CompositionArc &arc, ArcType kind,
                  const ExpansionFrame *frame, std::vector<Src> *out,
                  std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err, const std::string &authoring_layer_id = "") {
    uint32_t arc_stack_idx;
    std::string arc_site;
    if (arc.is_internal || arc.asset_path.empty()) {
      arc_stack_idx = src.stack_idx;
      arc_site = arc.prim_path.empty() ? src.site : arc.prim_path;
    } else {
      // Anchor = the FILE path of the LAYER that authored the arc (Resolve
      // derives its dir). A reference/payload authored in a SUBLAYER resolves
      // relative to that sublayer, not the layer-stack's root — fall back to the
      // stack identifier only when the authoring layer is unknown.
      // COPY, not a reference: GetOrLoad/InternLayerStack below can grow
      // layer_stacks and invalidate a reference into it.
      const std::string anchor = RealAnchorOf(
          !authoring_layer_id.empty()
              ? authoring_layer_id
              : layer_stacks[src.stack_idx].identifier);
      std::shared_ptr<Layer> arc_layer =
          reg_->GetOrLoad(*resolver, arc.asset_path, anchor, warn, err);
      if (!arc_layer) {
        if (options.error_when_asset_not_found) {
          AddIssue(ErrorCode::InvalidAssetPath, arc.asset_path,
                   "Arc asset not found: " + arc.asset_path, err);
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
      const std::string site =
          layer_stacks[arc_stack_idx].identifier + ":" + arc_site;
      AddIssue(ErrorCode::ArcCycle, site,
               "Composition cycle detected at arc: " + site, err);
      return;
    }
    if (frame && frame->depth + 1 >= options.max_depth) {
      AddIssue(ErrorCode::MaxDepthExceeded,
               layer_stacks[arc_stack_idx].identifier + ":" + arc_site,
               "Composition max depth exceeded", err);
      return;
    }
    // A same-stack arc that targets a namespace ANCESTOR of its own source site
    // grafts the source under itself: the composed namespace grows without
    // bound (e.g. `def "A" { def "B" (references = </A>) {} }`). The frame
    // chain cannot see this (each child prim starts a fresh expansion), so
    // reject it here.
    if (arc_stack_idx == src.stack_idx && arc_site != src.site &&
        IsAtOrUnder(src.site, arc_site)) {
      AddIssue(ErrorCode::ArcCycle, src.site,
               "Composition cycle detected: arc at " + src.site +
                   " targets ancestor " + arc_site,
               err);
      return;
    }

    NamespaceMapping local{arc_site, src.site};
    Src arc_src;
    arc_src.stack_idx = arc_stack_idx;
    arc_src.site = arc_site;
    arc_src.map = NamespaceMapping::Compose(src.map, local);
    // Compose this arc's layer offset under the parent's (root..arc chain), so
    // the referenced content's time samples are mapped into root/stage time.
    LayerOffset arc_off;
    if (!arc.layer_offset.empty()) {
      double o = 0.0, s = 1.0;
      Compositor::ParseLayerOffset(arc.layer_offset, o, s);
      arc_off = LayerOffset{o, s};
    }
    arc_src.offset = src.offset.Compose(arc_off);
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

  // --- cross-layer list-op merge (Phase 7 S5) -----------------------------

  enum class ArcSel { References, Payloads, Inherits, Specializes };

  static const std::vector<std::string> &SelectInlineArc(const PrimSpecMeta &m,
                                                         ArcSel f) {
    switch (f) {
      case ArcSel::References: return m.references;
      case ArcSel::Payloads: return m.payloads;
      case ArcSel::Inherits: return m.inherits;
      default: return m.specializes;
    }
  }
  static const ArcEdit *SelectArcEdit(const PrimSpecMeta &m, ArcSel f) {
    const ArcListOpEdits *e = m.arc_edits();
    if (!e) return nullptr;
    switch (f) {
      case ArcSel::References: return &e->references;
      case ArcSel::Payloads: return &e->payloads;
      case ArcSel::Inherits: return &e->inherits;
      default: return &e->specializes;
    }
  }

  // Compose one arc field across a site's specs (input is strong-first) per the
  // AOUSD list-op rules, applying weakest->strongest. Returns the merged arc
  // list in strong-first order (prepended-of-strongest ... appended-of-strongest).
  // A spec that does not author the field is a no-op; a bare/explicit list
  // replaces the weaker accumulation; prepend/append/delete edit it.
  // Returns (arc-string, authoring-layer-id) pairs so a relative reference/
  // payload asset path can be anchored to the layer that authored it (not the
  // layer-stack root). The layer-id rides along through the list-op merge.
  std::vector<std::pair<std::string, std::string>> MergeArcField(
      const std::vector<SpecRef> &specs, ArcSel f) const {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto it = specs.rbegin(); it != specs.rend(); ++it) {  // weakest first
      const PrimSpecMeta &m = it->spec->meta();
      const std::string &lid = it->layer_id;
      const ArcEdit *e = SelectArcEdit(m, f);
      const std::vector<std::string> &inl = SelectInlineArc(m, f);
      if ((!e || !e->authored) && inl.empty()) {
        continue;  // field not authored on this spec
      }
      auto tag = [&](const std::vector<std::string> &v) {
        std::vector<std::pair<std::string, std::string>> out;
        out.reserve(v.size());
        for (const std::string &s : v) out.emplace_back(s, lid);
        return out;
      };
      if (!e || !e->authored || e->is_explicit) {
        result = tag(inl);  // bare/explicit replaces weaker layers
        continue;
      }
      auto removeAll = [&](const std::vector<std::string> &rm) {
        for (const std::string &x : rm) {
          result.erase(std::remove_if(result.begin(), result.end(),
                                      [&](const auto &p) { return p.first == x; }),
                       result.end());
        }
      };
      removeAll(e->deleted);
      removeAll(e->prepended);  // dedup before re-adding
      removeAll(e->appended);
      std::vector<std::pair<std::string, std::string>> merged;
      merged.reserve(e->prepended.size() + result.size() + e->appended.size());
      auto pre = tag(e->prepended), app = tag(e->appended);
      merged.insert(merged.end(), pre.begin(), pre.end());
      merged.insert(merged.end(), result.begin(), result.end());
      merged.insert(merged.end(), app.begin(), app.end());
      result.swap(merged);
    }
    return result;
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

    // Phase 7 (S5): with apply_list_ops, gather each arc field once across the
    // whole site (cross-layer list-op merge: explicit-replace / prepend /
    // append / delete / dedup); otherwise expand each spec's arcs independently
    // (legacy strong-first concatenation). Variants are always per-spec.
    const bool merge = options.apply_list_ops;

    if (merge) {
      for (const auto &s : MergeArcField(specs, ArcSel::Inherits)) {
        ProcessArc(src, Compositor::ParseReference(s.first), ArcType::Inherit,
                   frame, out, spec_out, sels, chain, warn, err, s.second);
      }
    }

    for (const SpecRef &sr : specs) {
      const PrimSpec *spec = sr.spec;

      // Inherits (stronger than references) -- legacy per-spec path.
      if (!merge) {
        for (const std::string &s : spec->meta().inherits) {
          ProcessArc(src, Compositor::ParseReference(s), ArcType::Inherit, frame,
                     out, spec_out, sels, chain, warn, err);
        }
      }

      // Variants (weaker than inherits, stronger than references). For each
      // selected variant, graft its inline opinions and/or its content subtree.
      if (!spec->meta().variantSets().empty()) {
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
          // Composition arcs authored on the variant OPTION (e.g. a `payload`
          // that supplies the def-Mesh geometry — XGen assets author geometry on
          // the selected variant, not in its body). Compose at the variant's
          // strength (after the variant's own content/inline, before the host's
          // references), anchored to the layer that authored the variant so a
          // relative `@./geo.usd@` resolves correctly.
          for (const std::string &s : vd->inherits) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Inherit,
                       frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
          }
          for (const std::string &s : vd->references) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Reference,
                       frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
          }
          if (!vd->payloads.empty()) {
            const std::string root_prim_path = src.map.Apply(src.site);
            for (const std::string &pl_str : vd->payloads) {
              CompositionArc arc = Compositor::ParsePayload(pl_str);
              if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
                deferred_payload_prims.insert(root_prim_path);
                continue;
              }
              deferred_payload_prims.erase(root_prim_path);
              ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                         chain, warn, err, sr.layer_id);
            }
          }
          for (const std::string &s : vd->specializes) {
            ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize,
                       frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
          }
        }

        // Crate representation: pxr-authored crates store variant content as
        // bracketed HOLDER prims ("/Prim/{set=sel}/..." in the SAME layer stack)
        // and a VariantSetData that carries only {name, selected} -- no inline
        // VariantData.content, so SelectVariants() above finds nothing. For each
        // selected set, graft the selected holder as a Variant source mapped onto
        // the host, so its descendants compose as the host's children (matching
        // the USDA content path). USDA-authored variants have no holder prim at
        // this site, so Specs() is empty and this is a no-op for them.
        for (const VariantSetData &vss : spec->meta().variantSets()) {
          auto sit = sels->find(vss.name);
          if (sit == sels->end() || sit->second.empty()) continue;
          const std::string holder =
              src.site + "/{" + vss.name + "=" + sit->second + "}";
          if (Specs(src.stack_idx, holder).empty()) continue;  // not crate-style
          Src vsrc;
          vsrc.stack_idx = src.stack_idx;
          vsrc.site = holder;
          vsrc.map = NamespaceMapping::Compose(
              src.map, NamespaceMapping{holder, src.site});
          vsrc.offset = src.offset;
          vsrc.arc_kind = ArcType::Variant;
          const ExpansionFrame vframe{src.stack_idx, &vsrc.site, frame,
                                      frame ? frame->depth + 1 : 0};
          ExpandArcs(vsrc, &vframe, out, spec_out, sels, chain, warn, err);
        }
      }

      if (merge) continue;  // merged refs/payloads/specializes handled below

      // References. Anchor relative asset paths to THIS spec's authoring layer.
      for (const std::string &ref_str : spec->meta().references) {
        ProcessArc(src, Compositor::ParseReference(ref_str), ArcType::Reference,
                   frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
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
                     chain, warn, err, sr.layer_id);
        }
      }

      // Specializes (globally weakest; routed into spec_out by ProcessArc).
      for (const std::string &s : spec->meta().specializes) {
        ProcessArc(src, Compositor::ParseReference(s), ArcType::Specialize,
                   frame, out, spec_out, sels, chain, warn, err, sr.layer_id);
      }
    }

    if (merge) {
      // Cross-layer-merged references / payloads / specializes, processed once
      // in LIVRPS order (all inherits already emitted above, before variants).
      for (const auto &s : MergeArcField(specs, ArcSel::References)) {
        ProcessArc(src, Compositor::ParseReference(s.first), ArcType::Reference,
                   frame, out, spec_out, sels, chain, warn, err, s.second);
      }
      auto mpay = MergeArcField(specs, ArcSel::Payloads);
      if (!mpay.empty()) {
        const std::string root_prim_path = src.map.Apply(src.site);
        for (const auto &pl : mpay) {
          CompositionArc arc = Compositor::ParsePayload(pl.first);
          if (!ShouldLoadPayload(root_prim_path, arc.asset_path)) {
            deferred_payload_prims.insert(root_prim_path);
            continue;
          }
          deferred_payload_prims.erase(root_prim_path);
          ProcessArc(src, arc, ArcType::Payload, frame, out, spec_out, sels,
                     chain, warn, err, pl.second);
        }
      }
      for (const auto &s : MergeArcField(specs, ArcSel::Specializes)) {
        ProcessArc(src, Compositor::ParseReference(s.first), ArcType::Specialize,
                   frame, out, spec_out, sels, chain, warn, err, s.second);
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

  // Merge all sources into a fresh PrimSpec; return the composed child names.
  //
  // OPINIONS compose STRONG->WEAK (fill-absent: the strongest source that
  // authored a field/property wins). CHILD-NAME ORDER, however, composes
  // WEAK->STRONG -- pxr's PcpComposeSiteChildNames appends each node's (and each
  // layer's) primChildren weakest-first, so the weakest source that introduces a
  // name fixes its position and stronger duplicates are skipped (e.g. a
  // reference's `[LOD0, Materials]` precedes a local-only `LOD1` ->
  // `[LOD0, Materials, LOD1]`). So we collect names in a separate reverse pass.
  std::vector<std::string> ComposeInto(const std::vector<Src> &srcs,
                                        PrimSpec *out) {
    bool specifier_set = false;

    // Pass 1 (strong->weak): compose opinions.
    for (const Src &s : srcs) {
      // Variant source: graft the selected variant's inline opinions
      // (properties + relationships) with fill-absent semantics. Variant child
      // prims are not yet modeled (VariantData has no child storage).
      if (s.variant) {
        for (const auto &pr : s.variant->properties) {
          const PropSlot *ts = out->property(pr.name);
          if (!ts) {
            out->add_property(pr.name, pr.value, pr.flags);
          } else if (ts->value_offset == UINT32_MAX) {
            // Field-level fill-absent (see CopyLocalOpinions): a variant default
            // fills a stronger connection-only / declared-only slot.
            out->fill_property_value_if_absent(
                GetPropNameTable().intern(pr.name), pr.value);
          }
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

        if (!specifier_set) {
          out->set_specifier(spec->specifier());
          specifier_set = true;
        }

        // Remap relationship/connection TARGET paths from this arc's
        // (site-local) namespace into the composed namespace, so a referenced
        // asset's internal targets (e.g. material:binding, .connect) resolve to
        // their flattened paths. Identity for local opinions.
        Compositor::CopyLocalOpinions(
            *out, *spec, s.offset.offset, s.offset.scale,
            [&s](const std::string &p) { return s.map.Apply(p); });
      }
    }

    // Flatten drops variant SELECTION metadata: the selected variant's content
    // has already been grafted inline (ExpandArcs / variant Src), so pxr's
    // flattened output carries no `variants = {...}` or variantSets (usdcat emits
    // zero). Strip the vestigial selection that rode along via CopyLocalOpinions.
    // Use a const view for the emptiness checks so we never allocate the meta ext
    // just to clear an absent field.
    {
      const PrimSpecMeta &cm = out->meta();
      if (!cm.variantSelection.empty()) out->meta().variantSelection.clear();
      if (!cm.variantSets().empty()) out->meta().variantSets().clear();
      if (!cm.variantSelections().empty()) out->meta().variantSelections().clear();
    }

    // Pass 2 (weak->strong): compose child-name ORDER.
    return ComposeChildNames(srcs);
  }

  // Composed child-name ORDER for a prim's sources (weak->strong): iterate sources
  // and their layer-stack specs in reverse (weakest first); within a single layer
  // the primChildren are in authored (forward) order. Append unseen names. This is
  // ComposeInto's Pass 2, factored out so the parallel source-warming pre-pass
  // discovers exactly the same child namespace the serial compose walks.
  std::vector<std::string> ComposeChildNames(const std::vector<Src> &srcs) {
    std::vector<std::string> child_names;
    std::set<std::string> seen_child;
    for (auto sit = srcs.rbegin(); sit != srcs.rend(); ++sit) {
      const Src &s = *sit;
      if (s.variant) continue;  // variant child prims not modeled yet
      const std::vector<SpecRef> &specs = Specs(s.stack_idx, s.site);
      for (auto rit = specs.rbegin(); rit != specs.rend(); ++rit) {
        const PrimSpec *spec = rit->spec;
        const Layer *layer = rit->layer;
        for (uint32_t ci : spec->child_indices()) {
          const PrimSpec *cs = layer->prim(ci);
          if (!cs) continue;
          // Bracketed variant HOLDER prims ("{set=sel}") are namespace markers,
          // not real children -- the selected holder's descendants are grafted
          // onto the host as a Variant source (see ExpandArcs). Skip the markers
          // so they never leak into the composed child list.
          if (!cs->name().empty() && cs->name().front() == '{') continue;
          if (seen_child.insert(cs->name()).second) {
            child_names.push_back(cs->name());
          }
        }
      }
    }
    return child_names;
  }

  // --- lazy per-prim composition (Phase 10) -------------------------------

  // Compose just `prim_path` on first access (cached), reusing the exact
  // source-resolution + opinion-merge of BuildStageRec's per-prim step, but
  // without walking/emitting the whole namespace. Returns nullptr if the prim
  // authors no opinions. Assumes api_mu_ is held.
  const PrimSpec *ComposePrim_locked(const Path &prim_path, std::string *warn,
                                     std::string *err) {
    const std::string key = prim_path.str();
    auto it = composed_cache_.find(key);
    if (it != composed_cache_.end()) return it->second.get();

    const std::vector<Src> &srcs = SourcesForPath(prim_path, warn, err);
    if (!AnyAuthors(srcs)) return nullptr;

    auto spec = std::unique_ptr<PrimSpec>(new PrimSpec(prim_path.name()));
    spec->set_path(prim_path);
    std::vector<std::string> children = ComposeInto(srcs, spec.get());

    const PrimSpec *ret = spec.get();
    composed_cache_.emplace(key, std::move(spec));
    composed_children_[key] = std::move(children);
    return ret;
  }

  // --- ComputePrimIndex ---------------------------------------------------

  // Public entry: takes the lock once, then runs the lock-free worker.
  const PrimIndex *ComputePrimIndex(const Path &prim_path, std::string *warn,
                                    std::string *err) {
#if defined(TINYUSDZ_ENABLE_THREAD) && defined(TINYUSDZ_NEXT_FINE_LOCKS)
    // F4 fast path: a shared lock lets cache hits resolve concurrently. On a
    // miss we fall through to the exclusive build; ComputePrimIndex_locked
    // re-checks index_cache under the write lock, so a prim contended by many
    // threads is still built exactly once.
    {
      NEXT_PCP_READ_LOCK(api_mu_);
      auto it = index_cache.find(prim_path.str());
      if (it != index_cache.end()) return it->second.get();
    }
#endif
    NEXT_PCP_WRITE_LOCK(api_mu_);
    return ComputePrimIndex_locked(prim_path, warn, err);
  }

  // Assumes api_mu_ is already held. Internal callers (LoadPayload /
  // UnloadPayload / the serial PrewarmPrimIndices path) use this directly so the
  // lock is never re-entered (F6: the mutex is non-recursive).
  const PrimIndex *ComputePrimIndex_locked(const Path &prim_path,
                                           std::string *warn, std::string *err) {
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
        AddIssue(ErrorCode::IndexCapacityExceeded, key,
                 "PrimIndex node count exceeds uint16 capacity for " + key, err);
        return nullptr;
      }
      if (i != 0) index->MutableNode(0).children.push_back(ni);
      order.push_back(ni);
    }
    index->SetStrengthOrder(std::move(order));
    // Bind the path table AFTER the node loop interned every site, so the
    // size snapshot covers all of this index's site_path_idx values (F3).
    index->SetPathTable(&path_table, path_table.size());

    // Per-prim cache: pick the prototype deterministically (min path) so the
    // grouping does not depend on the order/threads of ComputePrimIndex (F5).
    RegisterInstance(key, srcs, /*prefer_min=*/true);

    for (const Site &site : sites) site_to_indices[site].insert(key);
    index_to_sites[key] = std::move(sites);

    const PrimIndex *ret = index.get();
    index_cache.emplace(key, std::move(index));
    return ret;
  }

  // --- BuildStage ---------------------------------------------------------

  // `src_path` is where composition opinions are gathered; `out_path` is where
  // the composed prim is placed (differs when a relocate renames it).
  struct BuildStageWork {
    Path src_path;
    Path out_path;
    uint32_t parent_idx = 0;
    bool is_root = false;
    uint32_t depth = 0;
  };

  // Temporary internal-breakdown profiling accumulators (ns), summed across all
  // root subtrees; printed by the top-level BuildStage under TINYUSDZ_NEXT_TIMING.
  mutable uint64_t prof_sources_ns_ = 0;
  mutable uint64_t prof_compose_ns_ = 0;
  mutable uint64_t prof_reg_ns_ = 0;

  void BuildStage(Layer *out, const BuildStageWork &root_work,
                  std::string *warn, std::string *err) {
    using ProfClock = std::chrono::steady_clock;
    const bool prof = options.enable_timing;
    std::vector<BuildStageWork> stack;
    stack.push_back(root_work);

    while (!stack.empty()) {
      BuildStageWork w = stack.back();
      stack.pop_back();

      // Namespace-depth backstop: an arc-induced namespace that keeps growing
      // (e.g. an ancestor cycle a stronger check missed) must surface as an
      // error, never as C++ stack exhaustion.
      if (w.depth > options.max_namespace_depth) {
        AddIssue(ErrorCode::MaxDepthExceeded, w.out_path.str(),
                 "BuildStage max namespace depth exceeded at: " + w.out_path.str(),
                 err);
        continue;
      }
      auto ps0 = prof ? ProfClock::now() : ProfClock::time_point{};
      const std::vector<Src> &srcs = SourcesForPath(w.src_path, warn, err);
      auto ps1 = prof ? ProfClock::now() : ProfClock::time_point{};

      PrimSpec spec(w.out_path.name());
      spec.set_path(w.out_path);
      std::vector<std::string> children = ComposeInto(srcs, &spec);
      auto ps2 = prof ? ProfClock::now() : ProfClock::time_point{};
      if (prof) {
        prof_sources_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ps1 - ps0).count();
        prof_compose_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ps2 - ps1).count();
      }

      // Instancing: register this prim. If it is an instance (not the prototype
      // of its group), link it to the prototype and do NOT duplicate its subtree
      // -- children are provided by the prototype (UsdPrim follows
      // instance_prototype).
      auto pr0 = prof ? ProfClock::now() : ProfClock::time_point{};
      RegisterInstance(w.out_path.str(), srcs);
      if (prof) prof_reg_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(ProfClock::now() - pr0).count();
      bool is_instance = false;
      {
        auto pit = prototype_of.find(w.out_path.str());
        if (pit != prototype_of.end() && pit->second != w.out_path.str()) {
          spec.meta().instance_prototype() = pit->second;
          is_instance = true;
        }
      }

      uint32_t idx = out->add_prim(std::move(spec));
      if (w.is_root) {
        out->add_root(idx);
      } else {
        out->set_parent(idx, w.parent_idx);
      }

      if (is_instance) continue;  // prototype provides the subtree

      // Push children in reverse order so the first child processes first
      // (preserving the original visitation order).
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const std::string &cn = *it;
        Path child_src = w.src_path.append_child(cn);
        Path child_out;
        auto rit = relocates_map.find(child_src.str());
        if (rit != relocates_map.end() &&
            Path(rit->second).parent().str() == w.out_path.str()) {
          child_out = Path(rit->second);
        } else {
          child_out = w.out_path.append_child(cn);
        }
        stack.push_back({child_src, child_out, idx, /*is_root=*/false,
                         w.depth + 1});
      }
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
    // Root (pseudo-root child) names compose WEAK->STRONG too, like nested
    // children (pxr PcpComposeSiteChildNames): iterate the root layer stack
    // weakest-first so a sublayer-only root prim keeps its position. Prototype
    // assignment is order-independent (see test_prototype_order_independent).
    std::set<std::string> seen_root;
    std::vector<std::string> root_names;
    for (auto lit = layer_stacks[0].layers.rbegin();
         lit != layer_stacks[0].layers.rend(); ++lit) {
      const auto &lp = *lit;
      for (uint32_t ri : lp->root_indices()) {
        const PrimSpec *ps = lp->prim(ri);
        if (!ps) continue;
        if (seen_root.insert(ps->name()).second) root_names.push_back(ps->name());
      }
    }
#if defined(TINYUSDZ_ENABLE_THREAD)
    // Pre-warm sources_cache in parallel (LIVRPS arc resolution dominates
    // build_stage). The serial walk below then runs against an already-warm
    // cache; output is byte-identical (warming only fills a cache, and property
    // emission is name-ordered so it does not depend on intern/parse order).
    // OPT-IN: only when num_threads is explicitly > 1 (not auto), because the
    // pre-warm currently wins on small compose-bound scenes but regresses huge
    // instanced scenes (the worker layer-parse is serialized by the global
    // PropNameTable lock, plus merge + name-id-bloat overhead). Perf tuning
    // (serial layer pre-load, instancing-aware warming, parallel merge) is TODO.
    if (options.num_threads > 1 && !root_names.empty()) {
      ParallelWarmSources(root_names, options.num_threads, warn, err);
    }
#endif
    prof_sources_ns_ = prof_compose_ns_ = prof_reg_ns_ = 0;
    for (const std::string &nm : root_names) {
      BuildStage(out.get(), {Path("/" + nm), Path("/" + nm), 0, true, 1},
                 warn, err);
    }
    if (options.enable_timing) {
      std::fprintf(stderr,
                   "[next_build] sources=%.1fms compose=%.1fms register=%.1fms\n",
                   prof_sources_ns_ / 1e6, prof_compose_ns_ / 1e6,
                   prof_reg_ns_ / 1e6);
    }

    switch (options.instance_flatten_mode) {
      case InstanceFlattenMode::Holder:
        FlattenInstances(out.get());
        break;
      case InstanceFlattenMode::ExtractedPrototypes:
        FlattenInstancesExtracted(out.get(), options.prototype_numbering);
        break;
      case InstanceFlattenMode::Native:
        break;
    }

    out->finalize();
    out->meta() = root->meta();
    stage->SetRootLayer(std::move(*out));
    return true;
  }

  // Convert native instancing to a self-contained flatten: each prototype group
  // keeps its prototype member as the shared content holder (made
  // non-instanceable), and every other member is emptied + internally references
  // that holder. See CompositionOptions::flatten_instances.
  void FlattenInstances(Layer *out) {
    if (instances_by_prototype.empty()) return;

    std::unordered_map<std::string, uint32_t> idx_by_path;
    for (uint32_t i = 0; i < out->prim_count(); ++i)
      if (const PrimSpec *p = out->prim(i)) idx_by_path[p->path().str()] = i;

    for (const auto &grp : instances_by_prototype) {
      const std::string &proto_path = grp.first;
      const std::vector<std::string> &members = grp.second;
      // A lone instanceable prim with no peers still composed its own subtree as
      // the prototype; leaving it untouched is already a valid flatten.
      bool has_instance = false;
      for (const std::string &m : members)
        if (m != proto_path) { has_instance = true; break; }
      if (!has_instance) continue;

      auto pit = idx_by_path.find(proto_path);
      if (pit == idx_by_path.end()) continue;
      // Holder: keep the composed subtree, drop the instanceable flag so it is a
      // plain reference target (an instanceable holder would hide its content).
      if (PrimSpec *proto = out->prim(pit->second)) {
        proto->meta().instanceable = false;
        proto->meta().instance_prototype().clear();
      }

      const std::string ref = "<" + proto_path + ">";
      for (const std::string &m : members) {
        if (m == proto_path) continue;
        auto mit = idx_by_path.find(m);
        if (mit == idx_by_path.end()) continue;
        PrimSpec *mp = out->prim(mit->second);
        if (!mp) continue;
        mp->meta().instanceable = true;
        mp->meta().instance_prototype().clear();
        // Avoid duplicate refs if FlattenInstances somehow runs twice.
        auto &refs = mp->meta().references;
        if (std::find(refs.begin(), refs.end(), ref) == refs.end())
          refs.push_back(ref);
      }
    }
  }

  // usdcat-style flatten: move each prototype group's shared subtree to a root
  // `over "/Flattened_Prototype_N"` and rewrite every member (holder included)
  // to `instanceable = true` + `references = </Flattened_Prototype_N>`. Numbering
  // per `numbering` (Deterministic = sort by prototype path; UsdcatCompatible =
  // pxr's two-stage scheme). See CompositionOptions::instance_flatten_mode.
  void FlattenInstancesExtracted(Layer *out, PrototypeNumbering numbering) {
    if (instances_by_prototype.empty()) return;

    // path -> prim index (for member rewrites in Pass 2). Member indices stay
    // valid: Pass 1 only ADDS prims (FP roots + clones), never reindexes.
    std::unordered_map<std::string, uint32_t> idx_by_path;
    for (uint32_t i = 0; i < out->prim_count(); ++i)
      if (const PrimSpec *p = out->prim(i)) idx_by_path[p->path().str()] = i;

    // --- order groups, assign /Flattened_Prototype_N ------------------------
    // members[0] == prototype path == namespace-pre-order-first instance.
    std::vector<std::string> ordered;
    {
      std::vector<std::string> protos;
      protos.reserve(instances_by_prototype.size());
      for (const auto &g : instances_by_prototype) protos.push_back(g.first);
      std::sort(protos.begin(), protos.end());  // by prototype path (== pxr front)
      if (numbering == PrototypeNumbering::Deterministic) {
        ordered = std::move(protos);
      } else {
        // UsdcatCompatible: label groups `__Prototype_{rank+1}` in front-order,
        // then re-sort by label STRING (pxr GetPrototypes() lexicographic sort;
        // differs from numeric at >9 groups: _1,_10,_2,...).
        std::vector<std::pair<std::string, std::string>> labelled;
        labelled.reserve(protos.size());
        for (size_t i = 0; i < protos.size(); ++i)
          labelled.emplace_back("__Prototype_" + std::to_string(i + 1), protos[i]);
        std::sort(labelled.begin(), labelled.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        ordered.reserve(labelled.size());
        for (auto &lp : labelled) ordered.push_back(std::move(lp.second));
      }
    }

    out->build_path_index();
    std::unordered_map<std::string, std::string> fpath;  // proto -> "/Flattened_Prototype_j"
    {
      size_t n = 1;
      for (const std::string &proto : ordered) {
        std::string p;
        do { p = "/Flattened_Prototype_" + std::to_string(n++); }
        while (out->prim_at_path(p) != nullptr);  // skip user-named clashes
        fpath[proto] = p;
      }
    }

    // FP a descendant `d` should reference when cloned into a prototype subtree:
    // `d` itself if it is a (nested) prototype group key, else d's prototype.
    auto fp_ref_for = [&](const std::string &d_path,
                          const std::string &d_inst_proto) -> const std::string * {
      auto it = fpath.find(d_path);
      if (it != fpath.end()) return &it->second;            // nested prototype
      if (!d_inst_proto.empty()) {
        auto j = fpath.find(d_inst_proto);
        if (j != fpath.end()) return &j->second;            // nested instance
      }
      return nullptr;
    };

    // --- Pass 1: emit FP roots + clone each prototype's shared subtree -------
    // Reads ORIGINAL prims; all of Pass 1 runs before any Pass 2 orphaning.
    struct CloneWork {
      uint32_t src_idx;
      uint32_t dst_parent_idx;
      std::string dst_parent_path;
    };
    for (const std::string &proto : ordered) {
      auto pit = idx_by_path.find(proto);
      if (pit == idx_by_path.end()) continue;
      const std::string &fp = fpath[proto];

      PrimSpec root(fp.substr(1));  // "Flattened_Prototype_j"
      root.set_path(Path(fp));
      root.set_specifier(PrimSpecifier::Over);  // typeless `over`, like usdcat
      uint32_t fp_idx = out->add_prim(std::move(root));
      out->add_root(fp_idx);

      // Push children reversed so the work STACK pops them first-child-first,
      // preserving source namespace order in the cloned subtree.
      std::vector<CloneWork> work;
      {
        const auto &cc = out->prim(pit->second)->child_indices();
        for (auto it = cc.rbegin(); it != cc.rend(); ++it)
          work.push_back({*it, fp_idx, fp});
      }

      while (!work.empty()) {
        CloneWork cw = work.back();
        work.pop_back();
        // Capture everything from the source BEFORE add_prim (it may realloc).
        std::string name, new_path, src_path, inst_proto;
        std::vector<uint32_t> src_children;
        PrimSpec clone;
        {
          const PrimSpec *src = out->prim(cw.src_idx);
          if (!src) continue;
          name = src->name();
          src_path = src->path().str();
          inst_proto = src->meta().instance_prototype();
          src_children = src->child_indices();
          new_path = cw.dst_parent_path + "/" + name;
          clone = src->Clone();
        }
        const std::string *ref = fp_ref_for(src_path, inst_proto);
        const bool leaf = (ref != nullptr);

        clone.set_name(name);
        clone.set_path(Path(new_path));
        clone.clear_child_indices();  // Clone copied stale source child links
        // Retarget internal material:binding / connections that pointed inside
        // the prototype (under `proto`) to the moved /Flattened_Prototype_N root.
        clone.remap_target_prefix(proto, fp);
        if (leaf) {
          clone.meta().instanceable = true;
          clone.meta().instance_prototype().clear();
          clone.meta().references.clear();
          clone.meta().references.push_back("<" + *ref + ">");
        }
        uint32_t new_idx = out->add_prim(std::move(clone));  // invalidates ptrs
        out->set_parent(new_idx, cw.dst_parent_idx);
        if (!leaf)
          for (auto it = src_children.rbegin(); it != src_children.rend(); ++it)
            work.push_back({*it, new_idx, new_path});
      }
    }

    // --- Pass 2: rewrite every member at its original namespace position -----
    for (const std::string &proto : ordered) {
      const std::string ref = "<" + fpath[proto] + ">";
      auto mit = instances_by_prototype.find(proto);
      if (mit == instances_by_prototype.end()) continue;
      for (const std::string &m : mit->second) {
        auto it = idx_by_path.find(m);
        if (it == idx_by_path.end()) continue;
        PrimSpec *mp = out->prim(it->second);
        if (!mp) continue;
        mp->meta().instanceable = true;
        mp->meta().instance_prototype().clear();
        mp->clear_child_indices();  // orphan inline subtree (now under the FP root)
        auto &refs = mp->meta().references;
        if (std::find(refs.begin(), refs.end(), ref) == refs.end())
          refs.push_back(ref);
      }
    }
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
    // Remap above already interned every site into path_table, so its size is
    // final for this index's snapshot (F3).
    idx->SetPathTable(&path_table, path_table.size());

    auto sit = w.index_to_sites.find(key);
    if (sit != w.index_to_sites.end()) {
      for (const Site &s : sit->second) site_to_indices[s].insert(key);
      index_to_sites[key] = sit->second;
    }

    index_cache.emplace(key, std::move(idx));

    if (options.detect_instances) {
      auto piit = w.pending_instance_.find(key);
      if (piit != w.pending_instance_.end() && piit->second.first) {
        // Match the per-prim cache's deterministic min-path rule so the
        // parallel merge and a serial ComputePrimIndex agree (F5).
        AssignPrototype(key, piit->second.second, /*prefer_min=*/true);
      }
    }
  }

  // Worker-side: warm this (private, SEEDED) Impl's sources_cache for the whole
  // subtree rooted at `root`, following the same child namespace the serial
  // compose walks (ComposeChildNames). The Impl was seeded with a snapshot of the
  // main's layer-stack table + ancestor sources_cache, so the root->frontier
  // ancestors are already cached (no re-resolution) and any new reference resolves
  // against the SAME anchors the serial build sees -> identical layer-stack
  // identifiers. Fills a cache only; missed paths still resolve identically in the
  // serial BuildStage.
  void WarmSubtree(const Path &root, uint32_t root_depth, std::string *warn,
                   std::string *err) {
    struct WItem {
      Path p;
      uint32_t depth;
    };
    std::vector<WItem> stack;
    stack.push_back({root, root_depth});
    while (!stack.empty()) {
      WItem it = stack.back();
      stack.pop_back();
      if (it.depth > options.max_namespace_depth) continue;
      const std::vector<Src> &srcs = SourcesForPath(it.p, warn, err);
      // Instancing-aware: the serial BuildStage composes only the prototype
      // member of an instance group and stops at every instance (is_instance ->
      // continue) -- it never descends an instance's subtree. Mirror that here so
      // warming does not resolve the (redundant) subtrees of every instance. On
      // heavily-instanced scenes the instances vastly outnumber the prototypes,
      // so these subtree resolutions dominate the worker + merge cost while the
      // serial build discards all but one per group. Stopping is byte-neutral:
      // warming only fills a cache; an unwarmed prototype subtree simply resolves
      // (identically) in the serial walk.
      if (options.detect_instances && IsInstanceableSources(srcs)) continue;
      const std::vector<std::string> children = ComposeChildNames(srcs);
      for (auto cit = children.rbegin(); cit != children.rend(); ++cit) {
        stack.push_back({it.p.append_child(*cit), it.depth + 1});
      }
    }
  }

  // Fold a seeded worker's warmed sources_cache into this (main) Impl. Layer-stack
  // indices below `seed_stack_count` are identical to main (the worker started
  // from main's snapshot) and pass through unremapped; only worker-NEW stacks
  // (>= seed_stack_count) are AdoptStack'd onto the shared table (dedup by
  // identifier). Sites are identifier strings and the variant pointer is into a
  // shared layer -> neither needs remap. Entries already present are skipped.
  void MergeSources(Impl &w, size_t seed_stack_count) {
    std::unordered_map<uint32_t, uint32_t> stack_remap;
    auto remap = [&](uint32_t ws) -> uint32_t {
      if (ws < seed_stack_count) return ws;  // identical to main (seeded)
      auto it = stack_remap.find(ws);
      if (it != stack_remap.end()) return it->second;
      uint32_t m = AdoptStack(w.layer_stacks[ws]);
      stack_remap.emplace(ws, m);
      return m;
    };
    for (auto &kv : w.sources_cache) {
      if (sources_cache.count(kv.first)) continue;
      std::vector<Src> srcs = kv.second;
      for (Src &s : srcs) s.stack_idx = remap(s.stack_idx);
      sources_cache.emplace(kv.first, std::move(srcs));
    }
  }

  // Parallel pre-warm of sources_cache (the LIVRPS arc resolution that dominates
  // build_stage). Serially BFS the namespace to discover a wide-enough frontier of
  // subtree roots (warming the shallow top levels into the main cache + table),
  // SNAPSHOT the main's layer-stack table + ancestor sources_cache, then have
  // worker threads (private Impls seeded from that snapshot, borrowing the
  // thread-safe shared registry) warm each frontier subtree's sources in parallel,
  // and merge the results back. The serial BuildStage then finds SourcesForPath a
  // cache hit. Byte-identical: composed values/instancing key off layer-stack
  // identifier strings, the seed gives workers the same identities the serial
  // build sees, property emission is name-ordered (parse-order-independent), and
  // any un-warmed path resolves serially.
  void ParallelWarmSources(const std::vector<std::string> &root_names, int nt,
                           std::string *warn, std::string *err) {
    using PWClock = std::chrono::steady_clock;
    const bool prof = options.enable_timing;
    auto t_start = prof ? PWClock::now() : PWClock::time_point{};
    const size_t want = static_cast<size_t>(nt) * 4;
    const uint32_t kMaxFrontierDepth = 8;
    std::vector<std::pair<Path, uint32_t>> level;  // (src_path, depth)
    for (const std::string &nm : root_names) {
      level.push_back({Path("/" + nm), 1});
    }
    for (uint32_t d = 0; d < kMaxFrontierDepth && level.size() < want; ++d) {
      std::vector<std::pair<Path, uint32_t>> next;
      bool grew = false;
      for (auto &pr : level) {
        if (pr.second > options.max_namespace_depth) {
          next.push_back(pr);
          continue;
        }
        const std::vector<Src> &srcs = SourcesForPath(pr.first, warn, err);
        // Instancing-aware (see WarmSubtree): an instanceable prim's subtree is
        // composed at most once (its prototype), so do not expand the frontier
        // into it -- treat it as a leaf root the worker will stop at immediately.
        if (options.detect_instances && IsInstanceableSources(srcs)) {
          next.push_back(pr);
          continue;
        }
        const std::vector<std::string> children = ComposeChildNames(srcs);
        if (children.empty()) {
          next.push_back(pr);  // leaf: nothing below to parallelize
        } else {
          grew = true;
          for (const std::string &cn : children) {
            next.push_back({pr.first.append_child(cn), pr.second + 1});
          }
        }
      }
      level = std::move(next);
      if (!grew) break;
    }
    if (level.size() < 2) return;  // not enough fan-out to parallelize
    auto t_discover = prof ? PWClock::now() : PWClock::time_point{};

    // Snapshot the main resolution context built by the serial discovery above.
    const size_t seed_stack_count = layer_stacks.size();

    const int W = std::min<int>(nt, static_cast<int>(level.size()));
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
      // Seed the worker with the main's resolution context so its reference
      // resolution produces identical layer-stack identities (the crux of
      // byte-identity), and ancestors are already cached (no re-resolution).
      wp->layer_stacks = layer_stacks;
      wp->stack_by_id = stack_by_id;
      wp->sources_cache = sources_cache;
      workers.push_back(std::move(wp));
    }

    // Workers operate on private (seeded) Impls + the thread-safe registry only;
    // the main thread holds api_mu_ throughout. Dynamic work-stealing -- frontier
    // subtrees are very uneven, so static chunks leave workers idle behind one
    // heavy subtree.
    std::atomic<size_t> next_node{0};
    std::vector<std::thread> ts;
    ts.reserve(static_cast<size_t>(W));
    for (int t = 0; t < W; ++t) {
      Impl *wk = workers[static_cast<size_t>(t)].get();
      std::string *pw = &wwarn[static_cast<size_t>(t)];
      std::string *pe = &werr[static_cast<size_t>(t)];
      ts.emplace_back([wk, &next_node, &level, pw, pe]() {
        for (;;) {
          size_t i = next_node.fetch_add(1);
          if (i >= level.size()) break;
          wk->WarmSubtree(level[i].first, level[i].second, pw, pe);
        }
      });
    }
    for (auto &t : ts) t.join();
    auto t_workers = prof ? PWClock::now() : PWClock::time_point{};

    for (int t = 0; t < W; ++t) {
      MergeSources(*workers[static_cast<size_t>(t)], seed_stack_count);
      if (warn) *warn += wwarn[static_cast<size_t>(t)];
      if (err) *err += werr[static_cast<size_t>(t)];
    }
    if (prof) {
      auto ms = [](PWClock::time_point a, PWClock::time_point b) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a)
                   .count() / 1e6;
      };
      auto t_merge = PWClock::now();
      std::fprintf(stderr,
                   "[next_warm] frontier=%zu W=%d discover=%.1fms workers=%.1fms "
                   "merge=%.1fms cache=%zu\n",
                   level.size(), W, ms(t_start, t_discover),
                   ms(t_discover, t_workers), ms(t_workers, t_merge),
                   sources_cache.size());
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
#if defined(TINYUSDZ_ENABLE_THREAD)
    // Use unique_lock (not lock_guard) so we can release api_mu_ during the
    // parallel build and reacquire it for the merge.
    std::unique_lock<PcpMutex> wlk(api_mu_);
    int nt = options.num_threads;
    if (nt < 0) nt = static_cast<int>(std::thread::hardware_concurrency());
    if (nt > 1) {
      // (a) Prefetch root-level reference/payload assets to warm the shared
      // registry (reduces redundant double-parses across workers).
      std::vector<std::pair<std::string, std::string>> assets;  // (asset, anchor)
      std::set<std::string> seen;
      // Anchor each layer's arcs to THAT layer's identifier (a sublayer's
      // relative reference resolves against the sublayer, not the stack root).
      const auto &ids = layer_stacks[0].layer_identifiers;
      for (size_t li = 0; li < layer_stacks[0].layers.size(); ++li) {
        const auto &lp = layer_stacks[0].layers[li];
        const std::string &anchor =
            li < ids.size() ? ids[li] : layer_stacks[0].identifier;
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

        // Release api_mu_ during the parallel build so that concurrent API
        // calls (ComputePrimIndex, SourcesForPath, etc.) are not blocked.
        // Workers operate on private Impls and only borrow the thread-safe
        // shared registry; they never touch shared cache state.
        wlk.unlock();

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

        // Reacquire api_mu_ for the serial merge into shared cache state.
        wlk.lock();

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
          Impl &wk = *workers[static_cast<size_t>(t)];
          for (const std::string &dp : wk.deferred_payload_prims) {
            deferred_payload_prims.insert(dp);
          }
          // Fold per-worker typed diagnostics into the main Impl (workers each
          // own a private issues_, so this join is race-free).
          for (auto &iss : wk.issues_) issues_.push_back(std::move(iss));
        }
        return true;
      }
    }
#else
    {
      NEXT_PCP_LOCK(api_mu_);
      for (const Path &p : paths) ComputePrimIndex_locked(p, warn, err);
    }
#endif
#if defined(TINYUSDZ_ENABLE_THREAD)
    // The thread-enabled serial fallback reaches here with `wlk` already
    // holding api_mu_. Do not take NEXT_PCP_LOCK again: the default mutex is
    // intentionally non-recursive.
    for (const Path &p : paths) ComputePrimIndex_locked(p, warn, err);
#endif
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
    composed_cache_.erase(key);
    composed_children_.erase(key);
    DropInstancing(key);
  }

  void Invalidate(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    Invalidate_locked(prim_path);
  }

  // Assumes api_mu_ is already held (called by LoadPayload / UnloadPayload).
  void Invalidate_locked(const Path &prim_path) {
    const std::string base = prim_path.str();
    std::set<std::string> to_drop;

    // index_cache is a std::map (sorted). Use lower_bound to scan only entries
    // at/under base instead of the entire map (Fix #12).
    for (auto it = index_cache.lower_bound(base);
         it != index_cache.end() && IsAtOrUnder(it->first, base); ++it) {
      to_drop.insert(it->first);
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
    // Phase 10: drop lazily-composed specs at/under the path.
    for (auto it = composed_cache_.begin(); it != composed_cache_.end();) {
      if (IsAtOrUnder(it->first, base)) it = composed_cache_.erase(it);
      else ++it;
    }
    for (auto it = composed_children_.begin(); it != composed_children_.end();) {
      if (IsAtOrUnder(it->first, base)) it = composed_children_.erase(it);
      else ++it;
    }
    // Diagnostics are a per-edit-cycle log: an invalidation begins a fresh
    // accumulation (matches GetCompositionIssues' contract; also bounds growth
    // across repeated recompositions). Issues are global, not path-keyed.
    issues_.clear();
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
    // spec_cache_ keys are (stack_idx, site), not prim paths; layer contents
    // changed so clear it entirely.
    spec_cache_.clear();
    // Sources and lazy composed specs can be populated without a PrimIndex, so
    // site_to_indices is not a complete dependency map for them. Be conservative
    // until lazy composition records its own layer-site dependencies.
    sources_cache.clear();
    composed_cache_.clear();
    composed_children_.clear();
    issues_.clear();  // fresh diagnostics for the recomposition.
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
    Invalidate_locked(prim_path);
    ComputePrimIndex_locked(prim_path, warn, err);  // recompose now.
    return true;
  }

  bool UnloadPayload(const Path &prim_path) {
    NEXT_PCP_LOCK(api_mu_);
    load_rules_.Unload(prim_path.str());
    Invalidate_locked(prim_path);
    // Recompose so the deferred-payload set (and HasDeferredPayload) reflects
    // the unload immediately -- Invalidate() drops the deferred entries under
    // the path, and ExpandArcs repopulates them on the next composition.
    std::string warn, err;
    ComputePrimIndex_locked(prim_path, &warn, &err);
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
    composed_cache_.clear();  // Phase 10
    composed_children_.clear();
    issues_.clear();  // fresh diagnostics for the recomposition.
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
  // Back-compat: a lone `flatten_instances = true` (mode left Native) means the
  // self-contained Holder flatten.
  if (cache.impl_->options.flatten_instances &&
      cache.impl_->options.instance_flatten_mode == InstanceFlattenMode::Native) {
    cache.impl_->options.instance_flatten_mode = InstanceFlattenMode::Holder;
  }
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

const PrimSpec *Cache::ComposePrim(const Path &prim_path, std::string *warn,
                                   std::string *err) {
#if defined(TINYUSDZ_ENABLE_THREAD) && defined(TINYUSDZ_NEXT_FINE_LOCKS)
  {
    NEXT_PCP_READ_LOCK(impl_->api_mu_);
    auto it = impl_->composed_cache_.find(prim_path.str());
    if (it != impl_->composed_cache_.end()) return it->second.get();
  }
#endif
  NEXT_PCP_WRITE_LOCK(impl_->api_mu_);
  return impl_->ComposePrim_locked(prim_path, warn, err);
}

std::vector<std::string> Cache::ComposedChildNames(const Path &prim_path,
                                                   std::string *warn,
                                                   std::string *err) {
  NEXT_PCP_WRITE_LOCK(impl_->api_mu_);
  impl_->ComposePrim_locked(prim_path, warn, err);  // ensure cached
  auto it = impl_->composed_children_.find(prim_path.str());
  return it != impl_->composed_children_.end() ? it->second
                                               : std::vector<std::string>();
}

bool Cache::IsInstance(const Path &p) const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  auto it = impl_->prototype_of.find(p.str());
  return it != impl_->prototype_of.end() && it->second != p.str();
}
Path Cache::GetPrototype(const Path &p) const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  auto it = impl_->prototype_of.find(p.str());
  return it != impl_->prototype_of.end() ? Path(it->second) : Path();
}
std::vector<Path> Cache::GetPrototypePaths() const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  std::vector<Path> out;
  out.reserve(impl_->instances_by_prototype.size());
  for (const auto &kv : impl_->instances_by_prototype) out.push_back(Path(kv.first));
  return out;
}
std::vector<Path> Cache::GetInstancesForPrototype(const Path &proto) const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  std::vector<Path> out;
  auto it = impl_->instances_by_prototype.find(proto.str());
  if (it != impl_->instances_by_prototype.end()) {
    for (const std::string &s : it->second) out.push_back(Path(s));
  }
  return out;
}
size_t Cache::PrototypeCount() const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
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
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  return impl_->load_rules_;
}
bool Cache::HasDeferredPayload(const Path &p) const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  return impl_->deferred_payload_prims.count(p.str()) != 0;
}
std::vector<Path> Cache::GetDeferredPayloadPaths() const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  std::vector<Path> out;
  out.reserve(impl_->deferred_payload_prims.size());
  for (const std::string &s : impl_->deferred_payload_prims) out.push_back(Path(s));
  return out;
}

std::vector<Cache::CompositionIssue> Cache::GetCompositionIssues() const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  return impl_->issues_;  // copy out under the lock (see header)
}
void Cache::ClearCompositionIssues() {
  NEXT_PCP_WRITE_LOCK(impl_->api_mu_);
  impl_->issues_.clear();
}

void Cache::Invalidate(const Path &prim_path) { impl_->Invalidate(prim_path); }
void Cache::InvalidateLayer(const std::string &id) { impl_->InvalidateLayer(id); }

bool Cache::HasComputedPrimIndex(const Path &prim_path) const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
  return impl_->index_cache.count(prim_path.str()) != 0;
}
size_t Cache::ComputedPrimIndexCount() const {
  NEXT_PCP_READ_LOCK(impl_->api_mu_);
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
  const bool timing = options.enable_timing;
  using Clock = std::chrono::steady_clock;
  auto ms = [](Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
  };
  const auto t0 = Clock::now();
  auto opened = Cache::Open(resolver, std::move(root_layer), root_identifier,
                            options);
  if (!opened) {
    if (err) *err += opened.error() + "\n";
    return false;
  }
  Cache cache = std::move(*opened);
  const auto t1 = Clock::now();
  bool ok = cache.BuildStage(out_stage, warn, err);
  const auto t2 = Clock::now();
  if (timing) {
    std::fprintf(stderr, "[next_compose] open=%.1fms build_stage=%.1fms\n",
                 ms(t1 - t0), ms(t2 - t1));
  }
  return ok;
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
