// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP Cache implementation (phase 1: sublayers + references)

#include "cache.hh"

#include "../composition/composition.hh"  // reuse ParseReference / CopyLocalOpinions

#include <map>
#include <set>
#include <unordered_map>

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

}  // namespace

struct Cache::Impl {
  AssetResolver *resolver = nullptr;
  CompositionOptions options;
  LayerRegistry registry;

  std::shared_ptr<Layer> root_layer;  // kept alive (also layer_stacks[0].layers[0])
  std::string root_identifier;

  std::vector<LayerStack> layer_stacks;             // table; [0] == root stack
  std::map<std::string, uint32_t> stack_by_id;      // dedup by resolved identifier

  std::unordered_map<std::string, std::unique_ptr<PrimIndex>> index_cache;
  std::unordered_map<Site, std::set<std::string>, SiteHash> site_to_indices;
  std::unordered_map<std::string, std::vector<Site>> index_to_sites;

  // --- layer stacks -------------------------------------------------------

  static std::string DirOf(const std::string &path) {
    return AssetResolver::GetDirectory(path);
  }

  // Build [layer, sublayers...] (strong first) for `layer`, loading sublayers
  // through the parse-once registry. `identifier` anchors relative paths.
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

  bool AnyLayerAuthors(const LayerStack &st, const std::string &site) const {
    return FindSpec(st, site, nullptr) != nullptr;
  }

  // --- reference expansion (pre-order DFS == strength order) --------------

  void ExpandRefs(const Src &src, std::set<std::string> cycle,
                  std::vector<Src> *out, std::string *warn, std::string *err) {
    out->push_back(src);

    const Layer *src_layer = nullptr;
    const PrimSpec *spec = FindSpec(layer_stacks[src.stack_idx], src.site, &src_layer);
    if (!spec) return;

    for (const std::string &ref_str : spec->meta().references) {
      CompositionArc arc = Compositor::ParseReference(ref_str);

      uint32_t ref_stack_idx;
      std::string ref_site;
      if (arc.is_internal || arc.asset_path.empty()) {
        // Internal reference: same layer stack, target prim path within it.
        ref_stack_idx = src.stack_idx;
        ref_site = arc.prim_path.empty() ? src.site : arc.prim_path;
      } else {
        const std::string anchor = DirOf(layer_stacks[src.stack_idx].identifier);
        std::shared_ptr<Layer> ref_layer =
            registry.GetOrLoad(*resolver, arc.asset_path, anchor, warn, err);
        if (!ref_layer) {
          if (options.error_when_asset_not_found && err) {
            *err += "Reference asset not found: " + arc.asset_path + "\n";
          }
          continue;
        }
        const std::string ref_id =
            resolver->ResolvePath(arc.asset_path, anchor);
        ref_stack_idx = InternLayerStack(ref_layer, ref_id, warn, err);
        if (!arc.prim_path.empty()) {
          ref_site = arc.prim_path;
        } else if (!ref_layer->meta().defaultPrim.empty()) {
          ref_site = "/" + ref_layer->meta().defaultPrim;
        } else {
          const auto &roots = ref_layer->root_indices();
          ref_site = roots.empty()
                         ? "/"
                         : "/" + ref_layer->prim(roots[0])->name();
        }
      }

      const std::string key =
          layer_stacks[ref_stack_idx].identifier + ":" + ref_site;
      if (cycle.count(key)) {
        if (err) *err += "Composition cycle detected at reference: " + key + "\n";
        continue;
      }
      if (cycle.size() >= options.max_depth) {
        if (err) *err += "Composition max depth exceeded\n";
        continue;
      }

      // Map referenced namespace -> this prim's namespace, composed to root.
      NamespaceMapping local{ref_site, src.site};
      Src ref_src;
      ref_src.stack_idx = ref_stack_idx;
      ref_src.site = ref_site;
      ref_src.map = NamespaceMapping::Compose(src.map, local);
      ref_src.offset = src.offset;  // phase 1: layer offset not yet parsed

      std::set<std::string> child_cycle = cycle;
      child_cycle.insert(key);
      ExpandRefs(ref_src, std::move(child_cycle), out, warn, err);
    }
  }

