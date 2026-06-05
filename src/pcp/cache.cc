// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// cache.cc - implementation of the cached / lazy composition engine.
//
#include "pcp/cache.hh"

#include <algorithm>
#include <functional>

#if defined(TINYUSDZ_ENABLE_THREAD) && !defined(__EMSCRIPTEN__)
#include <thread>
#endif

#include "composition.hh"  // CompositeSublayers, LayerToStage
#include "core/prim-spec.hh"
#include "pcp/cache-impl.hh"
#include "security-policy.hh"
#include "stage.hh"

namespace tinyusdz {
namespace pcp {

namespace {

// Return true if `path` equals `prefix` or is a namespace descendant of it
// ("/A" is a prefix of "/A" and "/A/B" but not of "/AB").
bool PathPrefixMatch(const std::string &path, const std::string &prefix) {
  if (path == prefix) return true;
  if (path.size() <= prefix.size()) return false;
  if (path.compare(0, prefix.size(), prefix) != 0) return false;
  return path[prefix.size()] == '/';
}

int ResolveThreadCount(int requested) {
#if defined(TINYUSDZ_ENABLE_THREAD) && !defined(__EMSCRIPTEN__)
  if (requested < 0) {
    unsigned int hw = std::thread::hardware_concurrency();
    return (hw > 0) ? static_cast<int>(hw) : 4;
  }
  return (requested < 1) ? 1 : requested;
#else
  (void)requested;
  return 1;
#endif
}

}  // namespace

// ===========================================================================
// Cache (public, pimpl-forwarding)
// ===========================================================================

Cache::Cache() : _impl(new Impl()) {}
Cache::~Cache() = default;
Cache::Cache(Cache &&) noexcept = default;
Cache &Cache::operator=(Cache &&) noexcept = default;

nonstd::expected<Cache, std::string> Cache::Open(
    AssetResolutionResolver &resolver, const Layer &root_layer,
    const CacheOptions &options) {
  Cache c;
  c._impl->opts = options;
  c._impl->resolver = &resolver;

  auto composited = std::make_shared<Layer>();
  std::string warn, err;
  if (!CompositeSublayers(resolver, root_layer, composited.get(), &warn, &err)) {
    return nonstd::make_unexpected("pcp::Cache: CompositeSublayers failed: " +
                                   err);
  }
  c._impl->composited_root = std::move(composited);
  c._impl->root_layer = c._impl->composited_root.get();
  return c;
}

const cg::PrimIndex *Cache::ComputePrimIndex(const Path &prim_path,
                                             std::string *warn,
                                             std::string *err) {
  return _impl->ComputePrimIndex(prim_path, warn, err);
}

nonstd::expected<bool, std::string> Cache::PrewarmPrimIndices(
    const std::vector<Path> &paths, std::string *warn, std::string *err) {
  return _impl->PrewarmPrimIndices(paths, warn, err);
}

bool Cache::BuildStage(Stage *stage, std::string *warn, std::string *err) {
  return _impl->BuildStage(stage, warn, err);
}

nonstd::expected<bool, std::string> Cache::LoadPayload(const Path &prim_path,
                                                       std::string *warn,
                                                       std::string *err) {
  return _impl->LoadPayload(prim_path, warn, err);
}

nonstd::expected<bool, std::string> Cache::UnloadPayload(
    const Path &prim_path) {
  return _impl->UnloadPayload(prim_path);
}

bool Cache::HasDeferredPayload(const Path &prim_path) const {
  return _impl->HasDeferredPayload(prim_path);
}

std::vector<Path> Cache::GetDeferredPayloadPaths() const {
  return _impl->GetDeferredPayloadPaths();
}

void Cache::Invalidate(const Path &prim_path) { _impl->Invalidate(prim_path); }

void Cache::InvalidateLayer(const std::string &resolved_layer_id) {
  _impl->InvalidateLayer(resolved_layer_id);
}

bool Cache::HasComputedPrimIndex(const Path &prim_path) const {
  return _impl->index_cache.find(prim_path.prim_part()) !=
         _impl->index_cache.end();
}

std::vector<Path> Cache::GetComputedPrimPaths() const {
  std::vector<Path> result;
  result.reserve(_impl->index_cache.size());
  for (const auto &kv : _impl->index_cache) {
    result.emplace_back(kv.first, "");
  }
  return result;
}

size_t Cache::ComputedPrimIndexCount() const {
  return _impl->index_cache.size();
}

const LayerRegistry &Cache::layer_registry() const { return _impl->layers; }

const Layer *Cache::composited_root_layer() const { return _impl->root_layer; }

// ===========================================================================
// Cache::Impl
// ===========================================================================

const Layer *Cache::Impl::LoadLayerThunk(void *userdata,
                                         const std::string &asset_path,
                                         const std::string &cwp,
                                         std::string *warn, std::string *err) {
  Impl *self = static_cast<Impl *>(userdata);
  return self->layers.GetOrLoad(*self->resolver, asset_path, cwp, warn, err);
}

const PrimSpec *Cache::Impl::FindLocalPrimSpec(const Path &prim_path) const {
  if (!root_layer) return nullptr;
  const PrimSpec *ps = nullptr;
  std::string find_err;
  if (!root_layer->find_primspec_at(prim_path, &ps, &find_err)) {
    return nullptr;
  }
  return ps;
}

nonstd::expected<std::shared_ptr<CachedPrimIndex>, std::string>
Cache::Impl::BuildEntry(const Path &prim_path, std::string *warn,
                        std::string *err) {
  (void)warn;
  const PrimSpec *local = FindLocalPrimSpec(prim_path);
  if (!local) {
    return nonstd::make_unexpected("pcp::Cache: prim not found: " +
                                   prim_path.prim_part());
  }

  auto entry = std::make_shared<CachedPrimIndex>();
  entry->ctx._resolver = resolver;
  entry->ctx._options = opts.composition;
  entry->ctx._root_layer = root_layer;
  entry->ctx.load_layer_fn = &Impl::LoadLayerThunk;
  entry->ctx.load_layer_userdata = this;

  // Register the (shared) root layer in this entry's own layer-stack table.
  uint16_t root_ls =
      entry->ctx.AddLayerStack(root_layer, "<root>", LayerOffset());

  cg::PrimIndexBuilder builder(&entry->ctx, prim_path, *local, root_ls);
  auto result = builder.Build();
  if (!result) {
    if (err) *err = result.error();
    return nonstd::make_unexpected(result.error());
  }
  entry->index = std::move(*result);
  return entry;
}

const cg::PrimIndex *Cache::Impl::ComputePrimIndex(const Path &prim_path,
                                                   std::string *warn,
                                                   std::string *err) {
  const std::string key = prim_path.prim_part();
  auto it = index_cache.find(key);
  if (it != index_cache.end()) {
    return &it->second->index;
  }

  auto built = BuildEntry(prim_path, warn, err);
  if (!built) {
    if (err) *err = built.error();
    return nullptr;
  }

  std::shared_ptr<CachedPrimIndex> entry = built.value();
  index_cache[key] = entry;
  RegisterDependencies(key, *entry);
  return &entry->index;
}

nonstd::expected<bool, std::string> Cache::Impl::PrewarmPrimIndices(
    const std::vector<Path> &paths, std::string *warn, std::string *err) {
  (void)warn;  // only consumed by the parallel branch below
  (void)err;
  const int nthreads = ResolveThreadCount(opts.num_threads);

#if defined(TINYUSDZ_ENABLE_THREAD) && !defined(__EMSCRIPTEN__)
  if (nthreads > 1 && paths.size() >= opts.min_paths_for_parallel) {
    return BuildParallel(paths, static_cast<size_t>(nthreads), warn, err);
  }
#else
  (void)nthreads;
#endif

  // Single-threaded path (also the only path on wasm).
  for (const auto &p : paths) {
    std::string w, e;
    (void)ComputePrimIndex(p, &w, &e);  // best-effort; skip failures
  }
  return true;
}

// -- Dependency tracking --

void Cache::Impl::RegisterDependencies(const std::string &prim_path,
                                       const CachedPrimIndex &entry) {
  UnregisterDependencies(prim_path);  // idempotent (e.g. after payload reload)

  for (uint16_t i = 0; i < entry.index.GetNodeCount(); i++) {
    const cg::CompNode &n = entry.index.GetNode(i);
    if (n.is_culled()) continue;
    if (n.layer_stack_idx == cg::CompNode::kInvalidIndex) continue;
    if (n.layer_stack_idx >= entry.ctx._layer_stacks.size()) continue;

    Site s;
    s.layer_id = entry.ctx._layer_stacks[n.layer_stack_idx].identifier;
    s.prim_path = entry.ctx._path_table[n.site_path_idx];
    site_to_indices[s].insert(prim_path);
    index_to_sites[prim_path].push_back(s);
  }
}

void Cache::Impl::UnregisterDependencies(const std::string &prim_path) {
  auto sit = index_to_sites.find(prim_path);
  if (sit == index_to_sites.end()) return;
  for (const Site &s : sit->second) {
    auto m = site_to_indices.find(s);
    if (m != site_to_indices.end()) {
      m->second.erase(prim_path);
      if (m->second.empty()) site_to_indices.erase(m);
    }
  }
  index_to_sites.erase(sit);
}

void Cache::Impl::DropIndex(const std::string &prim_path) {
  UnregisterDependencies(prim_path);
  index_cache.erase(prim_path);
}

void Cache::Impl::Invalidate(const Path &prim_path) {
  const std::string changed = prim_path.prim_part();
  std::set<std::string> victims;

  // (a) Indices that read a site at/under the changed prim path.
  for (const auto &kv : site_to_indices) {
    if (PathPrefixMatch(kv.first.prim_path, changed)) {
      victims.insert(kv.second.begin(), kv.second.end());
    }
  }
  // (b) The changed prim's own index and its namespace descendants.
  for (const auto &kv : index_cache) {
    if (PathPrefixMatch(kv.first, changed)) victims.insert(kv.first);
  }

  for (const auto &v : victims) DropIndex(v);
}

void Cache::Impl::InvalidateLayer(const std::string &resolved_layer_id) {
  std::set<std::string> victims;
  for (const auto &kv : site_to_indices) {
    if (kv.first.layer_id == resolved_layer_id) {
      victims.insert(kv.second.begin(), kv.second.end());
    }
  }
  // Drop dependent indices BEFORE dropping the layer (borrowed pointers).
  for (const auto &v : victims) DropIndex(v);
  layers.Drop(resolved_layer_id);
}

// -- Payloads --

bool Cache::Impl::HasDeferredPayload(const Path &prim_path) const {
  auto it = index_cache.find(prim_path.prim_part());
  if (it == index_cache.end()) return false;
  const cg::PrimIndex &index = it->second->index;
  for (uint16_t i = 0; i < index.GetNodeCount(); i++) {
    if (index.GetNode(i).is_payload_deferred()) return true;
  }
  return false;
}

std::vector<Path> Cache::Impl::GetDeferredPayloadPaths() const {
  std::vector<Path> result;
  for (const auto &kv : index_cache) {
    const cg::PrimIndex &index = kv.second->index;
    for (uint16_t i = 0; i < index.GetNodeCount(); i++) {
      if (index.GetNode(i).is_payload_deferred()) {
        result.emplace_back(kv.first, "");
        break;
      }
    }
  }
  return result;
}

nonstd::expected<bool, std::string> Cache::Impl::LoadPayload(
    const Path &prim_path, std::string *warn, std::string *err) {
  const std::string key = prim_path.prim_part();
  auto it = index_cache.find(key);
  if (it == index_cache.end()) {
    return nonstd::make_unexpected("pcp::Cache: PrimIndex not computed for " +
                                   key);
  }
  CachedPrimIndex &entry = *it->second;

  // Find the deferred payload descriptor for this prim.
  cg::DeferredPayloadInfo *info = nullptr;
  for (auto &dp : entry.ctx._deferred_payloads) {
    if (dp.prim_path.prim_part() == key) {
      info = &dp;
      break;
    }
  }
  if (!info) {
    return nonstd::make_unexpected("pcp::Cache: no deferred payload for " + key);
  }

  cg::CompNode &node = cg::GetMutableNode(entry.index, info->node_idx);
  if (!node.is_payload_deferred()) {
    return nonstd::make_unexpected("pcp::Cache: payload not in deferred state");
  }

  std::string asset_path = info->payload.asset_path.GetAssetPath();
  uint16_t ls_idx = cg::CompNode::kInvalidIndex;

  if (asset_path.empty()) {
    // Internal payload: the target lives in the root layer stack.
    Path target = info->payload.prim_path;
    const PrimSpec *target_ps = nullptr;
    std::string find_err;
    if (!target.is_valid() ||
        !root_layer->find_primspec_at(target, &target_ps, &find_err) ||
        !target_ps) {
      return nonstd::make_unexpected(
          "pcp::Cache: internal payload target not found: " +
          target.prim_part());
    }
    // Reuse the owning node's layer stack (the root layer stack).
    ls_idx = (node.parent != cg::CompNode::kInvalidIndex)
                 ? entry.index.GetNode(node.parent).layer_stack_idx
                 : 0;
  } else {
    if (!security_policy::ValidateAndNormalizeAssetPath(asset_path,
                                                        &asset_path)) {
      return nonstd::make_unexpected("pcp::Cache: unsafe payload asset path");
    }
    const Layer *layer = layers.GetOrLoad(*resolver, asset_path,
                                          info->current_working_path, warn, err);
    if (!layer) {
      return nonstd::make_unexpected("pcp::Cache: failed to load payload: " +
                                     asset_path);
    }
    // Resolve the target prim (defaultPrim fallback).
    Path target = info->payload.prim_path;
    if (!target.is_valid() || target.prim_part().empty()) {
      std::string dp = layer->metas().defaultPrim.str();
      if (!dp.empty()) target = Path("/" + dp, "");
    }
    const PrimSpec *target_ps = nullptr;
    std::string find_err;
    if (!target.is_valid() ||
        !layer->find_primspec_at(target, &target_ps, &find_err) || !target_ps) {
      return nonstd::make_unexpected(
          "pcp::Cache: payload target prim not found in " + asset_path);
    }
    ls_idx = entry.ctx.AddLayerStack(layer, asset_path, info->payload.layerOffset);
  }

  node.layer_stack_idx = ls_idx;
  node.flags = (node.flags & ~cg::NodeFlags::PayloadDeferred) |
               cg::NodeFlags::PayloadLoaded | cg::NodeFlags::HasSpecs;

  cg::RecomputeStrengthOrder(entry.index);
  RegisterDependencies(key, entry);  // node now contributes a site
  return true;
}

nonstd::expected<bool, std::string> Cache::Impl::UnloadPayload(
    const Path &prim_path) {
  const std::string key = prim_path.prim_part();
  auto it = index_cache.find(key);
  if (it == index_cache.end()) {
    return nonstd::make_unexpected("pcp::Cache: PrimIndex not computed for " +
                                   key);
  }
  CachedPrimIndex &entry = *it->second;

  bool found = false;
  for (uint16_t i = 0; i < entry.index.GetNodeCount(); i++) {
    cg::CompNode &n = cg::GetMutableNode(entry.index, i);
    if (n.arc_type == cg::ArcType::Payload && n.is_payload_loaded()) {
      n.flags = (n.flags & ~cg::NodeFlags::PayloadLoaded &
                 ~cg::NodeFlags::HasSpecs) |
                cg::NodeFlags::PayloadDeferred;
      n.layer_stack_idx = cg::CompNode::kInvalidIndex;
      found = true;
    }
  }
  if (!found) {
    return nonstd::make_unexpected("pcp::Cache: no loaded payloads for " + key);
  }

  cg::RecomputeStrengthOrder(entry.index);
  RegisterDependencies(key, entry);
  return true;
}

// -- BuildStage --

void Cache::Impl::GatherAllPrimPaths(std::vector<Path> *out) const {
  if (!root_layer) return;

  struct Item {
    std::string path;
    const PrimSpec *ps;
  };
  std::vector<Item> stack;
  for (const auto &pair : root_layer->primspecs()) {
    stack.push_back({"/" + pair.first, &pair.second});
  }
  while (!stack.empty()) {
    Item item = std::move(stack.back());
    stack.pop_back();
    out->emplace_back(item.path, "");
    for (const auto &child : item.ps->children()) {
      stack.push_back({item.path + "/" + child.name(), &child});
    }
  }
}

bool Cache::Impl::BuildStage(Stage *stage, std::string *warn,
                             std::string *err) {
  if (!stage) {
    if (err) *err = "pcp::Cache::BuildStage: stage is nullptr";
    return false;
  }

  // 1) Compute every prim's PrimIndex (honoring num_threads).
  std::vector<Path> all_paths;
  GatherAllPrimPaths(&all_paths);
  (void)PrewarmPrimIndices(all_paths, warn, err);

  // 2) Compose PrimSpecs from the cached indices into a single Layer, then
  //    reuse the existing LayerToStage reconstruct pipeline.
  Layer composed_layer;
  if (root_layer) {
    composed_layer.metas() = root_layer->metas();
  }

  // Build a parent -> direct-children adjacency map in a single O(N) pass.
  // The previous recursive prefix scan re-walked the whole index_cache for
  // every prim -> O(N^2) in prim count. A path's parent is everything before
  // its last '/'; a single leading '/' marks a root prim. Children are kept in
  // index_cache iteration order so the composed result is byte-identical.
  HashMap<std::string, std::vector<std::string>> children_of;
  std::vector<std::string> roots;
  for (const auto &kv : index_cache) {
    const std::string &p = kv.first;
    const size_t slash = p.rfind('/');
    if (slash == 0) {
      roots.push_back(p);  // "/Foo" -> root prim
    } else if (slash != std::string::npos) {
      children_of[p.substr(0, slash)].push_back(p);
    }
  }

  std::function<void(const std::string &, PrimSpec &)> compose_children =
      [&](const std::string &parent_path, PrimSpec &parent_ps) {
        auto cit = children_of.find(parent_path);
        if (cit == children_of.end()) return;
        for (const std::string &cpath : cit->second) {
          auto eit = index_cache.find(cpath);
          if (eit == index_cache.end()) continue;  // every child is a key
          const CachedPrimIndex &ce = *eit->second;
          PrimSpec child_ps;
          if (cg::ComposePrimSpecFromIndex(ce.ctx._layer_stacks,
                                           ce.ctx._path_table, ce.index,
                                           &child_ps, warn, err)) {
            compose_children(cpath, child_ps);
            parent_ps.children().push_back(std::move(child_ps));
          }
        }
      };

  for (const std::string &path_str : roots) {
    auto eit = index_cache.find(path_str);
    if (eit == index_cache.end()) continue;
    const CachedPrimIndex &entry = *eit->second;
    PrimSpec composed_ps;
    if (!cg::ComposePrimSpecFromIndex(entry.ctx._layer_stacks,
                                      entry.ctx._path_table, entry.index,
                                      &composed_ps, warn, err)) {
      continue;
    }
    compose_children(path_str, composed_ps);
    composed_layer.add_primspec(composed_ps.name(), composed_ps);
  }

  return LayerToStage(std::move(composed_layer), stage, warn, err);
}

}  // namespace pcp
}  // namespace tinyusdz
