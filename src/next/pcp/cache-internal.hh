// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache internal Impl declaration.
//
// This header declares Cache::Impl (data members + method declarations) so the
// method *definitions* can be split across several .cc translation units
// (cache.cc + cache-methods-*.cc) and compiled in parallel. It is a PRIVATE
// header: only cache.cc and cache-methods-*.cc include it. The former inline
// member definitions now live out-of-line in those .cc files.

#pragma once

#include "cache.hh"

#include "cache-lock.hh"
#include "cache-utils.hh"
#include "../composition/composition.hh"  // ParseReference / ParsePayload / CopyLocalOpinions
#include "../strfmt.hh"                    // IntToStr / UIntToStr
#include "../../logger.hh"                 // tinyusdz::logging TUSDZ_LOG_*

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
#include <thread>
#endif

namespace tinyusdz {
namespace next {
namespace pcp {

// ---------------------------------------------------------------------------
// File-scope composition helpers (were Cache::Impl-local / an anon namespace in
// cache.cc; promoted here so every cache-methods-*.cc TU shares one definition).
// ---------------------------------------------------------------------------

struct SpecRef {
  const PrimSpec *spec = nullptr;
  const Layer *layer = nullptr;
  std::string layer_id;
};

// A single composition source (one node's worth of provenance).
struct Src {
  uint32_t stack_idx = 0;
  // Index into the owning Impl's nm_pool_ (0 == identity). The namespace mapping
  // is created once per arc and SHARED by every Src in the subtree below it, so
  // storing an index (and copying it on child-build) avoids re-copying the two
  // path strings of a non-identity NamespaceMapping for all 140K+ descendants --
  // the dominant path-string churn in compose. Fills stack_idx's padding hole.
  uint32_t map_idx = 0;
  std::string site;            // prim path within the layer stack
  LayerOffset offset;
  ArcType arc_kind = ArcType::Root;  // arc this source arrived through
  // For Variant sources: the selected variant's inline opinions (lives inside a
  // shared layer's PrimSpecMeta, so the pointer is stable). null otherwise.
  const VariantData *variant = nullptr;
  // Memoized Specs(stack_idx, site) result for this exact (stack_idx, site), so
  // the 2-3 read-side lookups per composed prim (ComposeChildNames,
  // IsInstanceable, instance-key, ComposeOpinions) skip re-hashing `site`. Points
  // into spec_cache_by_stack_ (stable). Only set via SpecsFor() on the MAIN Impl's
  // stable sources_cache Srcs; MergeSources resets it (a worker's pointer would
  // dangle in main). Left null on child-build (fresh Src, different site).
  mutable const std::vector<SpecRef> *specs_ = nullptr;
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

  // Pool of namespace mappings shared by Src.map_idx. Index 0 is identity, so a
  // default Src (and the bulk of subtree children) needs no pool entry. New
  // mappings are appended only in the SERIAL arc expansion (ProcessArc / variant
  // ExpandArcs); the parallel opinion fill only READS the pool (Apply), and a
  // std::deque never relocates existing entries -> lock-free concurrent reads.
  std::deque<NamespaceMapping> nm_pool_{NamespaceMapping{}};
  const NamespaceMapping &Mapping(uint32_t idx) const { return nm_pool_[idx]; }
  uint32_t InternMapping(NamespaceMapping m) {
    if (m.is_identity()) return 0;
    uint32_t idx = static_cast<uint32_t>(nm_pool_.size());
    nm_pool_.push_back(std::move(m));
    return idx;
  }

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
                const std::string &message, std::string *err);
  // --- layer stacks -------------------------------------------------------

  static constexpr uint32_t kInvalidStack =
      (std::numeric_limits<uint32_t>::max)();

  LayerLoadOptions MakeLayerLoadOptions() const;

  uint32_t InternLayerStack(std::shared_ptr<Layer> layer,
                            const std::string &identifier, std::string *warn,
                            std::string *err);

  bool AppendLayerAndSublayers(LayerStack &st, std::shared_ptr<Layer> layer,
                               const std::string &identifier,
                               std::set<std::string> *visiting,
                               uint32_t depth, std::string *warn,
                               std::string *err);

  mutable std::deque<std::unordered_map<std::string, std::vector<SpecRef>>>
      spec_cache_by_stack_;

  const std::vector<SpecRef> &Specs(uint32_t stack_idx,
                                    const std::string &site) const;

  // Specs() with a per-Src memo: the 2-3 read-side spec lookups per composed prim
  // (ComposeChildNames, IsInstanceable, instance-key, ComposeOpinions) all key on
  // the SAME (stack_idx, site) -> cache the resolved vector pointer in the Src so
  // only the first pays the site-string hash. Safe only on stable sources_cache
  // Srcs (see Src::specs_); MergeSources clears any worker-set pointer.
  const std::vector<SpecRef> &SpecsFor(const Src &s) const;
  std::vector<SpecRef> FindSpecs(const LayerStack &st,
                                 const std::string &site) const;

