// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Composition arc implementation

#include "lightusd/composition.hh"

namespace lightusd {
namespace v1 {

const char* composition_arc_type_name(CompositionArcType type) {
    switch (type) {
        case CompositionArcType::None:       return "none";
        case CompositionArcType::SubLayer:   return "sublayer";
        case CompositionArcType::Inherit:    return "inherit";
        case CompositionArcType::VariantSet: return "variantSet";
        case CompositionArcType::Reference:  return "reference";
        case CompositionArcType::Payload:    return "payload";
        case CompositionArcType::Specialize: return "specialize";
        default:                             return "unknown";
    }
}

// ============================================================================
// ReferenceList Implementation
// ============================================================================

struct ReferenceList::Impl {
    std::vector<Reference> explicit_;
    std::vector<Reference> prepended_;
    std::vector<Reference> appended_;
    std::vector<Reference> deleted_;
    bool has_explicit_ = false;
};

ReferenceList::ReferenceList()
    : impl_(new Impl()) {}

ReferenceList::~ReferenceList() = default;

ReferenceList::ReferenceList(const ReferenceList& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

ReferenceList::ReferenceList(ReferenceList&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) impl_.reset(new Impl());
}

ReferenceList& ReferenceList::operator=(const ReferenceList& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

ReferenceList& ReferenceList::operator=(ReferenceList&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) impl_.reset(new Impl());
    }
    return *this;
}

bool ReferenceList::empty() const {
    return !impl_->has_explicit_ &&
           impl_->prepended_.empty() &&
           impl_->appended_.empty() &&
           impl_->deleted_.empty();
}

const std::vector<Reference>& ReferenceList::explicit_items() const {
    return impl_->explicit_;
}

void ReferenceList::set_explicit(const std::vector<Reference>& refs) {
    impl_->explicit_ = refs;
    impl_->has_explicit_ = true;
}

void ReferenceList::set_explicit(std::vector<Reference>&& refs) {
    impl_->explicit_ = std::move(refs);
    impl_->has_explicit_ = true;
}

const std::vector<Reference>& ReferenceList::prepended_items() const {
    return impl_->prepended_;
}

void ReferenceList::prepend(const Reference& ref) {
    impl_->prepended_.push_back(ref);
}

void ReferenceList::set_prepended(const std::vector<Reference>& refs) {
    impl_->prepended_ = refs;
}

const std::vector<Reference>& ReferenceList::appended_items() const {
    return impl_->appended_;
}

void ReferenceList::append(const Reference& ref) {
    impl_->appended_.push_back(ref);
}

void ReferenceList::set_appended(const std::vector<Reference>& refs) {
    impl_->appended_ = refs;
}

const std::vector<Reference>& ReferenceList::deleted_items() const {
    return impl_->deleted_;
}

void ReferenceList::add_delete(const Reference& ref) {
    impl_->deleted_.push_back(ref);
}

void ReferenceList::clear() {
    impl_->explicit_.clear();
    impl_->prepended_.clear();
    impl_->appended_.clear();
    impl_->deleted_.clear();
    impl_->has_explicit_ = false;
}

bool ReferenceList::has_explicit() const {
    return impl_->has_explicit_;
}

// ============================================================================
// PayloadList Implementation
// ============================================================================

struct PayloadList::Impl {
    std::vector<Payload> explicit_;
    std::vector<Payload> prepended_;
    std::vector<Payload> appended_;
    bool has_explicit_ = false;
};

PayloadList::PayloadList()
    : impl_(new Impl()) {}

PayloadList::~PayloadList() = default;

PayloadList::PayloadList(const PayloadList& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

PayloadList::PayloadList(PayloadList&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) impl_.reset(new Impl());
}

PayloadList& PayloadList::operator=(const PayloadList& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

PayloadList& PayloadList::operator=(PayloadList&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) impl_.reset(new Impl());
    }
    return *this;
}

bool PayloadList::empty() const {
    return !impl_->has_explicit_ &&
           impl_->prepended_.empty() &&
           impl_->appended_.empty();
}

const std::vector<Payload>& PayloadList::explicit_items() const {
    return impl_->explicit_;
}

void PayloadList::set_explicit(const std::vector<Payload>& payloads) {
    impl_->explicit_ = payloads;
    impl_->has_explicit_ = true;
}

const std::vector<Payload>& PayloadList::prepended_items() const {
    return impl_->prepended_;
}

void PayloadList::prepend(const Payload& payload) {
    impl_->prepended_.push_back(payload);
}

void PayloadList::set_prepended(const std::vector<Payload>& payloads) {
    impl_->prepended_ = payloads;
}

const std::vector<Payload>& PayloadList::appended_items() const {
    return impl_->appended_;
}

void PayloadList::append(const Payload& payload) {
    impl_->appended_.push_back(payload);
}

void PayloadList::set_appended(const std::vector<Payload>& payloads) {
    impl_->appended_ = payloads;
}

void PayloadList::clear() {
    impl_->explicit_.clear();
    impl_->prepended_.clear();
    impl_->appended_.clear();
    impl_->has_explicit_ = false;
}

bool PayloadList::has_explicit() const {
    return impl_->has_explicit_;
}

// ============================================================================
// PathList Implementation
// ============================================================================

struct PathList::Impl {
    std::vector<Path> explicit_;
    std::vector<Path> prepended_;
    std::vector<Path> appended_;
    bool has_explicit_ = false;
};

PathList::PathList()
    : impl_(new Impl()) {}

PathList::~PathList() = default;

PathList::PathList(const PathList& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

PathList::PathList(PathList&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) impl_.reset(new Impl());
}

PathList& PathList::operator=(const PathList& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

PathList& PathList::operator=(PathList&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) impl_.reset(new Impl());
    }
    return *this;
}

bool PathList::empty() const {
    return !impl_->has_explicit_ &&
           impl_->prepended_.empty() &&
           impl_->appended_.empty();
}

const std::vector<Path>& PathList::explicit_items() const {
    return impl_->explicit_;
}

void PathList::set_explicit(const std::vector<Path>& paths) {
    impl_->explicit_ = paths;
    impl_->has_explicit_ = true;
}

const std::vector<Path>& PathList::prepended_items() const {
    return impl_->prepended_;
}

void PathList::prepend(const Path& path) {
    impl_->prepended_.push_back(path);
}

void PathList::set_prepended(const std::vector<Path>& paths) {
    impl_->prepended_ = paths;
}

const std::vector<Path>& PathList::appended_items() const {
    return impl_->appended_;
}

void PathList::append(const Path& path) {
    impl_->appended_.push_back(path);
}

void PathList::set_appended(const std::vector<Path>& paths) {
    impl_->appended_ = paths;
}

void PathList::clear() {
    impl_->explicit_.clear();
    impl_->prepended_.clear();
    impl_->appended_.clear();
    impl_->has_explicit_ = false;
}

bool PathList::has_explicit() const {
    return impl_->has_explicit_;
}

} // namespace v1
} // namespace lightusd
