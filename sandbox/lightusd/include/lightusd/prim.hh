// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Prim class for scene hierarchy nodes

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lightusd/path.hh"
#include "lightusd/value.hh"
#include "lightusd/property.hh"

namespace lightusd {
namespace v1 {

// Forward declarations for composition types
class VariantSet;
class ReferenceList;
class PayloadList;
class PathList;
struct VariantSelection;

/// Specifier - how prim contributes to composition
enum class Specifier : uint8_t {
    Def,    // Defines a prim (most common)
    Over,   // Override existing prim
    Class,  // Abstract class for inheritance
};

/// Prim - a node in the scene hierarchy.
/// Contains properties (attributes and relationships) and child prims.
class Prim {
public:
    /// Default constructor
    Prim();

    /// Construct with name
    explicit Prim(const std::string& name);

    /// Construct with name and type
    Prim(const std::string& name, const std::string& type_name);

    /// Destructor
    ~Prim();

    /// Copy constructor
    Prim(const Prim& other);

    /// Move constructor
    Prim(Prim&& other) noexcept;

    /// Copy assignment
    Prim& operator=(const Prim& other);

    /// Move assignment
    Prim& operator=(Prim&& other) noexcept;

    // ========== Identity ==========

    /// Get prim name
    const std::string& name() const;

    /// Set prim name
    void set_name(const std::string& name);

    /// Get type name (e.g., "Mesh", "Xform", "Material")
    const std::string& type_name() const;

    /// Set type name
    void set_type_name(const std::string& type_name);

    /// Get full scene path (set by Stage)
    const Path& path() const;

    // ========== Specifier ==========

    /// Get specifier
    Specifier specifier() const;

    /// Set specifier
    void set_specifier(Specifier s);

    // ========== Active State ==========

    /// Check if prim is active
    bool is_active() const;

    /// Set active state
    void set_active(bool active);

    // ========== Metadata (Generic Dictionary) ==========

    /// Check if has metadata key
    bool has_metadata(const std::string& key) const;

    /// Get metadata value (returns nullptr if not found)
    const Value* get_metadata(const std::string& key) const;

    /// Set metadata value
    void set_metadata(const std::string& key, const Value& value);

    /// Remove metadata key
    bool remove_metadata(const std::string& key);

    /// Get all metadata keys
    std::vector<std::string> metadata_keys() const;

    /// Get metadata count
    size_t metadata_count() const;

    // ========== Common Metadata Accessors ==========

    /// Get kind (e.g., "component", "group", "assembly", "subcomponent")
    std::string kind() const;
    void set_kind(const std::string& kind);

    /// Get purpose (e.g., "default", "render", "proxy", "guide")
    std::string purpose() const;
    void set_purpose(const std::string& purpose);

    /// Check if hidden
    bool is_hidden() const;
    void set_hidden(bool hidden);

    /// Get documentation string
    std::string documentation() const;
    void set_documentation(const std::string& doc);

    /// Get API schemas (returns empty vector if not set)
    std::vector<std::string> api_schemas() const;
    void set_api_schemas(const std::vector<std::string>& schemas);
    void add_api_schema(const std::string& schema);

    // ========== Custom Data (nested dictionary) ==========

    /// Check if has custom data key
    bool has_custom_data(const std::string& key) const;

    /// Get custom data value
    const Value* get_custom_data(const std::string& key) const;

    /// Set custom data value
    void set_custom_data(const std::string& key, const Value& value);

    // ========== Properties ==========

    /// Check if has property with name
    bool has_property(const std::string& name) const;

    /// Get property by name (returns nullptr if not found)
    const Property* get_property(const std::string& name) const;

    /// Get mutable property by name (returns nullptr if not found)
    Property* get_property_mutable(const std::string& name);

    /// Set property (replaces existing)
    void set_property(const std::string& name, Property prop);

    /// Set attribute (convenience)
    void set_attribute(const std::string& name, Attribute attr);

    /// Set relationship (convenience)
    void set_relationship(const std::string& name, Relationship rel);

    /// Remove property
    bool remove_property(const std::string& name);

