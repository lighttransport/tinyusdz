# OpenUSD Composition Engine Architecture

## Overview

OpenUSD's composition engine is a sophisticated system that combines multiple sources of scene description (opinions) into a unified view of a 3D scene. At its core is the **PCP (Prim Cache Population)** module, which implements all composition semantics commonly referred to as "Layering and Referencing."

This document provides a comprehensive analysis of OpenUSD's composition architecture based on investigation of the source code, focusing on how it discovers, orders, and combines scene description from multiple sources.

## Core Concepts

### What is Composition?

Composition in USD is the process of:
1. **Discovering** all sources of opinions (scene description) that contribute to a specific prim
2. **Following** composition arcs (references, payloads, inherits, etc.) to build a complete composition structure
3. **Ordering** these opinions from strongest to weakest according to composition rules
4. **Caching** results for performance

### LIVRPS - The Strength Ordering Rule

USD uses the acronym **LIVRPS** to remember the strength ordering of composition arcs:
- **L**ocal (direct opinions in the layer stack)
- **I**nherits (class-based inheritance)
- **V**ariants (variant selections)
- **R**eferences (external file references)
- **P**ayloads (deferred references)
- **S**pecializes (specialized inheritance)

Stronger opinions override weaker ones. Local opinions are strongest, specializes are weakest.

## Key Components

### 1. PcpCache - The Main Entry Point

`PcpCache` is the primary interface for composition operations. It serves as:
- **Configuration container**: Stores root layer, session layer, variant fallbacks, payload inclusion set
- **Result cache**: Stores computed layer stacks, prim indexes, property indexes
- **Dependency manager**: Tracks what depends on what for change processing
- **Operating mode selector**: Can run in full mode or lighter "USD mode"

```cpp
// Key configuration stored in PcpCache:
- Root layer and session layer
- Variant fallback preferences
- Payload inclusion set (which payloads to load)
- File format target
- Expression variables
```

### 2. PcpLayerStack - Local Layer Composition

A `PcpLayerStack` represents the "local" composition at a specific site before any arcs bring in external references. It contains:

- **Layer hierarchy**: Session layers + sublayers in strong-to-weak order
- **Time offsets**: Layer-specific time scaling and translation
- **Relocations**: Namespace remapping rules
- **Expression variables**: Variables and their dependencies
- **Muted layers**: Layers to ignore
- **Layer tree**: Structure showing sublayer relationships

**Key insight**: A layer stack is what you get when you flatten all the layers at a particular location (without following any composition arcs to other files).

### 3. PcpPrimIndex - The Composition Result

`PcpPrimIndex` is the core result structure representing all composition for a prim. It contains:

- **Composition graph** (`PcpPrimIndex_Graph`): Nodes representing all sites contributing opinions
- **Error information**: Composition errors discovered during index construction
- **Prim stack**: Strong-to-weak ordered list of specs (in non-USD mode)
- **Payload state**: Which payloads are included
- **Instanceability**: Whether this prim can be instanced

**Key insight**: A PrimIndex answers "what scene description contributes to this prim and in what order?"

### 4. PcpNodeRef - Graph Node Reference

`PcpNodeRef` is a lightweight reference to a node in the prim index graph. Each node represents:

- **Site**: Layer stack + path where opinions exist
- **Arc information**: Type, parent, origin of the composition arc
- **Map function**: Path translation across the arc
- **Namespace depth**: Distance from root in namespace hierarchy
- **Permissions**: Access control flags
- **Spec presence**: Whether this site has actual opinions

## Composition Arc Types

### Arc Types in Strength Order

1. **Root Arc** (`PcpArcTypeRoot`)
   - Special arc for the root node
   - Always present, always strongest

2. **Inherit Arc** (`PcpArcTypeInherit`)
   - Class-based inheritance
   - Allows sharing of scene description across multiple prims
   - Supports "implied inherits" propagation

3. **Variant Arc** (`PcpArcTypeVariant`)
   - Variant set selections
   - Allows switching between different configurations
   - Path includes variant selection: `/Model{variant=high}`

4. **Relocate Arc** (`PcpArcTypeRelocate`)
   - Not a true composition arc
   - Namespace remapping applied during traversal
   - "Salted earth" policy: relocated sources can't contribute opinions

