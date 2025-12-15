// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Layer Registry for loading and caching layers

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/layer.hh"

namespace lightusd {
namespace v1 {

/// LayerRegistry manages layer loading and caching.
/// Provides a central point for layer lifecycle management.
class LayerRegistry {
public:
    LayerRegistry();
    ~LayerRegistry();

    // Non-copyable, movable
    LayerRegistry(const LayerRegistry&) = delete;
    LayerRegistry& operator=(const LayerRegistry&) = delete;
    LayerRegistry(LayerRegistry&&) noexcept;
    LayerRegistry& operator=(LayerRegistry&&) noexcept;

    /// Load or retrieve a cached layer by identifier.
    /// @param identifier Layer identifier (file path or anonymous ID)
    /// @return Pointer to layer on success, error on failure
    Result<Layer*> find_or_open(const std::string& identifier);

    /// Find a layer without loading it.
    /// @param identifier Layer identifier
    /// @return Pointer to layer if found, nullptr otherwise
    Layer* find(const std::string& identifier) const;

    /// Check if a layer is loaded.
    /// @param identifier Layer identifier
    /// @return true if layer is loaded
    bool contains(const std::string& identifier) const;

    /// Resolve an asset path relative to an anchor layer.
    /// Handles relative paths, search paths, and URI schemes.
    /// @param asset_path The asset path to resolve
    /// @param anchor_layer The layer containing the reference (for relative paths)
    /// @return Resolved absolute path
    std::string resolve_asset_path(const std::string& asset_path,
                                   const Layer* anchor_layer) const;

    /// Reload a layer from disk.
    /// Invalidates any cached data derived from this layer.
    /// @param identifier Layer identifier
    /// @return Success or error
    Result<void> reload(const std::string& identifier);

    /// Remove a layer from the registry.
    /// @param identifier Layer identifier
    /// @return true if layer was found and removed
    bool remove(const std::string& identifier);

    /// Get all loaded layer identifiers.
    /// @return Vector of layer identifiers
    std::vector<std::string> get_all_identifiers() const;

    /// Get all loaded layers.
    /// @return Vector of layer pointers
    std::vector<Layer*> get_all_layers() const;

    /// Get the number of loaded layers.
    /// @return Number of layers
    size_t size() const;

    /// Clear all loaded layers.
    void clear();

    /// Add search paths for asset resolution.
    /// @param paths Search paths to add
    void add_search_paths(const std::vector<std::string>& paths);

    /// Set search paths for asset resolution (replaces existing).
    /// @param paths New search paths
    void set_search_paths(const std::vector<std::string>& paths);

    /// Get current search paths.
    /// @return Current search paths
    const std::vector<std::string>& search_paths() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace v1
} // namespace lightusd
