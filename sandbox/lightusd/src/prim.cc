// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Prim implementation

#include "lightusd/prim.hh"
#include "lightusd/token.hh"
#include "lightusd/variant.hh"
#include "lightusd/composition.hh"
#include <algorithm>
#include <map>
#include <unordered_map>

namespace lightusd {
namespace v1 {

// ============================================================================
// Implementation structure (PIMPL)
// ============================================================================

struct Prim::Impl {
    std::string name_;
    std::string type_name_;
    Path path_;
    Specifier specifier_ = Specifier::Def;
    bool active_ = true;
    bool instanceable_ = false;

    // Metadata dictionary (kind, purpose, hidden, doc, apiSchemas, etc.)
    std::map<std::string, Value> metadata_;

    // Custom data dictionary (nested under customData)
    std::map<std::string, Value> custom_data_;

    // Asset info dictionary (nested under assetInfo)
    std::map<std::string, Value> asset_info_;

    // Properties stored by name
    std::unordered_map<std::string, Property> properties_;

    // Property insertion order (for explicit ordering)
    std::vector<std::string> property_order_;

    // Children (ordered)
    std::vector<std::unique_ptr<Prim>> children_;

    // Variant sets (ordered)
    std::vector<VariantSet> variant_sets_;

    // Variant selections
    std::map<std::string, std::string> variant_selections_;

    // Composition arcs
    ReferenceList references_;
    PayloadList payloads_;
    PathList inherits_;
    PathList specializes_;

    // Value clips
    ClipSets clips_;

    Impl() = default;

    Impl(const std::string& name)
        : name_(name) {
    }

    Impl(const std::string& name, const std::string& type_name)
        : name_(name)
        , type_name_(type_name) {
    }

    Impl(const Impl& other)
        : name_(other.name_)
        , type_name_(other.type_name_)
        , path_(other.path_)
        , specifier_(other.specifier_)
        , active_(other.active_)
        , instanceable_(other.instanceable_)
        , metadata_(other.metadata_)
        , custom_data_(other.custom_data_)
        , asset_info_(other.asset_info_)
        , properties_(other.properties_)
        , property_order_(other.property_order_)
        , variant_sets_(other.variant_sets_)
        , variant_selections_(other.variant_selections_)
        , references_(other.references_)
        , payloads_(other.payloads_)
        , inherits_(other.inherits_)
        , specializes_(other.specializes_)
        , clips_(other.clips_) {
        // Deep copy children
        children_.reserve(other.children_.size());
        for (const auto& child : other.children_) {
            children_.push_back(std::unique_ptr<Prim>(new Prim(*child)));
        }
    }

    Impl(Impl&& other) noexcept
        : name_(std::move(other.name_))
        , type_name_(std::move(other.type_name_))
        , path_(std::move(other.path_))
        , specifier_(other.specifier_)
        , active_(other.active_)
        , instanceable_(other.instanceable_)
        , metadata_(std::move(other.metadata_))
        , custom_data_(std::move(other.custom_data_))
        , asset_info_(std::move(other.asset_info_))
        , properties_(std::move(other.properties_))
        , property_order_(std::move(other.property_order_))
        , children_(std::move(other.children_))
        , variant_sets_(std::move(other.variant_sets_))
        , variant_selections_(std::move(other.variant_selections_))
        , references_(std::move(other.references_))
        , payloads_(std::move(other.payloads_))
        , inherits_(std::move(other.inherits_))
        , specializes_(std::move(other.specializes_))
        , clips_(std::move(other.clips_)) {
    }

    Prim* find_child(const std::string& name) {
        for (auto& child : children_) {
            if (child->name() == name) {
                return child.get();
            }
        }
        return nullptr;
    }

    const Prim* find_child(const std::string& name) const {
        for (const auto& child : children_) {
            if (child->name() == name) {
                return child.get();
            }
        }
        return nullptr;
    }

    VariantSet* find_variant_set(const std::string& name) {
        for (auto& vs : variant_sets_) {
            if (vs.name() == name) {
                return &vs;
            }
        }
        return nullptr;
    }

