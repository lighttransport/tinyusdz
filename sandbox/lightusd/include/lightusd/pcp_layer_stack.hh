// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PCP Layer Stack (resolved stack of layers)

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/path.hh"
#include "lightusd/composition.hh"
#include "lightusd/layer.hh"

namespace lightusd {
namespace v1 {

// Forward declaration
class LayerRegistry;

/// PcpLayerStackIdentifier - Uniquely identifies a layer stack
struct PcpLayerStackIdentifier {
    /// Root layer path (required)
    std::string root_layer_path;

    /// Session layer path (optional, for editorial overrides)
    std::string session_layer_path;

    /// Comparison for use as map key
    bool operator==(const PcpLayerStackIdentifier& other) const {
        return root_layer_path == other.root_layer_path &&
               session_layer_path == other.session_layer_path;
    }

    bool operator<(const PcpLayerStackIdentifier& other) const {
        if (root_layer_path != other.root_layer_path) {
            return root_layer_path < other.root_layer_path;
        }
        return session_layer_path < other.session_layer_path;
    }

    /// Check if identifier is valid
    bool is_valid() const { return !root_layer_path.empty(); }

    /// Create string representation
    std::string to_string() const {
        if (session_layer_path.empty()) {
            return root_layer_path;
        }
        return root_layer_path + " [session: " + session_layer_path + "]";
    }
};

/// RelocatesMap - Maps source paths to target paths for namespace remapping
using RelocatesMap = std::map<Path, Path>;

/// PcpLayerStack - A resolved stack of layers contributing opinions.
///
/// The layer stack represents the ordered set of layers that contribute
/// opinions at a particular site. Layers are ordered from strongest to
/// weakest (session layers first, then root and sublayers).
///
/// Layer stacks also contain composed relocates that apply to all
/// lookups within this stack.
class PcpLayerStack {
public:
    PcpLayerStack();
    ~PcpLayerStack();

    // Non-copyable, movable
    PcpLayerStack(const PcpLayerStack&) = delete;
    PcpLayerStack& operator=(const PcpLayerStack&) = delete;
    PcpLayerStack(PcpLayerStack&&) noexcept;
    PcpLayerStack& operator=(PcpLayerStack&&) noexcept;

    // ========== Identity ==========

    /// Get the identifier for this layer stack
    const PcpLayerStackIdentifier& identifier() const;

    /// Check if layer stack is valid (has at least root layer)
    bool is_valid() const;

    // ========== Layers ==========

    /// Get all layers in strength order (strongest first)
    /// Session layers come before root/sublayers
    const std::vector<Layer*>& layers() const;

    /// Get number of layers
    size_t layer_count() const;

    /// Get layer by index
    Layer* layer(size_t index) const;

    /// Get the root layer
    Layer* root_layer() const;

    /// Get the session layer (may be null)
    Layer* session_layer() const;

    /// Check if a layer is part of this stack
    bool contains_layer(const Layer* layer) const;

    /// Get layer offset for a specific layer
    LayerOffset get_layer_offset(size_t index) const;

    /// Get layer offset for a specific layer pointer
    LayerOffset get_layer_offset(const Layer* layer) const;

    // ========== Relocates ==========

    /// Get relocates mapping source paths to target paths
    const RelocatesMap& relocates_source_to_target() const;

    /// Get relocates mapping target paths to source paths (inverse)
    const RelocatesMap& relocates_target_to_source() const;

    /// Check if there are any relocates
    bool has_relocates() const;

    /// Apply relocates to a path (source → target)
    /// Returns the relocated path, or original if no relocate applies
    Path apply_relocates(const Path& source_path) const;

    /// Reverse relocates on a path (target → source)
    /// Returns the original path, or input if no relocate applies
    Path unapply_relocates(const Path& target_path) const;

    // ========== Composed Metadata ==========

    /// Get composed timeCodesPerSecond (default 24.0)
    double time_codes_per_second() const;

    /// Get composed framesPerSecond (default 24.0)
    double frames_per_second() const;

    /// Get composed startTimeCode (default 0.0)
    double start_time_code() const;

    /// Get composed endTimeCode (default 0.0)
    double end_time_code() const;

    // ========== Spec Lookup ==========

    /// Check if any layer has a prim spec at the given path
    bool has_prim_spec(const Path& path) const;

    /// Get all layers that have a prim spec at the given path
    std::vector<Layer*> get_layers_with_prim_spec(const Path& path) const;

    // ========== Building ==========

    /// Build a layer stack from an identifier.
    /// Loads root layer and all sublayers, composes relocates.
    /// @param identifier The layer stack identifier
    /// @param registry Layer registry for loading layers
    /// @param errors Output for any errors encountered
    /// @return New layer stack, or nullptr on failure
    static std::unique_ptr<PcpLayerStack> Build(
        const PcpLayerStackIdentifier& identifier,
        LayerRegistry* registry,
        std::vector<std::string>* errors = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace v1
} // namespace lightusd
