// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP Layer Stack Implementation - Local layer composition

#include "pcp-layer-stack.hh"
#include "pcp-compose-site.hh"
#include "layer.hh"
#include <algorithm>
#include <functional>
#include <sstream>

namespace tinyusdz {
namespace tydra {
namespace pcp {

// LayerStack Implementation

LayerStack::LayerStack(Layer* root_layer, Layer* session_layer, const std::string& identifier)
    : identifier_(identifier.empty() ? CreateLayerStackIdentifier(root_layer, session_layer) : identifier)
    , root_layer_(root_layer)
    , session_layer_(session_layer) {
}

LayerStack::~LayerStack() = default;

LayerStackPtr LayerStack::Create(
    Layer* root_layer,
    Layer* session_layer,
    const std::string& identifier) {

    if (!root_layer) {
        return nullptr;
    }

    auto stack = std::shared_ptr<LayerStack>(
        new LayerStack(root_layer, session_layer, identifier));

    // Build the layer stack
    stack->BuildLayerStack();
    stack->BuildLayerTree();
    stack->BuildRelocates();
    stack->BuildExpressionVariables();

    return stack;
}

const LayerStack::LayerEntry* LayerStack::FindLayer(const std::string& identifier) const {
    for (const auto& entry : layers_) {
        if (entry.layer && entry.layer->GetIdentifier() == identifier) {
            return &entry;
        }
    }
    return nullptr;
}

Path LayerStack::ApplyRelocates(const Path& path) const {
    for (const auto& relocate : relocates_) {
        if (path == relocate.source) {
            return relocate.target;
        }

        if (path.HasPrefix(relocate.source)) {
            // Apply relocation
            std::string path_str = path.full_path_name();
            std::string source_str = relocate.source.full_path_name();
            std::string target_str = relocate.target.full_path_name();

            path_str = target_str + path_str.substr(source_str.length());
            return Path(path_str);
        }
    }

    return path;
}

Path LayerStack::ApplyRelocatesReverse(const Path& path) const {
    for (const auto& relocate : relocates_) {
        if (path == relocate.target) {
            return relocate.source;
        }

        if (path.HasPrefix(relocate.target)) {
            // Apply reverse relocation
            std::string path_str = path.full_path_name();
            std::string target_str = relocate.target.full_path_name();
            std::string source_str = relocate.source.full_path_name();

            path_str = source_str + path_str.substr(target_str.length());
            return Path(path_str);
        }
    }

    return path;
}

bool LayerStack::IsRelocatedPath(const Path& path) const {
    // Check if path has been relocated (is a target)
    for (const auto& relocate : relocates_) {
        if (path == relocate.target || path.HasPrefix(relocate.target)) {
            return true;
        }
    }
    return false;
}

bool LayerStack::IsRelocatedSource(const Path& path) const {
    // Check if path is a relocation source (prohibited namespace)
    return relocated_sources_.count(path) > 0 ||
           std::any_of(relocated_sources_.begin(), relocated_sources_.end(),
                      [&path](const Path& source) {
                          return path.HasPrefix(source);
                      });
}

std::string LayerStack::ResolveExpressionVariable(const std::string& var_name) const {
    auto it = expression_variables_.find(var_name);
    if (it != expression_variables_.end()) {
        return it->second;
    }
    return "";
}

bool LayerStack::IsLayerMuted(const std::string& identifier) const {
    return muted_layers_.count(identifier) > 0;
}

LayerOffset LayerStack::GetLayerOffset(Layer* layer) const {
    if (!layer) {
        return LayerOffset();
    }

    for (const auto& entry : layers_) {
        if (entry.layer == layer) {
            return entry.offset;
        }
    }

    return LayerOffset();
}

double LayerStack::MapTime(double time, Layer* from_layer, Layer* to_layer) const {
    if (!from_layer || !to_layer || from_layer == to_layer) {
        return time;
    }

    // Get offsets for both layers
    LayerOffset from_offset = GetLayerOffset(from_layer);
    LayerOffset to_offset = GetLayerOffset(to_layer);

    // Map from source layer to root
    double root_time = from_offset.GetInverse().Transform(time);

    // Map from root to target layer
    return to_offset.Transform(root_time);
}

bool LayerStack::HasSpecs(const Path& path) const {
    for (const auto& entry : layers_) {
        if (entry.layer && entry.layer->HasSpec(path)) {
            return true;
        }
    }
    return false;
}

std::optional<PrimSpec> LayerStack::GetPrimSpec(const Path& path) const {
    for (const auto& entry : layers_) {
        if (entry.layer && entry.layer->HasSpec(path)) {
            PrimSpec spec;
            spec.layer = entry.layer;
            spec.path = path;
            return spec;
        }
    }
    return std::nullopt;
}

PrimStack LayerStack::GetPrimStack(const Path& path) const {
    PrimStack stack;

    for (const auto& entry : layers_) {
        if (entry.layer && entry.layer->HasSpec(path)) {
            PrimSpec spec;
            spec.layer = entry.layer;
            spec.path = path;
            stack.push_back(spec);
        }
    }

    return stack;
}

std::vector<Reference> LayerStack::ComposeReferences(const Path& path) const {
    return ComposeSiteListEditableField<Reference>(
        std::const_pointer_cast<LayerStack>(shared_from_this()),
        path,
        "references");
}

std::vector<Payload> LayerStack::ComposePayloads(const Path& path) const {
    return ComposeSiteListEditableField<Payload>(
        std::const_pointer_cast<LayerStack>(shared_from_this()),
        path,
        "payload");
}

std::vector<Path> LayerStack::ComposeInherits(const Path& path) const {
    return ComposeSiteListEditableField<Path>(
        std::const_pointer_cast<LayerStack>(shared_from_this()),
        path,
        "inherits");
}

std::vector<Path> LayerStack::ComposeSpecializes(const Path& path) const {
    return ComposeSiteListEditableField<Path>(
        std::const_pointer_cast<LayerStack>(shared_from_this()),
        path,
        "specializes");
}

VariantSelectionMap LayerStack::ComposeVariantSelections(const Path& path) const {
    VariantSelectionMap selections;

    // Compose from weakest to strongest
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (!it->layer) continue;

        auto prim = it->layer->GetPrim(path);
        if (!prim) continue;

        // Get variant selections from this layer
        auto layer_selections = prim->GetVariantSelections();
        for (const auto& [set_name, selection] : layer_selections) {
            selections[set_name] = selection;
        }
    }

