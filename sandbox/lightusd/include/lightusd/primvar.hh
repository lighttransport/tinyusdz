// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Primvar (Primitive Variable) support
// Defines how attribute values are interpolated across geometry

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lightusd/attribute.hh"
#include "lightusd/result.hh"
#include "lightusd/token.hh"
#include "lightusd/value.hh"

namespace lightusd {
namespace v1 {

// ============================================================================
// Interpolation modes for primvars
// ============================================================================

/// How primvar values are distributed across geometry
enum class Interpolation {
    Constant,     // One value for entire primitive (e.g., visibility)
    Uniform,      // One value per face/element (e.g., face groups)
    Vertex,       // One value per vertex, shared at corners (e.g., positions)
    FaceVarying,  // One value per face-vertex, can differ at corners (e.g., UVs)
    Varying,      // Like vertex but for subdivision surfaces (legacy)
};

/// Convert interpolation enum to USD token string
inline const char* interpolation_to_string(Interpolation interp) {
    switch (interp) {
        case Interpolation::Constant:    return "constant";
        case Interpolation::Uniform:     return "uniform";
        case Interpolation::Vertex:      return "vertex";
        case Interpolation::FaceVarying: return "faceVarying";
        case Interpolation::Varying:     return "varying";
    }
    return "constant";
}

/// Parse interpolation from string
inline std::optional<Interpolation> interpolation_from_string(std::string_view str) {
    if (str == "constant")    return Interpolation::Constant;
    if (str == "uniform")     return Interpolation::Uniform;
    if (str == "vertex")      return Interpolation::Vertex;
    if (str == "faceVarying") return Interpolation::FaceVarying;
    if (str == "varying")     return Interpolation::Varying;
    return std::nullopt;
}

// ============================================================================
// Primvar class
// ============================================================================

/// A primitive variable with interpolation information
///
/// Primvars are attributes with additional metadata about how values
/// are distributed across geometry. The naming convention is:
///   primvars:<name>                - The primvar value
///   primvars:<name>:indices        - Optional indices for indexed primvars
///   primvars:<name>:interpolation  - Interpolation mode (if not default)
///
/// Example:
///   primvars:st (texcoord2f[]) with faceVarying interpolation
///   primvars:displayColor (color3f[]) with vertex interpolation
///
class Primvar {
public:
    Primvar() = default;

    /// Create primvar from name and attribute
    Primvar(const std::string& name, const Attribute& attr,
            Interpolation interp = Interpolation::Constant);

    /// Create indexed primvar
    Primvar(const std::string& name, const Attribute& attr,
            const std::vector<int32_t>& indices,
            Interpolation interp = Interpolation::FaceVarying);

    // === Accessors ===

    /// Primvar name (without "primvars:" prefix)
    const std::string& name() const { return name_; }

    /// Full primvar attribute name (with "primvars:" prefix)
    std::string full_name() const { return "primvars:" + name_; }

    /// The underlying attribute
    const Attribute& attribute() const { return attribute_; }
    Attribute& attribute() { return attribute_; }

    /// Interpolation mode
    Interpolation interpolation() const { return interpolation_; }
    void set_interpolation(Interpolation interp) { interpolation_ = interp; }

    /// Element size (for array primvars, elements per interpolation point)
    /// Default is 0, meaning the entire array is the value
    uint32_t element_size() const { return element_size_; }
    void set_element_size(uint32_t size) { element_size_ = size; }

    // === Indexed primvar support ===

    /// Whether this primvar uses indices
    bool is_indexed() const { return !indices_.empty(); }

    /// Get indices (empty if not indexed)
    const std::vector<int32_t>& indices() const { return indices_; }

    /// Set indices
    void set_indices(const std::vector<int32_t>& indices) { indices_ = indices; }
    void set_indices(std::vector<int32_t>&& indices) { indices_ = std::move(indices); }

