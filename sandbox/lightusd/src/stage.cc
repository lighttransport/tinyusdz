// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Stage implementation

#include "lightusd/stage.hh"
#include "lightusd/token.hh"
#include "lightusd/usda_writer.hh"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <map>

namespace lightusd {
namespace v1 {

// ============================================================================
// Implementation structure (PIMPL)
// ============================================================================

struct Stage::Impl {
    std::vector<std::unique_ptr<Prim>> root_prims_;

    // Stage metadata (specific well-known fields)
    double start_time_code_ = 0.0;
    double end_time_code_ = 0.0;
    double frames_per_second_ = 24.0;
    double time_codes_per_second_ = 24.0;
    std::string up_axis_ = "Y";
    double meters_per_unit_ = 0.01;  // centimeters (common for CG)
    std::string default_prim_;

    // Generic metadata dictionary (for any layer metadata)
    std::map<std::string, Value> metadata_;

    // Custom layer data dictionary (nested under customLayerData)
    std::map<std::string, Value> custom_layer_data_;

    Impl() = default;

    Impl(const Impl& other)
        : start_time_code_(other.start_time_code_)
        , end_time_code_(other.end_time_code_)
        , frames_per_second_(other.frames_per_second_)
        , time_codes_per_second_(other.time_codes_per_second_)
        , up_axis_(other.up_axis_)
        , meters_per_unit_(other.meters_per_unit_)
        , default_prim_(other.default_prim_)
        , metadata_(other.metadata_)
        , custom_layer_data_(other.custom_layer_data_) {
        // Deep copy prims
        root_prims_.reserve(other.root_prims_.size());
        for (const auto& prim : other.root_prims_) {
            root_prims_.push_back(std::unique_ptr<Prim>(new Prim(*prim)));
        }
    }

