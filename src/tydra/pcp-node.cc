// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP Node Implementation - Node operations in composition graph

#include "pcp-node.hh"
#include "pcp-prim-index.hh"
#include "pcp-map-function.hh"
#include <queue>
#include <stack>

namespace tinyusdz {
namespace tydra {
namespace pcp {

// NodeRef Implementation

Site NodeRef::GetSite() const {
    if (!IsValid()) {
        return Site();
    }
    return graph_->GetNode(index_).site;
}

LayerStackPtr NodeRef::GetLayerStack() const {
    Site site = GetSite();
    return site.layer_stack;
}

Path NodeRef::GetPath() const {
    Site site = GetSite();
    return site.path;
}

const Arc& NodeRef::GetArc() const {
    static Arc invalid_arc;
    if (!IsValid()) {
        return invalid_arc;
    }
    return graph_->GetNode(index_).arc;
}

ArcType NodeRef::GetArcType() const {
    return GetArc().type;
}

NodeRef NodeRef::GetParent() const {
    if (!IsValid()) {
        return NodeRef();
    }

    auto& node = graph_->GetNode(index_);
    if (node.parent == SIZE_MAX) {
        return NodeRef();
    }

    return NodeRef(graph_, node.parent);
}

NodeRef NodeRef::GetOrigin() const {
    const Arc& arc = GetArc();
    if (arc.origin_node_index == SIZE_MAX) {
        return NodeRef();
    }
    return NodeRef(graph_, arc.origin_node_index);
}

int NodeRef::GetSiblingNumAtOrigin() const {
    return GetArc().sibling_num_at_origin;
}

int NodeRef::GetNamespaceDepth() const {
    return GetArc().namespace_depth;
}

std::shared_ptr<MapFunction> NodeRef::GetMapToParent() const {
    return GetArc().map_to_parent;
}

std::shared_ptr<MapFunction> NodeRef::GetMapToRoot() const {
    // Compose all map functions from this node to root
    if (IsRootNode()) {
        return MapFunction::CreateIdentity();
    }

    std::vector<MapFunctionPtr> maps;
    NodeRef current = *this;

    while (current.IsValid() && !current.IsRootNode()) {
        auto map = current.GetMapToParent();
        if (map && !map->IsIdentity()) {
            maps.push_back(map);
        }
        current = current.GetParent();
    }

    if (maps.empty()) {
        return MapFunction::CreateIdentity();
    }

    // Compose in reverse order (from this to root)
    MapFunctionPtr result = maps.back();
    for (int i = maps.size() - 2; i >= 0; --i) {
        result = result->Compose(maps[i]);
    }

    return result;
}

std::vector<NodeRef> NodeRef::GetChildren() const {
    if (!IsValid()) {
        return {};
    }

    auto& node = graph_->GetNode(index_);
    std::vector<NodeRef> children;
    children.reserve(node.children.size());

    for (size_t child_idx : node.children) {
        children.emplace_back(graph_, child_idx);
    }

    return children;
}

size_t NodeRef::GetChildCount() const {
    if (!IsValid()) {
        return 0;
    }
    return graph_->GetNode(index_).children.size();
}

NodeRef NodeRef::GetChild(size_t index) const {
    if (!IsValid()) {
        return NodeRef();
    }

    auto& node = graph_->GetNode(index_);
    if (index >= node.children.size()) {
        return NodeRef();
    }

    return NodeRef(graph_, node.children[index]);
}

NodeRef NodeRef::GetNextSibling() const {
    if (!IsValid() || IsRootNode()) {
        return NodeRef();
    }

    NodeRef parent = GetParent();
    if (!parent.IsValid()) {
        return NodeRef();
    }

    auto siblings = parent.GetChildren();
    for (size_t i = 0; i < siblings.size() - 1; ++i) {
        if (siblings[i] == *this) {
            return siblings[i + 1];
        }
    }

    return NodeRef();
}

NodeRef NodeRef::GetPrevSibling() const {
    if (!IsValid() || IsRootNode()) {
        return NodeRef();
    }

    NodeRef parent = GetParent();
    if (!parent.IsValid()) {
        return NodeRef();
    }

    auto siblings = parent.GetChildren();
    for (size_t i = 1; i < siblings.size(); ++i) {
        if (siblings[i] == *this) {
            return siblings[i - 1];
        }
    }

    return NodeRef();
}

NodeFlags NodeRef::GetFlags() const {
    if (!IsValid()) {
        return NodeFlags();
    }
    return graph_->GetNode(index_).flags;
}

bool NodeRef::HasSpecs() const {
    return GetFlags().has_specs;
}

bool NodeRef::IsInert() const {
    return GetFlags().is_inert;
}

bool NodeRef::IsImplied() const {
    return GetFlags().is_implied;
}

bool NodeRef::IsAncestral() const {
    return GetFlags().is_ancestral;
}

bool NodeRef::IsRestricted() const {
    return GetFlags().is_restricted;
}

bool NodeRef::IsProhibited() const {
    return GetFlags().is_prohibited;
}

bool NodeRef::IsInstanceable() const {
    return GetFlags().is_instanceable;
}

bool NodeRef::ContributesSpecs() const {
    return GetFlags().contributes_specs;
}

bool NodeRef::IsCulled() const {
    return GetFlags().was_culled;
}

bool NodeRef::NeedsChildrenCheck() const {
    return GetFlags().needs_children_check;
}

void NodeRef::SetFlags(const NodeFlags& flags) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags = flags;
}

void NodeRef::SetHasSpecs(bool has_specs) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.has_specs = has_specs;
}

void NodeRef::SetInert(bool inert) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.is_inert = inert;
}

