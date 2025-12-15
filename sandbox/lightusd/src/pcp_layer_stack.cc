// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PCP Layer Stack implementation

#include "lightusd/pcp_layer_stack.hh"
#include "lightusd/layer_registry.hh"

#include <algorithm>
#include <set>

namespace lightusd {
namespace v1 {

// ============================================================================
// PcpLayerStack::Impl
// ============================================================================

class PcpLayerStack::Impl {
public:
    PcpLayerStackIdentifier identifier_;

    // Layers in strength order (strongest first)
    // Session layer(s) first, then root layer, then sublayers
    std::vector<Layer*> layers_;
    std::vector<LayerOffset> layer_offsets_;

    // Relocates
    RelocatesMap relocates_source_to_target_;
    RelocatesMap relocates_target_to_source_;

    // Composed metadata (from strongest layer that defines it)
    double time_codes_per_second_ = 24.0;
    double frames_per_second_ = 24.0;
    double start_time_code_ = 0.0;
    double end_time_code_ = 0.0;
    bool has_time_codes_per_second_ = false;
    bool has_frames_per_second_ = false;
    bool has_start_time_code_ = false;
    bool has_end_time_code_ = false;

    void compose_metadata() {
        // Compose metadata from layers (strongest wins)
        // Use default values initially, override with first layer that has non-default
        for (const auto* layer : layers_) {
            // Check timeCodesPerSecond (default is 24.0)
            if (!has_time_codes_per_second_) {
                double tcps = layer->time_codes_per_second();
                if (tcps != 24.0) {  // Non-default value
                    time_codes_per_second_ = tcps;
                    has_time_codes_per_second_ = true;
                }
            }
            // Check framesPerSecond (default is 24.0)
            if (!has_frames_per_second_) {
                double fps = layer->frames_per_second();
                if (fps != 24.0) {
                    frames_per_second_ = fps;
                    has_frames_per_second_ = true;
                }
            }
            // Check startTimeCode (default is 0.0)
            if (!has_start_time_code_) {
                double stc = layer->start_time_code();
                if (stc != 0.0) {
                    start_time_code_ = stc;
                    has_start_time_code_ = true;
                }
            }
            // Check endTimeCode (default is 0.0)
            if (!has_end_time_code_) {
                double etc = layer->end_time_code();
                if (etc != 0.0) {
                    end_time_code_ = etc;
                    has_end_time_code_ = true;
                }
            }
        }
    }

