// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP Dependencies Implementation - Dependency tracking for composition

#include "pcp-dependencies.hh"
#include "pcp-prim-index.hh"
#include "pcp-node.hh"
#include <algorithm>
#include <queue>
#include <sstream>

namespace tinyusdz {
namespace tydra {
namespace pcp {

// Dependencies Implementation

Dependencies::Dependencies() = default;
Dependencies::~Dependencies() = default;

void Dependencies::AddPrimIndexDependencies(
    const Path& prim_path,
    const PrimIndex& prim_index) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Collect all sites used by this prim index
    std::vector<Site> sites;
    std::vector<Site> culled_sites;

    CollectDependenciesFromPrimIndex(prim_index, sites, culled_sites);

    // Add forward dependencies (site -> prim)
    for (const auto& site : sites) {
        AddSiteDependencyInternal(prim_path, site, false);
    }

    // Add culled dependencies for change tracking
    for (const auto& site : culled_sites) {
        AddSiteDependencyInternal(prim_path, site, true);
    }

    // Store reverse dependencies (prim -> sites)
    prim_to_sites_[prim_path] = std::move(sites);

    if (!culled_sites.empty()) {
        prim_culled_sites_[prim_path] = std::move(culled_sites);
    }
}

void Dependencies::AddDependency(
    const Path& prim_path,
    const Site& site,
    DependencyType type,
    bool is_culled) {

    std::lock_guard<std::mutex> lock(mutex_);
    AddSiteDependencyInternal(prim_path, site, is_culled);

    // Add to reverse map
    auto& sites = is_culled ? prim_culled_sites_[prim_path] : prim_to_sites_[prim_path];
    sites.push_back(site);
}

void Dependencies::AddExpressionDependency(
    const Path& prim_path,
    const std::string& variable_name) {

    std::lock_guard<std::mutex> lock(mutex_);

    expr_to_prims_[variable_name].insert(prim_path);
    prim_to_exprs_[prim_path].insert(variable_name);
}

void Dependencies::AddPayloadDependency(
    const Path& prim_path,
    const Path& payload_path) {

    std::lock_guard<std::mutex> lock(mutex_);

    payload_to_prims_[payload_path].insert(prim_path);
    prim_to_payloads_[prim_path].insert(payload_path);
}

void Dependencies::AddVariantDependency(
    const Path& prim_path,
    const Path& variant_prim_path,
    const std::string& variant_set) {

    std::lock_guard<std::mutex> lock(mutex_);

    VariantKey key{variant_prim_path, variant_set};
    variant_to_prims_[key].insert(prim_path);
    prim_to_variants_[prim_path].push_back(key);
}

void Dependencies::RemovePrimDependencies(
    const Path& prim_path,
    Lifeboat* lifeboat) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Remove site dependencies
    auto sites_it = prim_to_sites_.find(prim_path);
    if (sites_it != prim_to_sites_.end()) {
        // Remove from forward map (site -> prims)
        for (const auto& site : sites_it->second) {
            auto layer_it = site_to_prims_.find(site.layer_stack);
            if (layer_it != site_to_prims_.end()) {
                auto path_it = layer_it->second.find(site.path);
                if (path_it != layer_it->second.end()) {
                    path_it->second.erase(prim_path);

                    // Clean up empty entries
                    if (path_it->second.empty()) {
                        layer_it->second.erase(path_it);
                    }
                }

                if (layer_it->second.empty()) {
                    site_to_prims_.erase(layer_it);
                }
            }
        }

        // Retain layer stacks in lifeboat if requested
        if (lifeboat) {
            for (const auto& site : sites_it->second) {
                lifeboat->Retain(site.layer_stack);
            }
        }

        prim_to_sites_.erase(sites_it);
    }

