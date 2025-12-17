// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Layer implementation

#include "lightusd/layer.hh"
#include <map>
#include <algorithm>

namespace lightusd {
namespace v1 {

// ============================================================================
// Implementation structure (PIMPL)
// ============================================================================

struct Layer::Impl {
    std::string identifier_;
    std::string real_path_;

    // Sublayer paths (weakest to strongest)
    std::vector<std::string> sublayer_paths_;

    // Root prim specs
    std::vector<std::unique_ptr<Prim>> root_prims_;

    // Layer metadata
    std::string default_prim_;
    double start_time_code_ = 0.0;
    double end_time_code_ = 0.0;
    double frames_per_second_ = 24.0;
    double time_codes_per_second_ = 24.0;
    std::string up_axis_ = "Y";
    double meters_per_unit_ = 0.01;

    // Generic metadata
    std::map<std::string, Value> metadata_;

    // Custom layer data
    std::map<std::string, Value> custom_layer_data_;

    Impl() = default;

    Impl(const std::string& identifier)
        : identifier_(identifier) {}

    Impl(const Impl& other)
        : identifier_(other.identifier_)
        , real_path_(other.real_path_)
        , sublayer_paths_(other.sublayer_paths_)
        , default_prim_(other.default_prim_)
        , start_time_code_(other.start_time_code_)
        , end_time_code_(other.end_time_code_)
        , frames_per_second_(other.frames_per_second_)
        , time_codes_per_second_(other.time_codes_per_second_)
        , up_axis_(other.up_axis_)
        , meters_per_unit_(other.meters_per_unit_)
        , metadata_(other.metadata_)
        , custom_layer_data_(other.custom_layer_data_) {
        // Deep copy prims
        root_prims_.reserve(other.root_prims_.size());
        for (const auto& prim : other.root_prims_) {
            root_prims_.push_back(std::unique_ptr<Prim>(new Prim(*prim)));
        }
    }

    Impl(Impl&& other) noexcept
        : identifier_(std::move(other.identifier_))
        , real_path_(std::move(other.real_path_))
        , sublayer_paths_(std::move(other.sublayer_paths_))
        , root_prims_(std::move(other.root_prims_))
        , default_prim_(std::move(other.default_prim_))
        , start_time_code_(other.start_time_code_)
        , end_time_code_(other.end_time_code_)
        , frames_per_second_(other.frames_per_second_)
        , time_codes_per_second_(other.time_codes_per_second_)
        , up_axis_(std::move(other.up_axis_))
        , meters_per_unit_(other.meters_per_unit_)
        , metadata_(std::move(other.metadata_))
        , custom_layer_data_(std::move(other.custom_layer_data_)) {}

    Prim* find_root_prim(const std::string& name) {
        for (auto& prim : root_prims_) {
            if (prim->name() == name) {
                return prim.get();
            }
        }
        return nullptr;
    }