  std::vector<Src> ExpandSources(const Src &root, std::string *warn,
                                 std::string *err) {
    std::vector<Src> out;
    std::set<std::string> cycle;
    cycle.insert(layer_stacks[root.stack_idx].identifier + ":" + root.site);
    ExpandRefs(root, std::move(cycle), &out, warn, err);
    return out;
  }

  // Expand a whole list of base sources (each gets its own cycle set seeded by
  // its site). Preserves order (strength).
  std::vector<Src> ExpandList(const std::vector<Src> &base, std::string *warn,
                              std::string *err) {
    std::vector<Src> out;
    for (const Src &s : base) {
      std::set<std::string> cycle;
      cycle.insert(layer_stacks[s.stack_idx].identifier + ":" + s.site);
      ExpandRefs(s, std::move(cycle), &out, warn, err);
    }
    return out;
  }

  // --- value resolution ---------------------------------------------------

  // Merge all sources (strong->weak) into a fresh PrimSpec; return child names.
  std::vector<std::string> ComposeInto(const std::vector<Src> &srcs,
                                        PrimSpec *out) {
    std::vector<std::string> child_names;
    std::set<std::string> seen_child;
    bool specifier_set = false;

    for (const Src &s : srcs) {
      const Layer *layer = nullptr;
      const PrimSpec *spec = FindSpec(layer_stacks[s.stack_idx], s.site, &layer);
      if (!spec) continue;

      if (!specifier_set) {
        out->set_specifier(spec->specifier());
        specifier_set = true;
      }

      // type / properties / time-samples / metadata (strongest-wins, fill-absent)
      Compositor::CopyLocalOpinions(*out, *spec);

      // relationships (fill-absent, remap targets to root namespace)
      for (const std::string &rn : spec->relationship_names()) {
        if (out->relationship(rn)) continue;
        const std::vector<Path> *tgts = spec->relationship(rn);
        if (!tgts) continue;
        for (const Path &t : *tgts) {
          out->add_relationship(rn, Path(s.map.Apply(t.str())));
        }
      }

      // children (union across all sources, in strength order)
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
    const std::string key = prim_path.str();
    auto it = index_cache.find(key);
    if (it != index_cache.end()) return it->second.get();

    // Existence: a local spec must exist in the root layer stack.
    if (!AnyLayerAuthors(layer_stacks[0], key)) {
      return nullptr;
    }

    Src root;
    root.stack_idx = 0;
    root.site = key;
    std::vector<Src> srcs = ExpandSources(root, warn, err);

    auto index = std::unique_ptr<PrimIndex>(new PrimIndex());
    index->SetPath(prim_path);
    index->SetLayerStacks(&layer_stacks);

    std::vector<uint16_t> order;
    std::vector<Site> sites;
    for (size_t i = 0; i < srcs.size(); ++i) {
      const Src &s = srcs[i];
      CompNode n;
      n.arc_type = (i == 0) ? ArcType::Root : ArcType::Reference;
      n.parent = (i == 0) ? 0xFFFF : 0;
      n.layer_stack_idx = s.stack_idx;
      n.site_prim_path = s.site;
      n.map_to_root = s.map;
      n.offset = s.offset;
      if (AnyLayerAuthors(layer_stacks[s.stack_idx], s.site)) {
        n.flags |= NodeFlags::HasSpecs;
        sites.push_back(Site{layer_stacks[s.stack_idx].identifier, s.site});
      }
      uint16_t ni = index->AddNode(std::move(n));
      if (i != 0) index->MutableNode(0).children.push_back(ni);
      order.push_back(static_cast<uint16_t>(i));
    }
    index->SetStrengthOrder(std::move(order));

    // Register reverse dependencies.
    for (const Site &site : sites) {
      site_to_indices[site].insert(key);
    }
    index_to_sites[key] = std::move(sites);

    const PrimIndex *ret = index.get();
    index_cache.emplace(key, std::move(index));
    return ret;
  }

  // --- BuildStage ---------------------------------------------------------

  // Compose a prim from a list of base sources, then recurse into its children.
  // Children are derived from the EXPANDED sources (so referenced subtrees and
  // their namespace mapping carry down), each re-rooted at the child name.
  void BuildPrimFromBase(const std::vector<Src> &base, const std::string &name,
                         const Path &out_path, Layer *out, uint32_t parent_idx,
                         bool is_root, std::string *warn, std::string *err) {
    std::vector<Src> expanded = ExpandList(base, warn, err);

    PrimSpec spec(name);
    spec.set_path(out_path);
    std::vector<std::string> children = ComposeInto(expanded, &spec);

    uint32_t idx = out->add_prim(std::move(spec));
    if (is_root) {
      out->add_root(idx);
    } else {
      out->set_parent(idx, parent_idx);
    }

    for (const std::string &cn : children) {
      // Child sources = every expanded source re-rooted at the child. Sources
      // that don't author the child contribute nothing (FindSpec misses).
      std::vector<Src> child_base;
      child_base.reserve(expanded.size());
      for (const Src &s : expanded) {
        Src c;
        c.stack_idx = s.stack_idx;
        c.site = s.site + "/" + cn;
        c.map = s.map;
        c.offset = s.offset;
        child_base.push_back(std::move(c));
      }
      BuildPrimFromBase(child_base, cn, out_path.append_child(cn), out, idx,
                        /*is_root=*/false, warn, err);
    }
  }

  bool BuildStage(Stage *stage, std::string *warn, std::string *err) {
    auto out = std::unique_ptr<Layer>(new Layer());

    const Layer *root = layer_stacks[0].layers[0].get();
    for (uint32_t ri : root->root_indices()) {
      const std::string nm = root->prim(ri)->name();
      Src s;
      s.stack_idx = 0;
      s.site = "/" + nm;
      std::vector<Src> base{s};
      BuildPrimFromBase(base, nm, Path("/" + nm), out.get(), 0,
                        /*is_root=*/true, warn, err);
    }

    out->finalize();
    // Carry root layer metadata onto the composed result.
    out->meta() = root->meta();
    stage->SetRootLayer(std::move(*out));
    return true;
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
  }

  void Invalidate(const Path &prim_path) {
    const std::string base = prim_path.str();
    std::set<std::string> to_drop;

    // The prim itself + namespace descendants.
    for (const auto &kv : index_cache) {
      const std::string &k = kv.first;
      if (k == base || (k.size() > base.size() &&
                        k.compare(0, base.size(), base) == 0 &&
                        k[base.size()] == '/')) {
        to_drop.insert(k);
      }
    }
    // Indices that read a site at/under prim_path.
    for (const auto &kv : site_to_indices) {
      const std::string &sp = kv.first.prim_path;
      if (sp == base || (sp.size() > base.size() &&
                         sp.compare(0, base.size(), base) == 0 &&
                         sp[base.size()] == '/')) {
        for (const std::string &dep : kv.second) to_drop.insert(dep);
      }
    }
    for (const std::string &k : to_drop) DropIndex(k);
  }

  void InvalidateLayer(const std::string &layer_id) {
    std::set<std::string> to_drop;
    for (const auto &kv : site_to_indices) {
      if (kv.first.layer_id == layer_id) {
        for (const std::string &dep : kv.second) to_drop.insert(dep);
      }
    }
    for (const std::string &k : to_drop) DropIndex(k);
    registry.Drop(layer_id);
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
  return cache;
}

const PrimIndex *Cache::ComputePrimIndex(const Path &prim_path,
                                         std::string *warn, std::string *err) {
  return impl_->ComputePrimIndex(prim_path, warn, err);
}

bool Cache::BuildStage(Stage *stage, std::string *warn, std::string *err) {
  if (!stage) return false;
  return impl_->BuildStage(stage, warn, err);
}

void Cache::Invalidate(const Path &prim_path) { impl_->Invalidate(prim_path); }
void Cache::InvalidateLayer(const std::string &id) { impl_->InvalidateLayer(id); }

bool Cache::HasComputedPrimIndex(const Path &prim_path) const {
  return impl_->index_cache.count(prim_path.str()) != 0;
}
size_t Cache::ComputedPrimIndexCount() const { return impl_->index_cache.size(); }
const LayerRegistry &Cache::layer_registry() const { return impl_->registry; }

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