    // Remove culled site dependencies
    auto culled_it = prim_culled_sites_.find(prim_path);
    if (culled_it != prim_culled_sites_.end()) {
        if (lifeboat) {
            for (const auto& site : culled_it->second) {
                lifeboat->Retain(site.layer_stack);
            }
        }
        prim_culled_sites_.erase(culled_it);
    }

    // Remove expression dependencies
    auto expr_it = prim_to_exprs_.find(prim_path);
    if (expr_it != prim_to_exprs_.end()) {
        for (const auto& var : expr_it->second) {
            expr_to_prims_[var].erase(prim_path);
            if (expr_to_prims_[var].empty()) {
                expr_to_prims_.erase(var);
            }
        }
        prim_to_exprs_.erase(expr_it);
    }

    // Remove payload dependencies
    auto payload_it = prim_to_payloads_.find(prim_path);
    if (payload_it != prim_to_payloads_.end()) {
        for (const auto& payload : payload_it->second) {
            payload_to_prims_[payload].erase(prim_path);
            if (payload_to_prims_[payload].empty()) {
                payload_to_prims_.erase(payload);
            }
        }
        prim_to_payloads_.erase(payload_it);
    }

    // Remove variant dependencies
    auto variant_it = prim_to_variants_.find(prim_path);
    if (variant_it != prim_to_variants_.end()) {
        for (const auto& key : variant_it->second) {
            variant_to_prims_[key].erase(prim_path);
            if (variant_to_prims_[key].empty()) {
                variant_to_prims_.erase(key);
            }
        }
        prim_to_variants_.erase(variant_it);
    }
}

std::vector<Path> Dependencies::GetPrimsDependingOnSite(
    const Site& site) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto layer_it = site_to_prims_.find(site.layer_stack);
    if (layer_it != site_to_prims_.end()) {
        auto path_it = layer_it->second.find(site.path);
        if (path_it != layer_it->second.end()) {
            return std::vector<Path>(
                path_it->second.begin(),
                path_it->second.end());
        }
    }

    return {};
}

std::vector<Path> Dependencies::GetPrimsDependingOnSiteOrAncestors(
    const Site& site) const {

    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_set<Path> result_set;

    // Check site and all ancestors
    Path current = site.path;
    while (current.is_valid()) {
        Site ancestor_site{site.layer_stack, current};

        auto layer_it = site_to_prims_.find(ancestor_site.layer_stack);
        if (layer_it != site_to_prims_.end()) {
            auto path_it = layer_it->second.find(ancestor_site.path);
            if (path_it != layer_it->second.end()) {
                result_set.insert(path_it->second.begin(), path_it->second.end());
            }
        }

        if (current.is_root_path()) {
            break;
        }
        current = current.GetParentPath();
    }

    return std::vector<Path>(result_set.begin(), result_set.end());
}

std::vector<Path> Dependencies::GetPrimsDependingOnSiteOrDescendants(
    const Site& site) const {

    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_set<Path> result_set;

    auto layer_it = site_to_prims_.find(site.layer_stack);
    if (layer_it != site_to_prims_.end()) {
        // Check exact site
        auto path_it = layer_it->second.find(site.path);
        if (path_it != layer_it->second.end()) {
            result_set.insert(path_it->second.begin(), path_it->second.end());
        }

        // Check all descendants
        for (const auto& [path, prims] : layer_it->second) {
            if (path.HasPrefix(site.path)) {
                result_set.insert(prims.begin(), prims.end());
            }
        }
    }

    return std::vector<Path>(result_set.begin(), result_set.end());
}

void Dependencies::ForEachDependencyOnSite(
    const Site& site,
    std::function<void(const Path&)> visitor) const {

    auto prims = GetPrimsDependingOnSite(site);
    for (const auto& prim : prims) {
        visitor(prim);
    }
}

void Dependencies::ForEachDependencyOnSiteOrAncestors(
    const Site& site,
    std::function<void(const Path&)> visitor) const {

    auto prims = GetPrimsDependingOnSiteOrAncestors(site);
    for (const auto& prim : prims) {
        visitor(prim);
    }
}

