// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - PrimIndex implementation

#include "lightusd/prim_index.hh"
#include <map>

namespace lightusd {
namespace v1 {

// ============================================================================
// PrimIndex Implementation
// ============================================================================

struct PrimIndex::Impl {
    Path path_;
    CompositionNode root_node_;
    std::vector<std::string> errors_;
    bool valid_ = false;

    Impl() = default;

    Impl(const Impl& other)
        : path_(other.path_)
        , root_node_(other.root_node_)
        , errors_(other.errors_)
        , valid_(other.valid_) {}

    Impl(Impl&& other) noexcept
        : path_(std::move(other.path_))
        , root_node_(std::move(other.root_node_))
        , errors_(std::move(other.errors_))
        , valid_(other.valid_) {}

    void collect_nodes(const CompositionNode& node,
                       std::vector<const CompositionNode*>& result) const {
        result.push_back(&node);
        for (const auto& child : node.children) {
            collect_nodes(child, result);
        }
    }

    size_t count_layers(const CompositionNode& node) const {
        size_t count = node.layer_id.empty() ? 0 : 1;
        for (const auto& child : node.children) {
            count += count_layers(child);
        }
        return count;
    }
};

PrimIndex::PrimIndex()
    : impl_(new Impl()) {}

PrimIndex::~PrimIndex() = default;

PrimIndex::PrimIndex(const PrimIndex& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

PrimIndex::PrimIndex(PrimIndex&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

PrimIndex& PrimIndex::operator=(const PrimIndex& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

PrimIndex& PrimIndex::operator=(PrimIndex&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

const Path& PrimIndex::path() const {
    return impl_->path_;
}

void PrimIndex::set_path(const Path& path) {
    impl_->path_ = path;
    impl_->valid_ = path.is_valid();
}

bool PrimIndex::is_valid() const {
    return impl_->valid_;
}

const CompositionNode& PrimIndex::root_node() const {
    return impl_->root_node_;
}

CompositionNode& PrimIndex::root_node_mutable() {
    return impl_->root_node_;
}

std::vector<const CompositionNode*> PrimIndex::all_nodes() const {
    std::vector<const CompositionNode*> result;
    impl_->collect_nodes(impl_->root_node_, result);
    return result;
}

size_t PrimIndex::layer_count() const {
    return impl_->count_layers(impl_->root_node_);
}

bool PrimIndex::has_errors() const {
    return !impl_->errors_.empty();
}

const std::vector<std::string>& PrimIndex::errors() const {
    return impl_->errors_;
}

void PrimIndex::add_error(const std::string& error) {
    impl_->errors_.push_back(error);
}

void PrimIndex::clear_errors() {
    impl_->errors_.clear();
}

void PrimIndex::clear() {
    impl_->path_ = Path();
    impl_->root_node_ = CompositionNode();
    impl_->errors_.clear();
    impl_->valid_ = false;
}

void PrimIndex::swap(PrimIndex& other) noexcept {
    impl_.swap(other.impl_);
}

// ============================================================================
// PrimIndexCache Implementation
// ============================================================================

struct PrimIndexCache::Impl {
    std::map<std::string, PrimIndex> cache_;  // Key is path string

    const PrimIndex* find(const Path& path) const {
        std::string key = path.prim_part();
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

PrimIndexCache::PrimIndexCache()
    : impl_(new Impl()) {}

PrimIndexCache::~PrimIndexCache() = default;

const PrimIndex* PrimIndexCache::get(const Path& path) const {
    return impl_->find(path);
}

void PrimIndexCache::insert(const Path& path, PrimIndex index) {
    std::string key = path.prim_part();
    impl_->cache_[key] = std::move(index);
}

bool PrimIndexCache::remove(const Path& path) {
    std::string key = path.prim_part();
    return impl_->cache_.erase(key) > 0;
}

void PrimIndexCache::clear() {
    impl_->cache_.clear();
}

size_t PrimIndexCache::size() const {
    return impl_->cache_.size();
}

bool PrimIndexCache::contains(const Path& path) const {
    return impl_->find(path) != nullptr;
}

} // namespace v1
} // namespace lightusd