    void compose_relocates() {
        // Compose relocates from all layers
        // Later layers (weaker) can add but not override relocates
        for (const auto* layer : layers_) {
            // Get relocates from layer (if any)
            // Note: Layer class would need relocates() method
            // For now, this is a placeholder for future implementation
            // const auto& layer_relocates = layer->relocates();
            // for (const auto& [source, target] : layer_relocates) {
            //     if (relocates_source_to_target_.find(source) == relocates_source_to_target_.end()) {
            //         relocates_source_to_target_[source] = target;
            //         relocates_target_to_source_[target] = source;
            //     }
            // }
        }
    }
};

// ============================================================================
// PcpLayerStack public interface
// ============================================================================

PcpLayerStack::PcpLayerStack() : impl_(new Impl()) {}
PcpLayerStack::~PcpLayerStack() = default;
PcpLayerStack::PcpLayerStack(PcpLayerStack&&) noexcept = default;
PcpLayerStack& PcpLayerStack::operator=(PcpLayerStack&&) noexcept = default;

const PcpLayerStackIdentifier& PcpLayerStack::identifier() const {
    return impl_->identifier_;
}

bool PcpLayerStack::is_valid() const {
    return !impl_->layers_.empty();
}

const std::vector<Layer*>& PcpLayerStack::layers() const {
    return impl_->layers_;
}

size_t PcpLayerStack::layer_count() const {
    return impl_->layers_.size();
}

Layer* PcpLayerStack::layer(size_t index) const {
    return index < impl_->layers_.size() ? impl_->layers_[index] : nullptr;
}

Layer* PcpLayerStack::root_layer() const {
    // Root layer is the first non-session layer
    // For now, assuming no session layer, root is first
    return impl_->layers_.empty() ? nullptr : impl_->layers_[0];
}

Layer* PcpLayerStack::session_layer() const {
    // Session layer would be first if identifier has session_layer_path
    if (!impl_->identifier_.session_layer_path.empty() && !impl_->layers_.empty()) {
        return impl_->layers_[0];
    }
    return nullptr;
}

bool PcpLayerStack::contains_layer(const Layer* layer) const {
    return std::find(impl_->layers_.begin(), impl_->layers_.end(), layer)
           != impl_->layers_.end();
}

LayerOffset PcpLayerStack::get_layer_offset(size_t index) const {
    if (index < impl_->layer_offsets_.size()) {
        return impl_->layer_offsets_[index];
    }
    return LayerOffset();  // Identity offset
}

LayerOffset PcpLayerStack::get_layer_offset(const Layer* layer) const {
    for (size_t i = 0; i < impl_->layers_.size(); ++i) {
        if (impl_->layers_[i] == layer) {
            return get_layer_offset(i);
        }
    }
    return LayerOffset();
}

const RelocatesMap& PcpLayerStack::relocates_source_to_target() const {
    return impl_->relocates_source_to_target_;
}

const RelocatesMap& PcpLayerStack::relocates_target_to_source() const {
    return impl_->relocates_target_to_source_;
}

bool PcpLayerStack::has_relocates() const {
    return !impl_->relocates_source_to_target_.empty();
}

Path PcpLayerStack::apply_relocates(const Path& source_path) const {
    // Find the longest matching prefix in relocates
    Path best_match_source;
    Path best_match_target;

    for (const auto& pair : impl_->relocates_source_to_target_) {
        const Path& relocate_source = pair.first;
        const Path& relocate_target = pair.second;

        // Check if source_path starts with relocate_source
        if (source_path.has_prefix(relocate_source)) {
            // Use longest match
            if (relocate_source.prim_part().size() > best_match_source.prim_part().size()) {
                best_match_source = relocate_source;
                best_match_target = relocate_target;
            }
        }
    }

    if (best_match_source.is_valid()) {
        // Apply the relocate: replace prefix
        return source_path.replace_prefix(best_match_source, best_match_target);
    }

    return source_path;
}

Path PcpLayerStack::unapply_relocates(const Path& target_path) const {
    // Find the longest matching prefix in reverse relocates
    Path best_match_target;
    Path best_match_source;

    for (const auto& pair : impl_->relocates_target_to_source_) {
        const Path& relocate_target = pair.first;
        const Path& relocate_source = pair.second;

        if (target_path.has_prefix(relocate_target)) {
            if (relocate_target.prim_part().size() > best_match_target.prim_part().size()) {
                best_match_target = relocate_target;
                best_match_source = relocate_source;
            }
        }
    }

    if (best_match_target.is_valid()) {
        return target_path.replace_prefix(best_match_target, best_match_source);
    }

    return target_path;
}

double PcpLayerStack::time_codes_per_second() const {
    return impl_->time_codes_per_second_;
}

double PcpLayerStack::frames_per_second() const {
    return impl_->frames_per_second_;
}

double PcpLayerStack::start_time_code() const {
    return impl_->start_time_code_;
}

double PcpLayerStack::end_time_code() const {
    return impl_->end_time_code_;
}

bool PcpLayerStack::has_prim_spec(const Path& path) const {
    // Apply relocates first
    Path lookup_path = apply_relocates(path);

    for (const auto* layer : impl_->layers_) {
        // Check if layer has a prim spec at this path
        const Prim* prim = layer->get_prim_at_path(lookup_path);
        if (prim) return true;
    }
    return false;
}

std::vector<Layer*> PcpLayerStack::get_layers_with_prim_spec(const Path& path) const {
    std::vector<Layer*> result;
    Path lookup_path = apply_relocates(path);

    for (auto* layer : impl_->layers_) {
        const Prim* prim = layer->get_prim_at_path(lookup_path);
        if (prim) {
            result.push_back(layer);
        }
    }
    return result;
}

// ============================================================================
// PcpLayerStack::Build
// ============================================================================

std::unique_ptr<PcpLayerStack> PcpLayerStack::Build(
    const PcpLayerStackIdentifier& identifier,
    LayerRegistry* registry,
    std::vector<std::string>* errors)
{
    if (!identifier.is_valid()) {
        if (errors) errors->push_back("Invalid layer stack identifier");
        return nullptr;
    }

    if (!registry) {
        if (errors) errors->push_back("Layer registry is null");
        return nullptr;
    }

    auto stack = std::make_unique<PcpLayerStack>();
    stack->impl_->identifier_ = identifier;

    // Track visited layers to detect cycles
    std::set<std::string> visited;

    // Helper to recursively add layer and its sublayers
    std::function<bool(const std::string&, const LayerOffset&)> add_layer_recursive;
    add_layer_recursive = [&](const std::string& layer_path, const LayerOffset& offset) -> bool {
        // Check for cycle
        if (visited.count(layer_path)) {
            if (errors) {
                errors->push_back("Sublayer cycle detected: " + layer_path);
            }
            return false;
        }
        visited.insert(layer_path);

        // Load layer
        auto result = registry->find_or_open(layer_path);
        if (!result) {
            if (errors) {
                errors->push_back("Failed to load layer: " + layer_path + " - " + result.error().message);
            }
            return false;
        }

        Layer* layer = result.value();
        stack->impl_->layers_.push_back(layer);
        stack->impl_->layer_offsets_.push_back(offset);

        // Process sublayers (weaker, so added after this layer)
        const auto& sublayer_paths = layer->sublayer_paths();
        for (const auto& sublayer_path : sublayer_paths) {
            // Resolve sublayer path relative to this layer
            std::string resolved = registry->resolve_asset_path(sublayer_path, layer);

            // Sublayers inherit parent's offset (could compose with sublayer offset)
            // For now, use identity offset for sublayers
            if (!add_layer_recursive(resolved, LayerOffset())) {
                // Continue processing other sublayers even if one fails
            }
        }

        return true;
    };

    // Add session layer first (if any)
    if (!identifier.session_layer_path.empty()) {
        if (!add_layer_recursive(identifier.session_layer_path, LayerOffset())) {
            // Session layer failure is not fatal
            if (errors) {
                errors->push_back("Warning: Could not load session layer");
            }
        }
        visited.clear();  // Clear for root layer processing
    }

    // Add root layer and its sublayers
    if (!add_layer_recursive(identifier.root_layer_path, LayerOffset())) {
        if (errors && errors->empty()) {
            errors->push_back("Failed to load root layer: " + identifier.root_layer_path);
        }
        return nullptr;
    }

    // Compose metadata from layers
    stack->impl_->compose_metadata();

    // Compose relocates from layers
    stack->impl_->compose_relocates();

    return stack;
}

} // namespace v1
} // namespace lightusd