void Dependencies::ForEachDependencyOnSiteOrDescendants(
    const Site& site,
    std::function<void(const Path&)> visitor) const {

    auto prims = GetPrimsDependingOnSiteOrDescendants(site);
    for (const auto& prim : prims) {
        visitor(prim);
    }
}

std::vector<Site> Dependencies::GetSitesDependedOnBy(
    const Path& prim_path) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = prim_to_sites_.find(prim_path);
    if (it != prim_to_sites_.end()) {
        return it->second;
    }

    return {};
}

bool Dependencies::HasDependencies(const Path& prim_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return prim_to_sites_.count(prim_path) > 0;
}

bool Dependencies::HasDependents(const Site& site) const {
    auto prims = GetPrimsDependingOnSite(site);
    return !prims.empty();
}

std::vector<Path> Dependencies::GetPrimsDependingOnExpression(
    const std::string& variable_name) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = expr_to_prims_.find(variable_name);
    if (it != expr_to_prims_.end()) {
        return std::vector<Path>(it->second.begin(), it->second.end());
    }

    return {};
}

std::vector<Path> Dependencies::GetPrimsDependingOnPayload(
    const Path& payload_path) const {

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = payload_to_prims_.find(payload_path);
    if (it != payload_to_prims_.end()) {
        return std::vector<Path>(it->second.begin(), it->second.end());
    }

    return {};
}

std::vector<Path> Dependencies::GetPrimsDependingOnVariant(
    const Path& variant_prim_path,
    const std::string& variant_set) const {

    std::lock_guard<std::mutex> lock(mutex_);

    VariantKey key{variant_prim_path, variant_set};
    auto it = variant_to_prims_.find(key);
    if (it != variant_to_prims_.end()) {
        return std::vector<Path>(it->second.begin(), it->second.end());
    }

    return {};
}

void Dependencies::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    site_to_prims_.clear();
    prim_to_sites_.clear();
    expr_to_prims_.clear();
    prim_to_exprs_.clear();
    payload_to_prims_.clear();
    prim_to_payloads_.clear();
    variant_to_prims_.clear();
    prim_to_variants_.clear();
    prim_culled_sites_.clear();
}

Dependencies::Statistics Dependencies::GetStatistics() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Statistics stats;
    stats.num_prim_indexes = prim_to_sites_.size();

    // Count total dependencies
    for (const auto& [prim, sites] : prim_to_sites_) {
        stats.num_dependencies += sites.size();
    }

    // Count unique sites
    std::unordered_set<Site, Site::Hash> unique_sites;
    for (const auto& [layer_stack, path_map] : site_to_prims_) {
        for (const auto& [path, prims] : path_map) {
            unique_sites.insert({layer_stack, path});
        }
    }
    stats.num_sites = unique_sites.size();

    stats.num_expression_deps = expr_to_prims_.size();
    stats.num_payload_deps = payload_to_prims_.size();
    stats.num_variant_deps = variant_to_prims_.size();

    // Count culled dependencies
    for (const auto& [prim, sites] : prim_culled_sites_) {
        stats.num_culled_deps += sites.size();
    }

    return stats;
}

std::string Dependencies::DumpToString() const {
    std::ostringstream ss;
    auto stats = GetStatistics();

    ss << "Dependencies:\n";
    ss << "  Prim indexes: " << stats.num_prim_indexes << "\n";
    ss << "  Total dependencies: " << stats.num_dependencies << "\n";
    ss << "  Unique sites: " << stats.num_sites << "\n";
    ss << "  Expression deps: " << stats.num_expression_deps << "\n";
    ss << "  Payload deps: " << stats.num_payload_deps << "\n";
    ss << "  Variant deps: " << stats.num_variant_deps << "\n";
    ss << "  Culled deps: " << stats.num_culled_deps << "\n";

    return ss.str();
}

