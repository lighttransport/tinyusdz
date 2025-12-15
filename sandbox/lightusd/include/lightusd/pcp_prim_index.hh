// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PCP Prim Index (cached composition result)

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lightusd/path.hh"
#include "lightusd/token.hh"
#include "lightusd/pcp_node.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Layer;
class PcpLayerStack;

/// PrimStackEntry - An entry in the prim stack (layer + path with specs)
struct PrimStackEntry {
    Layer* layer = nullptr;          // Layer containing the spec
    Path path;                       // Path in that layer (may differ due to relocates)
    PcpLayerStack* layer_stack = nullptr;  // Owning layer stack

    bool is_valid() const { return layer != nullptr; }
};

/// PcpErrorType - Types of composition errors
enum class PcpErrorType {
    None = 0,
    ArcCycle,                // Circular composition reference
    UnresolvedReference,     // Asset path could not be resolved
    InvalidPath,             // Malformed prim path
    InvalidVariantSelection, // Selected variant doesn't exist
    SublayerCycle,           // Circular sublayer reference
    InternalError,           // Internal consistency error
};

/// PcpError - A composition error
struct PcpError {
    PcpErrorType type = PcpErrorType::None;
    Path site_path;              // Path where error occurred
    std::string layer_id;        // Layer identifier (if applicable)
    std::string message;         // Human-readable message

    bool is_valid() const { return type != PcpErrorType::None; }

    std::string to_string() const {
        std::string result;
        switch (type) {
            case PcpErrorType::ArcCycle: result = "Cycle"; break;
            case PcpErrorType::UnresolvedReference: result = "UnresolvedRef"; break;
            case PcpErrorType::InvalidPath: result = "InvalidPath"; break;
            case PcpErrorType::InvalidVariantSelection: result = "InvalidVariant"; break;
            case PcpErrorType::SublayerCycle: result = "SublayerCycle"; break;
            case PcpErrorType::InternalError: result = "Internal"; break;
            default: result = "Unknown"; break;
        }
        result += " at " + site_path.prim_part();
        if (!message.empty()) {
            result += ": " + message;
        }
        return result;
    }
};

/// PcpPrimIndex - The cached composition result for a single prim path.
///
/// Contains the composition tree (PcpNode), the prim stack (ordered list of
/// specs from strongest to weakest), and computed child/property names.
class PcpPrimIndex {
public:
    PcpPrimIndex();
    ~PcpPrimIndex();

    // Non-copyable, movable
    PcpPrimIndex(const PcpPrimIndex&) = delete;
    PcpPrimIndex& operator=(const PcpPrimIndex&) = delete;
    PcpPrimIndex(PcpPrimIndex&&) noexcept;
    PcpPrimIndex& operator=(PcpPrimIndex&&) noexcept;

    // ========== Identity ==========

    /// Get the composed prim path
    const Path& path() const;

    /// Check if this index is valid
    bool is_valid() const;

    // ========== Composition Tree ==========

    /// Get the root node of the composition tree
    const PcpNode& root_node() const;

    /// Get mutable root node (for building)
    PcpNode& root_node_mutable();

    /// Get all nodes in the composition tree (flattened, strong to weak)
    std::vector<const PcpNode*> get_all_nodes() const;

    /// Count total nodes in composition tree
    size_t node_count() const;

    // ========== Prim Stack ==========

    /// Get the prim stack (ordered list of specs, strongest first)
    const std::vector<PrimStackEntry>& prim_stack() const;

    /// Get prim stack entry count
    size_t prim_stack_size() const;

    /// Check if prim stack is empty (no specs anywhere)
    bool has_specs() const;

    // ========== Computed Names ==========

    /// Get all child prim names (from all layers, ordered)
    const std::vector<Token>& child_names() const;

    /// Get all property names (from all layers, ordered)
    const std::vector<Token>& property_names() const;

    /// Check if has a specific child
    bool has_child(const Token& name) const;

    /// Check if has a specific property
    bool has_property(const Token& name) const;

    // ========== Flags ==========

    /// Check if this prim has unloaded payloads
    bool has_payloads() const;

    /// Check if this prim is instanceable (can share composition)
    bool is_instanceable() const;

    // ========== Errors ==========

    /// Get composition errors
    const std::vector<PcpError>& errors() const;

    /// Check if there are any errors
    bool has_errors() const;

    /// Add an error (for building)
    void add_error(const PcpError& error);

    // ========== Building ==========

    /// Set the composed path
    void set_path(const Path& path);

    /// Set the root node
    void set_root_node(PcpNode node);

    /// Add a prim stack entry
    void add_prim_stack_entry(const PrimStackEntry& entry);

    /// Set child names
    void set_child_names(std::vector<Token> names);

    /// Set property names
    void set_property_names(std::vector<Token> names);

    /// Set has_payloads flag
    void set_has_payloads(bool value);

    /// Set is_instanceable flag
    void set_is_instanceable(bool value);

    /// Finalize the index after building (compute derived data)
    void finalize();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace v1
} // namespace lightusd
