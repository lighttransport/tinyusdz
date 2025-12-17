// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PCP Prim Index implementation

#include "lightusd/pcp_prim_index.hh"
#include "lightusd/pcp_layer_stack.hh"
#include "lightusd/layer.hh"

#include <algorithm>
#include <set>

namespace lightusd {
namespace v1 {

// ============================================================================
// PcpPrimIndex::Impl
// ============================================================================

class PcpPrimIndex::Impl {
public:
    Path path_;
    PcpNode root_node_;
    std::vector<PrimStackEntry> prim_stack_;
    std::vector<Token> child_names_;
    std::vector<Token> property_names_;
    std::vector<PcpError> errors_;
    bool has_payloads_ = false;
    bool is_instanceable_ = false;
    bool is_valid_ = false;

    // Helper to flatten composition tree
    void collect_nodes(const PcpNode& node, std::vector<const PcpNode*>& out) const {
        out.push_back(&node);
        for (const auto& child : node.children) {
            collect_nodes(child, out);
        }
    }

    // Helper to count nodes
    size_t count_nodes(const PcpNode& node) const {
        size_t count = 1;
        for (const auto& child : node.children) {
            count += count_nodes(child);
        }
        return count;
    }
};

// ============================================================================
// PcpPrimIndex public interface
// ============================================================================

PcpPrimIndex::PcpPrimIndex() : impl_(new Impl()) {}
PcpPrimIndex::~PcpPrimIndex() = default;
PcpPrimIndex::PcpPrimIndex(PcpPrimIndex&&) noexcept = default;
PcpPrimIndex& PcpPrimIndex::operator=(PcpPrimIndex&&) noexcept = default;

const Path& PcpPrimIndex::path() const {
    return impl_->path_;
}

bool PcpPrimIndex::is_valid() const {
    return impl_->is_valid_;
}

const PcpNode& PcpPrimIndex::root_node() const {
    return impl_->root_node_;
}

PcpNode& PcpPrimIndex::root_node_mutable() {
    return impl_->root_node_;
}

std::vector<const PcpNode*> PcpPrimIndex::get_all_nodes() const {
    std::vector<const PcpNode*> result;
    impl_->collect_nodes(impl_->root_node_, result);
    return result;
}

size_t PcpPrimIndex::node_count() const {
    return impl_->count_nodes(impl_->root_node_);
}

const std::vector<PrimStackEntry>& PcpPrimIndex::prim_stack() const {
    return impl_->prim_stack_;
}

size_t PcpPrimIndex::prim_stack_size() const {
    return impl_->prim_stack_.size();
}

bool PcpPrimIndex::has_specs() const {
    return !impl_->prim_stack_.empty();
}

const std::vector<Token>& PcpPrimIndex::child_names() const {
    return impl_->child_names_;
}

const std::vector<Token>& PcpPrimIndex::property_names() const {
    return impl_->property_names_;
}

bool PcpPrimIndex::has_child(const Token& name) const {
    return std::find(impl_->child_names_.begin(), impl_->child_names_.end(), name)
           != impl_->child_names_.end();
}

bool PcpPrimIndex::has_property(const Token& name) const {
    return std::find(impl_->property_names_.begin(), impl_->property_names_.end(), name)
           != impl_->property_names_.end();
}

bool PcpPrimIndex::has_payloads() const {
    return impl_->has_payloads_;
}

bool PcpPrimIndex::is_instanceable() const {
    return impl_->is_instanceable_;
}

const std::vector<PcpError>& PcpPrimIndex::errors() const {
    return impl_->errors_;
}

bool PcpPrimIndex::has_errors() const {
    return !impl_->errors_.empty();
}

void PcpPrimIndex::add_error(const PcpError& error) {
    impl_->errors_.push_back(error);
}

void PcpPrimIndex::set_path(const Path& path) {
    impl_->path_ = path;
}

void PcpPrimIndex::set_root_node(PcpNode node) {
    impl_->root_node_ = std::move(node);
}

void PcpPrimIndex::add_prim_stack_entry(const PrimStackEntry& entry) {
    impl_->prim_stack_.push_back(entry);
}

void PcpPrimIndex::set_child_names(std::vector<Token> names) {
    impl_->child_names_ = std::move(names);
}

void PcpPrimIndex::set_property_names(std::vector<Token> names) {
    impl_->property_names_ = std::move(names);
}

void PcpPrimIndex::set_has_payloads(bool value) {
    impl_->has_payloads_ = value;
}

void PcpPrimIndex::set_is_instanceable(bool value) {
    impl_->is_instanceable_ = value;
}

void PcpPrimIndex::finalize() {
    impl_->is_valid_ = true;

    // Remove duplicate child names while preserving order
    {
        std::set<Token> seen;
        std::vector<Token> unique;
        for (const auto& name : impl_->child_names_) {
            if (seen.insert(name).second) {
                unique.push_back(name);
            }
        }
        impl_->child_names_ = std::move(unique);
    }

    // Remove duplicate property names while preserving order
    {
        std::set<Token> seen;
        std::vector<Token> unique;
        for (const auto& name : impl_->property_names_) {
            if (seen.insert(name).second) {
                unique.push_back(name);
            }
        }
        impl_->property_names_ = std::move(unique);
    }
}

} // namespace v1
} // namespace lightusd
