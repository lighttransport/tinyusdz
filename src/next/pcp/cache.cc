// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache implementation
// Phase 1: sublayers + references.  Phase 2: ancestral opinions + deferred payloads.

#include "cache.hh"

#include "cache-lock.hh"
#include "cache-utils.hh"
#include "../composition/composition.hh"  // reuse ParseReference / ParsePayload / CopyLocalOpinions
#include "../strfmt.hh"                    // IntToStr / UIntToStr
#include "../../logger.hh"                 // tinyusdz::logging TUSDZ_LOG_*

#include <algorithm>
#include <cmath>
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

namespace {

// The layer stacks an arc chain crossed to reach a source, as (stack, map_idx)
// pairs from the root stack down to the source's own stack -- one entry per
// composition node. Implied class arcs are re-expressed in each of these
// namespaces, so the chain must survive the re-rooting a child prim does; an
// INTERMEDIATE stack's opinion on the class path is otherwise lost (bug69932).
// Shared, so propagating it to a whole subtree costs one refcount.
using ArcChain = std::vector<std::pair<uint32_t, uint32_t>>;

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
  // Composed expression-variable context visible at this source. Shared so
  // propagating a source to thousands of descendants stays cheap.
  std::shared_ptr<const Value> expression_variables;
  // The arc chain this source was reached through (null == the root stack).
  std::shared_ptr<const ArcChain> arc_chain;
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
  uint32_t map_idx;
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
  std::set<std::string> sources_in_progress;
  const std::vector<Src> empty_sources_;

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

  // Relocates are a per-LAYER-STACK property (LayerStack::relocates, built in
  // InternLayerStack) and are applied while deriving a prim's composition
  // sources (DeriveChildSources), so a relocate authored in a referenced or
  // payloaded layer stack renames prims inside THAT stack's namespace and the
  // rename maps through the arc into root space.



  // --- typed composition diagnostics (Phase 7 E4) -------------------------
  // Accumulated typed issues. Recorded regardless of whether a caller passed an
  // `err` pointer; when present, the rendered message is mirrored into *err so
  // the existing string-based API is unchanged.
  std::vector<CompositionIssue> issues_;

  #include "cache-layer-stack.inc"

  #include "cache-specs-instances.inc"

  #include "cache-arc-listops.inc"

  #include "cache-arc-expansion.inc"

  #include "cache-compose.inc"

  #include "cache-stage-build.inc"

  #include "cache-stage-fill.inc"

#if defined(TINYUSDZ_ENABLE_THREAD)
  #include "cache-parallel-merge.inc"
#endif

  #include "cache-parallel-warm.inc"

  #include "cache-invalidation.inc"


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
  if (cache.impl_->options.strict_aousd_conformance) {
    cache.impl_->options.usda_parse_options.strict_aousd_conformance = true;
  }
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
    TUSDZ_LOG_I("[next_compose] open=" + FormatMilliseconds(ms(t1 - t0)) +
                "ms build_stage=" + FormatMilliseconds(ms(t2 - t1)) + "ms");
  }
  return ok;
}

bool ComposeStageFromFile(const std::string &filename, AssetResolver &resolver,
                          Stage *out_stage, const CompositionOptions &options,
                          std::string *warn, std::string *err) {
  LayerLoadOptions lopts;
  lopts.max_memory = options.max_layer_memory;
  lopts.strict_aousd_conformance = options.strict_aousd_conformance;
  lopts.usda_parse_options = options.usda_parse_options;
  if (options.strict_aousd_conformance) {
    lopts.usda_parse_options.strict_aousd_conformance = true;
  }
  std::shared_ptr<Layer> root = LoadLayerFromFile(
      filename, warn, err, lopts);
  if (!root) return false;
  return ComposeStageFromLayer(std::move(root), resolver, out_stage, filename,
                               options, warn, err);
}

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
