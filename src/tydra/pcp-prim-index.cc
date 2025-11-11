// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP Prim Index Implementation

#include "pcp-prim-index.hh"
#include "pcp-cache.hh"
#include "pcp-compose-site.hh"
#include "pcp-dependencies.hh"
#include "pcp-layer-stack.hh"
#include "layer.hh"
#include "path.hh"
#include "prim.hh"
#include "value.hh"
#include <algorithm>
#include <sstream>
#include <stack>
#include <queue>
#include <blake3.h>

namespace tinyusdz {
namespace tydra {
namespace pcp {

// PrimIndexGraph implementation

PrimIndexGraph::PrimIndexGraph() = default;

PrimIndexGraph::~PrimIndexGraph() = default;

NodeRef PrimIndexGraph::CreateRootNode(const Site& site) {
    if (!nodes_.empty()) {
        // Root node already exists
        return NodeRef();
    }

    size_t index = AddNodeInternal(site, Arc(), SIZE_MAX);
    return NodeRef(this, index);
}

NodeRef PrimIndexGraph::AddChildNode(
    NodeRef parent,
    const Arc& arc,
    const Site& site) {

    if (!parent.IsValid() || parent.graph_ != this) {
        return NodeRef();
    }

    size_t index = AddNodeInternal(site, arc, parent.index_);
    return NodeRef(this, index);
}

size_t PrimIndexGraph::AddNodeInternal(
    const Site& site,
    const Arc& arc,
    size_t parent_index) {

    size_t index = nodes_.size();
    nodes_.emplace_back();

    Node& node = nodes_.back();
    node.site = site;
    node.arc = arc;
    node.parent = parent_index;

    // Add to parent's children
    if (parent_index != SIZE_MAX) {
        nodes_[parent_index].children.push_back(index);
    }

    // Add to site lookup
    site_to_node_[site] = index;

    finalized_ = false;
    return index;
}

NodeRef PrimIndexGraph::GetNode(size_t index) {
    if (index >= nodes_.size()) {
        return NodeRef();
    }
    return NodeRef(this, index);
}

const NodeRef PrimIndexGraph::GetNode(size_t index) const {
    if (index >= nodes_.size()) {
        return NodeRef();
    }
    return NodeRef(const_cast<PrimIndexGraph*>(this), index);
}

NodeRef PrimIndexGraph::GetRootNode() {
    if (nodes_.empty()) {
        return NodeRef();
    }
    return NodeRef(this, 0);
}

const NodeRef PrimIndexGraph::GetRootNode() const {
    if (nodes_.empty()) {
        return NodeRef();
    }
    return NodeRef(const_cast<PrimIndexGraph*>(this), 0);
}

std::vector<NodeRef> PrimIndexGraph::GetNodes() const {
    std::vector<NodeRef> result;
    result.reserve(nodes_.size());

    for (size_t i = 0; i < nodes_.size(); ++i) {
        result.push_back(NodeRef(const_cast<PrimIndexGraph*>(this), i));
    }

    return result;
}

std::vector<NodeRef> PrimIndexGraph::GetNodesInStrengthOrder() const {
    std::vector<NodeRef> nodes = GetNodes();

    // Sort by strength (stronger opinions first)
    std::stable_sort(nodes.begin(), nodes.end(),
        [this](const NodeRef& a, const NodeRef& b) {
            return CompareNodeStrength(a.index_, b.index_);
        });

    return nodes;
}

bool PrimIndexGraph::CompareNodeStrength(size_t a, size_t b) const {
    if (a >= nodes_.size() || b >= nodes_.size()) {
        return false;
    }

    const Node& node_a = nodes_[a];
    const Node& node_b = nodes_[b];

    // Root is always strongest
    if (a == 0) return true;
    if (b == 0) return false;

    // Compare arc types (LIVRPS order)
    if (node_a.arc.type != node_b.arc.type) {
        return static_cast<int>(node_a.arc.type) <
               static_cast<int>(node_b.arc.type);
    }

    // Within same arc type, compare sibling indices
    if (node_a.arc.sibling_num != node_b.arc.sibling_num) {
        return node_a.arc.sibling_num < node_b.arc.sibling_num;
    }

    // Compare depths (shallower is stronger)
    size_t depth_a = 0, depth_b = 0;
    size_t idx = a;
    while (nodes_[idx].parent != SIZE_MAX) {
        depth_a++;
        idx = nodes_[idx].parent;
    }

    idx = b;
    while (nodes_[idx].parent != SIZE_MAX) {
        depth_b++;
        idx = nodes_[idx].parent;
    }

    if (depth_a != depth_b) {
        return depth_a < depth_b;
    }

    // Default to index order
    return a < b;
}

NodeRef PrimIndexGraph::FindNode(const Site& site) {
    auto it = site_to_node_.find(site);
    if (it != site_to_node_.end()) {
        return NodeRef(this, it->second);
    }
    return NodeRef();
}

const NodeRef PrimIndexGraph::FindNode(const Site& site) const {
    auto it = site_to_node_.find(site);
    if (it != site_to_node_.end()) {
        return NodeRef(const_cast<PrimIndexGraph*>(this), it->second);
    }
    return NodeRef();
}

bool PrimIndexGraph::HasNode(const Site& site) const {
    return site_to_node_.find(site) != site_to_node_.end();
}

void PrimIndexGraph::RemoveNode(NodeRef node) {
    if (!node.IsValid() || node.graph_ != this) {
        return;
    }

    // Mark node and subtree as culled
    CullNode(node);
    finalized_ = false;
}

void PrimIndexGraph::CullNode(NodeRef node) {
    if (!node.IsValid() || node.graph_ != this) {
        return;
    }

    nodes_[node.index_].flags.culled = true;

    // Recursively cull children
    for (size_t child_idx : nodes_[node.index_].children) {
        CullNode(NodeRef(this, child_idx));
    }
}

bool PrimIndexGraph::IsNodeCulled(NodeRef node) const {
    if (!node.IsValid() || node.graph_ != this) {
        return true;
    }
    return nodes_[node.index_].flags.culled;
}

void PrimIndexGraph::Finalize() {
    // Remove culled nodes from site map
    for (auto it = site_to_node_.begin(); it != site_to_node_.end(); ) {
        if (nodes_[it->second].flags.culled) {
            it = site_to_node_.erase(it);
        } else {
            ++it;
        }
    }

    finalized_ = true;
}

std::unique_ptr<PrimIndexGraph> PrimIndexGraph::Clone() const {
    auto result = std::make_unique<PrimIndexGraph>();
    result->nodes_ = nodes_;
    result->site_to_node_ = site_to_node_;
    result->finalized_ = finalized_;
    return result;
}

void PrimIndexGraph::MergeGraph(
    const PrimIndexGraph& other,
    NodeRef parent_node,
    const Arc& arc_to_parent) {

    if (!parent_node.IsValid() || parent_node.graph_ != this) {
        return;
    }

    // Map from other graph's node indices to this graph's indices
    std::unordered_map<size_t, size_t> index_map;

    // Copy nodes from other graph
    for (size_t i = 0; i < other.nodes_.size(); ++i) {
        const Node& other_node = other.nodes_[i];

        // Skip if culled
        if (other_node.flags.culled) {
            continue;
        }

        size_t new_index = nodes_.size();
        index_map[i] = new_index;

        nodes_.push_back(other_node);
        Node& new_node = nodes_.back();

        // Update parent
        if (i == 0) {
            // Root of other graph becomes child of parent_node
            new_node.parent = parent_node.index_;
            new_node.arc = arc_to_parent;
            nodes_[parent_node.index_].children.push_back(new_index);
        } else if (other_node.parent != SIZE_MAX) {
            auto it = index_map.find(other_node.parent);
            if (it != index_map.end()) {
                new_node.parent = it->second;
            }
        }

        // Update children indices
        new_node.children.clear();
        for (size_t child_idx : other_node.children) {
            if (!other.nodes_[child_idx].flags.culled) {
                // Will be updated when child is processed
            }
        }

        // Add to site map
        site_to_node_[new_node.site] = new_index;
    }

    // Fix up children references
    for (const auto& [old_idx, new_idx] : index_map) {
        const Node& old_node = other.nodes_[old_idx];
        Node& new_node = nodes_[new_idx];

        for (size_t old_child : old_node.children) {
            auto it = index_map.find(old_child);
            if (it != index_map.end()) {
                new_node.children.push_back(it->second);
            }
        }
    }

    finalized_ = false;
}

std::string PrimIndexGraph::DumpToString() const {
    std::stringstream ss;
    ss << "PrimIndexGraph (" << nodes_.size() << " nodes):\n";

    for (size_t i = 0; i < nodes_.size(); ++i) {
        const Node& node = nodes_[i];
        ss << "  [" << i << "] " << node.site.path.ToString();

        if (node.site.layer_stack) {
            ss << " @ " << node.site.layer_stack->GetIdentifier();
        }

        if (node.arc.type != ArcType::Root) {
            ss << " (" << ArcTypeToString(node.arc.type);
            if (node.arc.sibling_num >= 0) {
                ss << " #" << node.arc.sibling_num;
            }
            ss << ")";
        }

        if (node.flags.culled) {
            ss << " [CULLED]";
        }
        if (node.flags.inert) {
            ss << " [INERT]";
        }
        if (node.flags.has_specs) {
            ss << " [HAS_SPECS]";
        }

        ss << "\n";
    }

    return ss.str();
}

std::string PrimIndexGraph::ExportToDot() const {
    std::stringstream ss;
    ss << "digraph PrimIndexGraph {\n";
    ss << "  rankdir=TB;\n";
    ss << "  node [shape=box];\n";

    // Define nodes
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const Node& node = nodes_[i];
        ss << "  n" << i << " [label=\"";
        ss << node.site.path.ToString();

        if (node.arc.type != ArcType::Root) {
            ss << "\\n(" << ArcTypeToString(node.arc.type) << ")";
        }

        if (node.flags.culled) {
            ss << "\\n[CULLED]";
        }

        ss << "\"];\n";
    }