    /// Get all property names (in current order)
    std::vector<std::string> property_names() const;

    /// Get property count
    size_t property_count() const;

    /// Set explicit property order (names must match existing properties)
    /// Properties not in the list will be appended in their current order
    bool set_property_order(const std::vector<std::string>& order);

    /// Reorder properties lexicographically (OpenUSD default)
    void reorder_properties_lexicographic();

    // ========== Attribute Helpers ==========

    /// Get attribute by name (returns nullptr if not found or not attribute)
    const Attribute* get_attribute(const std::string& name) const;

    /// Get mutable attribute
    Attribute* get_attribute_mutable(const std::string& name);

    /// Get relationship by name (returns nullptr if not found or not relationship)
    const Relationship* get_relationship(const std::string& name) const;

    /// Get mutable relationship
    Relationship* get_relationship_mutable(const std::string& name);

    // ========== Children ==========

    /// Get child count
    size_t child_count() const;

    /// Get child by index
    const Prim* child(size_t index) const;

    /// Get mutable child by index
    Prim* child_mutable(size_t index);

    /// Get child by name
    const Prim* child(const std::string& name) const;

    /// Get mutable child by name
    Prim* child_mutable(const std::string& name);

    /// Add child prim (takes ownership)
    bool add_child(Prim prim);

    /// Remove child by name
    bool remove_child(const std::string& name);

    /// Get all child names (in current order)
    std::vector<std::string> child_names() const;

    /// Set explicit child order (names must match existing children)
    /// Children not in the list will be appended in their current order
    bool set_child_order(const std::vector<std::string>& order);

    /// Reorder children lexicographically (OpenUSD default)
    void reorder_children_lexicographic();

    // ========== Instanceable ==========

    /// Check if prim is instanceable (for GPU instancing)
    bool is_instanceable() const;

    /// Set instanceable flag
    void set_instanceable(bool instanceable);

    // ========== Variant Sets ==========

    /// Get number of variant sets
    size_t variant_set_count() const;

    /// Check if variant set exists
    bool has_variant_set(const std::string& name) const;

    /// Get variant set by name (returns nullptr if not found)
    const VariantSet* get_variant_set(const std::string& name) const;
    VariantSet* get_variant_set_mutable(const std::string& name);

    /// Add variant set (returns false if name already exists)
    bool add_variant_set(VariantSet vs);

    /// Remove variant set by name
    bool remove_variant_set(const std::string& name);

    /// Get all variant set names (in order)
    std::vector<std::string> variant_set_names() const;

    // ========== Variant Selections ==========

    /// Get selected variant for a variant set (empty if not selected)
    std::string get_variant_selection(const std::string& variant_set_name) const;

    /// Set variant selection
    void set_variant_selection(const std::string& variant_set_name,
                               const std::string& variant_name);

    /// Clear variant selection
    void clear_variant_selection(const std::string& variant_set_name);

    /// Get all variant selections
    std::vector<VariantSelection> variant_selections() const;

    // ========== References ==========

    /// Get reference list
    const ReferenceList& references() const;
    ReferenceList& references_mutable();

    /// Check if has any references
    bool has_references() const;

    // ========== Payloads ==========

    /// Get payload list
    const PayloadList& payloads() const;
    PayloadList& payloads_mutable();

    /// Check if has any payloads
    bool has_payloads() const;

    // ========== Inherits ==========

    /// Get inherits path list
    const PathList& inherits() const;
    PathList& inherits_mutable();

    /// Check if has any inherits
    bool has_inherits() const;

    // ========== Specializes ==========

    /// Get specializes path list
    const PathList& specializes() const;
    PathList& specializes_mutable();

    /// Check if has any specializes
    bool has_specializes() const;

    // ========== Utility ==========

    /// Swap contents
    void swap(Prim& other) noexcept;

    /// Clear all data
    void clear();

private:
    friend class Stage;

    /// Set path (called by Stage during commit)
    void set_path(const Path& path);

    /// Recursively update child paths
    void update_child_paths();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Swap specialization
inline void swap(Prim& a, Prim& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