    Impl(Impl&& other) noexcept
        : root_prims_(std::move(other.root_prims_))
        , start_time_code_(other.start_time_code_)
        , end_time_code_(other.end_time_code_)
        , frames_per_second_(other.frames_per_second_)
        , time_codes_per_second_(other.time_codes_per_second_)
        , up_axis_(std::move(other.up_axis_))
        , meters_per_unit_(other.meters_per_unit_)
        , default_prim_(std::move(other.default_prim_))
        , metadata_(std::move(other.metadata_))
        , custom_layer_data_(std::move(other.custom_layer_data_)) {
    }

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

Stage::Stage()
    : impl_(new Impl()) {
}

Stage::~Stage() = default;

Stage::Stage(const Stage& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {
}

Stage::Stage(Stage&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

Stage& Stage::operator=(const Stage& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

Stage& Stage::operator=(Stage&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

Stage Stage::create() {
    return Stage();
}

// ============================================================================
// Root Prims
// ============================================================================

size_t Stage::root_prim_count() const {
    return impl_->root_prims_.size();
}

const Prim* Stage::root_prim(size_t index) const {
    if (index >= impl_->root_prims_.size()) {
        return nullptr;
    }
    return impl_->root_prims_[index].get();
}

Prim* Stage::root_prim_mutable(size_t index) {
    if (index >= impl_->root_prims_.size()) {
        return nullptr;
    }
    return impl_->root_prims_[index].get();
}

const Prim* Stage::root_prim(const std::string& name) const {
    return impl_->find_root_prim(name);
}

Prim* Stage::root_prim_mutable(const std::string& name) {
    return impl_->find_root_prim(name);
}

bool Stage::add_root_prim(Prim prim) {
    if (prim.name().empty()) {
        return false;
    }

    // Check for duplicate name
    if (impl_->find_root_prim(prim.name())) {
        return false;
    }

    // Set root path
    prim.set_path(Path("/" + prim.name()));
    prim.update_child_paths();

    impl_->root_prims_.push_back(std::unique_ptr<Prim>(new Prim(std::move(prim))));
    return true;
}

bool Stage::remove_root_prim(const std::string& name) {
    for (auto it = impl_->root_prims_.begin(); it != impl_->root_prims_.end(); ++it) {
        if ((*it)->name() == name) {
            impl_->root_prims_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> Stage::root_prim_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->root_prims_.size());
    for (const auto& prim : impl_->root_prims_) {
        names.push_back(prim->name());
    }
    return names;
}

// ============================================================================
// Prim Lookup
// ============================================================================

Result<const Prim*> Stage::get_prim_at_path(const Path& path) const {
    if (!path.is_valid() || !path.is_absolute()) {
        return Error("Invalid or non-absolute path");
    }

    // Parse path components
    std::string prim_path = path.prim_part();
    if (prim_path.empty() || prim_path == "/") {
        return Error("Cannot get pseudo-root prim");
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
        return Error("Empty path components");
    }

    // Navigate from root
    const Prim* current = impl_->find_root_prim(components[0]);
    if (!current) {
        return Error("Root prim not found: " + components[0]);
    }

    for (size_t i = 1; i < components.size(); ++i) {
        current = current->child(components[i]);
        if (!current) {
            return Error("Child prim not found: " + components[i]);
        }
    }

    return current;
}

Result<Prim*> Stage::get_prim_at_path_mutable(const Path& path) {
    auto result = get_prim_at_path(path);
    if (!result.ok()) {
        return Error(result.error().message);
    }
    // const_cast is safe here since we own the prims
    return const_cast<Prim*>(result.value());
}

// ============================================================================
// Traversal
// ============================================================================

void Stage::traverse_impl(const Prim& prim, int depth, VisitorFn visitor, void* userdata) {
    if (!visitor(prim, depth, userdata)) {
        return;
    }

    for (size_t i = 0; i < prim.child_count(); ++i) {
        const Prim* child = prim.child(i);
        if (child) {
            traverse_impl(*child, depth + 1, visitor, userdata);
        }
    }
}

void Stage::traverse_mutable_impl(Prim& prim, int depth, MutableVisitorFn visitor, void* userdata) {
    if (!visitor(prim, depth, userdata)) {
        return;
    }

    for (size_t i = 0; i < prim.child_count(); ++i) {
        Prim* child = prim.child_mutable(i);
        if (child) {
            traverse_mutable_impl(*child, depth + 1, visitor, userdata);
        }
    }
}

void Stage::traverse(VisitorFn visitor, void* userdata) const {
    if (!visitor) {
        return;
    }

    for (const auto& prim : impl_->root_prims_) {
        traverse_impl(*prim, 0, visitor, userdata);
    }
}

void Stage::traverse_mutable(MutableVisitorFn visitor, void* userdata) {
    if (!visitor) {
        return;
    }

    for (auto& prim : impl_->root_prims_) {
        traverse_mutable_impl(*prim, 0, visitor, userdata);
    }
}

// ============================================================================
// Generic Metadata Dictionary
// ============================================================================

bool Stage::has_metadata(const std::string& key) const {
    return impl_->metadata_.find(key) != impl_->metadata_.end();
}

const Value* Stage::get_metadata(const std::string& key) const {
    auto it = impl_->metadata_.find(key);
    if (it == impl_->metadata_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Stage::set_metadata(const std::string& key, const Value& value) {
    impl_->metadata_[key] = value;
}

bool Stage::remove_metadata(const std::string& key) {
    return impl_->metadata_.erase(key) > 0;
}

std::vector<std::string> Stage::metadata_keys() const {
    std::vector<std::string> keys;
    keys.reserve(impl_->metadata_.size());
    for (const auto& pair : impl_->metadata_) {
        keys.push_back(pair.first);
    }
    return keys;
}

size_t Stage::metadata_count() const {
    return impl_->metadata_.size();
}

// ============================================================================
// Common Stage Metadata Accessors
// ============================================================================

double Stage::start_time_code() const {
    return impl_->start_time_code_;
}

void Stage::set_start_time_code(double t) {
    impl_->start_time_code_ = t;
}

double Stage::end_time_code() const {
    return impl_->end_time_code_;
}

void Stage::set_end_time_code(double t) {
    impl_->end_time_code_ = t;
}

double Stage::frames_per_second() const {
    return impl_->frames_per_second_;
}

void Stage::set_frames_per_second(double fps) {
    impl_->frames_per_second_ = fps;
}

double Stage::time_codes_per_second() const {
    return impl_->time_codes_per_second_;
}

void Stage::set_time_codes_per_second(double tcps) {
    impl_->time_codes_per_second_ = tcps;
}

const std::string& Stage::up_axis() const {
    return impl_->up_axis_;
}

void Stage::set_up_axis(const std::string& axis) {
    impl_->up_axis_ = axis;
}

double Stage::meters_per_unit() const {
    return impl_->meters_per_unit_;
}

void Stage::set_meters_per_unit(double mpu) {
    impl_->meters_per_unit_ = mpu;
}

std::string Stage::documentation() const {
    const Value* v = get_metadata("documentation");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Stage::set_documentation(const std::string& doc) {
    set_metadata("documentation", Value::from_string(doc));
}

std::string Stage::comment() const {
    const Value* v = get_metadata("comment");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Stage::set_comment(const std::string& comment) {
    set_metadata("comment", Value::from_string(comment));
}

std::string Stage::owner() const {
    const Value* v = get_metadata("owner");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Stage::set_owner(const std::string& owner) {
    set_metadata("owner", Value::from_string(owner));
}

// ============================================================================
// Custom Layer Data
// ============================================================================

bool Stage::has_custom_layer_data(const std::string& key) const {
    return impl_->custom_layer_data_.find(key) != impl_->custom_layer_data_.end();
}

const Value* Stage::get_custom_layer_data(const std::string& key) const {
    auto it = impl_->custom_layer_data_.find(key);
    if (it == impl_->custom_layer_data_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Stage::set_custom_layer_data(const std::string& key, const Value& value) {
    impl_->custom_layer_data_[key] = value;
}

bool Stage::remove_custom_layer_data(const std::string& key) {
    return impl_->custom_layer_data_.erase(key) > 0;
}

std::vector<std::string> Stage::custom_layer_data_keys() const {
    std::vector<std::string> keys;
    keys.reserve(impl_->custom_layer_data_.size());
    for (const auto& pair : impl_->custom_layer_data_) {
        keys.push_back(pair.first);
    }
    return keys;
}

size_t Stage::custom_layer_data_count() const {
    return impl_->custom_layer_data_.size();
}

// ============================================================================
// Default Prim
// ============================================================================

const std::string& Stage::default_prim() const {
    return impl_->default_prim_;
}

void Stage::set_default_prim(const std::string& name) {
    impl_->default_prim_ = name;
}

const Prim* Stage::get_default_prim() const {
    if (impl_->default_prim_.empty()) {
        return nullptr;
    }
    return impl_->find_root_prim(impl_->default_prim_);
}

// ============================================================================
// Path Management
// ============================================================================

void Stage::commit() {
    // Update all prim paths
    for (auto& prim : impl_->root_prims_) {
        prim->set_path(Path("/" + prim->name()));
        prim->update_child_paths();
    }
}

// ============================================================================
// Export
// ============================================================================

std::string Stage::to_usda() const {
    UsdaWriter writer;
    return writer.format(*this);
}

std::string Stage::to_usda(int indent_size, bool use_tabs) const {
    UsdaFormatOptions opts;
    opts.use_tabs = use_tabs;
    opts.indent_size = static_cast<uint8_t>(indent_size);
    opts.build_indent_string();

    UsdaWriter writer(opts);
    return writer.format(*this);
}

// ============================================================================
// Utility
// ============================================================================

void Stage::swap(Stage& other) noexcept {
    impl_.swap(other.impl_);
}

void Stage::clear() {
    impl_->root_prims_.clear();
    impl_->start_time_code_ = 0.0;
    impl_->end_time_code_ = 0.0;
    impl_->frames_per_second_ = 24.0;
    impl_->time_codes_per_second_ = 24.0;
    impl_->up_axis_ = "Y";
    impl_->meters_per_unit_ = 0.01;
    impl_->default_prim_.clear();
    impl_->metadata_.clear();
    impl_->custom_layer_data_.clear();
}

} // namespace v1
} // namespace lightusd