void NodeRef::SetImplied(bool implied) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.is_implied = implied;
}

void NodeRef::SetAncestral(bool ancestral) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.is_ancestral = ancestral;
}

void NodeRef::SetRestricted(bool restricted) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.is_restricted = restricted;
}

void NodeRef::SetProhibited(bool prohibited) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.is_prohibited = prohibited;
}

void NodeRef::SetInstanceable(bool instanceable) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.is_instanceable = instanceable;
}

void NodeRef::SetContributesSpecs(bool contributes) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.contributes_specs = contributes;
}

void NodeRef::SetCulled(bool culled) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.was_culled = culled;
}

void NodeRef::SetNeedsChildrenCheck(bool needs_check) {
    if (!IsValid()) {
        return;
    }
    graph_->GetNode(index_).flags.needs_children_check = needs_check;
}

bool NodeRef::IsRootNode() const {
    if (!IsValid()) {
        return false;
    }
    return GetArc().type == ArcType::Root;
}

bool NodeRef::IsDueToAncestor() const {
    return IsAncestral();
}

NodeRef NodeRef::GetIntroductionNode() const {
    // Find where this arc was introduced
    NodeRef current = *this;

    while (current.IsValid()) {
        if (!current.IsImplied() && !current.IsAncestral()) {
            return current;
        }

        NodeRef origin = current.GetOrigin();
        if (origin.IsValid()) {
            current = origin;
        } else {
            current = current.GetParent();
        }
    }

    return NodeRef();
}

Path NodeRef::TranslatePathToRoot(const Path& path) const {
    return TranslatePathFromNodeToRoot(*this, path);
}

Path NodeRef::TranslatePathFromRoot(const Path& path) const {
    return TranslatePathFromRootToNode(*this, path);
}

std::vector<NodeRef> NodeRef::GetSubtree() const {
    std::vector<NodeRef> subtree;

    if (!IsValid()) {
        return subtree;
    }

    // Depth-first traversal
    std::stack<NodeRef> stack;
    stack.push(*this);

    while (!stack.empty()) {
        NodeRef current = stack.top();
        stack.pop();

        subtree.push_back(current);

        // Add children in reverse order for correct traversal
        auto children = current.GetChildren();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.push(*it);
        }
    }

    return subtree;
}