    const Prim* find_root_prim(const std::string& name) const {
        for (const auto& prim : root_prims_) {
            if (prim->name() == name) {
                return prim.get();
            }
        }
        return nullptr;
    }
};

// ============================================================================
// Constructors / Destructor
// ============================================================================

Layer::Layer()
    : impl_(new Impl()) {}

Layer::~Layer() = default;

Layer::Layer(const Layer& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

Layer::Layer(Layer&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

Layer& Layer::operator=(const Layer& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

Layer& Layer::operator=(Layer&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

Layer Layer::create() {
    return Layer();
}

Layer Layer::create(const std::string& identifier) {
    Layer layer;
    layer.set_identifier(identifier);
    return layer;
}

// ============================================================================
// Identity
// ============================================================================

const std::string& Layer::identifier() const {
    return impl_->identifier_;
}

void Layer::set_identifier(const std::string& id) {
    impl_->identifier_ = id;
}

bool Layer::is_anonymous() const {
    return impl_->identifier_.empty() ||
           impl_->identifier_.find("anon:") == 0;
}

const std::string& Layer::real_path() const {
    return impl_->real_path_;
}

void Layer::set_real_path(const std::string& path) {
    impl_->real_path_ = path;
}

// ============================================================================
// Sublayers
// ============================================================================

const std::vector<std::string>& Layer::sublayer_paths() const {
    return impl_->sublayer_paths_;
}

void Layer::set_sublayer_paths(const std::vector<std::string>& paths) {
    impl_->sublayer_paths_ = paths;
}

void Layer::add_sublayer_path(const std::string& path) {
    impl_->sublayer_paths_.push_back(path);
}

void Layer::insert_sublayer_path(size_t index, const std::string& path) {
    if (index >= impl_->sublayer_paths_.size()) {
        impl_->sublayer_paths_.push_back(path);
    } else {
        impl_->sublayer_paths_.insert(impl_->sublayer_paths_.begin() + static_cast<ptrdiff_t>(index), path);
    }
}

bool Layer::remove_sublayer_path(const std::string& path) {
    auto it = std::find(impl_->sublayer_paths_.begin(),
                        impl_->sublayer_paths_.end(), path);
    if (it != impl_->sublayer_paths_.end()) {
        impl_->sublayer_paths_.erase(it);
        return true;
    }
    return false;
}

// ============================================================================
// Root Prim Specs
// ============================================================================

size_t Layer::root_prim_count() const {
    return impl_->root_prims_.size();
}

const Prim* Layer::root_prim(size_t index) const {
    if (index >= impl_->root_prims_.size()) {
        return nullptr;
    }
    return impl_->root_prims_[index].get();
}

Prim* Layer::root_prim_mutable(size_t index) {
    if (index >= impl_->root_prims_.size()) {
        return nullptr;
    }
    return impl_->root_prims_[index].get();
}

const Prim* Layer::root_prim(const std::string& name) const {
    return impl_->find_root_prim(name);
}

Prim* Layer::root_prim_mutable(const std::string& name) {
    return impl_->find_root_prim(name);
}

bool Layer::add_root_prim(Prim prim) {
    if (prim.name().empty()) {
        return false;
    }
    if (impl_->find_root_prim(prim.name())) {
        return false;
    }
    impl_->root_prims_.push_back(std::unique_ptr<Prim>(new Prim(std::move(prim))));
    return true;
}

bool Layer::remove_root_prim(const std::string& name) {
    for (auto it = impl_->root_prims_.begin(); it != impl_->root_prims_.end(); ++it) {
        if ((*it)->name() == name) {
            impl_->root_prims_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> Layer::root_prim_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->root_prims_.size());
    for (const auto& prim : impl_->root_prims_) {
        names.push_back(prim->name());
    }
    return names;
}

// ============================================================================
// Layer Metadata
// ============================================================================

const std::string& Layer::default_prim() const {
    return impl_->default_prim_;
}

void Layer::set_default_prim(const std::string& name) {
    impl_->default_prim_ = name;
}

double Layer::start_time_code() const {
    return impl_->start_time_code_;
}

void Layer::set_start_time_code(double t) {
    impl_->start_time_code_ = t;
}

double Layer::end_time_code() const {
    return impl_->end_time_code_;
}

void Layer::set_end_time_code(double t) {
    impl_->end_time_code_ = t;
}

double Layer::frames_per_second() const {
    return impl_->frames_per_second_;
}

void Layer::set_frames_per_second(double fps) {
    impl_->frames_per_second_ = fps;
}

double Layer::time_codes_per_second() const {
    return impl_->time_codes_per_second_;
}

void Layer::set_time_codes_per_second(double tcps) {
    impl_->time_codes_per_second_ = tcps;
}

const std::string& Layer::up_axis() const {
    return impl_->up_axis_;
}

void Layer::set_up_axis(const std::string& axis) {
    impl_->up_axis_ = axis;
}

double Layer::meters_per_unit() const {
    return impl_->meters_per_unit_;
}

void Layer::set_meters_per_unit(double mpu) {
    impl_->meters_per_unit_ = mpu;
}

std::string Layer::documentation() const {
    const Value* v = get_metadata("documentation");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Layer::set_documentation(const std::string& doc) {
    set_metadata("documentation", Value::from_string(doc));
}

std::string Layer::comment() const {
    const Value* v = get_metadata("comment");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Layer::set_comment(const std::string& comment) {
    set_metadata("comment", Value::from_string(comment));
}

// ============================================================================
// Generic Metadata
// ============================================================================

bool Layer::has_metadata(const std::string& key) const {
    return impl_->metadata_.find(key) != impl_->metadata_.end();
}

const Value* Layer::get_metadata(const std::string& key) const {
    auto it = impl_->metadata_.find(key);
    if (it == impl_->metadata_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Layer::set_metadata(const std::string& key, const Value& value) {
    impl_->metadata_[key] = value;
}

bool Layer::remove_metadata(const std::string& key) {
    return impl_->metadata_.erase(key) > 0;
}

std::vector<std::string> Layer::metadata_keys() const {
    std::vector<std::string> keys;
    keys.reserve(impl_->metadata_.size());
    for (const auto& pair : impl_->metadata_) {
        keys.push_back(pair.first);
    }
    return keys;
}

// ============================================================================
// Custom Layer Data
// ============================================================================

bool Layer::has_custom_layer_data(const std::string& key) const {
    return impl_->custom_layer_data_.find(key) != impl_->custom_layer_data_.end();
}

const Value* Layer::get_custom_layer_data(const std::string& key) const {
    auto it = impl_->custom_layer_data_.find(key);
    if (it == impl_->custom_layer_data_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Layer::set_custom_layer_data(const std::string& key, const Value& value) {
    impl_->custom_layer_data_[key] = value;
}

// ============================================================================
// Prim Lookup
// ============================================================================

const Prim* Layer::get_prim_at_path(const Path& path) const {
    if (!path.is_valid() || !path.is_absolute()) {
        return nullptr;
    }

    std::string prim_path = path.prim_part();
    if (prim_path.empty() || prim_path == "/") {
        return nullptr;
    }

    // Remove leading "/"
    if (prim_path[0] == '/') {
        prim_path = prim_path.substr(1);
    }

    // Split by "/"
    std::vector<std::string> components;
    size_t start = 0;
    size_t pos;
    while ((pos = prim_path.find('/', start)) != std::string::npos) {
        if (pos > start) {
            components.push_back(prim_path.substr(start, pos - start));
        }
        start = pos + 1;
    }
    if (start < prim_path.size()) {
        components.push_back(prim_path.substr(start));
    }

    if (components.empty()) {
        return nullptr;
    }

    // Navigate from root
    const Prim* current = impl_->find_root_prim(components[0]);
    if (!current) {
        return nullptr;
    }

    for (size_t i = 1; i < components.size(); ++i) {
        current = current->child(components[i]);
        if (!current) {
            return nullptr;
        }
    }

    return current;
}

Prim* Layer::get_prim_at_path_mutable(const Path& path) {
    // Use const version and cast away const (safe since we own the data)
    return const_cast<Prim*>(get_prim_at_path(path));
}

// ============================================================================
// Utility
// ============================================================================

void Layer::clear() {
    impl_->identifier_.clear();
    impl_->real_path_.clear();
    impl_->sublayer_paths_.clear();
    impl_->root_prims_.clear();
    impl_->default_prim_.clear();
    impl_->start_time_code_ = 0.0;
    impl_->end_time_code_ = 0.0;
    impl_->frames_per_second_ = 24.0;
    impl_->time_codes_per_second_ = 24.0;
    impl_->up_axis_ = "Y";
    impl_->meters_per_unit_ = 0.01;
    impl_->metadata_.clear();
    impl_->custom_layer_data_.clear();
}

void Layer::swap(Layer& other) noexcept {
    impl_.swap(other.impl_);
}

bool Layer::empty() const {
    return impl_->root_prims_.empty() && impl_->sublayer_paths_.empty();
}

} // namespace v1
} // namespace lightusd