5. **Reference Arc** (`PcpArcTypeReference`)
   - External file references
   - Most common way to assemble scenes from components
   - Supports list-editing (prepend, append, delete, explicit)

6. **Payload Arc** (`PcpArcTypePayload`)
   - Deferred references with opt-in loading
   - Only loaded if in payload inclusion set
   - Supports dynamic file formats with arguments

7. **Specialize Arc** (`PcpArcTypeSpecialize`)
   - Specialized inheritance (weakest)
   - Similar to inherits but evaluated last
   - For "is-a" relationships with minimal override strength

## The Composition Algorithm

### High-Level Flow

```
PcpComputePrimIndex(primPath)
    ├── Validate input path
    ├── Set up asset resolver context
    ├── Pcp_BuildPrimIndex()
    │    ├── Build ancestral opinions
    │    ├── Create root node
    │    ├── Process composition tasks (priority queue)
    │    └── Propagate implied arcs
    ├── Cull nodes with no opinions
    ├── Enforce permissions
    ├── Determine instanceability
    └── Finalize graph
```

### Task-Based Evaluation

The algorithm uses a **priority queue** (max-heap) to process composition arcs in correct strength order:

```cpp
// Task priorities (higher number = higher priority = evaluated first)
EvalNodeRelocations               = 1 << 0   // Strongest
EvalImpliedRelocations            = 1 << 1
EvalNodeReferences                = 1 << 2
EvalNodePayloads                  = 1 << 3
EvalNodeInherits                  = 1 << 4
EvalNodeSpecializes               = 1 << 5
EvalImpliedSpecializes            = 1 << 6
EvalImpliedClasses                = 1 << 7
EvalNodeVariantSets               = 1 << 8-16 // Multiple sub-priorities
EvalNodeDynamicPayloads           = 1 << 17
EvalUnresolvedPrimPathError       = 1 << 18  // Weakest
```

### Building Ancestral Opinions

For any prim, USD first needs its parent's composition:

1. Check if parent index is cached (use it if available)
2. Otherwise, recursively build parent's PrimIndex
3. Clone parent's graph
4. Append child name to all sites in cloned graph
5. Apply restrictions if parent is instanceable

**Example**: To compose `/World/Props/Chair`, USD first composes `/World`, then `/World/Props`, then finally `/World/Props/Chair`.

### Processing Composition Arcs

For each arc type discovered at a node:

#### References
```python
def process_references(node):
    # Compose reference list from layer stack
    refs = compose_references(node.layerStack, node.path)

    for ref in refs:
        # Resolve asset path
        layer = resolve_and_load(ref.assetPath)

        # Create map for path translation
        mapExpr = create_map_expression(ref.primPath, node.path)

        # Add arc to graph
        add_arc(node, layer, ref.primPath, mapExpr, PcpArcTypeReference)
```

#### Payloads
Similar to references but with inclusion checking:
```python
def process_payloads(node):
    # Check if this payload should be included
    if not should_include_payload(node.path):
        return

    # Process like references but as payload arc type
    # Dynamic payloads compose arguments first
```

#### Inherits
```python
def process_inherits(node):
    # Direct inherits from this prim
    inherits = compose_inherits(node.layerStack, node.path)

    # Also handle implied inherits from classes
    propagate_implied_classes(node)
```

#### Variants
```python
def process_variants(node):
    # Discover variant sets
    variantSets = compose_variant_sets(node)

    for vset in variantSets:
        # Find selection (authored or fallback)
        selection = find_variant_selection(vset)

        # Add variant arc with identity mapping
        variant_path = f"{node.path}{{{vset}={selection}}}"
        add_arc(node, node.layerStack, variant_path, identity, PcpArcTypeVariant)
```

## Path Translation and Map Functions

### PcpMapFunction

Represents the transformation applied across arcs:
- **Path mappings**: Source namespace → Target namespace
- **Time offsets**: Layer offset and scale
- **Domain tracking**: Which paths can be mapped
- **Composition**: Functions can be chained
- **Inversion**: Bijective (one-to-one, invertible)

### Translation Examples

#### Simple Reference
```
# Model.usd defines /Model
# Scene.usd references it as /World/Model_1

MapFunction: /Model → /World/Model_1
Forward: /Model/Rig → /World/Model_1/Rig
Reverse: /World/Model_1/Rig → /Model/Rig
```