NodeRef NodeRef::FindNodeInSubtree(const Site& site) const {
    auto subtree = GetSubtree();

    for (const auto& node : subtree) {
        if (node.GetSite() == site) {
            return node;
        }
    }

    return NodeRef();
}

bool NodeRef::HasDirectArc() const {
    if (!IsValid()) {
        return false;
    }

    // Root always has direct arc
    if (IsRootNode()) {
        return true;
    }

    // Check if this arc is not implied or ancestral
    return !IsImplied() && !IsAncestral();
}

size_t NodeRef::GetDepthInTree() const {
    if (!IsValid() || IsRootNode()) {
        return 0;
    }

    size_t depth = 0;
    NodeRef current = GetParent();

    while (current.IsValid() && !current.IsRootNode()) {
        depth++;
        current = current.GetParent();
    }

    return depth + 1;
}

bool NodeRef::CanContributeSpecs() const {
    if (!IsValid()) {
        return false;
    }

    // Check various conditions that prevent contribution
    if (IsInert() || IsCulled() || IsProhibited()) {
        return false;
    }

    // Check permissions if enforced
    if (IsRestricted()) {
        // Would need permission context to determine
        return false;
    }

    return HasSpecs();
}

std::optional<PrimSpec> NodeRef::GetPrimSpec() const {
    if (!IsValid() || !HasSpecs()) {
        return std::nullopt;
    }

    Site site = GetSite();
    if (!site.layer_stack) {
        return std::nullopt;
    }

    // Get strongest layer with spec
    for (const auto& entry : site.layer_stack->GetLayers()) {
        if (entry.layer && entry.layer->HasSpec(site.path)) {
            PrimSpec spec;
            spec.layer = entry.layer;
            spec.path = site.path;
            return spec;
        }
    }

    return std::nullopt;
}

std::string NodeRef::GetDebugString() const {
    if (!IsValid()) {
        return "Invalid NodeRef";
    }

    std::ostringstream ss;
    ss << "Node[" << index_ << "]: ";
    ss << GetPath().full_path_name();
    ss << " (" << GetArcTypeName(GetArcType()) << ")";

    if (HasSpecs()) {
        ss << " [has_specs]";
    }
    if (IsInert()) {
        ss << " [inert]";
    }
    if (IsCulled()) {
        ss << " [culled]";
    }

    return ss.str();
}

std::string NodeRef::GetPathString() const {
    return GetPath().full_path_name();
}

// NodeIterator Implementation

NodeIterator::NodeIterator(NodeRef root, Order order)
    : root_(root)
    , order_(order) {

    Reset();
}

bool NodeIterator::HasNext() const {
    return current_index_ < queue_.size();
}

NodeRef NodeIterator::Next() {
    if (!HasNext()) {
        return NodeRef();
    }

    NodeRef current = queue_[current_index_++];

    // Handle skip children flag for depth-first
    if (skip_children_ && order_ == Order::DepthFirst) {
        skip_children_ = false;
        // Skip to next sibling or parent's sibling
        // This is handled by the queue already being built
    }

    return current;
}

void NodeIterator::Reset() {
    current_index_ = 0;
    skip_children_ = false;
    InitializeQueue();
}

void NodeIterator::SkipChildren() {
    skip_children_ = true;
}