    return selections;
}

std::vector<std::string> LayerStack::ComposeVariantSets(const Path& path) const {
    std::unordered_set<std::string> sets;

    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto layer_sets = prim->GetVariantSetNames();
        sets.insert(layer_sets.begin(), layer_sets.end());
    }

    return std::vector<std::string>(sets.begin(), sets.end());
}

std::vector<std::string> LayerStack::GetChildrenNames(const Path& path) const {
    std::unordered_set<std::string> children;

    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto layer_children = prim->GetChildrenNames();
        children.insert(layer_children.begin(), layer_children.end());
    }

    return std::vector<std::string>(children.begin(), children.end());
}

std::vector<std::string> LayerStack::GetPropertyNames(const Path& path) const {
    std::unordered_set<std::string> properties;

    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto layer_props = prim->GetPropertyNames();
        properties.insert(layer_props.begin(), layer_props.end());
    }

    return std::vector<std::string>(properties.begin(), properties.end());
}

bool LayerStack::HasField(const Path& path, const std::string& field_name) const {
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (prim && prim->HasField(field_name)) {
            return true;
        }
    }
    return false;
}

value::Value LayerStack::ComposeField(const Path& path, const std::string& field_name) const {
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (prim && prim->HasField(field_name)) {
            return prim->GetField(field_name);
        }
    }

    return value::Value();
}