#### With Relocations
```
# Relocation: /OldName → /NewName
# Reference: @file.usd@</Prim>

Combined MapFunction handles both transformations
```

## Dependency Tracking

### Dependency System Architecture

```cpp
// Dependencies stored as:
LayerStack → Path → Set<PrimIndexPath>

Example:
LayerStack1:
    /Model: {/World/Model_1, /World/Model_2}
    /Model/Mesh: {/World/Model_1/Mesh}
```

### Operations

1. **Recording Dependencies**
   - When PrimIndex is computed, record all sites it uses
   - Include culled dependencies for complete tracking
   - Track dynamic file format dependencies
   - Track expression variable usage

2. **Change Processing**
   - When scene description changes, query dependencies
   - Find all affected prim indexes
   - Invalidate and recompute as needed
   - Cascade changes through dependency graph

3. **Namespace Editing**
   - Use dependencies to find affected references
   - Generate fix-up edits
   - Maintain consistency across layers

## Instance Detection

### What Makes a Prim Instanceable?

A prim can be instanced when multiple prims share identical composition structure:

```cpp
bool is_instanceable(node) {
    return:
        // Must have direct arcs (not just ancestral)
        has_direct_arcs_in_chain(node) &&
        // Must have opinions to share
        node.has_specs() &&
        // Must not vary in structure
        !has_varying_children(node)
}
```

### Instance Key Computation
- Based on composition structure (arc types, sites)
- Excludes non-instanceable nodes
- Prim indexes with same key can share results

## Performance Optimizations

### 1. Multi-Level Caching
- **PrimIndex Cache**: Computed indexes by path
- **LayerStack Cache**: Shared across multiple indexes
- **Variant Selection Cache**: Memoized selection searches
- **Spec Presence Cache**: Pre-computed spec existence

### 2. Parallel Computation
```cpp
// Parents computed serially, children in parallel
_ComputePrimIndexesInParallel(roots) {
    for root in roots:
        compute_serially(root)
        compute_parallel(root.children)
}
```

### 3. Graph Optimizations
- **Duplicate Detection**: Avoid redundant nodes
- **Culling**: Remove nodes with no opinions
- **Finalization**: Compact memory layout
- **Copy-on-Write**: Efficient graph cloning

### 4. Task Queue Optimizations
- **Pre-flight Scanning**: Skip tasks for absent arc types
- **Task Deduplication**: Merge duplicate implied arc tasks
- **Priority Ordering**: Process in strength order

## Special Features

### USD Mode vs Full Mode

**USD Mode** (lighter weight):
- Disables: Relocates, Inherits, Permissions, Symmetry
- Doesn't cache prim stack
- Optimized for USD library usage

**Full Mode**:
- All features enabled
- Full permission checking
- Prim stack caching
- Used by specialized tools

### Dynamic File Formats

File formats that generate content based on arguments:
```python
# Payload with arguments
payload = @procedural.dll:main@</Root> (
    args = {
        "seed": 42,
        "complexity": "high"
    }
)
```
Arguments composed from scene description in strength order.

### Expression Variables

Variables available during composition:
```python
# Reference using expression variable
references = @{SHOW}/assets/model.usd@</Model>
```
Changes to variables invalidate dependent indexes.

## Error Handling

### Error Types

1. **Arc Cycles** (`PcpErrorArcCycle`)
   - Detected when arc would create infinite loop
   - Arc is rejected, composition continues

2. **Invalid Paths** (`PcpErrorInvalidPrimPath`)
   - Target of arc doesn't exist
   - Error recorded, composition continues

3. **Prohibited Namespace** (`PcpErrorArcToProhibitedChild`)
   - Accessing relocated source namespace
   - Entire subtree is prohibited

4. **Unresolved Assets** (`PcpErrorUnresolvedAssetPath`)
   - Reference/payload file can't be found
   - Error recorded, arc skipped

### Error as First-Class Results

Errors are part of the PrimIndex result:
```cpp
PcpPrimIndex index;
// Errors stored in index, not thrown as exceptions
for (const auto& error : index.GetLocalErrors()) {
    // Handle or report error
}
```