void NodeIterator::InitializeQueue() {
    queue_.clear();

    if (!root_.IsValid()) {
        return;
    }

    switch (order_) {
        case Order::DepthFirst: {
            // Pre-order depth-first traversal
            std::stack<NodeRef> stack;
            stack.push(root_);

            while (!stack.empty()) {
                NodeRef current = stack.top();
                stack.pop();

                queue_.push_back(current);

                // Add children in reverse order for correct traversal
                auto children = current.GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it) {
                    stack.push(*it);
                }
            }
            break;
        }

        case Order::BreadthFirst: {
            // Level-order traversal
            std::queue<NodeRef> bfs_queue;
            bfs_queue.push(root_);

            while (!bfs_queue.empty()) {
                NodeRef current = bfs_queue.front();
                bfs_queue.pop();

                queue_.push_back(current);

                for (const auto& child : current.GetChildren()) {
                    bfs_queue.push(child);
                }
            }
            break;
        }

        case Order::StrengthOrder: {
            // Build queue based on arc strength
            struct NodeWithStrength {
                NodeRef node;
                int strength;  // Lower value = stronger

                bool operator<(const NodeWithStrength& other) const {
                    return strength > other.strength;  // Priority queue is max-heap
                }
            };

            std::priority_queue<NodeWithStrength> pq;

            // Assign strength based on arc type
            auto GetArcStrength = [](ArcType type) -> int {
                switch (type) {
                    case ArcType::Root:      return 0;
                    case ArcType::Inherit:   return 1;
                    case ArcType::Variant:   return 2;
                    case ArcType::Relocate:  return 3;
                    case ArcType::Reference: return 4;
                    case ArcType::Payload:   return 5;
                    case ArcType::Specialize: return 6;
                    default:                 return 7;
                }
            };

            // Collect all nodes first
            std::vector<NodeRef> all_nodes = root_.GetSubtree();

            // Sort by strength
            for (const auto& node : all_nodes) {
                NodeWithStrength ns;
                ns.node = node;
                ns.strength = GetArcStrength(node.GetArcType());

                // Add secondary sorting by depth for stability
                ns.strength = ns.strength * 1000 + node.GetDepthInTree();

                pq.push(ns);
            }

            // Build queue from priority queue
            while (!pq.empty()) {
                queue_.push_back(pq.top().node);
                pq.pop();
            }
            break;
        }

        case Order::WeakToStrong: {
            // Same as strength order but reversed
            InitializeQueue();  // First build in strength order
            std::reverse(queue_.begin(), queue_.end());
            break;
        }
    }
}

// PrimIndexGraph Implementation

PrimIndexGraph::PrimIndexGraph() = default;
PrimIndexGraph::~PrimIndexGraph() = default;

NodeRef PrimIndexGraph::CreateRootNode(const Site& site) {
    Arc root_arc;
    root_arc.type = ArcType::Root;
    root_arc.parent_node_index = SIZE_MAX;
    root_arc.origin_node_index = SIZE_MAX;
    root_arc.namespace_depth = GetNamespaceDepth(site.path);

    size_t index = AddNodeInternal(site, root_arc, SIZE_MAX);
    return NodeRef(this, index);
}

NodeRef PrimIndexGraph::AddChildNode(
    NodeRef parent,
    const Arc& arc,
    const Site& site) {

    if (!parent.IsValid()) {
        return NodeRef();
    }

    size_t parent_index = parent.GetIndex();
    size_t index = AddNodeInternal(site, arc, parent_index);

    // Add to parent's children
    nodes_[parent_index].children.push_back(index);

    return NodeRef(this, index);
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

    // Root is typically the first node
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].arc.type == ArcType::Root) {
            return NodeRef(this, i);
        }
    }

    return NodeRef();
}

const NodeRef PrimIndexGraph::GetRootNode() const {
    return const_cast<PrimIndexGraph*>(this)->GetRootNode();
}

std::vector<NodeRef> PrimIndexGraph::GetNodes() const {
    std::vector<NodeRef> result;
    result.reserve(nodes_.size());

    for (size_t i = 0; i < nodes_.size(); ++i) {
        result.emplace_back(const_cast<PrimIndexGraph*>(this), i);
    }

    return result;
}

