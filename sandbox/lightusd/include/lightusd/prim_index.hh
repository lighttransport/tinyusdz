// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PrimIndex (composed prim result)
//
// PrimIndex is the result of composing a prim from multiple layers
// and composition arcs. It follows USD's LIVRPS strength ordering:
//   L - Local (direct opinions in root layer)
//   I - Inherits
//   V - VariantSets
//   R - References
//   P - Payloads
//   S - Specializes
//
// PrimIndex stores the composition graph and provides access to
// the composed (flattened) prim data.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/path.hh"
#include "lightusd/composition.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Layer;
class Prim;

/// CompositionNode - a node in the composition graph
/// Each node represents a contributing layer/prim spec
struct CompositionNode {
    CompositionArcType arc_type = CompositionArcType::None;
    Path path;                    // Path in source layer
    std::string layer_id;         // Source layer identifier
    LayerOffset layer_offset;     // Time offset/scale
    int depth = 0;                // Depth in composition tree

    /// Child nodes (for nested composition)
    std::vector<CompositionNode> children;

    CompositionNode() = default;
    CompositionNode(CompositionArcType type, const Path& p, const std::string& layer)
        : arc_type(type), path(p), layer_id(layer) {}
};

/// PrimIndex - the composed result of a prim
/// Contains the composition graph and provides access to composed data
class PrimIndex {
public:
    PrimIndex();
    ~PrimIndex();
    PrimIndex(const PrimIndex& other);
    PrimIndex(PrimIndex&& other) noexcept;
    PrimIndex& operator=(const PrimIndex& other);
    PrimIndex& operator=(PrimIndex&& other) noexcept;

    // ========== Identity ==========

    /// Get the composed prim path
    const Path& path() const;

    /// Set path
    void set_path(const Path& path);

    /// Check if this prim index is valid (has been computed)
    bool is_valid() const;

    // ========== Composition Graph ==========

    /// Get the root composition node
    const CompositionNode& root_node() const;

    /// Get mutable root node (for building)
    CompositionNode& root_node_mutable();

    /// Get all composition nodes (flattened)
    std::vector<const CompositionNode*> all_nodes() const;

    /// Get number of contributing layers
    size_t layer_count() const;

    // ========== Composition Errors ==========

    /// Check if there are composition errors
    bool has_errors() const;

    /// Get composition error messages
    const std::vector<std::string>& errors() const;

    /// Add an error message
    void add_error(const std::string& error);

    /// Clear errors
    void clear_errors();

    // ========== Utility ==========

    /// Clear all data
    void clear();

    /// Swap contents
    void swap(PrimIndex& other) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Swap specialization
inline void swap(PrimIndex& a, PrimIndex& b) noexcept {
    a.swap(b);
}

/// PrimIndexCache - caches PrimIndex results for a stage
/// This is similar to USD's Pcp (Prim Cache Population)
class PrimIndexCache {
public:
    PrimIndexCache();
    ~PrimIndexCache();
    PrimIndexCache(const PrimIndexCache&) = delete;  // Not copyable
    PrimIndexCache& operator=(const PrimIndexCache&) = delete;

    /// Get cached PrimIndex for a path (returns nullptr if not cached)
    const PrimIndex* get(const Path& path) const;

    /// Insert PrimIndex into cache (takes ownership)
    void insert(const Path& path, PrimIndex index);

    /// Remove from cache
    bool remove(const Path& path);

    /// Clear all cached indices
    void clear();

    /// Get number of cached indices
    size_t size() const;

    /// Check if path is cached
    bool contains(const Path& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace v1
} // namespace lightusd
