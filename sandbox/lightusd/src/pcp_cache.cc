// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PCP Cache implementation

#include "lightusd/pcp_cache.hh"
#include "lightusd/layer_registry.hh"
#include "lightusd/layer.hh"
#include "lightusd/prim.hh"
#include "lightusd/variant.hh"

#include <algorithm>
#include <set>

namespace lightusd {
namespace v1 {

// ============================================================================
// PcpCache::Impl
// ============================================================================

class PcpCache::Impl {
public:
    PcpCacheConfig config_;
    std::unique_ptr<LayerRegistry> layer_registry_;

    // Root layer stack
    std::unique_ptr<PcpLayerStack> root_layer_stack_;

    // Cached layer stacks (keyed by identifier)
    std::map<PcpLayerStackIdentifier, std::unique_ptr<PcpLayerStack>> layer_stack_cache_;

    // Cached prim indexes (keyed by path)
    std::map<Path, std::unique_ptr<PcpPrimIndex>> prim_index_cache_;

    // Payload inclusion set
    std::set<Path> included_payloads_;

    // Variant selections: prim_path -> (variant_set -> selection)
    std::map<Path, std::map<Token, Token>> variant_selections_;

    // Composition errors from last operation
    std::vector<PcpError> composition_errors_;

    // Build root layer stack
    bool build_root_layer_stack(std::vector<std::string>* errors) {
        PcpLayerStackIdentifier id;
        id.root_layer_path = config_.root_layer_path;
        id.session_layer_path = config_.session_layer_path;

        root_layer_stack_ = PcpLayerStack::Build(id, layer_registry_.get(), errors);
        return root_layer_stack_ != nullptr;
    }

    // Get variant selection for a prim/variant set
    Token get_variant_selection(const Path& prim_path, const Token& variant_set) const {
        // Check explicit selections
        auto it = variant_selections_.find(prim_path);
        if (it != variant_selections_.end()) {
            auto vit = it->second.find(variant_set);
            if (vit != it->second.end()) {
                return vit->second;
            }
        }

        // Check fallbacks
        auto fit = config_.variant_fallbacks.find(variant_set.str());
        if (fit != config_.variant_fallbacks.end()) {
            return Token(fit->second);
        }

        return Token();
    }

    // Check if payload is included
    bool is_payload_included(const Path& path) const {
        if (config_.include_payloads) return true;
        return included_payloads_.count(path) > 0;
    }

    // Build composition for a single prim
    void build_composition(PcpPrimIndex* index, const Path& path);