    // Define edges
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const Node& node = nodes_[i];
        for (size_t child : node.children) {
            ss << "  n" << i << " -> n" << child;

            const Node& child_node = nodes_[child];
            if (child_node.arc.type != ArcType::Root) {
                ss << " [label=\"" << ArcTypeToString(child_node.arc.type) << "\"]";
            }

            ss << ";\n";
        }
    }

    ss << "}\n";
    return ss.str();
}

// PrimIndex implementation

PrimIndex::PrimIndex()
    : graph_(std::make_unique<PrimIndexGraph>()) {
}

PrimIndex::~PrimIndex() = default;

NodeRef PrimIndex::GetRootNode() {
    return graph_->GetRootNode();
}

const NodeRef PrimIndex::GetRootNode() const {
    return graph_->GetRootNode();
}

std::vector<NodeRef> PrimIndex::GetNodesInStrengthOrder() const {
    return graph_->GetNodesInStrengthOrder();
}

const PrimStack& PrimIndex::GetPrimStack() const {
    if (!prim_stack_.has_value()) {
        const_cast<PrimIndex*>(this)->BuildPrimStack();
    }
    return prim_stack_.value();
}

bool PrimIndex::HasSpecs() const {
    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.HasSpecs() && !node.IsInert() && !node.IsCulled()) {
            return true;
        }
    }
    return false;
}

