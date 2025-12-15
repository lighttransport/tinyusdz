// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PCP Cache (main composition entry point)

#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/path.hh"
#include "lightusd/token.hh"
#include "lightusd/pcp_layer_stack.hh"
#include "lightusd/pcp_prim_index.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class LayerRegistry;

/// PcpCache configuration
struct PcpCacheConfig {
    /// Root layer path (required)
    std::string root_layer_path;

    /// Session layer path (optional, for editorial overrides)
    std::string session_layer_path;

    /// Variant fallbacks: map of variant set name -> default selection
    std::map<std::string, std::string> variant_fallbacks;

    /// Whether to include payloads by default
    bool include_payloads = false;

    /// Search paths for asset resolution
    std::vector<std::string> search_paths;
};

/// PcpCache - Main entry point for composition queries.
///
/// The PcpCache manages the composition of a USD scene, caching layer stacks
/// and prim indexes for efficient repeated queries. It provides APIs for:
/// - Computing prim indexes (composed prims)
/// - Controlling payload inclusion
/// - Setting variant selections
/// - Cache invalidation
class PcpCache {
public:
    explicit PcpCache(const PcpCacheConfig& config);
    ~PcpCache();

    // Non-copyable, movable
    PcpCache(const PcpCache&) = delete;
    PcpCache& operator=(const PcpCache&) = delete;
    PcpCache(PcpCache&&) noexcept;
    PcpCache& operator=(PcpCache&&) noexcept;

    // ========== Primary API ==========

    /// Compute or retrieve cached prim index for a path.
    /// This is the main entry point for composition queries.
    /// @param path The prim path to compose
    /// @return Pointer to cached prim index, or error
    Result<const PcpPrimIndex*> compute_prim_index(const Path& path);

    /// Compute prim indexes for multiple paths.
    /// @param paths Paths to compose
    /// @return Map of path to prim index (excludes failed paths)
    std::map<Path, const PcpPrimIndex*> compute_prim_indexes(
        const std::vector<Path>& paths);

    /// Get the root layer stack
    const PcpLayerStack* root_layer_stack() const;

    /// Compute a layer stack for an identifier
    Result<const PcpLayerStack*> compute_layer_stack(
        const PcpLayerStackIdentifier& identifier);

    // ========== Payload Control ==========

    /// Request payload inclusion for specific paths.
    /// @param paths Paths to include payloads for
    void request_payloads(const std::vector<Path>& paths);

    /// Request payload exclusion for specific paths.
    /// @param paths Paths to exclude payloads for
    void request_payloads_exclusion(const std::vector<Path>& paths);

    /// Check if payloads are included for a path.
    /// @param path Path to check
    /// @return true if payloads are included
    bool is_payload_included(const Path& path) const;

    /// Get all paths with included payloads.
    std::vector<Path> get_included_payloads() const;

    // ========== Variant Control ==========

    /// Set variant selection for a prim.
    /// @param prim_path Path of the prim with the variant set
    /// @param variant_set Name of the variant set
    /// @param selection Name of the variant to select
    void set_variant_selection(const Path& prim_path,
                               const Token& variant_set,
                               const Token& selection);

    /// Get variant selection for a prim.
    /// @param prim_path Path of the prim
    /// @param variant_set Name of the variant set
    /// @return Selected variant, or empty if not set
    Token get_variant_selection(const Path& prim_path,
                                const Token& variant_set) const;

    /// Get variant fallback for a variant set.
    /// @param variant_set Name of the variant set
    /// @return Fallback selection, or empty if not set
    Token get_variant_fallback(const Token& variant_set) const;

    /// Clear variant selection for a prim.
    /// @param prim_path Path of the prim
    /// @param variant_set Name of the variant set (empty = clear all)
    void clear_variant_selection(const Path& prim_path,
                                 const Token& variant_set = Token());

    // ========== Cache Management ==========

    /// Invalidate cached prim indexes for specific paths.
    /// Also invalidates descendants of these paths.
    /// @param paths Paths to invalidate
    void invalidate(const std::vector<Path>& paths);

    /// Clear all cached prim indexes.
    void clear_prim_indexes();

    /// Clear all cached data (layer stacks and prim indexes).
    void clear();

    /// Get all cached prim paths.
    std::vector<Path> get_cached_prim_paths() const;

    /// Get number of cached prim indexes.
    size_t cached_prim_index_count() const;

    /// Get number of cached layer stacks.
    size_t cached_layer_stack_count() const;

    // ========== Configuration ==========

    /// Get the configuration used to create this cache.
    const PcpCacheConfig& config() const;

    /// Get the layer registry.
    LayerRegistry* layer_registry();
    const LayerRegistry* layer_registry() const;

    // ========== Composition Errors ==========

    /// Get all composition errors from the last compute operations.
    const std::vector<PcpError>& composition_errors() const;

    /// Check if there are any composition errors.
    bool has_composition_errors() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace v1
} // namespace lightusd