std::optional<Permission> LayerStack::ComposePermission(const Path& path) const {
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto perm = prim->GetPermission();
        if (perm.has_value()) {
            return perm;
        }
    }
    return std::nullopt;
}

std::optional<bool> LayerStack::ComposeActive(const Path& path) const {
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto active = prim->IsActive();
        if (active.has_value()) {
            return active;
        }
    }
    return std::nullopt;
}

std::optional<bool> LayerStack::ComposeInstanceable(const Path& path) const {
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto instanceable = prim->IsInstanceable();
        if (instanceable.has_value()) {
            return instanceable;
        }
    }
    return std::nullopt;
}

std::optional<Kind> LayerStack::ComposeKind(const Path& path) const {
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto kind = prim->GetKind();
        if (kind.has_value()) {
            return kind;
        }
    }
    return std::nullopt;
}

std::optional<Purpose> LayerStack::ComposePurpose(const Path& path) const {
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto purpose = prim->GetPurpose();
        if (purpose.has_value()) {
            return purpose;
        }
    }
    return std::nullopt;
}

bool LayerStack::IsEquivalentTo(const LayerStack& other) const {
    if (this == &other) {
        return true;
    }

    if (root_layer_ != other.root_layer_ ||
        session_layer_ != other.session_layer_) {
        return false;
    }

    if (layers_.size() != other.layers_.size()) {
        return false;
    }

    for (size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].layer != other.layers_[i].layer ||
            layers_[i].offset.offset != other.layers_[i].offset.offset ||
            layers_[i].offset.scale != other.layers_[i].offset.scale) {
            return false;
        }
    }

    return relocates_ == other.relocates_ &&
           expression_variables_ == other.expression_variables_ &&
           muted_layers_ == other.muted_layers_;
}

size_t LayerStack::GetHash() const {
    if (cached_hash_.has_value()) {
        return cached_hash_.value();
    }

    size_t hash = 0;

    // Hash root and session layers
    hash ^= std::hash<void*>{}(root_layer_);
    hash ^= std::hash<void*>{}(session_layer_) << 1;

    // Hash all layers
    for (const auto& entry : layers_) {
        hash ^= std::hash<void*>{}(entry.layer) << 2;
        hash ^= std::hash<double>{}(entry.offset.offset) << 3;
        hash ^= std::hash<double>{}(entry.offset.scale) << 4;
    }

    // Hash relocates
    for (const auto& relocate : relocates_) {
        hash ^= std::hash<std::string>{}(relocate.source.full_path_name()) << 5;
        hash ^= std::hash<std::string>{}(relocate.target.full_path_name()) << 6;
    }

    cached_hash_ = hash;
    return hash;
}

std::string LayerStack::DumpToString() const {
    std::ostringstream ss;
    ss << "LayerStack '" << identifier_ << "':\n";

    ss << "  Layers (" << layers_.size() << "):\n";
    for (size_t i = 0; i < layers_.size(); ++i) {
        const auto& entry = layers_[i];
        ss << "    [" << i << "] " << (entry.layer ? entry.layer->GetIdentifier() : "null");

        if (!entry.offset.IsIdentity()) {
            ss << " (offset=" << entry.offset.offset
               << ", scale=" << entry.offset.scale << ")";
        }

        ss << "\n";
    }

    if (!relocates_.empty()) {
        ss << "  Relocates:\n";
        for (const auto& relocate : relocates_) {
            ss << "    " << relocate.source.full_path_name()
               << " -> " << relocate.target.full_path_name() << "\n";
        }
    }

    if (!expression_variables_.empty()) {
        ss << "  Expression Variables:\n";
        for (const auto& [name, value] : expression_variables_) {
            ss << "    " << name << " = " << value << "\n";
        }
    }

    if (!muted_layers_.empty()) {
        ss << "  Muted Layers:\n";
        for (const auto& id : muted_layers_) {
            ss << "    " << id << "\n";
        }
    }

    return ss.str();
}