bool PrimIndex::HasSpec(Layer* layer, const Path& path) const {
    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.IsInert() || node.IsCulled()) {
            continue;
        }

        const Site& site = node.GetSite();
        if (site.layer_stack) {
            for (const auto& entry : site.layer_stack->GetLayers()) {
                if (entry.layer == layer) {
                    // Apply relocations and map function
                    Path spec_path = path;
                    if (!entry.relocates.empty()) {
                        for (const auto& [from_path, to_path] : entry.relocates) {
                            if (spec_path.HasPrefix(from_path)) {
                                spec_path = spec_path.ReplacePrefix(from_path, to_path);
                            }
                        }
                    }

                    if (node.GetMapFunction()) {
                        spec_path = node.GetMapFunction()->MapPath(spec_path);
                    }

                    if (spec_path == site.path) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool PrimIndex::HasAnyPayloads() const {
    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.GetArcType() == ArcType::Payload) {
            return true;
        }
    }
    return false;
}

InstanceKey PrimIndex::ComputeInstanceKey() const {
    if (!instance_key_.has_value()) {
        // Use BLAKE3 to hash the composition structure
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        // Hash nodes in strength order
        for (const auto& node : GetNodesInStrengthOrder()) {
            if (node.IsInert() || node.IsCulled()) {
                continue;
            }

            const Site& site = node.GetSite();

            // Hash site path
            std::string path_str = site.path.ToString();
            blake3_hasher_update(&hasher, path_str.c_str(), path_str.length());

            // Hash layer stack identifier
            if (site.layer_stack) {
                std::string id = site.layer_stack->GetIdentifier();
                blake3_hasher_update(&hasher, id.c_str(), id.length());
            }

            // Hash arc type
            int arc_type = static_cast<int>(node.GetArcType());
            blake3_hasher_update(&hasher, &arc_type, sizeof(arc_type));

            // Hash sibling number
            int sibling_num = node.GetSiblingNum();
            blake3_hasher_update(&hasher, &sibling_num, sizeof(sibling_num));
        }

        // Get hash result
        uint8_t hash[32];
        blake3_hasher_finalize(&hasher, hash, 32);

        // Convert first 16 bytes to InstanceKey
        InstanceKey key;
        std::memcpy(&key.hash[0], hash, 16);

        instance_key_ = key;
    }

    return instance_key_.value();
}

bool PrimIndex::CanShareInstance(const PrimIndex& other) const {
    if (!IsInstanceable() || !other.IsInstanceable()) {
        return false;
    }

    return ComputeInstanceKey() == other.ComputeInstanceKey();
}

std::vector<std::string> PrimIndex::GetChildrenNames() const {
    if (!children_names_.has_value()) {
        std::set<std::string> names;

        for (const auto& node : GetNodesInStrengthOrder()) {
            if (node.IsInert() || node.IsCulled() || !node.HasSpecs()) {
                continue;
            }

            const Site& site = node.GetSite();
            if (site.layer_stack) {
                // Get children from each layer in the stack
                for (const auto& entry : site.layer_stack->GetLayers()) {
                    auto prim = entry.layer->GetPrim(site.path);
                    if (prim) {
                        for (const auto& child : prim->GetChildren()) {
                            names.insert(child->GetName());
                        }
                    }
                }
            }
        }

        children_names_ = std::vector<std::string>(names.begin(), names.end());
    }

    return children_names_.value();
}

std::vector<std::string> PrimIndex::GetPropertyNames() const {
    if (!property_names_.has_value()) {
        std::set<std::string> names;

        for (const auto& node : GetNodesInStrengthOrder()) {
            if (node.IsInert() || node.IsCulled() || !node.HasSpecs()) {
                continue;
            }

            const Site& site = node.GetSite();
            if (site.layer_stack) {
                // Get properties from each layer in the stack
                for (const auto& entry : site.layer_stack->GetLayers()) {
                    auto prim = entry.layer->GetPrim(site.path);
                    if (prim) {
                        for (const auto& [name, prop] : prim->GetProperties()) {
                            names.insert(name);
                        }
                    }
                }
            }
        }

        property_names_ = std::vector<std::string>(names.begin(), names.end());
    }

    return property_names_.value();
}

std::vector<Reference> PrimIndex::GetReferences() const {
    std::vector<Reference> result;

    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.GetArcType() == ArcType::Reference && !node.IsInert() && !node.IsCulled()) {
            // Get references from the parent node
            NodeRef parent = node.GetParent();
            if (parent.IsValid()) {
                const Site& parent_site = parent.GetSite();
                if (parent_site.layer_stack) {
                    auto refs = ComposeSiteReferences(
                        parent_site.layer_stack,
                        parent_site.path,
                        nullptr);

                    // Find the reference that created this node
                    int sibling_num = node.GetSiblingNum();
                    if (sibling_num >= 0 && sibling_num < refs.size()) {
                        result.push_back(refs[sibling_num]);
                    }
                }
            }
        }
    }

    return result;
}

std::vector<Payload> PrimIndex::GetPayloads() const {
    std::vector<Payload> result;

    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.GetArcType() == ArcType::Payload && !node.IsInert() && !node.IsCulled()) {
            // Get payloads from the parent node
            NodeRef parent = node.GetParent();
            if (parent.IsValid()) {
                const Site& parent_site = parent.GetSite();
                if (parent_site.layer_stack) {
                    auto payloads = ComposeSitePayloads(
                        parent_site.layer_stack,
                        parent_site.path,
                        nullptr);

                    // Find the payload that created this node
                    int sibling_num = node.GetSiblingNum();
                    if (sibling_num >= 0 && sibling_num < payloads.size()) {
                        result.push_back(payloads[sibling_num]);
                    }
                }
            }
        }
    }

    return result;
}

std::vector<Path> PrimIndex::GetInherits() const {
    std::vector<Path> result;

    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.GetArcType() == ArcType::Inherit && !node.IsInert() && !node.IsCulled()) {
            result.push_back(node.GetSite().path);
        }
    }

    return result;
}

std::vector<Path> PrimIndex::GetSpecializes() const {
    std::vector<Path> result;

    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.GetArcType() == ArcType::Specialize && !node.IsInert() && !node.IsCulled()) {
            result.push_back(node.GetSite().path);
        }
    }

    return result;
}