    /// Clear indices (make non-indexed)
    void clear_indices() { indices_.clear(); }

    // === Value access ===

    /// Get primvar value at time (default 0.0)
    /// Returns the value from time samples if available, otherwise default value
    Result<Value> get(double time = 0.0) const { return attribute_.get(time); }

    /// Get default value
    Result<Value> get_default() const { return attribute_.get_default(); }

    /// Check if primvar has a value (default or time samples)
    bool has_value() const { return attribute_.has_default() || attribute_.has_timesamples(); }

    /// Get value (for validation) - returns default if available
    Value value() const {
        auto result = attribute_.get_default();
        if (result) return std::move(result).value();
        return Value();
    }

    /// Get type name of the primvar value
    std::string type_name() const;

    // === Validation ===

    /// Expected element count for given geometry
    /// Returns 0 if cannot be determined
    size_t expected_count(size_t num_faces, size_t num_vertices,
                          size_t num_face_vertices) const;

    /// Validate primvar against geometry counts
    /// Returns error message if invalid, empty string if valid
    std::string validate(size_t num_faces, size_t num_vertices,
                         size_t num_face_vertices) const;

    // === Comparison ===
    bool operator==(const Primvar& other) const;
    bool operator!=(const Primvar& other) const { return !(*this == other); }

private:
    std::string name_;
    Attribute attribute_;
    Interpolation interpolation_ = Interpolation::Constant;
    uint32_t element_size_ = 0;
    std::vector<int32_t> indices_;
};

// ============================================================================
// PrimvarSet - Collection of primvars for a prim
// ============================================================================

/// Collection of primvars on a geometric primitive
class PrimvarSet {
public:
    PrimvarSet() = default;

    // === Primvar management ===

    /// Add or replace a primvar
    void set(const Primvar& primvar);
    void set(Primvar&& primvar);

    /// Get primvar by name (without "primvars:" prefix)
    const Primvar* get(std::string_view name) const;
    Primvar* get(std::string_view name);

    /// Check if primvar exists
    bool has(std::string_view name) const;

    /// Remove primvar
    bool remove(std::string_view name);

    /// Clear all primvars
    void clear();

    // === Iteration ===

    /// Get all primvar names
    std::vector<std::string> names() const;

    /// Number of primvars
    size_t size() const { return primvars_.size(); }

    /// Check if empty
    bool empty() const { return primvars_.empty(); }

    /// Iterator access
    auto begin() { return primvars_.begin(); }
    auto end() { return primvars_.end(); }
    auto begin() const { return primvars_.begin(); }
    auto end() const { return primvars_.end(); }

    // === Common primvars ===

    /// Get texture coordinates (primvars:st or primvars:uv)
    const Primvar* get_texcoords(std::string_view name = "st") const;

    /// Get display color (primvars:displayColor)
    const Primvar* get_display_color() const;

    /// Get display opacity (primvars:displayOpacity)
    const Primvar* get_display_opacity() const;

    // === Validation ===

    /// Validate all primvars against geometry
    std::vector<std::string> validate(size_t num_faces, size_t num_vertices,
                                      size_t num_face_vertices) const;

private:
    std::vector<Primvar> primvars_;
};

// ============================================================================
// Utility functions
// ============================================================================

/// Extract primvar from prim properties
/// Looks for primvars:<name>, primvars:<name>:indices, primvars:<name>:interpolation
Result<Primvar> extract_primvar(const class Prim& prim, std::string_view name);

/// Extract all primvars from a prim
Result<PrimvarSet> extract_primvars(const class Prim& prim);

/// Flatten indexed primvar to non-indexed
/// Expands values using indices
Result<Primvar> flatten_indexed_primvar(const Primvar& primvar);

/// Compute flattened values from indexed primvar
/// Returns the expanded array values
Result<Value> compute_flattened_values(const Primvar& primvar);

} // namespace v1
} // namespace lightusd
