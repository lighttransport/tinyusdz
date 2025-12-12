// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Variant and VariantSet implementation

#include "lightusd/variant.hh"
#include "lightusd/prim.hh"
#include <vector>
#include <algorithm>

namespace lightusd {
namespace v1 {

// ============================================================================
// Variant Implementation
// ============================================================================

struct Variant::Impl {
    std::string name_;
    std::unique_ptr<Prim> content_;

    Impl() = default;
    explicit Impl(const std::string& name) : name_(name) {}

    Impl(const Impl& other)
        : name_(other.name_) {
        if (other.content_) {
            content_ = std::unique_ptr<Prim>(new Prim(*other.content_));
        }
    }

    Impl(Impl&& other) noexcept
        : name_(std::move(other.name_))
        , content_(std::move(other.content_)) {}
};

Variant::Variant()
    : impl_(new Impl()) {}

Variant::Variant(const std::string& name)
    : impl_(new Impl(name)) {}

Variant::~Variant() = default;

Variant::Variant(const Variant& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

Variant::Variant(Variant&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

Variant& Variant::operator=(const Variant& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

Variant& Variant::operator=(Variant&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

const std::string& Variant::name() const {
    return impl_->name_;
}

void Variant::set_name(const std::string& name) {
    impl_->name_ = name;
}

const Prim* Variant::content() const {
    return impl_->content_.get();
}

Prim* Variant::content_mutable() {
    return impl_->content_.get();
}

void Variant::set_content(Prim prim) {
    impl_->content_ = std::unique_ptr<Prim>(new Prim(std::move(prim)));
}

bool Variant::has_content() const {
    return impl_->content_ != nullptr;
}

void Variant::clear() {
    impl_->content_.reset();
}

// ============================================================================
// VariantSet Implementation
// ============================================================================

struct VariantSet::Impl {
    std::string name_;
    std::vector<Variant> variants_;

    Impl() = default;
    explicit Impl(const std::string& name) : name_(name) {}

    Impl(const Impl& other)
        : name_(other.name_)
        , variants_(other.variants_) {}

    Impl(Impl&& other) noexcept
        : name_(std::move(other.name_))
        , variants_(std::move(other.variants_)) {}

    Variant* find_variant(const std::string& name) {
        for (auto& v : variants_) {
            if (v.name() == name) {
                return &v;
            }
        }
        return nullptr;
    }

    const Variant* find_variant(const std::string& name) const {
        for (const auto& v : variants_) {
            if (v.name() == name) {
                return &v;
            }
        }
        return nullptr;
    }
};

VariantSet::VariantSet()
    : impl_(new Impl()) {}

VariantSet::VariantSet(const std::string& name)
    : impl_(new Impl(name)) {}

VariantSet::~VariantSet() = default;

VariantSet::VariantSet(const VariantSet& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

VariantSet::VariantSet(VariantSet&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

VariantSet& VariantSet::operator=(const VariantSet& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

VariantSet& VariantSet::operator=(VariantSet&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

const std::string& VariantSet::name() const {
    return impl_->name_;
}

void VariantSet::set_name(const std::string& name) {
    impl_->name_ = name;
}

size_t VariantSet::variant_count() const {
    return impl_->variants_.size();
}

bool VariantSet::has_variant(const std::string& name) const {
    return impl_->find_variant(name) != nullptr;
}

const Variant* VariantSet::get_variant(const std::string& name) const {
    return impl_->find_variant(name);
}

Variant* VariantSet::get_variant_mutable(const std::string& name) {
    return impl_->find_variant(name);
}

const Variant* VariantSet::get_variant(size_t index) const {
    if (index >= impl_->variants_.size()) {
        return nullptr;
    }
    return &impl_->variants_[index];
}

Variant* VariantSet::get_variant_mutable(size_t index) {
    if (index >= impl_->variants_.size()) {
        return nullptr;
    }
    return &impl_->variants_[index];
}

bool VariantSet::add_variant(Variant variant) {
    if (variant.name().empty()) {
        return false;
    }
    if (impl_->find_variant(variant.name())) {
        return false;  // Already exists
    }
    impl_->variants_.push_back(std::move(variant));
    return true;
}

bool VariantSet::remove_variant(const std::string& name) {
    for (auto it = impl_->variants_.begin(); it != impl_->variants_.end(); ++it) {
        if (it->name() == name) {
            impl_->variants_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> VariantSet::variant_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->variants_.size());
    for (const auto& v : impl_->variants_) {
        names.push_back(v.name());
    }
    return names;
}

void VariantSet::clear() {
    impl_->variants_.clear();
}

} // namespace v1
} // namespace lightusd