VariantSelectionMap PrimIndex::GetVariantSelections() const {
    VariantSelectionMap result;

    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.GetArcType() == ArcType::Variant && !node.IsInert() && !node.IsCulled()) {
            // Extract variant set name and selection from node
            // This would typically be stored in the arc's metadata
            // For now, return empty map
        }
    }

    return result;
}

value::Value PrimIndex::ComposeField(const std::string& field_name) const {
    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.IsInert() || node.IsCulled() || !node.HasSpecs()) {
            continue;
        }

        const Site& site = node.GetSite();
        if (site.layer_stack) {
            // Check each layer in the stack
            for (const auto& entry : site.layer_stack->GetLayers()) {
                auto prim = entry.layer->GetPrim(site.path);
                if (prim) {
                    auto it = prim->GetFields().find(field_name);
                    if (it != prim->GetFields().end()) {
                        return it->second;
                    }
                }
            }
        }
    }

    return value::Value();
}

template<typename T>
std::optional<T> PrimIndex::ComposeFieldAs(const std::string& field_name) const {
    value::Value val = ComposeField(field_name);
    if (val.IsEmpty()) {
        return std::nullopt;
    }

    // Convert value to requested type
    // This would use value conversion utilities
    return std::nullopt;
}

void PrimIndex::BuildPrimStack() {
    PrimStack stack;

    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.IsInert() || node.IsCulled() || !node.HasSpecs()) {
            continue;
        }

        const Site& site = node.GetSite();
        if (site.layer_stack) {
            // Add specs from each layer in the stack
            for (const auto& entry : site.layer_stack->GetLayers()) {
                if (entry.layer->HasSpec(site.path)) {
                    PrimSpec spec;
                    spec.layer = entry.layer;
                    spec.path = site.path;
                    stack.push_back(spec);
                }
            }
        }
    }

    prim_stack_ = std::move(stack);
}

void PrimIndex::ClearCaches() {
    prim_stack_.reset();
    children_names_.reset();
    property_names_.reset();
    instance_key_.reset();
}

void PrimIndex::Finalize() {
    graph_->Finalize();
    BuildPrimStack();
}

std::unique_ptr<PrimIndex> PrimIndex::Clone() const {
    auto result = std::make_unique<PrimIndex>();
    result->path_ = path_;
    result->graph_ = graph_->Clone();
    result->payload_state_ = payload_state_;
    result->is_instanceable_ = is_instanceable_;
    result->errors_ = errors_;

    // Don't copy caches
    return result;
}

PrimIndex::Statistics PrimIndex::GetStatistics() const {
    Statistics stats;

    stats.num_nodes = graph_->GetNodeCount();
    stats.is_instanceable = is_instanceable_;
    stats.num_errors = errors_.size();

    for (const auto& node : GetNodesInStrengthOrder()) {
        if (node.IsCulled()) {
            stats.num_culled_nodes++;
        }

        if (node.HasSpecs()) {
            stats.num_specs++;
        }

        switch (node.GetArcType()) {
            case ArcType::Reference:
                stats.num_references++;
                break;
            case ArcType::Payload:
                stats.num_payloads++;
                break;
            case ArcType::Inherit:
                stats.num_inherits++;
                break;
            case ArcType::Specialize:
                stats.num_specializes++;
                break;
            case ArcType::Variant:
                stats.num_variants++;
                break;
            default:
                break;
        }
    }

    return stats;
}

std::string PrimIndex::DumpToString() const {
    std::stringstream ss;
    ss << "PrimIndex for " << path_.ToString() << ":\n";
    ss << graph_->DumpToString();

    auto stats = GetStatistics();
    ss << "Statistics:\n";
    ss << "  Nodes: " << stats.num_nodes << " (" << stats.num_culled_nodes << " culled)\n";
    ss << "  Specs: " << stats.num_specs << "\n";
    ss << "  References: " << stats.num_references << "\n";
    ss << "  Payloads: " << stats.num_payloads << "\n";
    ss << "  Inherits: " << stats.num_inherits << "\n";
    ss << "  Specializes: " << stats.num_specializes << "\n";
    ss << "  Variants: " << stats.num_variants << "\n";
    ss << "  Instanceable: " << (stats.is_instanceable ? "yes" : "no") << "\n";
    ss << "  Errors: " << stats.num_errors << "\n";

    return ss.str();
}

std::string PrimIndex::ExportToDot() const {
    return graph_->ExportToDot();
}

// PrimIndexBuilder implementation

PrimIndexBuilder::PrimIndexBuilder(
    Cache* cache,
    const Path& prim_path,
    const ComputePrimIndexOptions& options)
    : cache_(cache)
    , prim_path_(prim_path)
    , options_(options)
    , prim_index_(std::make_unique<PrimIndex>()) {

    prim_index_->SetPath(prim_path);
}

PrimIndexBuilder::~PrimIndexBuilder() = default;

std::unique_ptr<PrimIndex> PrimIndexBuilder::Build(std::vector<Error>& errors) {
    errors.clear();

    // Phase 1: Build ancestral opinions
    BuildAncestralOpinions();

    // Phase 2: Process composition queue
    ProcessCompositionQueue();

    // Phase 3: Cull empty nodes
    CullEmptyNodes();

    // Phase 4: Enforce permissions
    EnforcePermissions();

    // Phase 5: Determine instanceability
    DetermineInstanceability();

    // Phase 6: Finalize graph
    FinalizeGraph();

    // Copy errors
    errors = errors_;
    for (const auto& error : prim_index_->GetLocalErrors()) {
        errors.push_back(error);
    }

    return std::move(prim_index_);
}

