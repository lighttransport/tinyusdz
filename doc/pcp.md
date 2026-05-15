# TinyUSDZ PCP (Prim Composition Pipeline) Public API Documentation

Comprehensive API reference for the Prim Composition Pipeline system in TinyUSDZ, organized by module category.

**Document Version**: 1.0  
**Last Updated**: 2024-11-12  
**Author**: Syoyo Fujita, Light Transport Entertainment Inc.
**Powered By**: Claude code

---

## Table of Contents

1. [Core PCP Modules](#core-pcp-modules)
2. [Advanced Modules](#advanced-modules)
3. [Threading Module](#threading-module)
4. [Time-Based Composition Module](#time-based-composition-module)
5. [Usage Patterns](#usage-patterns)

---

## CORE PCP MODULES

These are the fundamental modules that implement the composition algorithm.

### Module: pcp-cache (pcp-cache.cc/hh)

**Purpose**: Central cache for managing PrimIndex computations and layer stacks with BLAKE3-based instancing.

#### Key Classes

##### `class Cache`

Primary composition cache management system.

**Constructor**:
```cpp
Cache(const CacheConfig& config)
```

**Configuration Structure**:
```cpp
struct CacheConfig {
    Layer* root_layer = nullptr;
    Layer* session_layer = nullptr;
    bool usd_mode = true;
    size_t max_memory_limit_mb = 0;  // 0 = unlimited
    bool enable_instancing = true;
    bool enable_payload_inclusion = true;
};
```

**Key Public Methods**:

- `LayerStackPtr GetRootLayerStack()` - Get the root layer stack
- `LayerStackPtr GetLayerStack(const std::string& identifier)` - Get named layer stack
- `PrimIndexPtr ComputePrimIndex(const Path& prim_path, ComputePrimIndexOptions options, std::vector<Error>* errors)` - Compute prim index with options
- `PrimIndexPtr GetPrimIndex(const Path& prim_path)` - Get cached prim index
- `bool HasPrimIndex(const Path& prim_path) const` - Check cache existence
- `void InvalidatePrimIndex(const Path& prim_path)` - Invalidate cached result
- `void InvalidateAll()` - Clear entire cache
- `Dependencies* GetDependencies()` - Get dependency graph

**Instance Key Computation**:
- Uses BLAKE3 hashing to detect identical compositions
- Hashes nodes in strength order, site information, and arc types
- Enables automatic instancing of identical prim indexes

---

### Module: pcp-prim-index (pcp-prim-index.cc/hh)

**Purpose**: Represents the composition result - a directed acyclic graph of all composition arcs affecting a prim.

#### Key Classes

##### `class PrimIndexGraph`

Core graph data structure holding composition nodes and relationships.

**Methods**:

- `NodeRef CreateRootNode(const Site& site)` - Create root composition node
- `NodeRef AddChildNode(NodeRef parent, const Arc& arc, const Site& site)` - Add child node to graph
- `NodeRef GetNode(size_t index)` - Get node by index
- `NodeRef GetRootNode()` - Get root node
- `std::vector<NodeRef> GetNodes() const` - Get all nodes
- `std::vector<NodeRef> GetNodesInStrengthOrder() const` - Get nodes sorted by strength (LIVRPS)
- `bool CompareNodeStrength(size_t a, size_t b) const` - Compare node strength

**Node Structure**:
```cpp
struct Node {
    Site site;                    // Composition site
    Arc arc;                      // Arc to parent
    size_t parent = SIZE_MAX;     // Parent index
    std::vector<size_t> children; // Child indices
    // Flags for composition state
};
```

##### `class PrimIndex`

High-level prim composition index.

**Methods**:

- `const Path& GetPrimPath() const` - Get prim path
- `LayerStackPtr GetRootLayerStack() const` - Get root layer stack
- `PrimIndexGraph& GetGraph()` - Get composition graph
- `const PrimIndexGraph& GetGraph() const` - Get graph (const)
- `NodeRef GetRootNode() const` - Get root composition node
- `std::vector<NodeRef> GetNodesInStrengthOrder() const` - Nodes by strength

**Data Members**:
```cpp
Path prim_path_;
LayerStackPtr root_layer_stack_;
std::unique_ptr<PrimIndexGraph> graph_;
InstanceKey instance_key_;
```

---

### Module: pcp-node (pcp-node.cc/hh)

**Purpose**: Represents a single composition node in the PrimIndex graph.

#### Key Classes

##### `class NodeRef`

Reference to a node within a PrimIndexGraph.

**Construction**: Typically obtained from PrimIndexGraph methods, not created directly.

**Key Methods**:

- `bool IsValid() const` - Check node validity
- `bool IsRootNode() const` - Check if node is root
- `Site GetSite() const` - Get composition site
- `LayerStackPtr GetLayerStack() const` - Get layer stack
- `Path GetPath() const` - Get prim path at this site
- `const Arc& GetArc() const` - Get arc to parent
- `ArcType GetArcType() const` - Get arc type (from LIVRPS enum)
- `NodeRef GetParent() const` - Get parent node
- `NodeRef GetOrigin() const` - Get origin node
- `int GetSiblingNumAtOrigin() const` - Get sibling number
- `int GetNamespaceDepth() const` - Get namespace depth
- `std::shared_ptr<MapFunction> GetMapToParent() const` - Get path mapping
- `std::shared_ptr<MapFunction> GetMapToRoot() const` - Compose all maps to root
- `std::vector<NodeRef> GetChildren() const` - Get all child nodes
- `size_t GetChildCount() const` - Number of children
- `NodeRef GetChild(size_t index) const` - Get child by index
- `NodeRef GetNextSibling() const` - Get next sibling
- `NodeRef GetPrevSibling() const` - Get previous sibling

**Data Members** (accessed via methods):
```cpp
PrimIndexGraph* graph_;
size_t index_;
```

---

### Module: pcp-map-function (pcp-map-function.cc/hh)

**Purpose**: Represents path and value translation functions for composition arcs.

#### Key Classes

##### `class MapFunction`

Path translation and value mapping for composition.

**Static Creation Methods**:

- `static MapFunctionPtr CreateIdentity()` - Create identity map
- `static MapFunctionPtr CreatePathMap(const Path& source, const Path& target)` - Create path mapping
- `static MapFunctionPtr CreateWithRelocates(const Relocates& relocates)` - Create with relocations
- `static MapFunctionPtr CreateWithLayerOffset(const LayerOffset& offset)` - Create with time offset
- `static MapFunctionPtr CreateComposite(MapFunctionPtr outer, MapFunctionPtr inner)` - Compose two maps

**Key Methods**:

- `bool IsIdentity() const` - Check if identity map
- `bool IsNull() const` - Check if null map
- `Path MapPath(const Path& path) const` - Apply path mapping
- `Path MapPathReverse(const Path& path) const` - Apply reverse mapping
- `std::optional<Path> TryMapPath(const Path& path) const` - Safe path mapping
- `MapFunctionPtr Compose(const MapFunctionPtr& other) const` - Compose with another map
- `MapFunctionPtr GetInverse() const` - Get inverse mapping if possible

**Data Members**:
```cpp
std::vector<std::pair<Path, Path>> path_mappings_;
Relocates relocates_;
LayerOffset time_offset_;
bool is_identity_;
bool is_null_;
bool is_invertible_;
```

---

### Module: pcp-layer-stack (pcp-layer-stack.cc/hh)

**Purpose**: Manages layer stacks - the local layer composition for a prim site.

#### Key Classes

##### `class LayerStack`

Manages local layer composition for composition sites.

**Static Creation**:
```cpp
static LayerStackPtr LayerStack::Create(
    Layer* root_layer,
    Layer* session_layer,
    const std::string& identifier)
```

**Key Methods**:

- `const std::string& GetIdentifier() const` - Get unique identifier
- `Layer* GetRootLayer() const` - Get root layer
- `Layer* GetSessionLayer() const` - Get session layer
- `const std::vector<LayerEntry>& GetLayers() const` - Get all layers in stack
- `const LayerEntry* FindLayer(const std::string& identifier) const` - Find layer by ID
- `Path ApplyRelocates(const Path& path) const` - Apply layer relocations forward
- `Path ApplyRelocatesReverse(const Path& path) const` - Apply relocations backward
- `bool IsRelocatedPath(const Path& path) const` - Check if path is relocated
- `const Relocates& GetRelocates() const` - Get all relocations
- `const std::vector<ExpressionVariable>& GetExpressionVariables() const` - Get expression variables

**LayerEntry Structure**:
```cpp
struct LayerEntry {
    Layer* layer;
    std::string layer_id;
    size_t position;  // Position in stack
};
```

**Layer Relationship**:
- Session layer (if present) > Root layer > Sublayers
- Higher position = stronger opinions

---

### Module: pcp-dependencies (pcp-dependencies.cc/hh)

**Purpose**: Tracks composition dependencies between prims.

#### Key Classes

##### `class Dependencies`

Tracks composition dependencies for incremental updates.

**Key Methods**:

- `void AddDependency(const Path& source, const Path& target)` - Record dependency
- `std::vector<Path> GetDependents(const Path& prim_path) const` - Get dependent prims
- `std::vector<Path> GetDependencies(const Path& prim_path) const` - Get dependencies
- `void InvalidateDependents(const Path& prim_path)` - Mark all dependents invalid
- `void Clear()` - Clear all dependencies
- `void Rebuild(const Cache& cache)` - Rebuild dependency graph

**Usage**: Used internally by Cache to track invalidation cascades when composition changes.

---

### Module: pcp-compose-site (pcp-compose-site.cc/hh)

**Purpose**: Defines composition sites and arcs - the basic units of USD composition.

#### Key Structures

##### `struct Site`

Composition site = (LayerStack, Path) pair.

```cpp
struct Site {
    LayerStackPtr layer_stack;  // Which layer stack
    Path path;                  // Which prim path
    
    // Hashing support
    struct Hash {
        size_t operator()(const Site& site) const;
    };
};
```

##### `enum class ArcType`

Composition arc types in LIVRPS order (from OpenUSD):

```cpp
enum class ArcType {
    INVALID = -1,
    Local = 0,
    Inherit = 1,
    VariantSelection = 2,
    Reference = 3,
    Payload = 4,
    Specialize = 5
};
```

LIVRPS = Local, Inherit, VariantSelection, Reference, Payload, Specialize (strength order)

##### `struct Arc`

Composition arc - connection between sites.

```cpp
struct Arc {
    ArcType type;                              // Arc type (LIVRPS)
    LayerOffset layer_offset;                  // Time offset
    MapFunctionPtr map_to_parent;              // Path mapping
    size_t origin_node_index = SIZE_MAX;       // Where arc originates
    int sibling_num_at_origin = 0;             // Sibling ordering
    int namespace_depth = 0;                   // For instancing
};
```

##### `struct Relocates`

Path relocation specification.

```cpp
struct Relocates {
    Path source;  // Source path to relocate
    Path target;  // Target path after relocation
};
```

##### `struct LayerOffset`

Time offset for layer composition.

```cpp
struct LayerOffset {
    double offset = 0.0;
    double scale = 1.0;
    
    bool IsIdentity() const { return offset == 0.0 && scale == 1.0; }
};
```

---

## ADVANCED MODULES

Extensions providing specific composition functionality.

### Module: pcp-changes (pcp-changes.hh)

**Purpose**: Change notification and processing system for incremental composition updates.

#### Key Classes

##### `enum class ChangeType`

```cpp
enum class ChangeType {
    LayerContentsChanged,
    LayerOffsetChanged,
    LayerRelocatesChanged,
    PrimSpecAdded,
    PrimSpecRemoved,
    PrimPropertyChanged,
    ReferenceAdded,
    ReferenceRemoved,
    PayloadAdded,
    PayloadRemoved,
    InheritAdded,
    InheritRemoved,
    SpecializeAdded,
    SpecializeRemoved,
    VariantSelectionChanged,
    ConnectionAdded,
    ConnectionRemoved,
    TargetPathChanged,
    SignificantChange,
    SubtreeChange
};
```

##### `struct ChangeEntry`

```cpp
struct ChangeEntry {
    ChangeType type;
    Path prim_path;
    std::string layer_id;
    std::string field_name;
    value::Value old_value;
    value::Value new_value;
    Path target_path;
    std::string variant_set_name;
    std::string variant_selection;
    
    bool IsSignificant() const;
    bool AffectsComposition() const;
};
```

##### `class ChangeList`

Collection of changes.

**Methods**:

- `void AddChange(const ChangeEntry& change)`
- `void AddLayerChange(const std::string& layer_id, ChangeType type)`
- `void AddPrimChange(const Path& prim_path, const std::string& layer_id, ChangeType type)`
- `void AddPropertyChange(const Path& prim_path, const std::string& property_name, const std::string& layer_id, const value::Value& old_value, const value::Value& new_value)`
- `bool IsEmpty() const`
- `size_t GetSize() const`
- `const std::vector<ChangeEntry>& GetChanges() const`
- `std::vector<ChangeEntry> GetChangesForPrim(const Path& prim_path) const`
- `std::vector<ChangeEntry> GetChangesForLayer(const std::string& layer_id) const`
- `std::vector<ChangeEntry> GetCompositionChanges() const`
- `std::vector<ChangeEntry> GetSignificantChanges() const`
- `void Merge(const ChangeList& other)`
- `void Optimize()` - Remove redundant changes

##### `class ChangeProcessor`

Processes changes and determines invalidation scope.

**Constructor**: `ChangeProcessor(Cache* cache)`

**Methods**:

- `ProcessingResult ProcessChanges(const ChangeList& changes)` - Process changes and return affected prims
- `bool RequiresRecomposition(const ChangeEntry& change) const`
- `bool AffectsInstancing(const ChangeEntry& change) const`
- `bool AffectsProperties(const ChangeEntry& change) const`
- `std::vector<Path> GetDependentPaths(const Path& path) const`
- `void InvalidatePrim(const Path& path)`
- `void InvalidateSubtree(const Path& path)`
- `void InvalidateAll()`

**ProcessingResult**:
```cpp
struct ProcessingResult {
    std::vector<Path> prims_to_recompose;
    std::vector<Path> prims_to_update_properties;
    std::vector<Path> prims_to_check_instances;
    bool requires_full_recompose = false;
    size_t num_changes_processed = 0;
};
```

##### `class ChangeSubscriber`

Manages change notifications.

**Methods**:

- `SubscriptionId Subscribe(ChangeCallback callback, const Path& filter_path = Path())`
- `SubscriptionId SubscribeToLayer(ChangeCallback callback, const std::string& layer_id)`
- `void Unsubscribe(SubscriptionId id)`
- `void UnsubscribeAll()`
- `void NotifyChanges(const ChangeList& changes)`
- `void BeginBatch()`
- `void EndBatch()`
- `bool IsInBatch() const`

##### `class ChangeTransaction`

Atomic change application with rollback support.

**Constructor**: `ChangeTransaction(Cache* cache)`

**Methods**:

- `void AddChange(const ChangeEntry& change)`
- `void AddChanges(const ChangeList& changes)`
- `void Begin()`
- `void Commit()`
- `void Rollback()`
- `bool IsActive() const`
- `const ChangeList& GetPendingChanges() const`

---

### Module: pcp-path-translation (pcp-path-translation.hh)

**Purpose**: Advanced path mapping and translation utilities across composition arcs.

#### Key Classes

##### `class PathTranslationContext`

Manages path translation state between nodes.

**Methods**:

- `void SetSourceNode(const NodeRef& node)`
- `void SetTargetNode(const NodeRef& node)`
- `void SetMapFunction(const MapFunctionPtr& map_func)`
- `Path TranslatePath(const Path& path) const`
- `Path ReverseTranslatePath(const Path& path) const`
- `std::optional<Path> TryTranslatePath(const Path& path) const`
- `std::optional<Path> TryReverseTranslatePath(const Path& path) const`
- `std::vector<Path> TranslatePaths(const std::vector<Path>& paths) const`
- `bool NeedsTranslation() const`
- `bool IsIdentity() const`

##### `class PathTranslator`

Manages path translations across prim index boundaries.

**Constructor**: `PathTranslator(Cache* cache)`

**Methods**:

- `Path TranslatePathFromNodeToNode(const Path& path, const NodeRef& source_node, const NodeRef& target_node) const`
- `Path TranslatePathBetweenPrimIndexes(const Path& path, const PrimIndex& source_index, const PrimIndex& target_index) const`
- `std::vector<TargetPath> FindAllTargetPaths(const Path& source_path, const PrimIndex& prim_index) const`
- `std::vector<Path> FindSourcePaths(const Path& target_path, const PrimIndex& prim_index) const`
- `MappingAnalysis AnalyzeMapping(const MapFunctionPtr& map_func, const std::vector<Path>& source_paths) const`

**TargetPath Structure**:
```cpp
struct TargetPath {
    Path path;
    NodeRef node;
    double strength;  // Composition strength
};
```

**MappingAnalysis Structure**:
```cpp
struct MappingAnalysis {
    bool is_one_to_one = true;
    bool is_onto = true;
    bool is_invertible = true;
    std::vector<Path> unmapped_sources;
    std::vector<Path> multiply_mapped_targets;
};
```

##### `class PathTranslationCache`

Optimized path translation caching.

**Methods**:

- `void CachePath(const Path& source, const Path& target, const MapFunctionPtr& map_func)`
- `std::optional<Path> GetCachedTranslation(const Path& source, const MapFunctionPtr& map_func) const`
- `void Clear()`
- `void ClearForMapFunction(const MapFunctionPtr& map_func)`
- `Statistics GetStatistics() const`
- `void ResetStatistics()`

**Statistics**:
```cpp
struct Statistics {
    size_t cache_hits = 0;
    size_t cache_misses = 0;
    size_t total_entries = 0;
    double hit_rate = 0.0;
};
```

##### `class FilteredPathTranslator`

Path translation with filtering predicates.

**Methods**:

- `void SetSourceFilter(PathPredicate predicate)`
- `void SetTargetFilter(PathPredicate predicate)`
- `std::optional<Path> TranslateIfAllowed(const Path& path, const MapFunctionPtr& map_func) const`
- `std::vector<Path> TranslatePathsWithFilter(const std::vector<Path>& paths, const MapFunctionPtr& map_func) const`
- `bool PassesSourceFilter(const Path& path) const`
- `bool PassesTargetFilter(const Path& path) const`

##### Utility Functions

```cpp
Path TranslatePropertyPath(const Path& property_path, const Path& source_prim, const Path& target_prim);
Path TranslateTargetPath(const Path& target_path, const MapFunctionPtr& map_func);
Path TranslateVariantPath(const Path& variant_path, const std::string& variant_set, const std::string& variant_selection);
bool IsTranslatable(const Path& path, const MapFunctionPtr& map_func);
Path GetCommonAncestor(const Path& path1, const Path& path2);
MapFunctionPtr ComposeMapFunctions(const std::vector<MapFunctionPtr>& functions);
MapFunctionPtr CreateMapFunctionFromPairs(const std::vector<std::pair<Path, Path>>& path_pairs);
std::optional<MapFunctionPtr> InvertMapFunction(const MapFunctionPtr& map_func);
```

---

### Module: pcp-instancing (pcp-instancing.hh)

**Purpose**: Instance detection and optimization for composition deduplication.

#### Key Classes

##### `struct InstanceKey`

128-bit hash for prim index comparison.

```cpp
struct InstanceKey {
    std::array<uint8_t, 16> hash;
    
    bool operator==(const InstanceKey& other) const;
    bool operator!=(const InstanceKey& other) const;
    
    struct Hash {
        size_t operator()(const InstanceKey& key) const;
    };
    
    std::string ToString() const;
    static InstanceKey FromString(const std::string& str);
};
```

##### `class InstanceDetector`

Detects instanceable prims.

**Constructor**: `InstanceDetector(Cache* cache)`

**Methods**:

- `bool CanBeInstanced(const PrimIndex& prim_index) const`
- `InstanceKey ComputeInstanceKey(const PrimIndex& prim_index) const`
- `std::vector<Path> FindInstanceablePrims() const`
- `std::vector<InstanceGroup> FindInstanceGroups() const`
- `bool CanShareInstance(const PrimIndex& index1, const PrimIndex& index2) const`
- `void SetConfig(const Config& config)`
- `const Config& GetConfig() const`

**Config Structure**:
```cpp
struct Config {
    bool enable_aggressive_instancing = false;
    bool ignore_local_overrides = false;
    bool instance_across_variants = false;
    size_t min_instance_count = 2;
    size_t max_instance_depth = 10;
};
```

**InstanceGroup Structure**:
```cpp
struct InstanceGroup {
    InstanceKey key;
    std::vector<Path> prim_paths;
    size_t estimated_memory_saved = 0;
};
```

##### `class InstanceManager`

Manages instance sharing and deduplication.

**Constructor**: `InstanceManager(Cache* cache)`

**Methods**:

- `void RegisterPrimIndex(const Path& path, const PrimIndex& prim_index)`
- `const PrimIndex* GetMasterForInstance(const Path& instance_path) const`
- `std::vector<Path> GetInstancesOfMaster(const Path& master_path) const`
- `OptimizationResult OptimizeInstances()`
- `Statistics GetStatistics() const`
- `void Clear()`
- `void ClearInstance(const Path& path)`

**OptimizationResult**:
```cpp
struct OptimizationResult {
    size_t instances_created = 0;
    size_t instances_shared = 0;
    size_t memory_saved_bytes = 0;
    std::vector<Path> optimized_prims;
};
```

**Statistics**:
```cpp
struct Statistics {
    size_t total_prims = 0;
    size_t instanceable_prims = 0;
    size_t unique_masters = 0;
    size_t shared_instances = 0;
    size_t memory_usage_bytes = 0;
    size_t memory_saved_bytes = 0;
};
```

##### `class InstanceCompositionOptimizer`

Optimizes composition for instancing.

**Constructor**: `InstanceCompositionOptimizer(Cache* cache)`

**Methods**:

- `std::vector<CompositionOptimization> SuggestOptimizations(const PrimIndex& prim_index) const`
- `bool ApplyOptimization(const CompositionOptimization& optimization)`
- `void ReorderArcsForInstancing(PrimIndex& prim_index)`
- `std::vector<PrototypePattern> DetectPrototypePatterns() const`
- `std::vector<ReferenceRestructuring> SuggestReferenceRestructuring() const`

##### `class InstanceKeyBuilder`

Builds instance keys with BLAKE3.

**Methods**:

- `void AddNode(const NodeRef& node)`
- `void AddPath(const Path& path)`
- `void AddString(const std::string& str)`
- `void AddBytes(const void* data, size_t size)`
- `void AddLayerStack(const LayerStackPtr& layer_stack)`
- `void AddMapFunction(const MapFunctionPtr& map_func)`
- `void AddArcType(ArcType type)`
- `void AddVariantSelection(const std::string& set_name, const std::string& selection)`
- `InstanceKey Finalize()`
- `void Reset()`

##### Utility Functions

```cpp
bool IsInstancePath(const Path& path);
Path GetMasterPath(const Path& instance_path);
bool IsInstancingBeneficial(const std::vector<Path>& candidate_paths, size_t threshold_bytes = 1024);
size_t EstimateInstancingBenefit(const PrimIndex& prim_index, size_t instance_count);

struct CommonStructure {
    std::vector<Path> common_children;
    std::vector<std::string> common_properties;
    std::vector<Reference> common_references;
    bool has_common_structure = false;
};

CommonStructure FindCommonStructure(const std::vector<PrimIndex>& prim_indexes);
```

---

### Module: pcp-diagnostics (pcp-diagnostics.hh)

**Purpose**: Debugging and diagnostic tools for composition validation.

#### Key Classes

##### `enum class DiagnosticLevel`

```cpp
enum class DiagnosticLevel {
    Error,
    Warning,
    Info,
    Debug,
    Trace
};
```

##### `struct DiagnosticMessage`

```cpp
struct DiagnosticMessage {
    DiagnosticLevel level;
    std::string category;
    std::string message;
    Path prim_path;
    std::string source_location;  // File:line
    std::chrono::steady_clock::time_point timestamp;
    
    std::string ToString() const;
};
```

##### `class DiagnosticCollector`

Collects diagnostic information during composition.

**Methods**:

- `void SetLevel(DiagnosticLevel level)`
- `DiagnosticLevel GetLevel() const`
- `void EnableCategory(const std::string& category)`
- `void DisableCategory(const std::string& category)`
- `bool IsCategoryEnabled(const std::string& category) const`
- `void AddMessage(const DiagnosticMessage& msg)`
- `void AddError(const std::string& message, const Path& path = Path())`
- `void AddWarning(const std::string& message, const Path& path = Path())`
- `void AddInfo(const std::string& message, const Path& path = Path())`
- `void AddDebug(const std::string& message, const Path& path = Path())`
- `void AddTrace(const std::string& message, const Path& path = Path())`
- `const std::vector<DiagnosticMessage>& GetMessages() const`
- `std::vector<DiagnosticMessage> GetMessagesByLevel(DiagnosticLevel level) const`
- `std::vector<DiagnosticMessage> GetMessagesByCategory(const std::string& category) const`
- `std::vector<DiagnosticMessage> GetMessagesForPrim(const Path& path) const`
- `void Clear()`
- `std::string GenerateReport() const`
- `std::string GenerateHTMLReport() const`
- `std::string GenerateJSONReport() const`

##### `class PrimIndexValidator`

Validates prim indexes for correctness.

**Methods**:

- `ValidationResult Validate(const PrimIndex& prim_index) const`
- `ValidationResult ValidateStructure(const PrimIndex& prim_index) const`
- `ValidationResult ValidateArcs(const PrimIndex& prim_index) const`
- `ValidationResult ValidatePaths(const PrimIndex& prim_index) const`
- `ValidationResult ValidateMapFunctions(const PrimIndex& prim_index) const`
- `ValidationResult ValidateInstancing(const PrimIndex& prim_index) const`
- `bool HasCycles(const PrimIndex& prim_index) const`
- `bool HasDuplicateArcs(const PrimIndex& prim_index) const`
- `bool HasInvalidRelocations(const PrimIndex& prim_index) const`
- `bool HasConflictingOpinions(const PrimIndex& prim_index) const`

**ValidationResult**:
```cpp
struct ValidationResult {
    bool is_valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> infos;
    
    void AddError(const std::string& error);
    void AddWarning(const std::string& warning);
    std::string ToString() const;
};
```

##### `class CompositionDumper`

Dumps composition details for debugging.

**Methods**:

- `std::string DumpPrimIndex(const PrimIndex& prim_index, const DumpOptions& options = DumpOptions()) const`
- `std::string DumpCompositionGraph(const PrimIndex& prim_index, const DumpOptions& options = DumpOptions()) const`
- `std::string DumpAsDot(const PrimIndex& prim_index, const DumpOptions& options = DumpOptions()) const`
- `std::string DumpCacheState(const Cache& cache, const DumpOptions& options = DumpOptions()) const`
- `std::string DumpLayerStack(const LayerStack& layer_stack, const DumpOptions& options = DumpOptions()) const`
- `void DumpToFile(const std::string& filename, const PrimIndex& prim_index, const DumpOptions& options = DumpOptions()) const`

**DumpOptions**:
```cpp
struct DumpOptions {
    bool include_specs = true;
    bool include_map_functions = true;
    bool include_layer_stacks = true;
    bool include_dependencies = true;
    bool include_timings = false;
    bool use_colors = false;
    size_t max_depth = SIZE_MAX;
    DiagnosticLevel detail_level = DiagnosticLevel::Info;
};
```

##### `class CompositionTracer`

Traces composition evaluation.

**Methods**:

- `void SetEnabled(bool enabled)`
- `bool IsEnabled() const`
- `void TraceNodeEvaluation(const NodeRef& node, const std::string& phase)`
- `void TraceArcAddition(const Arc& arc, const NodeRef& parent, const NodeRef& child)`
- `void TracePathTranslation(const Path& source, const Path& target, const MapFunctionPtr& func)`
- `void TraceDependencyUpdate(const Path& source, const Path& dependent)`
- `void TraceInstanceDetection(const Path& path, const InstanceKey& key)`
- `void TraceTaskQueued(const CompositionTask& task)`
- `void TraceTaskStarted(const CompositionTask& task)`
- `void TraceTaskCompleted(const CompositionTask& task, bool success)`
- `const std::vector<TraceEvent>& GetEvents() const`
- `std::string ExportAsJSON() const`
- `std::string ExportAsChrome() const`  // Chrome tracing format
- `void ExportToFile(const std::string& filename) const`
- `void Clear()`

**TraceEvent**:
```cpp
struct TraceEvent {
    std::string type;
    std::string description;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::microseconds duration;
    std::unordered_map<std::string, std::string> metadata;
};
```

##### `class ComplexityAnalyzer`

Analyzes composition complexity.

**Methods**:

- `ComplexityMetrics Analyze(const PrimIndex& prim_index) const`
- `std::vector<HotSpot> IdentifyHotSpots(const PrimIndex& prim_index) const`
- `std::vector<Simplification> SuggestSimplifications(const PrimIndex& prim_index) const`

**ComplexityMetrics**:
```cpp
struct ComplexityMetrics {
    size_t num_nodes = 0;
    size_t num_arcs = 0;
    size_t graph_depth = 0;
    size_t max_breadth = 0;
    size_t num_references = 0;
    size_t num_payloads = 0;
    size_t num_inherits = 0;
    size_t num_specializes = 0;
    size_t num_variants = 0;
    size_t num_relocations = 0;
    size_t cyclomatic_complexity = 0;
    double average_fanout = 0.0;
    
    std::string ToString() const;
};
```

##### Utility Functions

```cpp
std::string PrettyPrintPrimIndex(const PrimIndex& prim_index, size_t max_depth = SIZE_MAX);

struct PrimIndexDiff {
    std::vector<std::string> additions;
    std::vector<std::string> deletions;
    std::vector<std::string> modifications;
    bool are_equivalent = false;
};

PrimIndexDiff ComparePrimIndexes(const PrimIndex& index1, const PrimIndex& index2);
bool ValidateCompositionInvariants(const Cache& cache);

struct MemoryLeakReport {
    size_t leaked_nodes = 0;
    size_t leaked_arcs = 0;
    size_t leaked_map_functions = 0;
    std::vector<Path> suspicious_paths;
};

MemoryLeakReport CheckForMemoryLeaks(const Cache& cache);
```

---

### Module: pcp-debug-utils (pcp-debug-utils.hh)

**Purpose**: Enhanced debugging, analysis, and visualization tools.

#### Key Classes

##### `enum class DebugFormat`

```cpp
enum class DebugFormat {
    Text,
    JSON,
    HTML,
    GraphViz,
    CSV,
    Markdown
};
```

##### `class CompositionDetailedDumper`

Detailed composition dumping.

**Methods**:

- `std::string DumpDetailed(const PrimIndex& prim_index, DebugFormat format = DebugFormat::Text) const`
- `std::string DumpArcGraph(const PrimIndex& prim_index) const`
- `std::string DumpStrengthOrdering(const PrimIndex& prim_index) const`
- `std::string DumpNodeOpinions(const NodeRef& node, size_t max_opinions = 10) const`
- `std::string DumpDependencies(const Cache& cache, const Path& prim_path) const`
- `std::string DumpArcDetails(const Arc& arc, const NodeRef& parent_node) const`
- `std::string DumpComparison(const PrimIndex& index1, const PrimIndex& index2) const`
- `std::string DumpPerformanceMetrics(const CompositionMetrics& metrics) const`

##### `class PCPVerifier`

Comprehensive PCP verification.

**Methods**:

- `VerificationReport VerifyCache(const Cache& cache) const`
- `VerificationReport VerifyPrimIndex(const PrimIndex& prim_index) const`
- `IssueReport FindCommonIssues(const PrimIndex& prim_index) const`
- `std::vector<FixSuggestion> SuggestFixes(const VerificationReport& report, const PrimIndex& prim_index) const`

**VerificationReport**:
```cpp
struct VerificationReport {
    bool is_valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> info;
    
    size_t total_issues() const;
};
```

**IssueReport**:
```cpp
struct IssueReport {
    bool has_cycles = false;
    bool has_invalid_relocations = false;
    bool has_dead_code = false;
    bool has_redundant_arcs = false;
    bool has_infinite_loops = false;
    
    std::vector<Path> cyclic_paths;
    std::vector<Path> dead_paths;
    std::vector<std::pair<Arc, Arc>> redundant_arc_pairs;
};
```

##### `class CompositionDebugger`

Interactive composition debugger.

**Constructor**: `CompositionDebugger(Cache* cache)`

**Methods**:

- `std::vector<DebugStep> RecordCompositionSteps(const Path& prim_path)`
- `std::string GetStateAtStep(size_t step_number) const`
- `std::string TraceExecution(const Path& prim_path) const`
- `std::string CompareWithOpenUSD(const PrimIndex& tinyusdz_index, const std::string& openusd_dump) const`

**DebugStep**:
```cpp
struct DebugStep {
    size_t step_number = 0;
    std::string operation;
    NodeRef affected_node;
    std::string state_before;
    std::string state_after;
};
```

##### `class CompositionStatistics`

Statistical analysis of composition.

**Methods**:

- `CacheStats AnalyzeCache(const Cache& cache) const`
- `std::vector<CompositionPattern> FindPatterns(const Cache& cache) const`
- `std::string GenerateStatisticsReport(const Cache& cache) const`
- `std::string ExportAsCSV(const Cache& cache) const`
- `std::string ExportAsJSON(const Cache& cache) const`

**CacheStats**:
```cpp
struct CacheStats {
    size_t total_cached_prims = 0;
    size_t total_cached_layers = 0;
    size_t avg_nodes_per_index = 0;
    size_t max_depth = 0;
    size_t avg_arc_count = 0;
    double cache_hit_rate = 0.0;
    size_t total_memory_bytes = 0;
};
```

##### `class CompositionVisualizer`

Visualization helpers.

**Methods**:

- `std::string GenerateTreeView(const PrimIndex& prim_index) const`
- `std::string GenerateASCIIDiagram(const PrimIndex& prim_index) const`
- `std::string GenerateColoredDotGraph(const PrimIndex& prim_index) const`
- `std::string GenerateSVGSpec(const PrimIndex& prim_index) const`
- `std::string GenerateInteractiveHTML(const PrimIndex& prim_index) const`

##### `class MemoryAnalyzer`

Memory analysis for compositions.

**Methods**:

- `MemoryLayout AnalyzeMemoryUsage(const PrimIndex& prim_index) const`
- `std::vector<MemoryHotSpot> FindMemoryHotspots(const Cache& cache, size_t top_n = 10) const`
- `size_t EstimateMemoryForComposition(const Path& prim_path, size_t estimated_node_count) const`
- `std::vector<std::string> SuggestMemoryOptimizations(const Cache& cache) const`

**MemoryLayout**:
```cpp
struct MemoryLayout {
    size_t node_memory_bytes = 0;
    size_t arc_memory_bytes = 0;
    size_t map_function_memory_bytes = 0;
    size_t cached_data_memory_bytes = 0;
    size_t total_bytes = 0;
};
```

##### `class CompositionBenchmark`

Benchmarking utilities.

**Methods**:

- `BenchmarkResult BenchmarkPrimIndexCreation(Cache* cache, const Path& prim_path, int iterations = 100) const`
- `BenchmarkResult BenchmarkPathTranslation(const std::vector<Path>& paths, const MapFunctionPtr& map_func, int iterations = 100) const`
- `BenchmarkResult BenchmarkCacheOperations(Cache* cache, int iterations = 100) const`
- `BenchmarkResult BenchmarkComposition(Cache* cache, const std::vector<Path>& prim_paths) const`
- `std::string CompareBenchmarks(const BenchmarkResult& result1, const BenchmarkResult& result2) const`

**BenchmarkResult**:
```cpp
struct BenchmarkResult {
    std::string test_name;
    double time_ms = 0.0;
    size_t operations = 0;
    double ops_per_ms = 0.0;
    size_t memory_bytes = 0;
};
```

##### Utility Functions

```cpp
std::string QuickDump(const PrimIndex& prim_index);
std::string QuickDump(const Cache& cache);
std::string QuickDump(const NodeRef& node);
std::string QuickDump(const Arc& arc);

bool SaveDumpToFile(const std::string& filename, const std::string& content,
                    DebugFormat format = DebugFormat::Text);

std::string CompareDumps(const std::string& dump1, const std::string& dump2);
```

---

### Module: pcp-performance (pcp-performance.hh)

**Purpose**: Performance monitoring, profiling, and optimization.

#### Key Classes

##### `class PerfTimer`

Simple performance timer.

**Methods**:

- `void Start()`
- `void Stop()`
- `void Reset()`
- `double GetElapsedSeconds() const`
- `double GetElapsedMilliseconds() const`
- `double GetElapsedMicroseconds() const`
- `bool IsRunning() const`

##### `class PerfCounter`

Atomic performance counter.

**Methods**:

- `void Increment(size_t value = 1)`
- `void Decrement(size_t value = 1)`
- `void Reset()`
- `size_t Get() const`

##### `struct CompositionMetrics`

Comprehensive composition performance metrics.

```cpp
struct CompositionMetrics {
    double total_time_ms = 0.0;
    double prim_index_build_time_ms = 0.0;
    double arc_evaluation_time_ms = 0.0;
    double path_translation_time_ms = 0.0;
    double instance_detection_time_ms = 0.0;
    double dependency_update_time_ms = 0.0;
    
    size_t num_prim_indexes_built = 0;
    size_t num_nodes_created = 0;
    size_t num_arcs_evaluated = 0;
    size_t num_paths_translated = 0;
    size_t num_cache_hits = 0;
    size_t num_cache_misses = 0;
    
    size_t memory_allocated_bytes = 0;
    size_t memory_deallocated_bytes = 0;
    size_t peak_memory_bytes = 0;
    size_t current_memory_bytes = 0;
    
    size_t num_tasks_queued = 0;
    size_t num_tasks_processed = 0;
    size_t max_queue_depth = 0;
    double average_task_time_ms = 0.0;
    
    std::string ToString() const;
    std::string ToJSON() const;
};
```

##### `class PerformanceMonitor`

Real-time performance monitoring.

**Methods**:

- `void SetEnabled(bool enabled)`
- `bool IsEnabled() const`
- `void StartSession(const std::string& session_name = "")`
- `void EndSession()`
- `void StartTimer(const std::string& name)`
- `void StopTimer(const std::string& name)`
- `double GetTimerValue(const std::string& name) const`
- `void IncrementCounter(const std::string& name, size_t value = 1)`
- `void DecrementCounter(const std::string& name, size_t value = 1)`
- `size_t GetCounterValue(const std::string& name) const`
- `void RecordAllocation(size_t bytes)`
- `void RecordDeallocation(size_t bytes)`
- `CompositionMetrics GetMetrics() const`
- `void ResetMetrics()`
- `std::string GenerateReport() const`
- `std::string GenerateDetailedReport() const`
- `void SaveReport(const std::string& filename) const`

##### `class ScopedTimer`

RAII timer for automatic timing.

**Constructor**: `ScopedTimer(PerformanceMonitor* monitor, const std::string& name)`

##### `class PerformanceProfiler`

Hierarchical performance profiling.

**Methods**:

- `void StartProfiling()`
- `void StopProfiling()`
- `bool IsProfiling() const`
- `void PushSection(const std::string& name)`
- `void PopSection()`
- `const ProfileNode* GetRootNode() const`
- `std::string GenerateFlatReport() const`
- `std::string GenerateHierarchicalReport() const`
- `std::string GenerateFlameGraph() const`
- `void ExportToChrome(const std::string& filename) const`
- `void ExportToCallgrind(const std::string& filename) const`
- `void Clear()`

**ProfileNode**:
```cpp
struct ProfileNode {
    std::string name;
    double self_time_ms = 0.0;
    double total_time_ms = 0.0;
    size_t call_count = 0;
    std::vector<std::unique_ptr<ProfileNode>> children;
    
    double GetAverageTime() const;
};
```

##### `class ScopedProfiler`

RAII profiler section.

**Constructor**: `ScopedProfiler(PerformanceProfiler* profiler, const std::string& section)`

##### `class CachePerformanceAnalyzer`

Cache-specific performance analysis.

**Constructor**: `CachePerformanceAnalyzer(Cache* cache)`

**Methods**:

- `CacheStatistics Analyze() const`
- `std::vector<OptimizationSuggestion> SuggestOptimizations() const`
- `void StartMonitoring(std::chrono::milliseconds interval)`
- `void StopMonitoring()`
- `TimeSeriesData GetTimeSeriesData() const`

**CacheStatistics**:
```cpp
struct CacheStatistics {
    double overall_hit_rate = 0.0;
    double prim_index_hit_rate = 0.0;
    double layer_stack_hit_rate = 0.0;
    double path_translation_hit_rate = 0.0;
    
    size_t num_cached_prim_indexes = 0;
    size_t num_cached_layer_stacks = 0;
    size_t num_cached_translations = 0;
    size_t total_cache_memory_bytes = 0;
    
    double average_lookup_time_ms = 0.0;
    double average_insertion_time_ms = 0.0;
    size_t num_evictions = 0;
    size_t num_invalidations = 0;
    
    std::string ToString() const;
};
```

##### `class CompositionOptimizer`

Composition optimization strategies.

**Constructor**: `CompositionOptimizer(Cache* cache)`

**Methods**:

- `void SetStrategy(OptimizationStrategy strategy)`
- `OptimizationStrategy GetStrategy() const`
- `OptimizationResult OptimizeComposition(PrimIndex& prim_index)`
- `void EnableParallelComposition(size_t num_threads = 0)`
- `void DisableParallelComposition()`
- `bool IsParallelEnabled() const`
- `void EnableBatching(size_t batch_size = 100)`
- `void DisableBatching()`
- `void ProcessBatch()`

**OptimizationStrategy**:
```cpp
enum class OptimizationStrategy {
    None,
    LazyEvaluation,
    Parallelization,
    Caching,
    Instancing,
    Batching
};
```

##### `class MemoryProfiler`

Memory allocation tracking.

**Methods**:

- `void TrackAllocation(void* ptr, size_t size, const std::string& type = "")`
- `void TrackDeallocation(void* ptr)`
- `MemoryStatistics GetStatistics() const`
- `std::vector<LeakInfo> DetectLeaks() const`
- `double GetFragmentationRatio() const`
- `void Clear()`

**MemoryStatistics**:
```cpp
struct MemoryStatistics {
    size_t current_usage_bytes = 0;
    size_t peak_usage_bytes = 0;
    size_t total_allocated_bytes = 0;
    size_t total_deallocated_bytes = 0;
    size_t num_allocations = 0;
    size_t num_deallocations = 0;
    
    std::unordered_map<std::string, size_t> usage_by_type;
    
    std::string ToString() const;
};
```

##### Utility Functions

```cpp
// Global performance instances
PerformanceMonitor* GetGlobalPerformanceMonitor();
PerformanceProfiler* GetGlobalPerformanceProfiler();

// Convenience macros
#define PCP_PROFILE(profiler, name) ScopedProfiler _prof(profiler, name)
#define PCP_TIMER(monitor, name) ScopedTimer _timer(monitor, name)
```

---

## THREADING MODULE

### Module: pcp-threading (pcp-threading.hh)

**Purpose**: Thread pool and parallel composition evaluation infrastructure.

#### Key Classes

##### `class CompositionTask`

Represents a composition task for parallel execution.

```cpp
class CompositionTask {
public:
    enum class State {
        PENDING,
        RUNNING,
        COMPLETED,
        FAILED,
    };
    
    CompositionTask(const Path& path, CompositionCallback callback);
    
    Path prim_path;
    CompositionCallback callback;
    std::atomic<State> state;
    Error error;
    int error_code;
    std::shared_ptr<PrimIndex> result;
};
```

##### `class WorkCounter`

Thread-safe work counter for synchronization.

**Methods**:

- `void Increment()`
- `void Decrement()`
- `void Wait(int target = 0)`
- `int Count() const`
- `void Reset(int value = 0)`

##### `class ThreadPool`

Thread pool for parallel composition.

**Constructor**: `ThreadPool(size_t num_threads = 0)` (0 = auto-detect)

**Methods**:

- `bool SubmitTask(std::shared_ptr<CompositionTask> task)`
- `size_t SubmitTasks(const std::vector<std::shared_ptr<CompositionTask>>& tasks)`
- `void WaitAll()`
- `void WaitUntil(size_t count)`
- `size_t PendingTaskCount() const`
- `size_t CompletedTaskCount() const`
- `size_t ThreadCount() const`
- `void SetEnabled(bool enabled)`
- `bool IsEnabled() const`
- `void Shutdown()`

##### `template<typename KeyType, typename ValueType> class ThreadSafeCache`

Thread-safe cache wrapper for parallel access.

**Methods**:

- `bool Get(const KeyType& key, ValueType& value)`
- `void Put(const KeyType& key, const ValueType& value)`
- `bool Remove(const KeyType& key)`
- `void Clear()`
- `size_t Size() const`
- `bool Contains(const KeyType& key) const`

##### `class ParallelCompositionEvaluator`

Parallel composition evaluation.

**Constructor**: `ParallelCompositionEvaluator(Cache* cache, size_t num_threads = 0)`

**Methods**:

- `std::vector<EvaluationResult> EvaluateParallel(const std::vector<Path>& prim_paths, const ComputePrimIndexOptions& options)`
- `ThreadPool* GetThreadPool()`

**EvaluationResult**:
```cpp
struct EvaluationResult {
    Path path;
    std::shared_ptr<PrimIndex> prim_index;
    std::vector<Error> errors;
};
```

---

## TIME-BASED COMPOSITION MODULE

### Module: pcp-timesample (pcp-timesample.hh)

**Purpose**: Animation timeline support for time-sampled composition evaluation.

#### Key Types

##### `using TimeCode = double`

Time in seconds.

##### `enum class InterpolationMode`

```cpp
enum class InterpolationMode {
    LINEAR,      // Linear interpolation between keyframes
    STEP,        // Step function (no interpolation)
    CUBIC,       // Cubic spline interpolation
    BEZIER,      // Bezier curve interpolation
    CONSTANT,    // Hold value until next keyframe
};
```

#### Key Structures

##### `struct TimeSample`

Single time sample in a composition timeline.

```cpp
struct TimeSample {
    TimeCode time;
    std::shared_ptr<PrimIndex> composition;
    InterpolationMode interpolation;
    float interpolation_parameter;  // 0.0 to 1.0 for curve shape
};
```

#### Key Classes

##### `class AnimationTimeline`

Animation timeline for a single prim.

**Constructor**: `AnimationTimeline(const Path& prim_path)`

**Methods**:

- `void AddKeyframe(TimeCode time, std::shared_ptr<PrimIndex> composition, InterpolationMode interpolation = InterpolationMode::LINEAR)`
- `bool EvaluateAt(TimeCode time, std::shared_ptr<PrimIndex>& composition)`
- `std::shared_ptr<PrimIndex> GetAt(TimeCode time, bool use_cache = true)`
- `std::vector<TimeCode> GetKeyframeTimes() const`
- `std::shared_ptr<TimeSample> GetKeyframeAt(size_t index) const`
- `size_t GetKeyframeCount() const`
- `bool GetTimeRange(TimeCode& start_time, TimeCode& end_time) const`
- `bool IsTimeInRange(TimeCode time) const`
- `void Clear()`
- `const Path& GetPrimPath() const`

##### `class CompositionTimeline`

Timeline management for entire composition.

**Methods**:

- `void AddTimeline(const Path& prim_path, std::shared_ptr<AnimationTimeline> timeline)`
- `std::shared_ptr<AnimationTimeline> GetTimeline(const Path& prim_path) const`
- `bool EvaluateFrame(TimeCode time, std::shared_ptr<PrimIndex>& frame)`
- `bool GetTimeRange(TimeCode& start_time, TimeCode& end_time) const`
- `size_t GetTimelineCount() const`
- `std::vector<Path> GetAnimatedPrims() const`
- `bool HasAnimation(const Path& prim_path) const`
- `void Clear()`
- `void SetFramesPerSecond(double fps)`
- `TimeCode FrameToTime(int frame_number) const`
- `int TimeToFrame(TimeCode time) const`

##### `class TimeBasedCompositionEvaluator`

Time-aware composition evaluation.

**Constructor**: `TimeBasedCompositionEvaluator(Cache* cache)`

**Methods**:

- `std::shared_ptr<PrimIndex> EvaluateAtTime(const Path& prim_path, TimeCode time, std::vector<Error>* errors = nullptr)`
- `CompositionTimeline* GetTimeline()`
- `std::vector<std::pair<TimeCode, std::shared_ptr<PrimIndex>>> EvaluateSequence(const Path& prim_path, TimeCode start_time, TimeCode end_time, TimeCode frame_step)`
- `void CacheCompositionAt(TimeCode time, std::shared_ptr<PrimIndex> composition)`

---

## USAGE PATTERNS

### Pattern 1: Basic Cache and Composition

```cpp
#include "pcp-cache.hh"
#include "pcp-prim-index.hh"

using namespace tinyusdz::tydra::pcp;

// Create cache with root layer
CacheConfig config;
config.root_layer = my_root_layer;
config.usd_mode = true;
auto cache = std::make_unique<Cache>(config);

// Compute prim index
Path prim_path("/Model/Geometry/Mesh");
std::vector<Error> errors;
auto prim_index = cache->ComputePrimIndex(prim_path, {}, &errors);

if (prim_index) {
    // Access composition graph
    auto root_node = prim_index->GetRootNode();
    auto nodes = prim_index->GetGraph().GetNodesInStrengthOrder();
    
    // Iterate through nodes in strength order
    for (const auto& node : nodes) {
        auto site = node.GetSite();
        auto arc_type = node.GetArcType();
    }
}
```

### Pattern 2: Path Translation

```cpp
#include "pcp-path-translation.hh"

using namespace tinyusdz::tydra::pcp;

PathTranslator translator(cache.get());

// Translate path through composition arcs
Path source_path("/Geom/Mesh");
Path target_path = translator.TranslatePathFromNodeToNode(
    source_path,
    source_node,
    target_node);

// Find all target paths for a source
auto targets = translator.FindAllTargetPaths(source_path, prim_index);
for (const auto& target : targets) {
    printf("Path: %s at strength %f\n",
           target.path.full_path_name().c_str(),
           target.strength);
}
```

### Pattern 3: Change Detection and Processing

```cpp
#include "pcp-changes.hh"

using namespace tinyusdz::tydra::pcp;

ChangeList changes;
changes.AddPrimChange(
    Path("/Model"),
    "root.usda",
    ChangeType::ReferenceAdded);

ChangeProcessor processor(cache.get());
auto result = processor.ProcessChanges(changes);

// Handle affected prims
for (const auto& prim_path : result.prims_to_recompose) {
    // Invalidate and recompute
    cache->InvalidatePrimIndex(prim_path);
    cache->ComputePrimIndex(prim_path, {}, nullptr);
}
```

### Pattern 4: Instance Detection and Optimization

```cpp
#include "pcp-instancing.hh"

using namespace tinyusdz::tydra::pcp;

InstanceDetector detector(cache.get());

// Find all instanceable prims
auto instanceable = detector.FindInstanceablePrims();
printf("Found %zu instanceable prims\n", instanceable.size());

// Find instance groups
auto groups = detector.FindInstanceGroups();
for (const auto& group : groups) {
    printf("Instance group with %zu prims saves %zu bytes\n",
           group.prim_paths.size(),
           group.estimated_memory_saved);
}

// Optimize instances
InstanceManager manager(cache.get());
auto result = manager.OptimizeInstances();
printf("Optimization saved %zu bytes\n", result.memory_saved_bytes);
```

### Pattern 5: Diagnostics and Validation

```cpp
#include "pcp-diagnostics.hh"

using namespace tinyusdz::tydra::pcp;

// Validate composition
PrimIndexValidator validator;
auto validation = validator.Validate(prim_index);

if (!validation.is_valid) {
    printf("Validation failed:\n");
    for (const auto& error : validation.errors) {
        printf("  ERROR: %s\n", error.c_str());
    }
}

// Dump composition details
CompositionDumper dumper;
CompositionDumper::DumpOptions options;
options.detail_level = DiagnosticLevel::Debug;
std::string dump = dumper.DumpPrimIndex(prim_index, options);

// Generate diagnostics report
DiagnosticCollector collector;
collector.SetLevel(DiagnosticLevel::Info);
// ... composition operations add diagnostics ...
std::string report = collector.GenerateReport();
```

### Pattern 6: Performance Monitoring

```cpp
#include "pcp-performance.hh"

using namespace tinyusdz::tydra::pcp;

PerformanceMonitor monitor;
monitor.SetEnabled(true);
monitor.StartSession("composition");

{
    ScopedTimer timer(&monitor, "prim_index_computation");
    auto prim_index = cache->ComputePrimIndex(prim_path, {}, nullptr);
}

monitor.EndSession();

auto metrics = monitor.GetMetrics();
printf("Composition took %.2f ms\n", metrics.total_time_ms);
printf("Cache hits: %zu / %zu\n", metrics.num_cache_hits,
       metrics.num_cache_hits + metrics.num_cache_misses);

std::string report = monitor.GenerateReport();
monitor.SaveReport("perf_report.txt");
```

### Pattern 7: Parallel Composition

```cpp
#include "pcp-threading.hh"

using namespace tinyusdz::tydra::pcp;

ParallelCompositionEvaluator evaluator(cache.get(), 4);

std::vector<Path> prim_paths = {
    Path("/Model/Geometry/Mesh1"),
    Path("/Model/Geometry/Mesh2"),
    Path("/Model/Geometry/Mesh3"),
};

auto results = evaluator.EvaluateParallel(prim_paths, {});

for (const auto& result : results) {
    if (result.prim_index) {
        printf("Computed: %s\n", result.path.full_path_name().c_str());
    } else {
        printf("Failed: %s - %s\n", result.path.full_path_name().c_str(),
               result.errors[0].message.c_str());
    }
}
```

### Pattern 8: Time-Based Composition

```cpp
#include "pcp-timesample.hh"

using namespace tinyusdz::tydra::pcp;

TimeBasedCompositionEvaluator time_evaluator(cache.get());
auto timeline = time_evaluator.GetTimeline();
timeline->SetFramesPerSecond(24.0);

// Add keyframes
auto anim_timeline = std::make_shared<AnimationTimeline>(Path("/Model"));

for (int frame = 0; frame < 100; ++frame) {
    double time = timeline->FrameToTime(frame);
    auto prim_index = cache->ComputePrimIndex(Path("/Model"), {}, nullptr);
    anim_timeline->AddKeyframe(time, prim_index, InterpolationMode::LINEAR);
}

timeline->AddTimeline(Path("/Model"), anim_timeline);

// Evaluate at specific time
double eval_time = 1.5;  // 1.5 seconds
auto evaluated = time_evaluator.EvaluateAtTime(Path("/Model"), eval_time, nullptr);

// Evaluate animation sequence
auto sequence = time_evaluator.EvaluateSequence(
    Path("/Model"),
    0.0,      // start time
    4.0,      // end time
    1.0/24.0  // frame step
);
```

### Pattern 9: Change Subscription

```cpp
#include "pcp-changes.hh"

using namespace tinyusdz::tydra::pcp;

ChangeSubscriber subscriber;

// Subscribe to changes on specific path
auto sub_id = subscriber.Subscribe(
    [](const ChangeList& changes) {
        printf("Changes detected on %zu prims\n", changes.GetSize());
    },
    Path("/Model"));

// Process changes
ChangeList new_changes;
new_changes.AddPrimChange(Path("/Model/Mesh"), "session.usda",
                          ChangeType::PrimSpecAdded);
subscriber.NotifyChanges(new_changes);

// Batch notifications
subscriber.BeginBatch();
// ... add multiple changes ...
subscriber.EndBatch();  // All batched changes notified together

subscriber.Unsubscribe(sub_id);
```

### Pattern 10: Complexity Analysis

```cpp
#include "pcp-debug-utils.hh"

using namespace tinyusdz::tydra::pcp;

CompositionStatistics stats;
auto cache_stats = stats.AnalyzeCache(*cache);

printf("Total cached prims: %zu\n", cache_stats.total_cached_prims);
printf("Average nodes per index: %zu\n", cache_stats.avg_nodes_per_index);
printf("Cache hit rate: %.1f%%\n", cache_stats.cache_hit_rate * 100);

ComplexityAnalyzer analyzer;
auto metrics = analyzer.Analyze(prim_index);

printf("Composition complexity:\n");
printf("  Nodes: %zu\n", metrics.num_nodes);
printf("  Arcs: %zu\n", metrics.num_arcs);
printf("  Depth: %zu\n", metrics.graph_depth);
printf("  Cyclomatic complexity: %zu\n", metrics.cyclomatic_complexity);

auto hotspots = analyzer.IdentifyHotSpots(prim_index);
for (const auto& spot : hotspots) {
    printf("HotSpot: %s (%s, severity %zu)\n",
           spot.path.full_path_name().c_str(),
           spot.type.c_str(),
           spot.severity);
}
```

---

## Conclusion

This PCP API provides a comprehensive, well-structured system for USD composition pipeline evaluation. The modular design allows for:

- Core composition computation with caching
- Change-driven incremental updates
- Performance profiling and optimization
- Parallel evaluation capabilities
- Time-based animation support
- Extensive debugging and validation tools

All modules are production-ready and thoroughly tested against OpenUSD compatibility requirements.


## Copyright

2025 - Present, Light Transport Entertainment Inc.
