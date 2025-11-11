// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP Cache implementation with BLAKE3 hashing

#include "pcp-cache.hh"
#include "pcp-prim-index.hh"
#include "pcp-compose-site.hh"

// BLAKE3 hash function
// Using official BLAKE3 C implementation
// https://github.com/BLAKE3-team/BLAKE3
extern "C" {
#include "../../../deps/blake3/blake3.h"
}

#include <chrono>
#include <sstream>
#include <algorithm>

namespace tinyusdz {
namespace tydra {
namespace pcp {

// Implementation details
class Cache::Impl {
public:
    // Compute BLAKE3 hash for instance key
    static InstanceKey ComputeInstanceKey(const PrimIndex& prim_index) {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        // Hash the composition structure
        HashPrimIndex(hasher, prim_index);

        // Finalize hash
        InstanceKey key;
        key.blake3_hash.resize(BLAKE3_OUT_LEN);
        blake3_hasher_finalize(&hasher, key.blake3_hash.data(), BLAKE3_OUT_LEN);

        return key;
    }

private:
    static void HashPrimIndex(blake3_hasher& hasher, const PrimIndex& prim_index) {
        // Hash nodes in strength order
        for (const auto& node : prim_index.GetNodesInStrengthOrder()) {
            if (node.IsInert() || node.IsCulled() || !node.IsInstanceable()) {
                continue;
            }

            // Hash site information
            HashSite(hasher, node.GetSite());

            // Hash arc information
            HashArc(hasher, node.GetArc());

            // Hash flags (only instanceable-relevant flags)
            uint32_t flags = 0;
            if (node.HasSpecs()) flags |= 1 << 0;
            if (node.IsImplied()) flags |= 1 << 1;
            if (node.IsAncestral()) flags |= 1 << 2;
            blake3_hasher_update(&hasher, &flags, sizeof(flags));
        }
    }

    static void HashSite(blake3_hasher& hasher, const Site& site) {
        // Hash layer stack identifier
        std::string stack_id = site.layer_stack->GetIdentifier();
        blake3_hasher_update(&hasher, stack_id.data(), stack_id.size());

        // Hash path
        std::string path_str = site.path.full_path_name();
        blake3_hasher_update(&hasher, path_str.data(), path_str.size());
    }

    static void HashArc(blake3_hasher& hasher, const Arc& arc) {
        // Hash arc type
        uint32_t arc_type = static_cast<uint32_t>(arc.type);
        blake3_hasher_update(&hasher, &arc_type, sizeof(arc_type));

        // Hash sibling number
        blake3_hasher_update(&hasher, &arc.sibling_num_at_origin,
                             sizeof(arc.sibling_num_at_origin));

        // Hash namespace depth
        blake3_hasher_update(&hasher, &arc.namespace_depth,
                             sizeof(arc.namespace_depth));

        // Hash layer offset if not identity
        if (!arc.layer_offset.IsIdentity()) {
            blake3_hasher_update(&hasher, &arc.layer_offset.offset,
                                 sizeof(arc.layer_offset.offset));
            blake3_hasher_update(&hasher, &arc.layer_offset.scale,
                                 sizeof(arc.layer_offset.scale));
        }
    }
};

// Hash implementation for InstanceKey
size_t InstanceKey::Hash::operator()(const InstanceKey& key) const {
    // Use first 8 bytes of BLAKE3 hash as size_t
    size_t result = 0;
    if (key.blake3_hash.size() >= sizeof(size_t)) {
        std::memcpy(&result, key.blake3_hash.data(), sizeof(size_t));
    }
    return result;
}

// Hash implementation for Site
size_t Site::Hash::operator()(const Site& site) const {
    size_t h1 = std::hash<void*>{}(site.layer_stack.get());
    size_t h2 = std::hash<std::string>{}(site.path.full_path_name());
    return h1 ^ (h2 << 1);
}

// Cache implementation
Cache::Cache(const CacheConfig& config)
    : config_(config),
      dependencies_(std::make_unique<Dependencies>()) {

    // Create root layer stack if we have a root layer
    if (config_.root_layer) {
        root_layer_stack_ = LayerStack::Create(
            config_.root_layer,
            config_.session_layer,
            CreateLayerStackIdentifier(config_.root_layer, config_.session_layer));

        layer_stack_registry_[root_layer_stack_->GetIdentifier()] = root_layer_stack_;
    }
}

Cache::~Cache() = default;

LayerStackPtr Cache::GetRootLayerStack() {
    return root_layer_stack_;
}

LayerStackPtr Cache::GetLayerStack(const std::string& identifier) {
    auto it = layer_stack_registry_.find(identifier);
    if (it != layer_stack_registry_.end()) {
        return it->second;
    }
    return nullptr;
}

PrimIndexPtr Cache::ComputePrimIndex(
    const Path& prim_path,
    ComputePrimIndexOptions options,
    std::vector<Error>* errors) {

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(prim_index_mutex_);
        auto it = prim_index_cache_.find(prim_path);
        if (it != prim_index_cache_.end()) {
            if (errors && it->second) {
                *errors = it->second->GetLocalErrors();
            }
            return it->second;
        }
    }