void PrimIndexBuilder::BuildAncestralOpinions() {
    // Start with root layer stack
    LayerStackPtr root_stack = cache_->GetLayerStack(cache_->GetRootLayer());
    if (!root_stack) {
        RecordError(ErrorType::InternalError, "Failed to get root layer stack");
        return;
    }

    // Create root node
    Site root_site;
    root_site.layer_stack = root_stack;
    root_site.path = prim_path_;

    NodeRef root_node = prim_index_->GetGraph().CreateRootNode(root_site);
    if (!root_node.IsValid()) {
        RecordError(ErrorType::InternalError, "Failed to create root node");
        return;
    }

    // Mark root as having specs if it exists in any layer
    bool has_specs = false;
    for (const auto& entry : root_stack->GetLayers()) {
        if (entry.layer->HasSpec(prim_path_)) {
            has_specs = true;
            break;
        }
    }
    root_node.SetHasSpecs(has_specs);

    // Add initial tasks for root node
    AddTask(CompositionTask(TaskType::EvalRelocations, root_node, 10));
    AddTask(CompositionTask(TaskType::EvalReferences, root_node, 20));
    AddTask(CompositionTask(TaskType::EvalPayloads, root_node, 30));
    AddTask(CompositionTask(TaskType::EvalInherits, root_node, 40));
    AddTask(CompositionTask(TaskType::EvalSpecializes, root_node, 50));
    AddTask(CompositionTask(TaskType::EvalVariants, root_node, 60));

    // Build ancestor chain if needed
    if (options_.include_ancestral_opinions && !prim_path_.IsRootPath()) {
        Path parent_path = prim_path_.GetParentPath();
        while (!parent_path.IsEmpty()) {
            // Check if parent has any specs
            bool parent_has_specs = false;
            for (const auto& entry : root_stack->GetLayers()) {
                if (entry.layer->HasSpec(parent_path)) {
                    parent_has_specs = true;
                    break;
                }
            }

            if (parent_has_specs) {
                // Add tasks to incorporate parent's opinions
                // This would typically involve inheriting certain fields
                // For now, we just note that the parent exists
            }

            parent_path = parent_path.GetParentPath();
        }
    }
}

void PrimIndexBuilder::ProcessCompositionQueue() {
    while (HasTasks()) {
        CompositionTask task = PopTask();
        num_tasks_processed_++;

        // Track max queue size for statistics
        if (task_queue_.size() > max_queue_size_) {
            max_queue_size_ = task_queue_.size();
        }

        switch (task.type) {
            case TaskType::EvalRelocations:
                EvalNodeRelocations(task.node);
                break;

            case TaskType::EvalReferences:
                EvalNodeReferences(task.node);
                break;

            case TaskType::EvalPayloads:
                EvalNodePayloads(task.node);
                break;

            case TaskType::EvalInherits:
                EvalNodeInherits(task.node);
                break;

            case TaskType::EvalSpecializes:
                EvalNodeSpecializes(task.node);
                break;

            case TaskType::EvalVariants:
                EvalNodeVariants(task.node);
                break;

            case TaskType::EvalImpliedSpecializes:
                EvalImpliedSpecializes(task.node);
                break;

            case TaskType::EvalVariantSets:
                EvalNodeVariantSets(task.node);
                break;

            default:
                break;
        }
    }
}

void PrimIndexBuilder::AddTask(const CompositionTask& task) {
    task_queue_.push(task);
}

CompositionTask PrimIndexBuilder::PopTask() {
    CompositionTask task = task_queue_.top();
    task_queue_.pop();
    return task;
}

bool PrimIndexBuilder::HasTasks() const {
    return !task_queue_.empty();
}

void PrimIndexBuilder::EvalNodeRelocations(NodeRef node) {
    // Relocations are handled during layer stack composition
    // Here we just apply any additional relocations from the node's arc

    if (!node.IsValid() || node.IsInert()) {
        return;
    }

    // Process implied relocations
    EvalImpliedRelocations(node);
}

void PrimIndexBuilder::EvalNodeReferences(NodeRef node) {
    if (!node.IsValid() || node.IsInert() || !node.HasSpecs()) {
        return;
    }

    const Site& site = node.GetSite();
    if (!site.layer_stack) {
        return;
    }

    // Get references from this site
    std::unordered_set<std::string> expr_vars_used;
    auto references = ComposeSiteReferences(
        site.layer_stack,
        site.path,
        options_.expr_vars ? &expr_vars_used : nullptr);

    // Process each reference
    int sibling_num = 0;
    for (const auto& ref : references) {
        // Resolve asset path
        std::string resolved_path = cache_->ResolveAsset(ref.asset_path);
        if (resolved_path.empty()) {
            RecordError(
                ErrorType::UnresolvedReference,
                "Could not resolve reference: " + ref.asset_path,
                site.path);
            continue;
        }

        // Load referenced layer
        Layer* ref_layer = cache_->FindOrOpenLayer(resolved_path);
        if (!ref_layer) {
            RecordError(
                ErrorType::UnresolvedReference,
                "Could not open referenced layer: " + resolved_path,
                site.path);
            continue;
        }

        // Get layer stack for referenced site
        LayerStackPtr ref_stack = cache_->GetLayerStack(ref_layer);
        if (!ref_stack) {
            RecordError(
                ErrorType::InternalError,
                "Could not get layer stack for reference",
                site.path);
            continue;
        }

        // Create target site
        Site target_site;
        target_site.layer_stack = ref_stack;
        target_site.path = ref.prim_path.empty() ?
            Path("/") : Path(ref.prim_path);

        // Check for cycles
        if (DetectCycle(node, target_site)) {
            RecordError(
                ErrorType::InvalidReferenceStructure,
                "Reference would create a cycle",
                site.path,
                target_site.path,
                ArcType::Reference);
            continue;
        }

        // Create map function for this arc
        MapFunctionPtr map_func = std::make_shared<MapFunction>();
        if (!ref.prim_path.empty()) {
            map_func->SetSourceToTargetMap(
                {{target_site.path, site.path}});
        }

        // Add arc
        NodeRef ref_node = AddArc(
            node,
            ArcType::Reference,
            target_site,
            map_func,
            ref.layer_offset,
            sibling_num++,
            errors_);

        if (ref_node.IsValid()) {
            // Add tasks for referenced node
            AddTask(CompositionTask(TaskType::EvalRelocations, ref_node, 10));
            AddTask(CompositionTask(TaskType::EvalReferences, ref_node, 20));
            AddTask(CompositionTask(TaskType::EvalPayloads, ref_node, 30));
            AddTask(CompositionTask(TaskType::EvalInherits, ref_node, 40));
            AddTask(CompositionTask(TaskType::EvalSpecializes, ref_node, 50));
            AddTask(CompositionTask(TaskType::EvalVariants, ref_node, 60));
        }
    }
}