  const PrimSpec *FindSpec(const LayerStack &st, const std::string &site,
                           const Layer **out_layer) const;

  bool AnyAuthors(const std::vector<Src> &srcs) const;

  // --- payload policy -----------------------------------------------------

  bool ShouldLoadPayload(const std::string &root_prim_path,
                         const std::string &asset);

  // --- variant selection --------------------------------------------------

  // Record `spec`'s variant selections into the accumulator (strong-first wins,
  // so a selection authored on a stronger source overrides a weaker one).
  void RecordSelections(const PrimSpec &spec,
                        std::map<std::string, std::string> *sels) const;

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
      const std::map<std::string, std::string> &sels) const;

  // --- instancing ---------------------------------------------------------

  // Instanceable iff some contributing spec set instanceable=true AND the prim
  // actually has a composition arc (more than just its own root opinions).
  bool IsInstanceableSources(const std::vector<Src> &srcs) const;

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
  std::string ComputeInstanceKeyImpl(const std::vector<Src> &srcs) const;

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
                       bool prefer_min = false);

  void RegisterInstance(const std::string &prim_path,
                        const std::vector<Src> &srcs, bool prefer_min = false);

  void DropInstancing(const std::string &prim_path);

  enum class ArcSel { References, Payloads, Inherits, Specializes };

  static const std::vector<std::string> &SelectInlineArc(const PrimSpecMeta &m,
                                                         ArcSel f);

  static const ArcEdit *SelectArcEdit(const PrimSpecMeta &m, ArcSel f);

  // Compose one arc field across a site's specs (input is strong-first) per the
  // AOUSD list-op rules, applying weakest->strongest. Returns (arc-string,
  // authoring-layer-id) pairs so relative reference/payload asset paths can be
  // anchored to the layer that authored them.
  std::vector<std::pair<std::string, std::string>> MergeArcField(
      const std::vector<SpecRef> &specs, ArcSel f) const;

  static std::string RealAnchorOf(std::string id);

  void ProcessArc(const Src &src, const CompositionArc &arc, ArcType kind,
                  const ExpansionFrame *frame, std::vector<Src> *out,
                  std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err, const std::string &authoring_layer_id = "");

  // Pre-order DFS == strength order. Per source: local (pushed) > inherits >
  // references > payloads; specializes routed to `spec_out` (globally weakest).
  void ExpandArcs(const Src &src, const ExpansionFrame *frame,
                  std::vector<Src> *out, std::vector<Src> *spec_out,
                  std::map<std::string, std::string> *sels,
                  const std::vector<uint32_t> &chain, std::string *warn,
                  std::string *err);

  std::vector<Src> ExpandList(const std::vector<Src> &base, std::string *warn,
                              std::string *err);

  // --- ancestral source resolution (cached) -------------------------------

  // The expanded composition sources for `path`. A root-level prim seeds from
  // the root layer stack; a descendant re-roots each of its parent's expanded
  // sources at the child name (so arcs authored on an ancestor reach it), then
  // expands that child's own arcs. Cached per path.
  const std::vector<Src> &SourcesForPath(const Path &path, std::string *warn,
                                         std::string *err);

  std::vector<std::string> ComposeInto(const std::vector<Src> &srcs,
                                        PrimSpec *out);
  // ComposeInto's Pass 1 (opinion merge), factored out so the parallel compose
  // can fill PrimSpec opinion slots independently after a serial structure pass
  // has fixed every prim's index/parent/child-order. Writes only into `out`
  // (and interns property names into the shared, locked PropNameTable), so
  // distinct slots fill concurrently without interfering.
  void ComposeOpinions(const std::vector<Src> &srcs, PrimSpec *out);

  // Composed child-name ORDER for a prim's sources (weak->strong): iterate sources
  // and their layer-stack specs in reverse (weakest first); within a single layer
  // the primChildren are in authored (forward) order. Append unseen names. This is
  // ComposeInto's Pass 2, factored out so the parallel source-warming pre-pass
  // discovers exactly the same child namespace the serial compose walks.
  std::vector<std::string> ComposeChildNames(const std::vector<Src> &srcs);

  // --- lazy per-prim composition (Phase 10) -------------------------------

  // Compose just `prim_path` on first access (cached), reusing the exact
  // source-resolution + opinion-merge of BuildStageRec's per-prim step, but
  // without walking/emitting the whole namespace. Returns nullptr if the prim
  // authors no opinions. Assumes api_mu_ is held.
  const PrimSpec *ComposePrim_locked(const Path &prim_path, std::string *warn,
                                     std::string *err);