void Dependencies::DumpStatistics(std::ostream& out) const {
    out << DumpToString();
}

// Private methods

void Dependencies::AddSiteDependencyInternal(
    const Path& prim_path,
    const Site& site,
    bool is_culled) {

    if (is_culled) {
        // For culled dependencies, we don't add to the forward map
        // They're only tracked in prim_culled_sites_
        return;
    }

    auto path_set = GetOrCreatePathSet(
        site_to_prims_,
        site.layer_stack,
        site.path);

    if (path_set) {
        path_set->insert(prim_path);
    }
}

void Dependencies::RemoveSiteDependencyInternal(
    const Path& prim_path,
    const Site& site) {

    auto layer_it = site_to_prims_.find(site.layer_stack);
    if (layer_it != site_to_prims_.end()) {
        auto path_it = layer_it->second.find(site.path);
        if (path_it != layer_it->second.end()) {
            path_it->second.erase(prim_path);

            if (path_it->second.empty()) {
                layer_it->second.erase(path_it);
            }
        }

        if (layer_it->second.empty()) {
            site_to_prims_.erase(layer_it);
        }
    }
}

Dependencies::PathSet* Dependencies::GetOrCreatePathSet(
    LayerStackToPathMap& map,
    const LayerStackPtr& layer_stack,
    const Path& path) {

    auto& path_map = map[layer_stack];
    auto& path_set = path_map[path];
    return &path_set;
}

void Dependencies::CollectDependenciesFromPrimIndex(
    const PrimIndex& prim_index,
    std::vector<Site>& sites,
    std::vector<Site>& culled_sites) {

    // Collect dependencies from all nodes in the prim index
    for (const auto& node : prim_index.GetNodesInStrengthOrder()) {
        Site site = node.GetSite();

        if (node.IsCulled()) {
            culled_sites.push_back(site);
        } else {
            sites.push_back(site);
        }

        // Also track ancestral dependencies
        Path current = site.path;
        while (!current.is_root_path() && current.is_valid()) {
            current = current.GetParentPath();
            Site ancestor_site{site.layer_stack, current};

            // Check if we've already added this ancestor
            bool already_added = std::any_of(
                sites.begin(), sites.end(),
                [&ancestor_site](const Site& s) {
                    return s == ancestor_site;
                });

            if (!already_added) {
                sites.push_back(ancestor_site);
            }
        }
    }

    // Remove duplicates
    std::sort(sites.begin(), sites.end(),
              [](const Site& a, const Site& b) {
                  if (a.layer_stack != b.layer_stack) {
                      return a.layer_stack < b.layer_stack;
                  }
                  return a.path < b.path;
              });
    sites.erase(std::unique(sites.begin(), sites.end()), sites.end());

    std::sort(culled_sites.begin(), culled_sites.end(),
              [](const Site& a, const Site& b) {
                  if (a.layer_stack != b.layer_stack) {
                      return a.layer_stack < b.layer_stack;
                  }
                  return a.path < b.path;
              });
    culled_sites.erase(std::unique(culled_sites.begin(), culled_sites.end()),
                      culled_sites.end());
}

// TransitiveDependencyCalculator Implementation

TransitiveDependencyCalculator::TransitiveDependencyCalculator(
    const Dependencies& deps)
    : deps_(deps) {
}

std::unordered_set<Path> TransitiveDependencyCalculator::GetTransitiveDependents(
    const Site& site) {

    std::unordered_set<Path> result;
    visited_prims_.clear();
    CollectTransitiveDependents(site, result);
    return result;
}

std::unordered_set<Path> TransitiveDependencyCalculator::GetTransitiveDependents(
    const std::vector<Site>& sites) {

    std::unordered_set<Path> result;
    visited_prims_.clear();

    for (const auto& site : sites) {
        CollectTransitiveDependents(site, result);
    }

    return result;
}

