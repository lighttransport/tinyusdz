// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PCP PrimIndex
//
// Per-prim composition graph (DAG of CompNodes in strength order). Mirrors the
// design of composition_graph::PrimIndex but on next types. Standalone, C++14.
//
// NOTE (memory): Phase 1 stores the site path as a std::string and children as
// a small vector per node for clarity. Interning the path table and packing
// CompNode into a fixed <=40B struct with uint16 indices is the next
// optimization (tracked in the plan); the public shape here is stable.

#pragma once

#include "arc-types.hh"
#include "namespace-mapping.hh"
#include "../layer/layer.hh"
#include "../prim/path.hh"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {
namespace pcp {

/// An ordered stack of layers (strong -> weak): a root/referenced layer plus
/// its composed sublayers. Layers are shared_ptr so a parsed file exists once
/// and is refcount-shared across every node/index that uses it (parse-once).
struct LayerStack {
  std::vector<std::shared_ptr<Layer>> layers;  // strongest first
  std::vector<std::string> layer_identifiers;  // resolved id for each layer
  std::string identifier;                       // resolved asset path (registry key)
  LayerOffset offset;                           // cumulative time offset to root
};

/// One composition source for a prim. The site prim path is interned into a
/// shared path table (owned by the Cache; borrowed by PrimIndex) and referenced
/// by a small index, so per-node storage stays compact and paths are deduped
/// across every cached index.
struct CompNode {
  ArcType arc_type = ArcType::Root;
  NodeFlags flags = NodeFlags::None;
  uint16_t parent = 0xFFFF;          // 0xFFFF == none (root)
  uint32_t layer_stack_idx = 0;      // index into Cache's layer-stack table
  uint32_t site_path_idx = 0;        // index into Cache's interned path table
  LayerOffset offset;                // cumulative time offset for this node
  NamespaceMapping map_to_root;      // remap this node's namespace -> root namespace
  std::vector<uint16_t> children;    // child node indices (build order)

  bool has_specs() const { return HasFlag(flags, NodeFlags::HasSpecs); }
  bool is_inert() const { return HasFlag(flags, NodeFlags::Inert); }
  bool is_culled() const { return HasFlag(flags, NodeFlags::Culled); }
};

/// Composition options.
struct CompositionOptions {
  bool load_payloads = true;       // default policy when payload_policy is null.
  uint32_t max_depth = 256;        // arc recursion limit / cycle backstop.
  uint32_t max_namespace_depth = 1024;  // composed prim-tree depth backstop
                                        // (BuildStage); errors instead of
                                        // exhausting the C++ stack.
  bool error_when_asset_not_found = false;
  bool detect_instances = true;    // group instanceable prims into prototypes.

  // Phase 7 (S5): apply cross-layer list-op merging when gathering a site's
  // composition arcs. Default ON (AOUSD-conformant): each arc field is composed
  // once across the site's specs weakest->strongest per SdfListOp rules
  // (explicit-replace / prepend / append / delete / dedup) -- validated to match
  // pxrUSD's flatten on cross-layer delete/explicit cases. Set false for the
  // legacy behavior (each spec's arcs expanded independently, i.e. strong-first
  // concatenation; an identically-authored arc in two sublayers expands twice).
  bool apply_list_ops = true;
  int num_threads = 1;             // PrewarmPrimIndices worker hint (see note).

  /// Per-payload load policy. Invoked with (prim path that authors the payload,
  /// payload asset path); return true to load, false to defer. When null, the
  /// `load_payloads` flag is used. (Per-prim Load/UnloadPayload overrides this.)
  /// Must be thread-safe when PrewarmPrimIndices runs with num_threads != 1.
  std::function<bool(const Path &, const std::string &)> payload_policy;
};

/// The composed graph for a single prim. Borrows its layer-stack table from the
/// owning Cache (the table outlives every PrimIndex it backs).
class PrimIndex {
 public:
  static constexpr uint16_t kInvalidNode = 0xFFFF;
  static constexpr size_t kMaxNodeCount = kInvalidNode;

  PrimIndex() = default;

  const Path &GetPath() const { return prim_path_; }
  const CompNode &GetRootNode() const { return nodes_[0]; }
  const CompNode &GetNode(uint16_t i) const { return nodes_[i]; }
  uint16_t GetNodeCount() const { return static_cast<uint16_t>(nodes_.size()); }
  const std::vector<CompNode> &GetNodes() const { return nodes_; }
  const std::vector<uint16_t> &GetStrengthOrder() const { return strength_order_; }
  const std::deque<LayerStack> *GetLayerStacks() const { return layer_stacks_; }

  /// Resolve a node's interned site prim path (empty if no path table bound).
  ///
  /// The path/layer tables are std::deque (Phase 9 F3): appends never move
  /// existing elements, so a PrimIndex handed to another thread can resolve its
  /// nodes' sites while a concurrent build appends new entries -- no dangling
  /// reference. The bounds check uses `path_table_size_`, a snapshot captured
  /// (under the build lock) when this index was bound to the table, instead of
  /// the live `deque::size()`, so it is also race-free against those appends.
  const std::string &SitePath(const CompNode &n) const {
    static const std::string kEmpty;
    if (!path_table_ || n.site_path_idx >= path_table_size_) return kEmpty;
    return (*path_table_)[n.site_path_idx];
  }

  bool HasAnySpecs() const {
    for (const auto &n : nodes_) if (n.has_specs()) return true;
    return false;
  }

  std::string DumpToString() const {
    std::string s = "PrimIndex<" + prim_path_.str() + "> nodes=" +
                    std::to_string(nodes_.size()) + "\n";
    for (uint16_t oi : strength_order_) {
      const CompNode &n = nodes_[oi];
      s += "  [" + std::to_string(oi) + "] " + ArcTypeName(n.arc_type) +
           " stack=" + std::to_string(n.layer_stack_idx) + " site=" +
           SitePath(n) + (n.has_specs() ? " (specs)" : "") + "\n";
    }
    return s;
  }

  // Build API (used by Cache::Impl).
  void SetPath(const Path &p) { prim_path_ = p; }
  void SetLayerStacks(const std::deque<LayerStack> *t) { layer_stacks_ = t; }
  // `published_size` is the table size at bind time; SitePath bounds-checks
  // against it (race-free) rather than the live deque size (see SitePath).
  void SetPathTable(const std::deque<std::string> *t, size_t published_size) {
    path_table_ = t;
    path_table_size_ = published_size;
  }
  uint16_t AddNode(CompNode &&n) {
    if (nodes_.size() >= kMaxNodeCount) return kInvalidNode;
    nodes_.push_back(std::move(n));
    return static_cast<uint16_t>(nodes_.size() - 1);
  }
  CompNode &MutableNode(uint16_t i) { return nodes_[i]; }
  void SetStrengthOrder(std::vector<uint16_t> &&o) { strength_order_ = std::move(o); }

 private:
  Path prim_path_;
  std::vector<CompNode> nodes_;            // node 0 == root
  std::vector<uint16_t> strength_order_;   // strongest first
  // Borrowed, stable-address tables owned by the Cache (std::deque so concurrent
  // appends never invalidate elements a held PrimIndex resolves; F3).
  const std::deque<LayerStack> *layer_stacks_ = nullptr;
  const std::deque<std::string> *path_table_ = nullptr;   // interned site paths
  size_t path_table_size_ = 0;  // snapshot of path_table size at bind time
};

}  // namespace pcp
}  // namespace next
}  // namespace tinyusdz