void LayerStack::DumpLayerTree(std::ostream& out) const {
    DumpLayerTreeRecursive(out, layer_tree_, 0);
}

LayerStack::Statistics LayerStack::GetStatistics() const {
    Statistics stats;
    stats.num_layers = layers_.size();
    stats.num_relocates = relocates_.size();
    stats.num_muted = muted_layers_.size();

    // Count sublayers
    for (const auto& entry : layers_) {
        if (entry.layer && entry.layer != root_layer_ && entry.layer != session_layer_) {
            stats.num_sublayers++;
        }
    }

    // Count total specs
    std::unordered_set<Path> seen_paths;
    for (const auto& entry : layers_) {
        if (!entry.layer) continue;

        auto all_paths = entry.layer->GetAllPrimPaths();
        for (const auto& path : all_paths) {
            if (seen_paths.insert(path).second) {
                stats.total_specs++;
            }
        }
    }

    return stats;
}

// Private methods

void LayerStack::BuildLayerStack() {
    layers_.clear();
    std::unordered_set<std::string> seen;

    // Start with session layer if present
    if (session_layer_) {
        FlattenSublayers(session_layer_, LayerOffset(), layers_, seen);
    }

    // Add root layer and its sublayers
    if (root_layer_) {
        FlattenSublayers(root_layer_, LayerOffset(), layers_, seen);
    }
}

void LayerStack::BuildLayerTree() {
    if (session_layer_) {
        layer_tree_ = BuildLayerTreeRecursive(session_layer_, LayerOffset());
    } else if (root_layer_) {
        layer_tree_ = BuildLayerTreeRecursive(root_layer_, LayerOffset());
    }
}

void LayerStack::BuildRelocates() {
    relocates_.clear();
    relocated_sources_.clear();

    for (const auto& entry : layers_) {
        if (!entry.layer) continue;
        ComposeRelocatesForLayer(entry.layer, relocates_);
    }

    // Build set of relocated sources for fast lookup
    for (const auto& relocate : relocates_) {
        relocated_sources_.insert(relocate.source);
    }
}

void LayerStack::BuildExpressionVariables() {
    expression_variables_.clear();

    // Compose from weakest to strongest
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (!it->layer) continue;

        auto layer_vars = it->layer->GetExpressionVariables();
        for (const auto& [name, value] : layer_vars) {
            expression_variables_[name] = value;
        }
    }
}

void LayerStack::FlattenSublayers(
    Layer* layer,
    const LayerOffset& parent_offset,
    std::vector<LayerEntry>& result,
    std::unordered_set<std::string>& seen) {

    if (!layer) {
        return;
    }

    const std::string& identifier = layer->GetIdentifier();
    if (seen.count(identifier) > 0) {
        return;  // Already processed (avoid cycles)
    }

    seen.insert(identifier);

    // Check if muted
    if (IsLayerMuted(identifier)) {
        return;
    }

    // Add this layer
    LayerEntry entry;
    entry.layer = layer;
    entry.offset = parent_offset;
    result.push_back(entry);

    // Process sublayers
    auto sublayers = layer->GetSublayerPaths();
    for (const auto& sublayer_path : sublayers) {
        // Load sublayer
        Layer* sublayer = LoadSublayer(layer, sublayer_path);
        if (!sublayer) {
            continue;
        }

        // Get sublayer offset
        LayerOffset sublayer_offset = layer->GetSublayerOffset(sublayer_path);

        // Compose offsets
        LayerOffset combined_offset = parent_offset.ComposeWith(sublayer_offset);

        // Recursively flatten
        FlattenSublayers(sublayer, combined_offset, result, seen);
    }
}

