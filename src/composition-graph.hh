// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// composition-graph.hh - DAG-based USD composition engine
//
// This is a parallel implementation alongside the existing iterative
// layer-flattening approach in composition.hh. It builds a per-prim
// composition DAG (PrimIndex) that preserves opinion provenance, supports
// lazy payload loading, and enables instancing detection during composition.
//
// Implements AOUSD Core Spec Section 10 (Composition) with:
//   - Full LIVRPS ordering via priority-ordered task queue
//   - Globally-weak specializes (propagated to root)
//   - Multi-level implied inherits/specializes
//   - Instancing-aware composition (InstanceKey-based deduplication)
//   - Incremental payload load/unload
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "nonstd/expected.hpp"

#include "asset-resolution.hh"
#include "core/composition-types.hh"
#include "core/prim-spec.hh"
#include "layer.hh"
#include "namespace-mapping.hh"

namespace tinyusdz {

// Forward declarations
class Stage;

namespace composition_graph {

// ---------------------------------------------------------------------------
// ArcType -- composition arc types in LIVRPS strength order
// ---------------------------------------------------------------------------

/// Arc types enumerated in strength order (lower value = stronger).
/// Matches the LIVRPS ordering from AOUSD Core Spec 10.4.
enum class ArcType : uint8_t {
  Root = 0,        ///< Root node (direct local opinions)
  SubLayer = 1,    ///< L: sublayer
  Inherit = 2,     ///< I: inherits (class-based)
  Variant = 3,     ///< V: variant selection
  Relocate = 4,    ///< Between V and R (matches PCP enum ordering)
  Reference = 5,   ///< R: references
  Payload = 6,     ///< P: payloads
  Specialize = 7,  ///< S: specializes (weakest, globally weak)
};

/// Return true if this is a class-based arc (inherits or specializes).
inline bool IsClassBasedArc(ArcType t) {
  return t == ArcType::Inherit || t == ArcType::Specialize;
}

/// Get a human-readable name for an arc type.
const char *ArcTypeName(ArcType t);

// ---------------------------------------------------------------------------
// NodeFlags -- bitfield flags for CompNode state
// ---------------------------------------------------------------------------

enum class NodeFlags : uint16_t {
  None = 0,
  HasSpecs = 1 << 0,          ///< Node has authored specs at this path
  Inert = 1 << 1,             ///< Node cannot contribute opinions
  Culled = 1 << 2,            ///< Pruned during culling pass
  PermissionDenied = 1 << 3,  ///< Permission blocks this node
  PayloadLoaded = 1 << 4,     ///< Payload content has been loaded
  PayloadDeferred = 1 << 5,   ///< Payload exists but not yet loaded
  IsImpliedArc = 1 << 6,      ///< Implied inherit/specialize (propagated)
  IsDueToAncestor = 1 << 7,   ///< From ancestral composition, not direct
};

inline NodeFlags operator|(NodeFlags a, NodeFlags b) {
  return static_cast<NodeFlags>(static_cast<uint16_t>(a) |
                                static_cast<uint16_t>(b));
}
inline NodeFlags operator&(NodeFlags a, NodeFlags b) {
  return static_cast<NodeFlags>(static_cast<uint16_t>(a) &
                                static_cast<uint16_t>(b));
}
inline NodeFlags operator~(NodeFlags a) {
  return static_cast<NodeFlags>(~static_cast<uint16_t>(a));
}
inline bool HasFlag(NodeFlags flags, NodeFlags flag) {
  return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

// ---------------------------------------------------------------------------
// CompNode -- tightly packed composition DAG node (40 bytes)
// ---------------------------------------------------------------------------

/// A single node in the per-prim composition DAG.
///
/// Nodes are stored in a contiguous vector (PrimIndex::_nodes) and linked
/// via uint16_t indices. Children are stored as a singly-linked sibling list
/// in strength order (strongest first).
///
/// The uint16_t index limit (65535) is far beyond real-world needs -- PCP
/// typically has 10-50 nodes per prim.
struct CompNode {
  static constexpr uint16_t kInvalidIndex = 0xFFFF;

  uint16_t parent{kInvalidIndex};       ///< Parent node index
  uint16_t first_child{kInvalidIndex};  ///< First child (strongest)
  uint16_t next_sibling{kInvalidIndex}; ///< Next sibling (weaker)
  uint16_t origin{kInvalidIndex};       ///< Origin node for implied arcs

  ArcType arc_type{ArcType::Root};      ///< 1 byte
  uint8_t depth{0};                     ///< Depth in composition DAG

  NodeFlags flags{NodeFlags::None};     ///< 2 bytes of bitflags

  uint16_t layer_stack_idx{kInvalidIndex}; ///< Index into LayerStackEntry table
  uint16_t layer_idx{0};                   ///< Layer index within stack

  uint16_t map_expr_idx{kInvalidIndex};    ///< Index into MapExpr pool

  uint32_t site_path_idx{0};               ///< Index into interned path table

  int32_t strength_order{0};               ///< Pre-computed (lower = stronger)

  uint16_t sibling_num{0};  ///< Ordering among same-type siblings at origin
  uint16_t _pad{0};         ///< Alignment padding

  // Convenience queries
  bool has_parent() const { return parent != kInvalidIndex; }
  bool has_children() const { return first_child != kInvalidIndex; }
  bool has_next_sibling() const { return next_sibling != kInvalidIndex; }
  bool has_specs() const { return HasFlag(flags, NodeFlags::HasSpecs); }
  bool is_inert() const { return HasFlag(flags, NodeFlags::Inert); }
  bool is_culled() const { return HasFlag(flags, NodeFlags::Culled); }
  bool is_payload_deferred() const {
    return HasFlag(flags, NodeFlags::PayloadDeferred);
  }
  bool is_payload_loaded() const {
    return HasFlag(flags, NodeFlags::PayloadLoaded);
  }
  bool is_implied_arc() const { return HasFlag(flags, NodeFlags::IsImpliedArc); }
};

static_assert(sizeof(CompNode) <= 40, "CompNode must fit in 40 bytes");

// ---------------------------------------------------------------------------
// Shared tables -- owned by CompositionGraph, referenced by PrimIndices
// ---------------------------------------------------------------------------

/// Entry in the layer stack table. Identifies a layer and its cumulative
/// time offset. Multiple nodes can reference the same layer via this table.
struct LayerStackEntry {
  const Layer *layer{nullptr};  ///< Borrowed pointer (owned by _loaded_layers)
  std::string identifier;       ///< Asset path or layer name
  LayerOffset offset;           ///< Cumulative layer offset to root
};

/// Namespace mapping expression with lazy composition.
///
/// Map expressions form a chain: each expression has an optional parent.
/// The composed mapping (from this expression through all ancestors to root)
/// is cached on first access.
struct MapExpr {
  NamespaceMapping mapping;  ///< Direct mapping for this arc
  int32_t parent_expr{-1};   ///< Index of parent expression (-1 = identity)

  /// Lazily computed full chain from this expression to root.
  mutable nonstd::optional<NamespaceMapping> _composed_cache;

  /// Get the fully composed mapping (composes up the chain, cached).
  const NamespaceMapping &GetComposed(const std::vector<MapExpr> &pool) const {
    if (_composed_cache.has_value()) return *_composed_cache;

    if (parent_expr < 0) {
      _composed_cache = mapping;
    } else {
      const NamespaceMapping &parent_composed =
          pool[static_cast<size_t>(parent_expr)].GetComposed(pool);
      _composed_cache = ComposeNamespaceMappings(parent_composed, mapping);
    }
    return *_composed_cache;
  }

  /// Apply the fully composed mapping to a path.
  Path Apply(const Path &path, const std::vector<MapExpr> &pool) const {
    return GetComposed(pool).Apply(path);
  }
};

// ---------------------------------------------------------------------------
// OpinionSource -- tracks where a resolved value came from
// ---------------------------------------------------------------------------

/// Identifies the source of a resolved opinion (for provenance tracking).
struct OpinionSource {
  uint16_t node_idx{CompNode::kInvalidIndex};
  uint16_t layer_stack_idx{CompNode::kInvalidIndex};
  uint16_t layer_idx{0};
  std::string site_path;  ///< Path within the source layer
};

// ---------------------------------------------------------------------------
// PrimIndex -- per-prim composition graph
// ---------------------------------------------------------------------------

class PrimIndexBuilder;  // forward decl

/// The composition graph for a single prim. Contains a DAG of CompNode
/// entries representing all composition arcs that contribute opinions
/// to this prim. Nodes are stored in a contiguous vector for cache
/// efficiency, with a pre-computed strength order for fast value resolution.
class PrimIndex {
 public:
  PrimIndex() = default;

  /// The absolute prim path this index represents.
  const Path &GetPath() const { return _prim_path; }

  /// Root node (always index 0).
  const CompNode &GetRootNode() const { return _nodes[0]; }

  /// Access a node by index.
  const CompNode &GetNode(uint16_t idx) const { return _nodes[idx]; }

  /// Number of nodes in the graph.
  uint16_t GetNodeCount() const {
    return static_cast<uint16_t>(_nodes.size());
  }

  /// Iterate node indices in strength order (strongest first).
  /// This is the primary iteration order for value resolution.
  const std::vector<uint16_t> &GetStrengthOrder() const {
    return _strength_order;
  }

  /// True if any node in the graph has authored specs.
  bool HasAnySpecs() const;

  /// True if all payload arcs are loaded (no deferred payloads).
  bool IsPayloadLoaded() const;

  /// True if this prim has instanceable=true metadata.
  bool IsInstanceable() const { return _is_instanceable; }

  /// Dump the graph as a human-readable string (for debugging).
  std::string DumpToString() const;

 private:
  friend class PrimIndexBuilder;
  friend class CompositionGraph;

  Path _prim_path;

  /// Contiguous node pool. Node 0 is always the root.
  std::vector<CompNode> _nodes;

  /// Node indices sorted by strength (strongest first).
  /// Pre-computed after graph construction.
  std::vector<uint16_t> _strength_order;

  /// Whether this prim has instanceable=true.
  bool _is_instanceable{false};

  // Shared tables (borrowed pointers, owned by CompositionGraph)
  const std::vector<std::string> *_path_table{nullptr};
  const std::vector<LayerStackEntry> *_layer_stacks{nullptr};
  const std::vector<MapExpr> *_map_expressions{nullptr};
};

// ---------------------------------------------------------------------------
// InstanceKey -- for instancing deduplication
// ---------------------------------------------------------------------------

/// A 128-bit hash that captures the structural identity of a prim's
/// composition graph. Two prims with equal InstanceKeys have structurally
/// identical composition arcs and will produce identical child namespaces.
///
/// Used to share PrimIndices between instances (AOUSD Core Spec 11.4).
struct InstanceKey {
  uint64_t hash_lo{0};
  uint64_t hash_hi{0};

  bool operator==(const InstanceKey &rhs) const {
    return hash_lo == rhs.hash_lo && hash_hi == rhs.hash_hi;
  }
  bool operator!=(const InstanceKey &rhs) const { return !(*this == rhs); }
  bool is_valid() const { return hash_lo != 0 || hash_hi != 0; }
};

struct InstanceKeyHasher {
  size_t operator()(const InstanceKey &k) const {
    return static_cast<size_t>(k.hash_lo);
  }
};

/// Compute an InstanceKey from a completed PrimIndex.
/// Returns an invalid key if the prim is not instanceable.
InstanceKey ComputeInstanceKey(const PrimIndex &index,
                               const std::vector<LayerStackEntry> &stacks,
                               const std::vector<std::string> &path_table);

// ---------------------------------------------------------------------------
// DeferredPayloadInfo -- tracks unloaded payloads for incremental loading
// ---------------------------------------------------------------------------

/// Information stored for a payload that was skipped during initial
/// composition. Contains enough context to load it later.
struct DeferredPayloadInfo {
  Path prim_path;         ///< Prim that owns the payload arc
  Payload payload;        ///< The payload descriptor
  uint16_t node_idx;      ///< Node index in the PrimIndex

  // Asset resolution context at discovery time
  std::string current_working_path;
  std::vector<std::string> asset_search_paths;
};

// ---------------------------------------------------------------------------
// CompositionGraphOptions
// ---------------------------------------------------------------------------

struct CompositionGraphOptions {
  /// Payload loading policy. Return true to load, false to defer.
  /// When nullptr (default), all payloads are loaded eagerly.
  std::function<bool(const Path &prim_path, const Payload &payload)>
      payload_policy;

  /// Maximum composition depth (prevents infinite recursion).
  uint32_t max_depth{256};

  /// Enable instancing detection during composition.
  bool detect_instances{true};

  /// File format handlers for non-USD assets.
  std::unordered_map<std::string, FileFormatHandler> fileformats;

  /// Maximum memory limit in MB.
  size_t max_memory_mb{16384};

  /// Make an error when referenced asset is not found.
  bool error_when_asset_not_found{false};

  /// Make an error when referenced asset has unsupported format.
  bool error_when_unsupported_fileformat{false};
};

// ---------------------------------------------------------------------------
// CompositionGraph -- top-level composition engine
// ---------------------------------------------------------------------------

/// DAG-based USD composition engine.
///
/// Builds a PrimIndex (composition graph) for each prim in the scene.
/// Preserves opinion provenance, supports lazy payload loading, and
/// enables instancing detection.
///
/// Usage:
///   // After sublayer composition (L phase)
///   auto result = CompositionGraph::Compose(resolver, root_layer, options);
///   if (result) {
///     CompositionGraph graph = std::move(*result);
///     Stage stage;
///     graph.BuildStage(&stage, &warn, &err);
///   }
///
class CompositionGraph {
 public:
  CompositionGraph() = default;
  ~CompositionGraph() = default;

  // Move only (owns loaded layers)
  CompositionGraph(CompositionGraph &&) = default;
  CompositionGraph &operator=(CompositionGraph &&) = default;
  CompositionGraph(const CompositionGraph &) = delete;
  CompositionGraph &operator=(const CompositionGraph &) = delete;

  /// Compose a scene from a root layer (sublayers should already be composed).
  ///
  /// Builds PrimIndex DAGs for all prims, detects instances, and
  /// handles lazy payloads according to the options.
  ///
  /// @param[in] resolver Asset resolution resolver
  /// @param[in] root_layer The root layer (after CompositeSublayers)
  /// @param[in] options Composition options
  /// @return CompositionGraph on success, error string on failure
  static nonstd::expected<CompositionGraph, std::string> Compose(
      AssetResolutionResolver &resolver, const Layer &root_layer,
      const CompositionGraphOptions &options = {});

  // -- Query API --

  /// Get the PrimIndex for a specific prim path.
  /// Returns nullptr if not found.
  const PrimIndex *GetPrimIndex(const Path &prim_path) const;

  /// Get all prim paths that have PrimIndices.
  std::vector<Path> GetAllPrimPaths() const;

  /// Number of unique prototypes detected.
  size_t GetPrototypeCount() const;

  /// Get all instance paths sharing a prototype.
  std::vector<Path> GetInstancesForPrototype(size_t prototype_idx) const;

  /// Get the prototype PrimIndex for a given prototype index.
  const PrimIndex *GetPrototypePrimIndex(size_t prototype_idx) const;

  // -- Lazy payload API --

  /// Load a deferred payload and recompose the affected prim.
  nonstd::expected<bool, std::string> LoadPayload(
      const Path &prim_path, AssetResolutionResolver &resolver);

  /// Unload a payload and remove its contributed opinions.
  nonstd::expected<bool, std::string> UnloadPayload(const Path &prim_path);

  /// Get all prims with deferred (unloaded) payloads.
  std::vector<Path> GetDeferredPayloadPaths() const;

  /// Check if a specific prim has deferred payloads.
  bool HasDeferredPayload(const Path &prim_path) const;

  // -- Stage construction --

  /// Build a Stage from the composition graph.
  ///
  /// Uses ValueResolver to compose PrimSpecs from the DAG, then calls
  /// the existing ReconstructPrim pipeline from composition-reconstruct.cc.
  /// The resulting Stage is identical in structure to what the existing
  /// CompositeAllArcs + LayerToStage pipeline produces.
  bool BuildStage(Stage *stage, std::string *warn, std::string *err) const;

  // -- Diagnostics --

  /// Estimate total memory usage in bytes.
  size_t EstimateMemoryUsage() const;

  /// Dump all PrimIndices as a human-readable string.
  std::string DumpToString() const;

 private:
  friend class PrimIndexBuilder;

  // -- Shared tables --

  /// Interned path table. All prim paths stored once.
  std::vector<std::string> _path_table;

  /// Path string -> index lookup for interning.
  std::unordered_map<std::string, uint32_t> _path_intern_map;

  /// Layer stack entries (shared across all PrimIndices).
  std::vector<LayerStackEntry> _layer_stacks;

  /// Namespace mapping expressions (shared, with lazy composition).
  std::vector<MapExpr> _map_expressions;

  // -- Per-prim composition graphs --

  /// Map: prim path string -> PrimIndex.
  std::unordered_map<std::string, std::shared_ptr<PrimIndex>> _prim_indices;

  // -- Instancing --

  /// Map: InstanceKey -> prototype PrimIndex (shared among instances).
  std::unordered_map<InstanceKey, size_t, InstanceKeyHasher>
      _instance_key_to_prototype;

  /// Prototype PrimIndices.
  std::vector<std::shared_ptr<PrimIndex>> _prototypes;

  /// Map: prim path -> prototype index (-1 if not an instance).
  std::unordered_map<std::string, int> _instance_to_prototype;

  // -- Lazy payloads --

  /// Deferred payload descriptors for incremental loading.
  std::vector<DeferredPayloadInfo> _deferred_payloads;

  // -- Loaded layers (kept alive for value resolution) --

  /// Layers loaded during composition. Must outlive all PrimIndices
  /// because nodes reference these layers via LayerStackEntry pointers.
  std::vector<std::unique_ptr<Layer>> _loaded_layers;

  /// The root layer (borrowed, must outlive the graph).
  const Layer *_root_layer{nullptr};

  /// Asset resolver (borrowed, must outlive the graph).
  AssetResolutionResolver *_resolver{nullptr};

  /// Options used for composition.
  CompositionGraphOptions _options;

  // -- Internal helpers --

  /// Intern a path string, returning its index in _path_table.
  uint32_t InternPath(const std::string &path_str);

  /// Add a layer stack entry, returning its index.
  uint16_t AddLayerStack(const Layer *layer, const std::string &id,
                         const LayerOffset &offset);

  /// Add a map expression, returning its index.
  uint16_t AddMapExpression(const NamespaceMapping &mapping,
                            int32_t parent_expr);

  /// Build PrimIndex for a single prim (recursive for children).
  bool BuildPrimIndex(const std::string &prim_path, const PrimSpec &primspec,
                      uint16_t root_layer_stack_idx, std::string *warn,
                      std::string *err);

  /// Compose a PrimSpec from a PrimIndex by walking the DAG in strength order.
  bool ComposePrimSpecFromIndex(const PrimIndex &index, PrimSpec *out,
                                std::string *warn, std::string *err) const;
};

// ---------------------------------------------------------------------------
// TaskType -- composition task types for the priority queue
// ---------------------------------------------------------------------------

/// Task types ordered by processing priority.
/// Lower value = higher priority = processed first.
///
/// The ordering implements LIVRPS: inherits before variants before
/// references before payloads before specializes.
enum class TaskType : uint8_t {
  EvalRootNode = 0,
  EvalSubLayers = 1,
  EvalImpliedInherits = 2,
  EvalInherits = 3,
  EvalVariants = 4,
  EvalReferences = 5,
  EvalPayloads = 6,
  EvalImpliedSpecializes = 7,
  EvalSpecializes = 8,
  EvalRelocates = 9,
};

/// A single composition task in the priority queue.
struct CompositionTask {
  TaskType type{TaskType::EvalRootNode};
  uint16_t node_idx{CompNode::kInvalidIndex};  ///< Node that spawned this task
  uint8_t list_op_idx{0};                       ///< ListOp index
  uint8_t item_idx{0};                          ///< Item within ListOp

  /// Comparison for std::priority_queue (reversed: lower type = higher prio).
  bool operator<(const CompositionTask &rhs) const {
    if (type != rhs.type)
      return static_cast<uint8_t>(type) > static_cast<uint8_t>(rhs.type);
    return node_idx > rhs.node_idx;
  }
};

// ---------------------------------------------------------------------------
// PrimIndexBuilder -- builds a PrimIndex via the task queue
// ---------------------------------------------------------------------------

/// Builder for constructing a PrimIndex by processing composition arcs
/// via a priority-ordered task queue.
///
/// This is an internal class used by CompositionGraph::Compose().
class PrimIndexBuilder {
 public:
  PrimIndexBuilder(CompositionGraph *graph, const Path &prim_path,
                   const PrimSpec &root_primspec,
                   uint16_t root_layer_stack_idx);

  /// Build the PrimIndex by processing all composition arcs.
  nonstd::expected<PrimIndex, std::string> Build();

 private:
  // Task handlers
  bool EvalRootNode(std::string *err);
  bool EvalSubLayers(uint16_t node_idx, std::string *err);
  bool EvalInherits(uint16_t node_idx, std::string *err);
  bool EvalVariants(uint16_t node_idx, std::string *err);
  bool EvalReferences(uint16_t node_idx, std::string *err);
  bool EvalPayloads(uint16_t node_idx, std::string *err);
  bool EvalSpecializes(uint16_t node_idx, std::string *err);
  bool EvalRelocates(uint16_t node_idx, std::string *err);

  // Implied arc propagation
  bool PropagateImpliedInherits(uint16_t source_node, std::string *err);
  bool PropagateImpliedSpecializes(uint16_t source_node, std::string *err);

  // Node management
  uint16_t AddNode(ArcType arc_type, uint16_t parent_idx,
                   uint16_t layer_stack_idx, uint16_t layer_idx,
                   uint32_t site_path_idx, uint16_t map_expr_idx);

  void AppendChild(uint16_t parent_idx, uint16_t child_idx);

  // Cycle detection
  bool WouldCreateCycle(const std::string &layer_id,
                        const std::string &prim_path) const;

  // Strength order computation
  void ComputeStrengthOrder();

  // Node culling
  void CullInertNodes();

  // Variant collection
  void CollectVariantOpinions(uint16_t node_idx);
  void ResolveAndApplyVariants(std::string *err);

  // Scan a PrimSpec's metadata for composition arcs and enqueue tasks
  void ScanArcsAndEnqueueTasks(uint16_t node_idx, const PrimSpec &ps);

  /// Look up the PrimSpec that a node points to.
  /// Returns nullptr if the node's layer/path is invalid.
  const PrimSpec *GetPrimSpecForNode(uint16_t node_idx) const;

  // State
  CompositionGraph *_graph;
  PrimIndex _result;
  const PrimSpec *_root_primspec;
  uint16_t _root_layer_stack_idx;

  // Task queue
  std::priority_queue<CompositionTask> _task_queue;

  // Cycle detection set: (layer_id, prim_path) pairs on current stack
  std::set<std::pair<std::string, std::string>> _arc_stack;

  // Path-based cycle detection for inherits/specializes
  std::set<std::string> _inherit_visited;
  std::set<std::string> _specialize_visited;

  // Variant opinion collection: prim_path -> list of VariantSelectionMaps
  std::map<std::string, std::vector<VariantSelectionMap>> _variant_opinions;

  // Current composition depth (for recursion limit)
  uint32_t _depth{0};
};

}  // namespace composition_graph

// Convenience aliases at tinyusdz namespace level
using CompositionGraph = composition_graph::CompositionGraph;
using CompositionGraphOptions = composition_graph::CompositionGraphOptions;

}  // namespace tinyusdz