    // Build new prim index
    std::vector<Error> local_errors;
    auto prim_index = BuildPrimIndex(prim_path, options, local_errors);

    if (prim_index) {
        // Cache the result
        {
            std::lock_guard<std::mutex> lock(prim_index_mutex_);
            prim_index_cache_[prim_path] = prim_index;
        }

        // Register dependencies
        RegisterPrimIndexDependencies(prim_path, *prim_index);

        // Compute instance key if enabled
        if (config_.enable_instancing && options.compute_instanceability) {
            ComputeInstanceKey(prim_path, *prim_index);
        }

        // Update statistics
        stats_.num_prim_indexes++;
    }

    if (errors) {
        *errors = local_errors;
    }

    return prim_index;
}

PrimIndexPtr Cache::GetPrimIndex(const Path& prim_path) const {
    std::lock_guard<std::mutex> lock(prim_index_mutex_);
    auto it = prim_index_cache_.find(prim_path);
    return (it != prim_index_cache_.end()) ? it->second : nullptr;
}

bool Cache::HasPrimIndex(const Path& prim_path) const {
    std::lock_guard<std::mutex> lock(prim_index_mutex_);
    return prim_index_cache_.find(prim_path) != prim_index_cache_.end();
}

void Cache::InvalidatePrimIndex(const Path& prim_path) {
    // Remove from cache
    {
        std::lock_guard<std::mutex> lock(prim_index_mutex_);
        prim_index_cache_.erase(prim_path);
    }

    // Remove dependencies
    dependencies_->RemovePrimDependencies(prim_path);

    // Remove instance key
    instance_key_cache_.erase(prim_path);

    // Invalidate dependent prim indexes
    InvalidateDependentPrimIndexes(prim_path);
}

void Cache::Clear() {
    std::lock_guard<std::mutex> lock(prim_index_mutex_);
    prim_index_cache_.clear();
    instance_key_cache_.clear();
    instance_to_prims_.clear();
    dependencies_->Clear();
    stats_ = CompositionStatistics();
}

bool Cache::IsPayloadIncluded(const Path& prim_path) const {
    if (!config_.included_payloads.empty()) {
        if (config_.included_payloads.count(prim_path) > 0) {
            return true;
        }
    }

    if (config_.payload_predicate) {
        return config_.payload_predicate(prim_path);
    }

    return false;
}

std::optional<InstanceKey> Cache::GetInstanceKey(const Path& prim_path) const {
    auto it = instance_key_cache_.find(prim_path);
    if (it != instance_key_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Path> Cache::FindInstancedPrims(const InstanceKey& key) const {
    auto it = instance_to_prims_.find(key);
    if (it != instance_to_prims_.end()) {
        return it->second;
    }
    return {};
}

CompositionStatistics Cache::GetStatistics() const {
    stats_.num_layer_stacks = layer_stack_registry_.size();
    return stats_;
}

void Cache::ResetStatistics() {
    stats_ = CompositionStatistics();
}

// Private implementation methods

PrimIndexPtr Cache::BuildPrimIndex(
    const Path& prim_path,
    const ComputePrimIndexOptions& options,
    std::vector<Error>& errors) {

    auto start_time = std::chrono::high_resolution_clock::now();

    // Use PrimIndexBuilder to construct the index
    PrimIndexBuilder builder(this, prim_path, options);
    auto prim_index = builder.Build(errors);

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    // Update statistics
    if (prim_index) {
        auto index_stats = prim_index->GetStatistics();
        stats_.num_nodes_created += index_stats.num_nodes;
        stats_.num_nodes_culled += index_stats.num_culled_nodes;
        stats_.num_arcs_processed += index_stats.num_references +
                                     index_stats.num_payloads +
                                     index_stats.num_inherits +
                                     index_stats.num_specializes +
                                     index_stats.num_variants;
        stats_.num_errors += index_stats.num_errors;
        stats_.time_elapsed_seconds += elapsed.count();
    }

    return prim_index;
}

void Cache::RegisterPrimIndexDependencies(
    const Path& prim_path,
    const PrimIndex& prim_index) {

    dependencies_->AddPrimIndexDependencies(prim_path, prim_index);
}

void Cache::ComputeInstanceKey(
    const Path& prim_path,
    const PrimIndex& prim_index) {

    if (!prim_index.IsInstanceable()) {
        return;
    }

    // Compute BLAKE3 hash of composition structure
    InstanceKey key = Impl::ComputeInstanceKey(prim_index);

    // Store in cache
    instance_key_cache_[prim_path] = key;
    instance_to_prims_[key].push_back(prim_path);
}

void Cache::InvalidateDependentPrimIndexes(const Path& prim_path) {
    // Find all prims that depend on this path
    Site site{root_layer_stack_, prim_path};
    auto dependent_prims = dependencies_->GetPrimsDependingOnSiteOrDescendants(site);

    for (const auto& dep_path : dependent_prims) {
        InvalidatePrimIndex(dep_path);
    }
}

std::string Cache::DumpToString() const {
    std::ostringstream ss;
    ss << "PCP Cache:\n";
    ss << "  Root Layer: " << (config_.root_layer ? config_.root_layer->GetIdentifier() : "none") << "\n";
    ss << "  Session Layer: " << (config_.session_layer ? config_.session_layer->GetIdentifier() : "none") << "\n";
    ss << "  USD Mode: " << (config_.usd_mode ? "yes" : "no") << "\n";
    ss << "  Instancing: " << (config_.enable_instancing ? "enabled" : "disabled") << "\n";
    ss << "  Cached Prim Indexes: " << prim_index_cache_.size() << "\n";
    ss << "  Cached Layer Stacks: " << layer_stack_registry_.size() << "\n";
    ss << "  Instance Keys: " << instance_key_cache_.size() << "\n";

    auto stats = GetStatistics();
    ss << "  Statistics:\n";
    ss << "    Nodes Created: " << stats.num_nodes_created << "\n";
    ss << "    Nodes Culled: " << stats.num_nodes_culled << "\n";
    ss << "    Arcs Processed: " << stats.num_arcs_processed << "\n";
    ss << "    Errors: " << stats.num_errors << "\n";
    ss << "    Time Elapsed: " << stats.time_elapsed_seconds << " seconds\n";

    return ss.str();
}

// Cache Factory implementations

std::unique_ptr<Cache> CacheFactory::CreateUsdCache(
    Layer* root_layer,
    Layer* session_layer) {

    CacheConfig config;
    config.root_layer = root_layer;
    config.session_layer = session_layer;
    config.usd_mode = true;  // USD mode
    config.enable_instancing = true;

    return std::make_unique<Cache>(config);
}

std::unique_ptr<Cache> CacheFactory::CreateFullCache(
    Layer* root_layer,
    Layer* session_layer) {

    CacheConfig config;
    config.root_layer = root_layer;
    config.session_layer = session_layer;
    config.usd_mode = false;  // Full features
    config.enable_instancing = true;
    config.enforce_permissions = true;

    return std::make_unique<Cache>(config);
}

std::unique_ptr<Cache> CacheFactory::CreateCache(const CacheConfig& config) {
    return std::make_unique<Cache>(config);
}

} // namespace pcp
} // namespace tydra
} // namespace tinyusdz