void PrimIndexBuilder::EvalNodePayloads(NodeRef node) {
    if (!node.IsValid() || node.IsInert() || !node.HasSpecs()) {
        return;
    }

    const Site& site = node.GetSite();
    if (!site.layer_stack) {
        return;
    }

    // Check if payloads should be included
    if (!ShouldIncludePayload(site.path)) {
        return;
    }

    // Get payloads from this site
    std::unordered_set<std::string> expr_vars_used;
    auto payloads = ComposeSitePayloads(
        site.layer_stack,
        site.path,
        options_.expr_vars ? &expr_vars_used : nullptr);

    // Process each payload
    int sibling_num = 0;
    for (const auto& payload : payloads) {
        // Resolve asset path
        std::string resolved_path = cache_->ResolveAsset(payload.asset_path);
        if (resolved_path.empty()) {
            RecordError(
                ErrorType::UnresolvedPayload,
                "Could not resolve payload: " + payload.asset_path,
                site.path);
            continue;
        }

        // Load payload layer
        Layer* payload_layer = cache_->FindOrOpenLayer(resolved_path);
        if (!payload_layer) {
            RecordError(
                ErrorType::UnresolvedPayload,
                "Could not open payload layer: " + resolved_path,
                site.path);
            continue;
        }

        // Get layer stack for payload site
        LayerStackPtr payload_stack = cache_->GetLayerStack(payload_layer);
        if (!payload_stack) {
            RecordError(
                ErrorType::InternalError,
                "Could not get layer stack for payload",
                site.path);
            continue;
        }

        // Create target site
        Site target_site;
        target_site.layer_stack = payload_stack;
        target_site.path = payload.prim_path.empty() ?
            Path("/") : Path(payload.prim_path);

        // Check for cycles
        if (DetectCycle(node, target_site)) {
            RecordError(
                ErrorType::InvalidPayloadStructure,
                "Payload would create a cycle",
                site.path,
                target_site.path,
                ArcType::Payload);
            continue;
        }

        // Create map function for this arc
        MapFunctionPtr map_func = std::make_shared<MapFunction>();
        if (!payload.prim_path.empty()) {
            map_func->SetSourceToTargetMap(
                {{target_site.path, site.path}});
        }

        // Add arc
        NodeRef payload_node = AddArc(
            node,
            ArcType::Payload,
            target_site,
            map_func,
            payload.layer_offset,
            sibling_num++,
            errors_);

        if (payload_node.IsValid()) {
            // Payloads are lazy - only add minimal tasks
            AddTask(CompositionTask(TaskType::EvalRelocations, payload_node, 10));

            // Only evaluate further if explicitly requested
            if (options_.include_all_payloads) {
                AddTask(CompositionTask(TaskType::EvalReferences, payload_node, 20));
                AddTask(CompositionTask(TaskType::EvalPayloads, payload_node, 30));
                AddTask(CompositionTask(TaskType::EvalInherits, payload_node, 40));
                AddTask(CompositionTask(TaskType::EvalSpecializes, payload_node, 50));
                AddTask(CompositionTask(TaskType::EvalVariants, payload_node, 60));
            }
        }
    }
}

void PrimIndexBuilder::EvalNodeInherits(NodeRef node) {
    if (!node.IsValid() || node.IsInert() || !node.HasSpecs()) {
        return;
    }

    const Site& site = node.GetSite();
    if (!site.layer_stack) {
        return;
    }

    // Get inherits from this site
    std::unordered_set<std::string> expr_vars_used;
    auto inherits = ComposeSiteInherits(
        site.layer_stack,
        site.path,
        options_.expr_vars ? &expr_vars_used : nullptr);

    // Process each inherit
    int sibling_num = 0;
    for (const auto& inherit_path : inherits) {
        // Inherits are always in the same layer stack
        Site target_site;
        target_site.layer_stack = site.layer_stack;
        target_site.path = inherit_path;

        // Check for cycles
        if (DetectCycle(node, target_site)) {
            RecordError(
                ErrorType::InvalidInheritStructure,
                "Inherit would create a cycle",
                site.path,
                target_site.path,
                ArcType::Inherit);
            continue;
        }

        // Inherits use identity mapping
        MapFunctionPtr map_func = MapFunction::Identity();

        // Add arc
        NodeRef inherit_node = AddArc(
            node,
            ArcType::Inherit,
            target_site,
            map_func,
            LayerOffset(),
            sibling_num++,
            errors_);

        if (inherit_node.IsValid()) {
            // Process inherited node
            AddTask(CompositionTask(TaskType::EvalRelocations, inherit_node, 10));
            AddTask(CompositionTask(TaskType::EvalReferences, inherit_node, 20));
            AddTask(CompositionTask(TaskType::EvalPayloads, inherit_node, 30));
            AddTask(CompositionTask(TaskType::EvalInherits, inherit_node, 40));
            AddTask(CompositionTask(TaskType::EvalSpecializes, inherit_node, 50));
            AddTask(CompositionTask(TaskType::EvalVariants, inherit_node, 60));

            // Inherits can propagate implied classes
            EvalImpliedClasses(inherit_node);
        }
    }
}

