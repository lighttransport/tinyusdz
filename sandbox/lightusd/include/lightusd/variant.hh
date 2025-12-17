// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Variant and VariantSet classes

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace lightusd {
namespace v1 {

// Forward declarations
class Prim;

/// Variant - a named variation containing prim specs
/// Each variant can contain properties and child prims
class Variant {
public:
    Variant();
    explicit Variant(const std::string& name);
    ~Variant();
    Variant(const Variant& other);
    Variant(Variant&& other) noexcept;
    Variant& operator=(const Variant& other);
    Variant& operator=(Variant&& other) noexcept;

    /// Get variant name
    const std::string& name() const;

    /// Set variant name
    void set_name(const std::string& name);

    /// Get the prim containing this variant's specs
    /// This is the "content" of the variant
    const Prim* content() const;
    Prim* content_mutable();

    /// Set content (takes ownership)
    void set_content(Prim prim);

    /// Check if variant has content
    bool has_content() const;

    /// Clear content
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// VariantSet - a named set of variants
/// e.g., "modelingVariant" with variants "highRes", "lowRes"
class VariantSet {
public:
    VariantSet();
    explicit VariantSet(const std::string& name);
    ~VariantSet();
    VariantSet(const VariantSet& other);
    VariantSet(VariantSet&& other) noexcept;
    VariantSet& operator=(const VariantSet& other);
    VariantSet& operator=(VariantSet&& other) noexcept;

    /// Get variant set name
    const std::string& name() const;

    /// Set variant set name
    void set_name(const std::string& name);

    // ========== Variants ==========

    /// Get number of variants
    size_t variant_count() const;

    /// Check if variant exists
    bool has_variant(const std::string& name) const;

    /// Get variant by name (returns nullptr if not found)
    const Variant* get_variant(const std::string& name) const;
    Variant* get_variant_mutable(const std::string& name);

    /// Get variant by index
    const Variant* get_variant(size_t index) const;
    Variant* get_variant_mutable(size_t index);

    /// Add variant (returns false if name already exists)
    bool add_variant(Variant variant);

    /// Remove variant by name
    bool remove_variant(const std::string& name);

    /// Get all variant names (in order)
    std::vector<std::string> variant_names() const;

    /// Clear all variants
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// VariantSelection - selected variant for a variant set
struct VariantSelection {
    std::string variant_set_name;
    std::string variant_name;

    VariantSelection() = default;
    VariantSelection(const std::string& set_name, const std::string& var_name)
        : variant_set_name(set_name), variant_name(var_name) {}

    bool operator==(const VariantSelection& other) const {
        return variant_set_name == other.variant_set_name &&
               variant_name == other.variant_name;
    }
};

} // namespace v1
} // namespace lightusd
