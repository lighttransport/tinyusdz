// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PCP Node (composition tree node)

#pragma once

#include <vector>

#include "lightusd/path.hh"
#include "lightusd/composition.hh"

namespace lightusd {
namespace v1 {

// Forward declaration
class PcpLayerStack;

/// PcpNode - A node in the composition tree representing one opinion source.
///
/// The composition tree is built by traversing composition arcs (references,
/// inherits, variants, etc.) and creating nodes for each opinion source.
/// Nodes are ordered by strength according to LIVRPS ordering.
struct PcpNode {
    /// Type of composition arc that introduced this node
    CompositionArcType arc_type = CompositionArcType::None;

    /// Path in the source layer stack (site path)
    Path site_path;

    /// Source layer stack (not owned)
    PcpLayerStack* layer_stack = nullptr;

    /// Time/namespace transformation from this node to parent
    LayerOffset map_to_parent;

    /// Strength ordering among siblings with same arc type (lower = stronger)
    int sibling_index = 0;

    /// Depth from root node
    int depth = 0;

    /// Whether any layer in this node's layer stack has specs at site_path
    bool has_specs = false;

    /// Whether this subtree is culled (no opinions anywhere)
    bool is_culled = false;

    /// Child composition nodes, ordered by strength (strongest first)
    std::vector<PcpNode> children;

    // ========== Convenience Methods ==========

    /// Check if this is the root node
    bool is_root() const { return arc_type == CompositionArcType::None; }

    /// Check if this node was introduced by a class-based arc (inherit/specialize)
    bool is_class_based() const {
        return arc_type == CompositionArcType::Inherit ||
               arc_type == CompositionArcType::Specialize;
    }

    /// Check if this node was introduced by a direct arc (reference/payload)
    bool is_direct_arc() const {
        return arc_type == CompositionArcType::Reference ||
               arc_type == CompositionArcType::Payload;
    }

    /// Get the number of children
    size_t child_count() const { return children.size(); }

    /// Get child by index
    const PcpNode* child(size_t index) const {
        return index < children.size() ? &children[index] : nullptr;
    }

    /// Get mutable child by index
    PcpNode* child_mutable(size_t index) {
        return index < children.size() ? &children[index] : nullptr;
    }

    /// Find child by arc type and site path
    const PcpNode* find_child(CompositionArcType type, const Path& path) const {
        for (const auto& child : children) {
            if (child.arc_type == type && child.site_path == path) {
                return &child;
            }
        }
        return nullptr;
    }

    // ========== Tree Operations ==========

    /// Insert a child node maintaining strength order.
    /// Children are sorted by: arc_type (enum order), then sibling_index.
    void insert_child(PcpNode child) {
        // Find insertion point
        auto it = children.begin();
        while (it != children.end()) {
            // Arc type determines primary ordering (lower enum = stronger)
            if (static_cast<int>(child.arc_type) < static_cast<int>(it->arc_type)) {
                break;
            }
            // Within same arc type, use sibling_index
            if (child.arc_type == it->arc_type && child.sibling_index < it->sibling_index) {
                break;
            }
            ++it;
        }
        children.insert(it, std::move(child));
    }

    /// Recursively count all nodes in subtree (including this node)
    size_t subtree_size() const {
        size_t count = 1;
        for (const auto& child : children) {
            count += child.subtree_size();
        }
        return count;
    }

    /// Recursively check if any node in subtree has specs
    bool subtree_has_specs() const {
        if (has_specs) return true;
        for (const auto& child : children) {
            if (child.subtree_has_specs()) return true;
        }
        return false;
    }
};

/// Compare two nodes by strength (for sorting).
/// Returns true if 'a' is stronger than 'b'.
inline bool compare_node_strength(const PcpNode& a, const PcpNode& b) {
    // Arc type determines primary ordering
    if (a.arc_type != b.arc_type) {
        return static_cast<int>(a.arc_type) < static_cast<int>(b.arc_type);
    }
    // Within same arc type, lower sibling_index is stronger
    return a.sibling_index < b.sibling_index;
}

} // namespace v1
} // namespace lightusd