## Implementation Details and Gotchas

### 1. Stack Frames for Recursive Calls

Recursive index building uses stack frames to:
- Track parent-child relationships across recursive calls
- Detect cycles across call boundaries
- Share variant selections and payload paths
- Enable duplicate detection across frames

### 2. "Inert" Nodes

Nodes marked inert:
- Don't contribute specs
- Exist as placeholders in graph
- Used for relocated sources
- Enable implied arc propagation

### 3. Namespace Depth

Used for strength comparison:
```cpp
// Counts non-variant path elements
depth("/World/Props/Chair") = 3
depth("/Model{variant=high}/Mesh") = 2  // variant not counted
```

### 4. Variant Selection Paths

Special handling throughout:
- Not considered namespace children
- Identity mapping within same layer stack
- Stripped when computing namespace depth

### 5. Lifeboat Pattern

During changes:
```cpp
// Keep objects alive during change processing
PcpLifeboat lifeboat;
lifeboat.Retain(layerStack);
// ... process changes ...
// Objects destroyed when lifeboat goes out of scope
```

## Practical Example

### Scene Structure
```
# chair.usd
def Xform "Chair" {
    def Mesh "Seat"
    def Mesh "Back"
    def Mesh "Legs"
}

# room.usd
class "_FurnitureClass" {
    float scale = 1.0
}

def Scope "Room" (
    variantSets = ["quality"]
    variants = {
        string quality = "medium"
    }
) {
    variantSet "quality" = {
        "low" {
            def "Chair" (
                references = @chair.usd@</Chair>
            ) {
                float scale = 0.8
            }
        }
        "medium" {
            def "Chair" (
                inherits = </_FurnitureClass>
                references = @chair.usd@</Chair>
            ) {
                float scale = 1.0
            }
        }
        "high" {
            def "Chair" (
                inherits = </_FurnitureClass>
                references = @chair.usd@</Chair>
                payload = @chair_details.usd@</Details>
            ) {
                float scale = 1.2
            }
        }
    }
}
```

### Composition Result for `/Room{quality=medium}/Chair`

```
PrimIndex:
  Node 0: room.usd    /Room{quality=medium}/Chair  (root)
  Node 1: room.usd    /_FurnitureClass             (inherit)
  Node 2: room.usd    /Room{quality=medium}        (variant)
  Node 3: chair.usd   /Chair                       (reference)

Strength order: Local > Inherit > Variant > Reference
```

### Opinion Resolution

For the `scale` attribute:
1. Check local: `room.usd:/Room{quality=medium}/Chair` → scale = 1.0 ✓
2. Would check inherit if no local opinion
3. Would check variant if no inherit opinion
4. Would check reference if no variant opinion

Final value: `scale = 1.0` (from local opinion in variant)

## Summary

OpenUSD's composition engine is a sophisticated system that:

1. **Discovers** scene description from multiple sources via composition arcs
2. **Orders** opinions according to LIVRPS strength rules
3. **Translates** paths across namespace boundaries
4. **Caches** aggressively for performance
5. **Tracks** dependencies for change management
6. **Detects** instances for memory efficiency
7. **Handles** errors gracefully without exceptions
8. **Optimizes** through parallel evaluation and culling

The PCP module implements these features through a task-based architecture with recursive index construction, extensive caching, and careful attention to strength ordering. This enables USD to efficiently compose complex scenes from many sources while maintaining predictable and consistent behavior.

## Key Takeaways for TinyUSDZ Implementation

Based on this investigation, a robust composition implementation should consider:

1. **Recursive Architecture**: Parent composition before children
2. **Task-Based Evaluation**: Priority queue for correct strength ordering
3. **Multi-Level Caching**: Index, layer stack, and selection caches
4. **Path Translation**: Sophisticated mapping across namespace boundaries
5. **Dependency Tracking**: Complete system for change propagation
6. **Error Handling**: Errors as results, not exceptions
7. **Instance Detection**: Share composition for identical structures
8. **Deferred Loading**: Payload inclusion control
9. **Graph Optimization**: Culling, deduplication, finalization
10. **Parallel Evaluation**: Where dependencies allow

The composition engine is the heart of USD's power and flexibility, enabling complex scene assembly while maintaining performance through careful engineering and optimization.