void PrimIndexBuilder::EvalNodeSpecializes(NodeRef node) {
    if (!node.IsValid() || node.IsInert() || !node.HasSpecs()) {
        return;
    }

    const Site& site = node.GetSite();
    if (!site.layer_stack) {
        return;
    }

    // Get specializes from this site
    std::unordered_set<std::string> expr_vars_used;
    auto specializes = ComposeSiteSpecializes(
        site.layer_stack,
        site.path,
        options_.expr_vars ? &expr_vars_used : nullptr);

    // Process each specialize
    int sibling_num = 0;
    for (const auto& specialize_path : specializes) {
        // Specializes are always in the same layer stack
        Site target_site;
        target_site.layer_stack = site.layer_stack;
        target_site.path = specialize_path;

        // Check for cycles
        if (DetectCycle(node, target_site)) {
            RecordError(
                ErrorType::InvalidSpecializeStructure,
                "Specialize would create a cycle",
                site.path,
                target_site.path,
                ArcType::Specialize);
            continue;
        }

        // Specializes use identity mapping
        MapFunctionPtr map_func = MapFunction::Identity();

        // Add arc
        NodeRef specialize_node = AddArc(
            node,
            ArcType::Specialize,
            target_site,
            map_func,
            LayerOffset(),
            sibling_num++,
            errors_);

        if (specialize_node.IsValid()) {
            // Process specialized node
            AddTask(CompositionTask(TaskType::EvalRelocations, specialize_node, 10));
            AddTask(CompositionTask(TaskType::EvalReferences, specialize_node, 20));
            AddTask(CompositionTask(TaskType::EvalPayloads, specialize_node, 30));
            AddTask(CompositionTask(TaskType::EvalInherits, specialize_node, 40));
            AddTask(CompositionTask(TaskType::EvalSpecializes, specialize_node, 50));
            AddTask(CompositionTask(TaskType::EvalVariants, specialize_node, 60));
        }
    }
}

void PrimIndexBuilder::EvalNodeVariants(NodeRef node) {
    if (!node.IsValid() || node.IsInert() || !node.HasSpecs()) {
        return;
    }

    const Site& site = node.GetSite();
    if (!site.layer_stack) {
        return;
    }

    // Get variant sets from this site
    std::vector<std::string> variant_sets = ComposeSiteVariantSets(
        site.layer_stack,
        site.path);

    // Process each variant set
    for (const auto& variant_set : variant_sets) {
        EvalNodeVariantSelection(node, variant_set);
    }
}

void PrimIndexBuilder::EvalNodeVariantSets(NodeRef node) {
    // This is called to process variant set definitions
    // For now, handled in EvalNodeVariants
}

void PrimIndexBuilder::EvalNodeVariantSelection(
    NodeRef node,
    const std::string& variant_set) {

    // Get selected variant
    std::string selection = GetVariantSelection(node.GetSite().path, variant_set);
    if (selection.empty()) {
        // No selection or default selection
        return;
    }

    // Create variant path
    Path variant_path = node.GetSite().path.AppendChild(
        "{" + variant_set + "=" + selection + "}");

    // Create site for variant
    Site variant_site;
    variant_site.layer_stack = node.GetSite().layer_stack;
    variant_site.path = variant_path;

    // Check if variant exists
    bool has_specs = false;
    for (const auto& entry : variant_site.layer_stack->GetLayers()) {
        if (entry.layer->HasSpec(variant_path)) {
            has_specs = true;
            break;
        }
    }

    if (!has_specs) {
        // Variant doesn't exist
        return;
    }

    // Variants use identity mapping
    MapFunctionPtr map_func = MapFunction::Identity();

    // Add variant arc
    NodeRef variant_node = AddArc(
        node,
        ArcType::Variant,
        variant_site,
        map_func,
        LayerOffset(),
        0,  // sibling_num
        errors_);

    if (variant_node.IsValid()) {
        variant_node.SetHasSpecs(true);

        // Process variant node
        AddTask(CompositionTask(TaskType::EvalRelocations, variant_node, 10));
        AddTask(CompositionTask(TaskType::EvalReferences, variant_node, 20));
        AddTask(CompositionTask(TaskType::EvalPayloads, variant_node, 30));
        AddTask(CompositionTask(TaskType::EvalInherits, variant_node, 40));
        AddTask(CompositionTask(TaskType::EvalSpecializes, variant_node, 50));
        // Don't evaluate variants within variants
    }
}

void PrimIndexBuilder::EvalImpliedRelocations(NodeRef node) {
    // Implied relocations come from ancestral relocations
    // These are handled during layer stack composition
}

void PrimIndexBuilder::EvalImpliedClasses(NodeRef node) {
    // Classes can be implied through inherits
    // Check if this node represents a class and propagate

    if (!node.IsValid() || !node.HasSpecs()) {
        return;
    }

    // Check if this is a class (has __class__ specifier)
    bool is_class = false;
    const Site& site = node.GetSite();
    if (site.layer_stack) {
        for (const auto& entry : site.layer_stack->GetLayers()) {
            auto prim = entry.layer->GetPrim(site.path);
            if (prim) {
                auto it = prim->GetFields().find("specifier");
                if (it != prim->GetFields().end()) {
                    // Check if specifier is "class"
                    // is_class = (it->second == "class");
                }
            }
        }
    }

    if (is_class) {
        // Mark node as representing a class
        node.SetIsClass(true);
    }
}

void PrimIndexBuilder::EvalImpliedSpecializes(NodeRef node) {
    // Specializes can be implied through certain composition structures
    // For now, this is a placeholder
}

void PrimIndexBuilder::EvalNodeDynamicPayloads(NodeRef node) {
    // Dynamic payloads are payloads that can be determined at runtime
    // based on metadata or other factors
    // For now, this is a placeholder
}