std::vector<NodeRef> PrimIndexGraph::GetNodesInStrengthOrder() const {
    auto root = GetRootNode();
    if (!root.IsValid()) {
        return {};
    }

    NodeIterator iter(root, NodeIterator::Order::StrengthOrder);
    std::vector<NodeRef> result;

    while (iter.HasNext()) {
        result.push_back(iter.Next());
    }

    return result;
}

NodeRef PrimIndexGraph::FindNode(const Site& site) {
    auto it = site_to_node_.find(site);
    if (it != site_to_node_.end()) {
        return NodeRef(this, it->second);
    }
    return NodeRef();
}

const NodeRef PrimIndexGraph::FindNode(const Site& site) const {
    return const_cast<PrimIndexGraph*>(this)->FindNode(site);
}

bool PrimIndexGraph::HasNode(const Site& site) const {
    return site_to_node_.count(site) > 0;
}

void PrimIndexGraph::RemoveNode(NodeRef node) {
    if (!node.IsValid() || node.GetGraph() != this) {
        return;
    }

    size_t index = node.GetIndex();

    // Remove from site map
    site_to_node_.erase(nodes_[index].site);

    // Remove from parent's children
    if (nodes_[index].parent != SIZE_MAX) {
        auto& parent_children = nodes_[nodes_[index].parent].children;
        parent_children.erase(
            std::remove(parent_children.begin(), parent_children.end(), index),
            parent_children.end());
    }

    // Recursively remove children
    for (size_t child_idx : nodes_[index].children) {
        RemoveNode(NodeRef(this, child_idx));
    }

    // Mark as invalid (don't actually remove from vector to preserve indices)
    nodes_[index] = Node();
}

void PrimIndexGraph::CullNode(NodeRef node) {
    if (node.IsValid() && node.GetGraph() == this) {
        node.SetCulled(true);
    }
}

bool PrimIndexGraph::IsNodeCulled(NodeRef node) const {
    return node.IsValid() && node.GetGraph() == this && node.IsCulled();
}

void PrimIndexGraph::Finalize() {
    if (finalized_) {
        return;
    }

    // Compact node storage, rebuild indices, etc.
    // For now, just mark as finalized
    finalized_ = true;
}

std::unique_ptr<PrimIndexGraph> PrimIndexGraph::Clone() const {
    auto clone = std::make_unique<PrimIndexGraph>();
    clone->nodes_ = nodes_;
    clone->site_to_node_ = site_to_node_;
    clone->finalized_ = false;  // Clone is not finalized
    return clone;
}

void PrimIndexGraph::MergeGraph(
    const PrimIndexGraph& other,
    NodeRef parent_node,
    const Arc& arc_to_parent) {

    if (!parent_node.IsValid() || parent_node.GetGraph() != this) {
        return;
    }

    // Map from other graph indices to this graph indices
    std::unordered_map<size_t, size_t> index_map;

    // Copy nodes from other graph
    for (size_t i = 0; i < other.nodes_.size(); ++i) {
        const Node& other_node = other.nodes_[i];

        // Skip invalid nodes
        if (other_node.site.path.empty()) {
            continue;
        }

        // Create new node in this graph
        Node new_node = other_node;

        // Update arc if this is the root of the merged graph
        if (other_node.arc.type == ArcType::Root) {
            new_node.arc = arc_to_parent;
            new_node.parent = parent_node.GetIndex();
        } else if (other_node.parent != SIZE_MAX) {
            // Update parent index (will be fixed up later)
            new_node.parent = other_node.parent;
        }

        // Add to this graph
        size_t new_index = nodes_.size();
        nodes_.push_back(new_node);
        site_to_node_[new_node.site] = new_index;
        index_map[i] = new_index;

        // Connect to parent if this is the root being merged
        if (other_node.arc.type == ArcType::Root) {
            nodes_[parent_node.GetIndex()].children.push_back(new_index);
        }
    }

    // Fix up parent and children indices
    for (const auto& [old_idx, new_idx] : index_map) {
        Node& node = nodes_[new_idx];

        // Update parent index
        if (node.parent != SIZE_MAX && node.parent != parent_node.GetIndex()) {
            auto it = index_map.find(node.parent);
            if (it != index_map.end()) {
                node.parent = it->second;
            }
        }

        // Update children indices
        std::vector<size_t> new_children;
        for (size_t old_child : node.children) {
            auto it = index_map.find(old_child);
            if (it != index_map.end()) {
                new_children.push_back(it->second);
            }
        }
        node.children = new_children;

        // Update origin node index in arc
        if (node.arc.origin_node_index != SIZE_MAX) {
            auto it = index_map.find(node.arc.origin_node_index);
            if (it != index_map.end()) {
                node.arc.origin_node_index = it->second;
            }
        }
    }
}