    const VariantSet* find_variant_set(const std::string& name) const {
        for (const auto& vs : variant_sets_) {
            if (vs.name() == name) {
                return &vs;
            }
        }
        return nullptr;
    }
};

// ============================================================================
// Constructors / Destructor
// ============================================================================

Prim::Prim()
    : impl_(new Impl()) {
}

Prim::Prim(const std::string& name)
    : impl_(new Impl(name)) {
}

Prim::Prim(const std::string& name, const std::string& type_name)
    : impl_(new Impl(name, type_name)) {
}

Prim::~Prim() = default;

Prim::Prim(const Prim& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {
}

Prim::Prim(Prim&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

Prim& Prim::operator=(const Prim& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

Prim& Prim::operator=(Prim&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

// ============================================================================
// Identity
// ============================================================================

const std::string& Prim::name() const {
    return impl_->name_;
}

void Prim::set_name(const std::string& name) {
    impl_->name_ = name;
}

const std::string& Prim::type_name() const {
    return impl_->type_name_;
}

void Prim::set_type_name(const std::string& type_name) {
    impl_->type_name_ = type_name;
}

const Path& Prim::path() const {
    return impl_->path_;
}

// ============================================================================
// Specifier
// ============================================================================

Specifier Prim::specifier() const {
    return impl_->specifier_;
}

void Prim::set_specifier(Specifier s) {
    impl_->specifier_ = s;
}

// ============================================================================
// Active State
// ============================================================================

bool Prim::is_active() const {
    return impl_->active_;
}

void Prim::set_active(bool active) {
    impl_->active_ = active;
}

// ============================================================================
// Metadata (Generic Dictionary)
// ============================================================================

bool Prim::has_metadata(const std::string& key) const {
    return impl_->metadata_.find(key) != impl_->metadata_.end();
}

const Value* Prim::get_metadata(const std::string& key) const {
    auto it = impl_->metadata_.find(key);
    if (it == impl_->metadata_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Prim::set_metadata(const std::string& key, const Value& value) {
    impl_->metadata_[key] = value;
}

bool Prim::remove_metadata(const std::string& key) {
    return impl_->metadata_.erase(key) > 0;
}

std::vector<std::string> Prim::metadata_keys() const {
    std::vector<std::string> keys;
    keys.reserve(impl_->metadata_.size());
    for (const auto& pair : impl_->metadata_) {
        keys.push_back(pair.first);
    }
    return keys;
}

size_t Prim::metadata_count() const {
    return impl_->metadata_.size();
}

// ============================================================================
// Common Metadata Accessors
// ============================================================================

std::string Prim::kind() const {
    const Value* v = get_metadata("kind");
    if (v) {
        if (const Token* t = v->as_token()) {
            return t->str();
        }
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Prim::set_kind(const std::string& kind) {
    set_metadata("kind", Value::from_token(Token(kind)));
}

std::string Prim::purpose() const {
    const Value* v = get_metadata("purpose");
    if (v) {
        if (const Token* t = v->as_token()) {
            return t->str();
        }
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Prim::set_purpose(const std::string& purpose) {
    set_metadata("purpose", Value::from_token(Token(purpose)));
}

bool Prim::is_hidden() const {
    const Value* v = get_metadata("hidden");
    if (v) {
        if (const bool* b = v->as_bool()) {
            return *b;
        }
    }
    return false;
}

void Prim::set_hidden(bool hidden) {
    set_metadata("hidden", Value::from_bool(hidden));
}

std::string Prim::documentation() const {
    const Value* v = get_metadata("documentation");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Prim::set_documentation(const std::string& doc) {
    set_metadata("documentation", Value::from_string(doc));
}

std::vector<std::string> Prim::api_schemas() const {
    std::vector<std::string> result;
    const Value* v = get_metadata("apiSchemas");
    if (v && v->is_array()) {
        // For now, return empty - array of tokens needs special handling
        // TODO: Implement proper token array support
    }
    return result;
}

void Prim::set_api_schemas(const std::vector<std::string>& schemas) {
    // Store as string array for now
    // TODO: Store as token array when supported
    set_metadata("apiSchemas", Value::from_string_array(schemas));
}

void Prim::add_api_schema(const std::string& schema) {
    std::vector<std::string> schemas = api_schemas();
    schemas.push_back(schema);
    set_api_schemas(schemas);
}

// ============================================================================
// Custom Data
// ============================================================================

bool Prim::has_custom_data(const std::string& key) const {
    return impl_->custom_data_.find(key) != impl_->custom_data_.end();
}

const Value* Prim::get_custom_data(const std::string& key) const {
    auto it = impl_->custom_data_.find(key);
    if (it == impl_->custom_data_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Prim::set_custom_data(const std::string& key, const Value& value) {
    impl_->custom_data_[key] = value;
}

// ============================================================================
// Asset Info
// ============================================================================

bool Prim::has_asset_info(const std::string& key) const {
    return impl_->asset_info_.find(key) != impl_->asset_info_.end();
}

const Value* Prim::get_asset_info(const std::string& key) const {
    auto it = impl_->asset_info_.find(key);
    if (it == impl_->asset_info_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Prim::set_asset_info(const std::string& key, const Value& value) {
    impl_->asset_info_[key] = value;
}

std::vector<std::string> Prim::asset_info_keys() const {
    std::vector<std::string> keys;
    keys.reserve(impl_->asset_info_.size());
    for (const auto& pair : impl_->asset_info_) {
        keys.push_back(pair.first);
    }
    return keys;
}

size_t Prim::asset_info_count() const {
    return impl_->asset_info_.size();
}

std::string Prim::asset_identifier() const {
    const Value* v = get_asset_info("identifier");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Prim::set_asset_identifier(const std::string& path) {
    set_asset_info("identifier", Value::from_string(path));
}

std::string Prim::asset_name() const {
    const Value* v = get_asset_info("name");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Prim::set_asset_name(const std::string& name) {
    set_asset_info("name", Value::from_string(name));
}

std::string Prim::asset_version() const {
    const Value* v = get_asset_info("version");
    if (v) {
        if (const std::string* s = v->as_string()) {
            return *s;
        }
    }
    return "";
}

void Prim::set_asset_version(const std::string& version) {
    set_asset_info("version", Value::from_string(version));
}

// ============================================================================
// Properties
// ============================================================================

bool Prim::has_property(const std::string& name) const {
    return impl_->properties_.find(name) != impl_->properties_.end();
}

const Property* Prim::get_property(const std::string& name) const {
    auto it = impl_->properties_.find(name);
    if (it == impl_->properties_.end()) {
        return nullptr;
    }
    return &it->second;
}

Property* Prim::get_property_mutable(const std::string& name) {
    auto it = impl_->properties_.find(name);
    if (it == impl_->properties_.end()) {
        return nullptr;
    }
    return &it->second;
}

void Prim::set_property(const std::string& name, Property prop) {
    // Track insertion order if this is a new property
    if (impl_->properties_.find(name) == impl_->properties_.end()) {
        impl_->property_order_.push_back(name);
    }
    impl_->properties_[name] = std::move(prop);
}

void Prim::set_attribute(const std::string& name, Attribute attr) {
    // Track insertion order if this is a new property
    if (impl_->properties_.find(name) == impl_->properties_.end()) {
        impl_->property_order_.push_back(name);
    }
    impl_->properties_[name] = Property(std::move(attr));
}

void Prim::set_relationship(const std::string& name, Relationship rel) {
    // Track insertion order if this is a new property
    if (impl_->properties_.find(name) == impl_->properties_.end()) {
        impl_->property_order_.push_back(name);
    }
    impl_->properties_[name] = Property(std::move(rel));
}

bool Prim::remove_property(const std::string& name) {
    if (impl_->properties_.erase(name) > 0) {
        // Remove from property order
        auto it = std::find(impl_->property_order_.begin(),
                            impl_->property_order_.end(), name);
        if (it != impl_->property_order_.end()) {
            impl_->property_order_.erase(it);
        }
        return true;
    }
    return false;
}

std::vector<std::string> Prim::property_names() const {
    // Return in current order (insertion order by default)
    return impl_->property_order_;
}

size_t Prim::property_count() const {
    return impl_->properties_.size();
}

bool Prim::set_property_order(const std::vector<std::string>& order) {
    // Validate that all names in order exist
    for (const auto& name : order) {
        if (impl_->properties_.find(name) == impl_->properties_.end()) {
            return false;
        }
    }

    // Build new order: specified names first, then remaining in current order
    std::vector<std::string> new_order;
    new_order.reserve(impl_->properties_.size());

    // Add specified names
    for (const auto& name : order) {
        new_order.push_back(name);
    }

    // Add remaining properties not in order
    for (const auto& name : impl_->property_order_) {
        if (std::find(order.begin(), order.end(), name) == order.end()) {
            new_order.push_back(name);
        }
    }

    impl_->property_order_ = std::move(new_order);
    return true;
}

void Prim::reorder_properties_lexicographic() {
    std::sort(impl_->property_order_.begin(), impl_->property_order_.end());
}

// ============================================================================
// Attribute Helpers
// ============================================================================

const Attribute* Prim::get_attribute(const std::string& name) const {
    const Property* prop = get_property(name);
    if (!prop) {
        return nullptr;
    }
    return prop->as_attribute();
}

Attribute* Prim::get_attribute_mutable(const std::string& name) {
    Property* prop = get_property_mutable(name);
    if (!prop) {
        return nullptr;
    }
    return prop->as_attribute_mutable();
}

const Relationship* Prim::get_relationship(const std::string& name) const {
    const Property* prop = get_property(name);
    if (!prop) {
        return nullptr;
    }
    return prop->as_relationship();
}

Relationship* Prim::get_relationship_mutable(const std::string& name) {
    Property* prop = get_property_mutable(name);
    if (!prop) {
        return nullptr;
    }
    return prop->as_relationship_mutable();
}

// ============================================================================
// Children
// ============================================================================

size_t Prim::child_count() const {
    return impl_->children_.size();
}

const Prim* Prim::child(size_t index) const {
    if (index >= impl_->children_.size()) {
        return nullptr;
    }
    return impl_->children_[index].get();
}

Prim* Prim::child_mutable(size_t index) {
    if (index >= impl_->children_.size()) {
        return nullptr;
    }
    return impl_->children_[index].get();
}

const Prim* Prim::child(const std::string& name) const {
    return impl_->find_child(name);
}

Prim* Prim::child_mutable(const std::string& name) {
    return impl_->find_child(name);
}

bool Prim::add_child(Prim prim) {
    if (prim.name().empty()) {
        return false;
    }

    // Check for duplicate name
    if (impl_->find_child(prim.name())) {
        return false;
    }

    // Update child's path if this prim has a path
    if (impl_->path_.is_valid()) {
        prim.set_path(impl_->path_.append_child(prim.name()));
        prim.update_child_paths();
    }

    impl_->children_.push_back(std::unique_ptr<Prim>(new Prim(std::move(prim))));
    return true;
}

bool Prim::remove_child(const std::string& name) {
    for (auto it = impl_->children_.begin(); it != impl_->children_.end(); ++it) {
        if ((*it)->name() == name) {
            impl_->children_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> Prim::child_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->children_.size());
    for (const auto& child : impl_->children_) {
        names.push_back(child->name());
    }
    return names;
}

bool Prim::set_child_order(const std::vector<std::string>& order) {
    // Validate that all names in order exist
    for (const auto& name : order) {
        if (!impl_->find_child(name)) {
            return false;
        }
    }

    // Build map of current children
    std::unordered_map<std::string, std::unique_ptr<Prim>> child_map;
    for (auto& child : impl_->children_) {
        child_map[child->name()] = std::move(child);
    }
    impl_->children_.clear();

    // Add in specified order
    for (const auto& name : order) {
        auto it = child_map.find(name);
        if (it != child_map.end()) {
            impl_->children_.push_back(std::move(it->second));
            child_map.erase(it);
        }
    }

    // Add remaining children not in order
    for (auto& pair : child_map) {
        impl_->children_.push_back(std::move(pair.second));
    }

    return true;
}

void Prim::reorder_children_lexicographic() {
    std::sort(impl_->children_.begin(), impl_->children_.end(),
              [](const std::unique_ptr<Prim>& a, const std::unique_ptr<Prim>& b) {
                  return a->name() < b->name();
              });
}

// ============================================================================
// Instanceable
// ============================================================================

bool Prim::is_instanceable() const {
    return impl_->instanceable_;
}

void Prim::set_instanceable(bool instanceable) {
    impl_->instanceable_ = instanceable;
}

// ============================================================================
// Variant Sets
// ============================================================================

size_t Prim::variant_set_count() const {
    return impl_->variant_sets_.size();
}

bool Prim::has_variant_set(const std::string& name) const {
    return impl_->find_variant_set(name) != nullptr;
}

const VariantSet* Prim::get_variant_set(const std::string& name) const {
    return impl_->find_variant_set(name);
}

VariantSet* Prim::get_variant_set_mutable(const std::string& name) {
    return impl_->find_variant_set(name);
}

bool Prim::add_variant_set(VariantSet vs) {
    if (vs.name().empty()) {
        return false;
    }
    if (impl_->find_variant_set(vs.name())) {
        return false;  // Already exists
    }
    impl_->variant_sets_.push_back(std::move(vs));
    return true;
}

bool Prim::remove_variant_set(const std::string& name) {
    for (auto it = impl_->variant_sets_.begin(); it != impl_->variant_sets_.end(); ++it) {
        if (it->name() == name) {
            impl_->variant_sets_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<std::string> Prim::variant_set_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->variant_sets_.size());
    for (const auto& vs : impl_->variant_sets_) {
        names.push_back(vs.name());
    }
    return names;
}

// ============================================================================
// Variant Selections
// ============================================================================

std::string Prim::get_variant_selection(const std::string& variant_set_name) const {
    auto it = impl_->variant_selections_.find(variant_set_name);
    if (it != impl_->variant_selections_.end()) {
        return it->second;
    }
    return "";
}

void Prim::set_variant_selection(const std::string& variant_set_name,
                                  const std::string& variant_name) {
    impl_->variant_selections_[variant_set_name] = variant_name;
}

void Prim::clear_variant_selection(const std::string& variant_set_name) {
    impl_->variant_selections_.erase(variant_set_name);
}

std::vector<VariantSelection> Prim::variant_selections() const {
    std::vector<VariantSelection> result;
    result.reserve(impl_->variant_selections_.size());
    for (const auto& pair : impl_->variant_selections_) {
        result.push_back(VariantSelection(pair.first, pair.second));
    }
    return result;
}

// ============================================================================
// References
// ============================================================================

const ReferenceList& Prim::references() const {
    return impl_->references_;
}

ReferenceList& Prim::references_mutable() {
    return impl_->references_;
}

bool Prim::has_references() const {
    return !impl_->references_.empty();
}

// ============================================================================
// Payloads
// ============================================================================

const PayloadList& Prim::payloads() const {
    return impl_->payloads_;
}

PayloadList& Prim::payloads_mutable() {
    return impl_->payloads_;
}

bool Prim::has_payloads() const {
    return !impl_->payloads_.empty();
}

// ============================================================================
// Inherits
// ============================================================================

const PathList& Prim::inherits() const {
    return impl_->inherits_;
}

PathList& Prim::inherits_mutable() {
    return impl_->inherits_;
}

bool Prim::has_inherits() const {
    return !impl_->inherits_.empty();
}

// ============================================================================
// Specializes
// ============================================================================

const PathList& Prim::specializes() const {
    return impl_->specializes_;
}

PathList& Prim::specializes_mutable() {
    return impl_->specializes_;
}

bool Prim::has_specializes() const {
    return !impl_->specializes_.empty();
}

// ============================================================================
// Value Clips
// ============================================================================

bool Prim::has_clips() const {
    return !impl_->clips_.empty();
}

const ClipSets& Prim::clips() const {
    return impl_->clips_;
}

ClipSets& Prim::clips_mutable() {
    return impl_->clips_;
}

void Prim::set_clips(const ClipSets& clips) {
    impl_->clips_ = clips;
}

void Prim::set_clips(ClipSets&& clips) {
    impl_->clips_ = std::move(clips);
}

bool Prim::has_clip_set(const std::string& name) const {
    return impl_->clips_.has(name);
}

const ClipSet* Prim::get_clip_set(const std::string& name) const {
    return impl_->clips_.get(name);
}

ClipSet* Prim::get_clip_set_mutable(const std::string& name) {
    return impl_->clips_.get_mutable(name);
}

void Prim::set_clip_set(const std::string& name, ClipSet clip_set) {
    impl_->clips_.set(name, std::move(clip_set));
}

bool Prim::remove_clip_set(const std::string& name) {
    return impl_->clips_.remove(name);
}

std::vector<std::string> Prim::clip_set_names() const {
    return impl_->clips_.names();
}

ClipResolver Prim::create_clip_resolver(const std::string& clip_set_name) const {
    ClipResolver resolver;
    const ClipSet* cs = impl_->clips_.get(clip_set_name);
    if (cs) {
        resolver.set_clip_set(*cs);
    }
    return resolver;
}

ClipResolver Prim::create_clip_resolver() const {
    return create_clip_resolver("default");
}

// ============================================================================
// Utility
// ============================================================================

void Prim::swap(Prim& other) noexcept {
    impl_.swap(other.impl_);
}

void Prim::clear() {
    impl_->name_.clear();
    impl_->type_name_.clear();
    impl_->path_ = Path();
    impl_->specifier_ = Specifier::Def;
    impl_->active_ = true;
    impl_->instanceable_ = false;
    impl_->metadata_.clear();
    impl_->custom_data_.clear();
    impl_->asset_info_.clear();
    impl_->properties_.clear();
    impl_->property_order_.clear();
    impl_->children_.clear();
    impl_->variant_sets_.clear();
    impl_->variant_selections_.clear();
    impl_->references_.clear();
    impl_->payloads_.clear();
    impl_->inherits_.clear();
    impl_->specializes_.clear();
    impl_->clips_.clear();
}

// ============================================================================
// Private (Friend) Methods
// ============================================================================

void Prim::set_path(const Path& path) {
    impl_->path_ = path;
}

void Prim::update_child_paths() {
    for (auto& child : impl_->children_) {
        if (impl_->path_.is_valid()) {
            child->set_path(impl_->path_.append_child(child->name()));
            child->update_child_paths();
        }
    }
}

} // namespace v1
} // namespace lightusd