std::unordered_set<Site, Site::Hash>
TransitiveDependencyCalculator::GetTransitiveDependencies(
    const Path& prim_path) {

    std::unordered_set<Site, Site::Hash> result;
    visited_sites_.clear();
    CollectTransitiveDependencies(prim_path, result);
    return result;
}

void TransitiveDependencyCalculator::CollectTransitiveDependents(
    const Site& site,
    std::unordered_set<Path>& result) {

    // Get direct dependents
    auto direct_deps = deps_.GetPrimsDependingOnSite(site);

    for (const auto& prim : direct_deps) {
        if (visited_prims_.insert(prim).second) {
            result.insert(prim);

            // Get sites this prim depends on and recursively collect
            auto sites = deps_.GetSitesDependedOnBy(prim);
            for (const auto& dep_site : sites) {
                if (dep_site != site) {  // Avoid infinite recursion
                    CollectTransitiveDependents(dep_site, result);
                }
            }
        }
    }
}

void TransitiveDependencyCalculator::CollectTransitiveDependencies(
    const Path& prim_path,
    std::unordered_set<Site, Site::Hash>& result) {

    auto sites = deps_.GetSitesDependedOnBy(prim_path);

    for (const auto& site : sites) {
        if (visited_sites_.insert(site).second) {
            result.insert(site);

            // Get prims depending on this site
            auto prims = deps_.GetPrimsDependingOnSite(site);
            for (const auto& prim : prims) {
                if (prim != prim_path) {  // Avoid infinite recursion
                    CollectTransitiveDependencies(prim, result);
                }
            }
        }
    }
}

// ChangeProcessor Implementation

ChangeProcessor::ChangeProcessor(Dependencies& deps)
    : deps_(deps) {
}

ChangeProcessor::Results ChangeProcessor::ProcessLayerChanges(
    const std::vector<std::pair<Layer*, std::vector<Path>>>& layer_changes) {

    Results results;

    for (const auto& [layer, changed_paths] : layer_changes) {
        // Find layer stacks containing this layer
        // (Would need layer stack registry to implement this properly)

        for (const auto& path : changed_paths) {
            // For now, just mark as resynced
            // In full implementation, would determine if structure or property changed
            results.resynced_prims.insert(path);
        }
    }

    return results;
}

ChangeProcessor::Results ChangeProcessor::ProcessExpressionChanges(
    const std::vector<std::string>& changed_variables) {

    Results results;

    for (const auto& var : changed_variables) {
        auto affected = deps_.GetPrimsDependingOnExpression(var);
        results.resynced_prims.insert(affected.begin(), affected.end());
    }

    return results;
}

ChangeProcessor::Results ChangeProcessor::ProcessPayloadChanges(
    const std::unordered_set<Path>& changed_payloads) {

    Results results;

    for (const auto& payload : changed_payloads) {
        auto affected = deps_.GetPrimsDependingOnPayload(payload);
        results.payload_prims.insert(affected.begin(), affected.end());
    }

    return results;
}

ChangeProcessor::Results ChangeProcessor::ProcessVariantChanges(
    const std::vector<VariantKey>& changed_variants) {

    Results results;

    for (const auto& key : changed_variants) {
        auto affected = deps_.GetPrimsDependingOnVariant(
            key.prim_path, key.variant_set);
        results.variant_prims.insert(affected.begin(), affected.end());
    }

    return results;
}

void ChangeProcessor::ProcessSiteChange(
    const Site& site,
    bool is_resync,
    Results& results) {

    if (is_resync) {
        // Structure changed - need to resync
        auto affected = deps_.GetPrimsDependingOnSiteOrDescendants(site);
        results.resynced_prims.insert(affected.begin(), affected.end());
    } else {
        // Properties changed only
        auto affected = deps_.GetPrimsDependingOnSite(site);
        results.changed_prims.insert(affected.begin(), affected.end());
    }
}

} // namespace pcp
} // namespace tydra
} // namespace tinyusdz