  // --- ComputePrimIndex ---------------------------------------------------

  // Public entry: takes the lock once, then runs the lock-free worker.
  const PrimIndex *ComputePrimIndex(const Path &prim_path, std::string *warn,
                                    std::string *err);

  // Assumes api_mu_ is already held. Internal callers (LoadPayload /
  // UnloadPayload / the serial PrewarmPrimIndices path) use this directly so the
  // lock is never re-entered (F6: the mutex is non-recursive).
  const PrimIndex *ComputePrimIndex_locked(const Path &prim_path,
                                           std::string *warn, std::string *err);

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
                  std::string *warn, std::string *err);

  std::vector<std::pair<uint32_t, std::string>> fill_;

  // OPINION-FILL PASS: compose each slot's opinions from its (already cached)
  // sources. Slots are disjoint PrimSpec objects, sources_cache is only read, and
  // add_prim is done -- so distinct slots fill concurrently. Byte-identical to
  // the serial ComposeInto: the same ComposeOpinions runs on the same (slot,
  // srcs) pairs; only the order across independent slots changes.
  void FillOpinions(Layer *out);

#if defined(TINYUSDZ_ENABLE_THREAD)
  void MergeWorkerIndex(Impl &w, const std::string &key);

  void MergeSources(Impl &w, size_t seed_stack_count, size_t seed_pool_count);
#endif

  bool BuildStage(Stage *stage, std::string *warn, std::string *err);

  // Convert native instancing to a self-contained flatten: each prototype group
  // keeps its prototype member as the shared content holder (made
  // non-instanceable), and every other member is emptied + internally references
  // that holder. See CompositionOptions::flatten_instances.
  void FlattenInstances(Layer *out);

  // usdcat-style flatten: move each prototype group's shared subtree to a root
  // `over "/Flattened_Prototype_N"` and rewrite every member (holder included)
  // to `instanceable = true` + `references = </Flattened_Prototype_N>`. Numbering
  // per `numbering` (Deterministic = sort by prototype path; UsdcatCompatible =
  // pxr's two-stage scheme). See CompositionOptions::instance_flatten_mode.
  void FlattenInstancesExtracted(Layer *out, PrototypeNumbering numbering);
#if defined(TINYUSDZ_ENABLE_THREAD)

  // --- parallel-build merge helpers (main thread only) --------------------

  // Adopt a worker's prebuilt LayerStack into the main table (dedup by resolved
  // identifier). Returns the main-table index. The LayerStack is copied, which
  // just bumps the shared_ptr<Layer> refcounts -- the layers stay shared.
  uint32_t AdoptStack(const LayerStack &ws);

  // Worker-side: warm this (private, SEEDED) Impl's sources_cache for the whole
  // subtree rooted at `root`, following the same child namespace the serial
  // compose walks (ComposeChildNames). The Impl was seeded with a snapshot of the
  // main's layer-stack table + ancestor sources_cache, so the root->frontier
  // ancestors are already cached (no re-resolution) and any new reference resolves
  // against the SAME anchors the serial build sees -> identical layer-stack
  // identifiers. Fills a cache only; missed paths still resolve identically in the
  // serial BuildStage.
  void WarmSubtree(const Path &root, uint32_t root_depth, std::string *warn,
                   std::string *err);

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
                           std::string *warn, std::string *err);
#endif  // TINYUSDZ_ENABLE_THREAD

  // Batch build. With threads enabled and num_threads != 1, this first prefetches
  // first-level reference/payload layers into the (thread-safe) registry in
  // parallel, then builds the per-prim indices CONCURRENTLY: each worker runs the
  // ordinary build on its own private Impl (borrowing the shared, parse-once
  // registry), and a deterministic input-order merge folds the results into the
  // cache. Composed values are identical to a serial run. Non-threaded builds (or
  // num_threads <= 1) use the plain serial loop.
  bool PrewarmPrimIndices(const std::vector<Path> &paths, std::string *warn,
                           std::string *err);

  std::string ComputeInstanceKey(const Path &path, std::string *warn,
                                 std::string *err);
  // --- invalidation -------------------------------------------------------

  void DropIndex(const std::string &key);

  void Invalidate(const Path &prim_path);

  // Assumes api_mu_ is already held (called by LoadPayload / UnloadPayload).
  void Invalidate_locked(const Path &prim_path);

  void InvalidateLayer(const std::string &layer_id);

  // --- payloads -----------------------------------------------------------

  bool LoadPayload(const Path &prim_path, bool with_descendants,
                   std::string *warn, std::string *err);

  bool UnloadPayload(const Path &prim_path);

  void SetLoadRules(const LoadRules &rules);

};

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