LayerStack::LayerTree LayerStack::BuildLayerTreeRecursive(
    Layer* layer,
    const LayerOffset& parent_offset) {

    LayerTree tree;
    tree.layer = layer;
    tree.offset = parent_offset;

    if (layer) {
        auto sublayers = layer->GetSublayerPaths();
        for (const auto& sublayer_path : sublayers) {
            Layer* sublayer = LoadSublayer(layer, sublayer_path);
            if (sublayer && !IsLayerMuted(sublayer->GetIdentifier())) {
                LayerOffset sublayer_offset = layer->GetSublayerOffset(sublayer_path);
                LayerOffset combined_offset = parent_offset.ComposeWith(sublayer_offset);

                tree.sublayers.push_back(
                    BuildLayerTreeRecursive(sublayer, combined_offset));
            }
        }
    }

    return tree;
}

void LayerStack::ComposeRelocatesForLayer(
    Layer* layer,
    Relocates& result) {

    if (!layer) return;

    auto layer_relocates = layer->GetRelocates();

    // Add to result (later layers are stronger)
    for (const auto& relocate : layer_relocates) {
        // Check if this source is already relocated
        bool already_relocated = false;
        for (auto& existing : result) {
            if (existing.source == relocate.source) {
                // Update with stronger opinion
                existing.target = relocate.target;
                already_relocated = true;
                break;
            }
        }

        if (!already_relocated) {
            result.push_back(relocate);
        }
    }
}

void LayerStack::DumpLayerTreeRecursive(
    std::ostream& out,
    const LayerTree& tree,
    int depth) const {

    for (int i = 0; i < depth; ++i) {
        out << "  ";
    }

    out << "- " << (tree.layer ? tree.layer->GetIdentifier() : "null");

    if (!tree.offset.IsIdentity()) {
        out << " (offset=" << tree.offset.offset
            << ", scale=" << tree.offset.scale << ")";
    }

    out << "\n";

    for (const auto& sublayer : tree.sublayers) {
        DumpLayerTreeRecursive(out, sublayer, depth + 1);
    }
}

// Helper functions

Layer* LoadSublayer(Layer* parent, const std::string& sublayer_path) {
    // TODO: Implement actual sublayer loading
    // This would use asset resolution to find and load the sublayer
    // For now, return nullptr
    return nullptr;
}

std::string CreateLayerStackIdentifier(
    Layer* root_layer,
    Layer* session_layer) {

    std::ostringstream ss;

    if (session_layer) {
        ss << session_layer->GetIdentifier() << ":";
    }

    if (root_layer) {
        ss << root_layer->GetIdentifier();
    }

    return ss.str();
}

// LayerStackRegistry Implementation

LayerStackPtr LayerStackRegistry::GetOrCreate(
    Layer* root_layer,
    Layer* session_layer) {

    std::string identifier = CreateLayerStackIdentifier(root_layer, session_layer);

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already exists
    auto it = registry_.find(identifier);
    if (it != registry_.end()) {
        auto stack = it->second.lock();
        if (stack) {
            return stack;
        }
        // Weak pointer expired, remove it
        registry_.erase(it);
    }

    // Create new layer stack
    auto stack = LayerStack::Create(root_layer, session_layer, identifier);
    if (stack) {
        registry_[identifier] = stack;
    }

    return stack;
}

LayerStackPtr LayerStackRegistry::Find(const std::string& identifier) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = registry_.find(identifier);
    if (it != registry_.end()) {
        return it->second.lock();
    }

    return nullptr;
}

void LayerStackRegistry::Remove(const std::string& identifier) {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_.erase(identifier);
}

void LayerStackRegistry::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_.clear();
}

std::vector<LayerStackPtr> LayerStackRegistry::GetAll() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<LayerStackPtr> result;
    for (const auto& [id, weak_ptr] : registry_) {
        auto stack = weak_ptr.lock();
        if (stack) {
            result.push_back(stack);
        }
    }

    return result;
}

} // namespace pcp
} // namespace tydra
} // namespace tinyusdz