    // Process composition arcs for a node
    void process_arcs(PcpNode& node, PcpPrimIndex* index,
                      std::set<std::pair<Path, const PcpLayerStack*>>& visited);
};

// ============================================================================
// Composition Building
// ============================================================================

void PcpCache::Impl::build_composition(PcpPrimIndex* index, const Path& path) {
    if (!root_layer_stack_) return;

    // Create root node
    PcpNode root_node;
    root_node.arc_type = CompositionArcType::None;
    root_node.site_path = path;
    root_node.layer_stack = root_layer_stack_.get();
    root_node.depth = 0;

    // Track visited sites for cycle detection
    std::set<std::pair<Path, const PcpLayerStack*>> visited;

    // Process composition
    process_arcs(root_node, index, visited);

    // Set root node
    index->set_root_node(std::move(root_node));

    // Build prim stack from composition tree
    std::function<void(const PcpNode&)> collect_prim_stack = [&](const PcpNode& node) {
        if (node.has_specs && node.layer_stack) {
            auto layers = node.layer_stack->get_layers_with_prim_spec(node.site_path);
            for (auto* layer : layers) {
                PrimStackEntry entry;
                entry.layer = layer;
                entry.path = node.site_path;
                entry.layer_stack = node.layer_stack;
                index->add_prim_stack_entry(entry);
            }
        }
        for (const auto& child : node.children) {
            collect_prim_stack(child);
        }
    };
    collect_prim_stack(index->root_node());

    // Collect child and property names
    std::set<Token> child_names;
    std::set<Token> property_names;

    std::function<void(const PcpNode&)> collect_names = [&](const PcpNode& node) {
        if (node.layer_stack) {
            auto layers = node.layer_stack->get_layers_with_prim_spec(node.site_path);
            for (auto* layer : layers) {
                const Prim* prim = layer->get_prim_at_path(node.site_path);
                if (prim) {
                    for (const auto& name : prim->child_names()) {
                        child_names.insert(Token(name));
                    }
                    for (const auto& name : prim->property_names()) {
                        property_names.insert(Token(name));
                    }
                }
            }
        }
        for (const auto& child : node.children) {
            collect_names(child);
        }
    };
    collect_names(index->root_node());

    index->set_child_names(std::vector<Token>(child_names.begin(), child_names.end()));
    index->set_property_names(std::vector<Token>(property_names.begin(), property_names.end()));
}

void PcpCache::Impl::process_arcs(PcpNode& node, PcpPrimIndex* index,
                                   std::set<std::pair<Path, const PcpLayerStack*>>& visited) {
    // Cycle detection
    auto site = std::make_pair(node.site_path, node.layer_stack);
    if (visited.count(site)) {
        PcpError error;
        error.type = PcpErrorType::ArcCycle;
        error.site_path = node.site_path;
        error.message = "Composition cycle detected";
        index->add_error(error);
        return;
    }
    visited.insert(site);

    // Check if specs exist
    if (node.layer_stack) {
        node.has_specs = node.layer_stack->has_prim_spec(node.site_path);
    }

    // Get layers with specs at this site
    if (!node.layer_stack) {
        visited.erase(site);
        return;
    }

    auto layers = node.layer_stack->get_layers_with_prim_spec(node.site_path);

    // Process arcs in LIVRPS order
    int sibling_index = 0;

    // 1. Inherits (strongest after local)
    for (auto* layer : layers) {
        const Prim* prim = layer->get_prim_at_path(node.site_path);
        if (!prim || !prim->has_inherits()) continue;

        const PathList& inherits = prim->inherits();
        auto process_inherit = [&](const Path& target_path) {
            PcpNode child;
            child.arc_type = CompositionArcType::Inherit;
            child.site_path = target_path;
            child.layer_stack = node.layer_stack;
            child.sibling_index = sibling_index++;
            child.depth = node.depth + 1;

            process_arcs(child, index, visited);
            node.insert_child(std::move(child));
        };

        for (const auto& p : inherits.prepended_items()) process_inherit(p);
        if (inherits.has_explicit()) {
            for (const auto& p : inherits.explicit_items()) process_inherit(p);
        }
        for (const auto& p : inherits.appended_items()) process_inherit(p);
    }

    // 2. Variants
    // Collect all variant set names from all layers (union)
    std::set<std::string> all_variant_sets;
    for (auto* layer : layers) {
        const Prim* prim = layer->get_prim_at_path(node.site_path);
        if (prim) {
            for (const auto& vs_name : prim->variant_set_names()) {
                all_variant_sets.insert(vs_name);
            }
        }
    }

    // Process each variant set
    for (const auto& vs_name : all_variant_sets) {
        // Determine the selection: explicit > authored > fallback
        Token selection;

        // Check explicit selection in cache
        auto sel_it = variant_selections_.find(node.site_path.strip_variant_selections());
        if (sel_it != variant_selections_.end()) {
            auto vs_it = sel_it->second.find(Token(vs_name));
            if (vs_it != sel_it->second.end()) {
                selection = vs_it->second;
            }
        }

        // Check authored selection on prim (strongest layer wins)
        if (selection.empty()) {
            for (auto* layer : layers) {
                const Prim* prim = layer->get_prim_at_path(node.site_path);
                if (prim) {
                    std::string authored = prim->get_variant_selection(vs_name);
                    if (!authored.empty()) {
                        selection = Token(authored);
                        break;
                    }
                }
            }
        }

        // Check fallback
        if (selection.empty()) {
            auto fb_it = config_.variant_fallbacks.find(vs_name);
            if (fb_it != config_.variant_fallbacks.end()) {
                selection = Token(fb_it->second);
            }
        }

        // If still empty, use first available variant from strongest layer
        if (selection.empty()) {
            for (auto* layer : layers) {
                const Prim* prim = layer->get_prim_at_path(node.site_path);
                if (prim) {
                    const VariantSet* vs = prim->get_variant_set(vs_name);
                    if (vs && vs->variant_count() > 0) {
                        auto names = vs->variant_names();
                        if (!names.empty()) {
                            selection = Token(names[0]);
                        }
                        break;
                    }
                }
            }
        }

        // Skip if no selection available
        if (selection.empty()) {
            PcpError error;
            error.type = PcpErrorType::InvalidVariantSelection;
            error.site_path = node.site_path;
            error.message = "No variant available for variant set: " + vs_name;
            index->add_error(error);
            continue;
        }

        // Create variant path: /Prim{variantSet=selection}
        Path variant_path = node.site_path.append_variant_selection(vs_name, selection.str());
        if (!variant_path.is_valid()) {
            continue;
        }

        // Check for cycle (use the variant path with the same layer stack)
        auto visit_key = std::make_pair(variant_path, node.layer_stack);
        if (visited.count(visit_key)) {
            PcpError error;
            error.type = PcpErrorType::ArcCycle;
            error.site_path = variant_path;
            error.message = "Variant cycle detected";
            index->add_error(error);
            continue;
        }

        PcpNode child;
        child.arc_type = CompositionArcType::VariantSet;
        child.site_path = variant_path;
        child.layer_stack = node.layer_stack;
        child.sibling_index = sibling_index++;
        child.depth = node.depth + 1;

        // Process arcs from the variant
        process_arcs(child, index, visited);
        node.insert_child(std::move(child));
    }

    // 3. References
    for (auto* layer : layers) {
        const Prim* prim = layer->get_prim_at_path(node.site_path);
        if (!prim || !prim->has_references()) continue;

        const ReferenceList& refs = prim->references();
        auto process_ref = [&](const Reference& ref) {
            PcpLayerStack* target_stack = node.layer_stack;

            // External reference - load new layer stack
            if (!ref.asset_path.empty()) {
                std::string resolved = layer_registry_->resolve_asset_path(
                    ref.asset_path, layer);

                PcpLayerStackIdentifier id;
                id.root_layer_path = resolved;

                auto it = layer_stack_cache_.find(id);
                if (it != layer_stack_cache_.end()) {
                    target_stack = it->second.get();
                } else {
                    std::vector<std::string> errors;
                    auto new_stack = PcpLayerStack::Build(id, layer_registry_.get(), &errors);
                    if (!new_stack) {
                        PcpError error;
                        error.type = PcpErrorType::UnresolvedReference;
                        error.site_path = node.site_path;
                        error.layer_id = layer->identifier();
                        error.message = "Failed to load reference: " + ref.asset_path;
                        index->add_error(error);
                        return;
                    }
                    target_stack = new_stack.get();
                    layer_stack_cache_[id] = std::move(new_stack);
                }
            }

            // Determine target path
            Path target_path = ref.prim_path;
            if (target_path.is_empty() && target_stack && target_stack->root_layer()) {
                const std::string& default_prim = target_stack->root_layer()->default_prim();
                if (!default_prim.empty()) {
                    target_path = Path("/" + default_prim);
                }
            }

            if (target_path.is_empty()) {
                PcpError error;
                error.type = PcpErrorType::InvalidPath;
                error.site_path = node.site_path;
                error.message = "Reference has no target path";
                index->add_error(error);
                return;
            }

            PcpNode child;
            child.arc_type = CompositionArcType::Reference;
            child.site_path = target_path;
            child.layer_stack = target_stack;
            child.map_to_parent = ref.layer_offset;
            child.sibling_index = sibling_index++;
            child.depth = node.depth + 1;

            process_arcs(child, index, visited);
            node.insert_child(std::move(child));
        };

        for (const auto& r : refs.prepended_items()) process_ref(r);
        if (refs.has_explicit()) {
            for (const auto& r : refs.explicit_items()) process_ref(r);
        }
        for (const auto& r : refs.appended_items()) process_ref(r);
    }

    // 4. Payloads
    bool has_unloaded_payloads = false;
    for (auto* layer : layers) {
        const Prim* prim = layer->get_prim_at_path(node.site_path);
        if (!prim || !prim->has_payloads()) continue;

        if (!is_payload_included(node.site_path)) {
            has_unloaded_payloads = true;
            continue;
        }

        const PayloadList& payloads = prim->payloads();
        auto process_payload = [&](const Payload& payload) {
            PcpLayerStack* target_stack = node.layer_stack;

            if (!payload.asset_path.empty()) {
                std::string resolved = layer_registry_->resolve_asset_path(
                    payload.asset_path, layer);

                PcpLayerStackIdentifier id;
                id.root_layer_path = resolved;

                auto it = layer_stack_cache_.find(id);
                if (it != layer_stack_cache_.end()) {
                    target_stack = it->second.get();
                } else {
                    std::vector<std::string> errors;
                    auto new_stack = PcpLayerStack::Build(id, layer_registry_.get(), &errors);
                    if (!new_stack) {
                        PcpError error;
                        error.type = PcpErrorType::UnresolvedReference;
                        error.site_path = node.site_path;
                        error.layer_id = layer->identifier();
                        error.message = "Failed to load payload: " + payload.asset_path;
                        index->add_error(error);
                        return;
                    }
                    target_stack = new_stack.get();
                    layer_stack_cache_[id] = std::move(new_stack);
                }
            }

            Path target_path = payload.prim_path;
            if (target_path.is_empty() && target_stack && target_stack->root_layer()) {
                const std::string& default_prim = target_stack->root_layer()->default_prim();
                if (!default_prim.empty()) {
                    target_path = Path("/" + default_prim);
                }
            }

            if (target_path.is_empty()) {
                PcpError error;
                error.type = PcpErrorType::InvalidPath;
                error.site_path = node.site_path;
                error.message = "Payload has no target path";
                index->add_error(error);
                return;
            }

            PcpNode child;
            child.arc_type = CompositionArcType::Payload;
            child.site_path = target_path;
            child.layer_stack = target_stack;
            child.map_to_parent = payload.layer_offset;
            child.sibling_index = sibling_index++;
            child.depth = node.depth + 1;

            process_arcs(child, index, visited);
            node.insert_child(std::move(child));
        };

        for (const auto& p : payloads.prepended_items()) process_payload(p);
        if (payloads.has_explicit()) {
            for (const auto& p : payloads.explicit_items()) process_payload(p);
        }
        for (const auto& p : payloads.appended_items()) process_payload(p);
    }

    if (has_unloaded_payloads) {
        index->set_has_payloads(true);
    }

    // 5. Specializes (weakest)
    for (auto* layer : layers) {
        const Prim* prim = layer->get_prim_at_path(node.site_path);
        if (!prim || !prim->has_specializes()) continue;

        const PathList& specializes = prim->specializes();
        auto process_spec = [&](const Path& target_path) {
            PcpNode child;
            child.arc_type = CompositionArcType::Specialize;
            child.site_path = target_path;
            child.layer_stack = node.layer_stack;
            child.sibling_index = sibling_index++;
            child.depth = node.depth + 1;

            process_arcs(child, index, visited);
            node.insert_child(std::move(child));
        };

        for (const auto& p : specializes.prepended_items()) process_spec(p);
        if (specializes.has_explicit()) {
            for (const auto& p : specializes.explicit_items()) process_spec(p);
        }
        for (const auto& p : specializes.appended_items()) process_spec(p);
    }

    visited.erase(site);
}

// ============================================================================
// PcpCache public interface
// ============================================================================

PcpCache::PcpCache(const PcpCacheConfig& config) : impl_(new Impl()) {
    impl_->config_ = config;
    impl_->layer_registry_ = std::make_unique<LayerRegistry>();

    // Set up search paths
    impl_->layer_registry_->set_search_paths(config.search_paths);

    // Build root layer stack
    std::vector<std::string> errors;
    if (!impl_->build_root_layer_stack(&errors)) {
        for (const auto& err : errors) {
            PcpError error;
            error.type = PcpErrorType::InternalError;
            error.message = err;
            impl_->composition_errors_.push_back(error);
        }
    }
}

PcpCache::~PcpCache() = default;
PcpCache::PcpCache(PcpCache&&) noexcept = default;
PcpCache& PcpCache::operator=(PcpCache&&) noexcept = default;

Result<const PcpPrimIndex*> PcpCache::compute_prim_index(const Path& path) {
    // Check cache
    auto it = impl_->prim_index_cache_.find(path);
    if (it != impl_->prim_index_cache_.end()) {
        return it->second.get();
    }

    // Validate
    if (!impl_->root_layer_stack_) {
        return Error("Root layer stack not loaded");
    }

    // Create new index
    auto index = std::make_unique<PcpPrimIndex>();
    index->set_path(path);

    // Build composition
    impl_->build_composition(index.get(), path);

    // Finalize
    index->finalize();

    // Cache and return
    auto* ptr = index.get();
    impl_->prim_index_cache_[path] = std::move(index);

    return ptr;
}

std::map<Path, const PcpPrimIndex*> PcpCache::compute_prim_indexes(
    const std::vector<Path>& paths)
{
    std::map<Path, const PcpPrimIndex*> result;
    for (const auto& path : paths) {
        auto r = compute_prim_index(path);
        if (r) {
            result[path] = r.value();
        }
    }
    return result;
}

const PcpLayerStack* PcpCache::root_layer_stack() const {
    return impl_->root_layer_stack_.get();
}

Result<const PcpLayerStack*> PcpCache::compute_layer_stack(
    const PcpLayerStackIdentifier& identifier)
{
    // Check if it's the root layer stack
    if (impl_->root_layer_stack_ &&
        impl_->root_layer_stack_->identifier() == identifier) {
        return impl_->root_layer_stack_.get();
    }

    // Check cache
    auto it = impl_->layer_stack_cache_.find(identifier);
    if (it != impl_->layer_stack_cache_.end()) {
        return it->second.get();
    }

    // Build new layer stack
    std::vector<std::string> errors;
    auto stack = PcpLayerStack::Build(identifier, impl_->layer_registry_.get(), &errors);
    if (!stack) {
        return Error(errors.empty() ? "Failed to build layer stack" : errors[0]);
    }

    auto* ptr = stack.get();
    impl_->layer_stack_cache_[identifier] = std::move(stack);
    return ptr;
}

void PcpCache::request_payloads(const std::vector<Path>& paths) {
    for (const auto& path : paths) {
        impl_->included_payloads_.insert(path);
    }
    invalidate(paths);
}

void PcpCache::request_payloads_exclusion(const std::vector<Path>& paths) {
    for (const auto& path : paths) {
        impl_->included_payloads_.erase(path);
    }
    invalidate(paths);
}

bool PcpCache::is_payload_included(const Path& path) const {
    return impl_->is_payload_included(path);
}

std::vector<Path> PcpCache::get_included_payloads() const {
    return std::vector<Path>(impl_->included_payloads_.begin(),
                              impl_->included_payloads_.end());
}

void PcpCache::set_variant_selection(const Path& prim_path,
                                      const Token& variant_set,
                                      const Token& selection)
{
    impl_->variant_selections_[prim_path][variant_set] = selection;
    invalidate({prim_path});
}

Token PcpCache::get_variant_selection(const Path& prim_path,
                                       const Token& variant_set) const
{
    return impl_->get_variant_selection(prim_path, variant_set);
}

Token PcpCache::get_variant_fallback(const Token& variant_set) const {
    auto it = impl_->config_.variant_fallbacks.find(variant_set.str());
    if (it != impl_->config_.variant_fallbacks.end()) {
        return Token(it->second);
    }
    return Token();
}

void PcpCache::clear_variant_selection(const Path& prim_path,
                                        const Token& variant_set)
{
    auto it = impl_->variant_selections_.find(prim_path);
    if (it != impl_->variant_selections_.end()) {
        if (variant_set.empty()) {
            impl_->variant_selections_.erase(it);
        } else {
            it->second.erase(variant_set);
        }
    }
    invalidate({prim_path});
}

void PcpCache::invalidate(const std::vector<Path>& paths) {
    for (const auto& path : paths) {
        auto it = impl_->prim_index_cache_.begin();
        while (it != impl_->prim_index_cache_.end()) {
            if (it->first == path || it->first.has_prefix(path)) {
                it = impl_->prim_index_cache_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void PcpCache::clear_prim_indexes() {
    impl_->prim_index_cache_.clear();
}

void PcpCache::clear() {
    impl_->prim_index_cache_.clear();
    impl_->layer_stack_cache_.clear();
}

std::vector<Path> PcpCache::get_cached_prim_paths() const {
    std::vector<Path> result;
    for (const auto& pair : impl_->prim_index_cache_) {
        result.push_back(pair.first);
    }
    return result;
}

size_t PcpCache::cached_prim_index_count() const {
    return impl_->prim_index_cache_.size();
}

size_t PcpCache::cached_layer_stack_count() const {
    return impl_->layer_stack_cache_.size() + (impl_->root_layer_stack_ ? 1 : 0);
}

const PcpCacheConfig& PcpCache::config() const {
    return impl_->config_;
}

LayerRegistry* PcpCache::layer_registry() {
    return impl_->layer_registry_.get();
}

const LayerRegistry* PcpCache::layer_registry() const {
    return impl_->layer_registry_.get();
}

const std::vector<PcpError>& PcpCache::composition_errors() const {
    return impl_->composition_errors_;
}

bool PcpCache::has_composition_errors() const {
    return !impl_->composition_errors_.empty();
}

} // namespace v1
} // namespace lightusd