std::string PrimIndexGraph::DumpToString() const {
    std::ostringstream ss;
    ss << "PrimIndexGraph (" << nodes_.size() << " nodes):\n";

    auto root = GetRootNode();
    if (root.IsValid()) {
        DumpNodeRecursive(ss, root, 0);
    }

    return ss.str();
}

std::string PrimIndexGraph::ExportToDot() const {
    std::ostringstream ss;
    ss << "digraph PrimIndexGraph {\n";
    ss << "  rankdir=TB;\n";
    ss << "  node [shape=box];\n\n";

    // Output nodes
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const Node& node = nodes_[i];
        if (node.site.path.empty()) {
            continue;  // Skip invalid nodes
        }

        ss << "  node_" << i << " [label=\"";
        ss << node.site.path.full_path_name() << "\\n";
        ss << "(" << GetArcTypeName(node.arc.type) << ")";

        if (node.flags.has_specs) {
            ss << "\\n[specs]";
        }
        if (node.flags.was_culled) {
            ss << "\\n[culled]";
        }

        ss << "\"];\n";
    }

    ss << "\n";

    // Output edges
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const Node& node = nodes_[i];
        for (size_t child_idx : node.children) {
            ss << "  node_" << i << " -> node_" << child_idx;
            ss << " [label=\"" << GetArcTypeName(nodes_[child_idx].arc.type) << "\"];\n";
        }
    }

    ss << "}\n";
    return ss.str();
}

// Private methods

size_t PrimIndexGraph::AddNodeInternal(
    const Site& site,
    const Arc& arc,
    size_t parent_index) {

    Node node;
    node.site = site;
    node.arc = arc;
    node.parent = parent_index;

    size_t index = nodes_.size();
    nodes_.push_back(node);

    // Add to site map
    site_to_node_[site] = index;

    return index;
}

bool PrimIndexGraph::CompareNodeStrength(size_t a, size_t b) const {
    if (a >= nodes_.size() || b >= nodes_.size()) {
        return false;
    }

    const Node& node_a = nodes_[a];
    const Node& node_b = nodes_[b];

    // Compare by arc type strength
    if (node_a.arc.type != node_b.arc.type) {
        return IsStrongerThan(node_a.arc.type, node_b.arc.type);
    }

    // Same arc type - compare by sibling number
    if (node_a.arc.sibling_num_at_origin != node_b.arc.sibling_num_at_origin) {
        return node_a.arc.sibling_num_at_origin < node_b.arc.sibling_num_at_origin;
    }

    // Compare by namespace depth
    return node_a.arc.namespace_depth < node_b.arc.namespace_depth;
}

void PrimIndexGraph::DumpNodeRecursive(
    std::ostream& out,
    NodeRef node,
    int indent) const {

    for (int i = 0; i < indent; ++i) {
        out << "  ";
    }

    out << "- " << node.GetDebugString() << "\n";

    for (const auto& child : node.GetChildren()) {
        DumpNodeRecursive(out, child, indent + 1);
    }
}

} // namespace pcp
} // namespace tydra
} // namespace tinyusdz