NodeRef PrimIndexBuilder::AddArc(
    NodeRef parent,
    ArcType arc_type,
    const Site& target_site,
    const MapFunctionPtr& map_function,
    const LayerOffset& layer_offset,
    int sibling_num,
    std::vector<Error>& errors) {

    if (!parent.IsValid()) {
        return NodeRef();
    }

    // Check for duplicate nodes
    NodeRef existing = FindDuplicateNode(target_site);
    if (existing.IsValid()) {
        // Reuse existing node
        return existing;
    }

    // Create arc
    Arc arc;
    arc.type = arc_type;
    arc.sibling_num = sibling_num;
    arc.map_function = map_function;
    arc.layer_offset = layer_offset;

    // Add child node
    NodeRef child = prim_index_->GetGraph().AddChildNode(
        parent, arc, target_site);

    if (child.IsValid()) {
        // Set node properties
        child.SetMapFunction(map_function);

        // Check if target has specs
        bool has_specs = false;
        if (target_site.layer_stack) {
            for (const auto& entry : target_site.layer_stack->GetLayers()) {
                if (entry.layer->HasSpec(target_site.path)) {
                    has_specs = true;
                    break;
                }
            }
        }
        child.SetHasSpecs(has_specs);
    }

    return child;
}

bool PrimIndexBuilder::DetectCycle(
    NodeRef parent,
    const Site& target_site) const {

    // Walk up the parent chain looking for the target site
    NodeRef current = parent;
    while (current.IsValid()) {
        if (current.GetSite() == target_site) {
            return true;  // Found cycle
        }
        current = current.GetParent();
    }

    return false;
}

NodeRef PrimIndexBuilder::FindDuplicateNode(
    const Site& site) const {

    return prim_index_->GetGraph().FindNode(site);
}

bool PrimIndexBuilder::ShouldIncludePayload(const Path& path) const {
    // Check payload inclusion rules
    if (options_.include_all_payloads) {
        return true;
    }

    if (options_.include_payload_predicate) {
        return options_.include_payload_predicate(path);
    }

    // Check if payload state indicates inclusion
    return prim_index_->GetPayloadState() == PayloadState::Included;
}

std::string PrimIndexBuilder::GetVariantSelection(
    const Path& path,
    const std::string& variant_set) const {

    // Check variant selection from options
    if (options_.variant_selections) {
        auto it = options_.variant_selections->find({path, variant_set});
        if (it != options_.variant_selections->end()) {
            return it->second;
        }
    }

    // Check cached selections
    for (const auto& [node, cache] : variant_caches_) {
        auto it = cache.selections.find(variant_set);
        if (it != cache.selections.end()) {
            return it->second;
        }
    }

    // Get default selection from authored variant sets
    // This would query the variant set definition for its default

    return "";
}

void PrimIndexBuilder::RecordError(
    ErrorType type,
    const std::string& message,
    const Path& prim_path,
    const Path& target_path,
    ArcType arc_type) {

    Error error;
    error.type = type;
    error.message = message;
    error.site.path = prim_path;
    error.target_path = target_path;
    error.arc_type = arc_type;

    errors_.push_back(error);
    prim_index_->AddError(error);
}

void PrimIndexBuilder::CullEmptyNodes() {
    // Cull nodes that don't contribute any opinions
    for (const auto& node : prim_index_->GetGraph().GetNodes()) {
        if (!node.HasSpecs() && !node.HasChildren()) {
            prim_index_->GetGraph().CullNode(node);
        }
    }
}

void PrimIndexBuilder::EnforcePermissions() {
    // Check and enforce permission restrictions
    // This would check for "private" or "public" specifiers
    // and adjust visibility accordingly

    for (const auto& node : prim_index_->GetGraph().GetNodes()) {
        if (node.IsCulled()) {
            continue;
        }

        // Check permissions
        const Site& site = node.GetSite();
        if (site.layer_stack) {
            for (const auto& entry : site.layer_stack->GetLayers()) {
                auto prim = entry.layer->GetPrim(site.path);
                if (prim) {
                    auto it = prim->GetFields().find("permission");
                    if (it != prim->GetFields().end()) {
                        // Handle permission restrictions
                        // e.g., if permission == "private", cull external references
                    }
                }
            }
        }
    }
}

void PrimIndexBuilder::DetermineInstanceability() {
    // Determine if this prim index can be instanced

    bool is_instanceable = true;

    // Check for conditions that prevent instancing
    for (const auto& node : prim_index_->GetGraph().GetNodes()) {
        if (node.IsCulled()) {
            continue;
        }

        // Check for non-instanceable arc types
        if (node.GetArcType() == ArcType::Specialize) {
            // Specializes typically prevent instancing
            is_instanceable = false;
            break;
        }

        // Check for local opinions that would make instances differ
        if (node.GetArcType() == ArcType::Root && node.HasSpecs()) {
            // Local opinions can prevent instancing
            // unless they're uniform across instances
        }
    }

    prim_index_->SetInstanceable(is_instanceable);
}

void PrimIndexBuilder::FinalizeGraph() {
    // Finalize the graph structure
    prim_index_->GetGraph().Finalize();

    // Build prim stack
    prim_index_->BuildPrimStack();

    // Set payload state
    if (prim_index_->HasAnyPayloads()) {
        if (options_.include_all_payloads) {
            prim_index_->SetPayloadState(PayloadState::Included);
        } else {
            prim_index_->SetPayloadState(PayloadState::Excluded);
        }
    }
}

// Global compute function

std::unique_ptr<PrimIndex> ComputePrimIndex(
    Cache* cache,
    const Path& prim_path,
    const ComputePrimIndexOptions& options,
    std::vector<Error>* errors) {

    PrimIndexBuilder builder(cache, prim_path, options);

    std::vector<Error> local_errors;
    auto result = builder.Build(local_errors);

    if (errors) {
        *errors = std::move(local_errors);
    }

    return result;
}

} // namespace pcp
} // namespace tydra
} // namespace tinyusdz