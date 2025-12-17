// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Primvar implementation

#include "lightusd/primvar.hh"
#include "lightusd/prim.hh"
#include "lightusd/types.hh"

namespace lightusd {
namespace v1 {

// ============================================================================
// Primvar implementation
// ============================================================================

Primvar::Primvar(const std::string& name, const Attribute& attr, Interpolation interp)
    : name_(name), attribute_(attr), interpolation_(interp) {}

Primvar::Primvar(const std::string& name, const Attribute& attr,
                 const std::vector<int32_t>& indices, Interpolation interp)
    : name_(name), attribute_(attr), interpolation_(interp), indices_(indices) {}

std::string Primvar::type_name() const {
    if (!has_value()) return "";
    const TypeDescriptor* desc = get_type_descriptor(attribute_.type_id());
    return desc ? desc->name : "unknown";
}

size_t Primvar::expected_count(size_t num_faces, size_t num_vertices,
                               size_t num_face_vertices) const {
    switch (interpolation_) {
        case Interpolation::Constant:
            return 1;
        case Interpolation::Uniform:
            return num_faces;
        case Interpolation::Vertex:
        case Interpolation::Varying:
            return num_vertices;
        case Interpolation::FaceVarying:
            return num_face_vertices;
    }
    return 0;
}

std::string Primvar::validate(size_t num_faces, size_t num_vertices,
                              size_t num_face_vertices) const {
    if (!has_value()) {
        return "Primvar '" + name_ + "' has no value";
    }

    Value val = value();
    if (!val.is_array()) {
        // Scalar values are valid for constant interpolation
        if (interpolation_ != Interpolation::Constant) {
            return "Primvar '" + name_ + "' has scalar value but non-constant interpolation";
        }
        return "";
    }

    size_t actual_count = val.array_size();
    size_t expected = expected_count(num_faces, num_vertices, num_face_vertices);

    // Handle element_size
    if (element_size_ > 0 && actual_count > 0) {
        if (actual_count % element_size_ != 0) {
            return "Primvar '" + name_ + "' array size " + std::to_string(actual_count) +
                   " is not divisible by element size " + std::to_string(element_size_);
        }
        actual_count = actual_count / element_size_;
    }

    // For indexed primvars, check indices count instead
    if (is_indexed()) {
        size_t indices_count = indices_.size();
        if (element_size_ > 0 && indices_count > 0) {
            if (indices_count % element_size_ != 0) {
                return "Primvar '" + name_ + "' indices size " + std::to_string(indices_count) +
                       " is not divisible by element size " + std::to_string(element_size_);
            }
            indices_count = indices_count / element_size_;
        }

        if (indices_count != expected) {
            return "Primvar '" + name_ + "' has " + std::to_string(indices_count) +
                   " indices but expected " + std::to_string(expected) +
                   " for " + interpolation_to_string(interpolation_) + " interpolation";
        }

        // Also validate that indices are within bounds
        for (int32_t idx : indices_) {
            if (idx < 0 || static_cast<size_t>(idx) >= val.array_size()) {
                return "Primvar '" + name_ + "' has out-of-bounds index " + std::to_string(idx);
            }
        }
    } else {
        if (actual_count != expected) {
            return "Primvar '" + name_ + "' has " + std::to_string(actual_count) +
                   " values but expected " + std::to_string(expected) +
                   " for " + interpolation_to_string(interpolation_) + " interpolation";
        }
    }

    return "";
}

bool Primvar::operator==(const Primvar& other) const {
    return name_ == other.name_ &&
           interpolation_ == other.interpolation_ &&
           element_size_ == other.element_size_ &&
           indices_ == other.indices_;
    // Note: attribute comparison would need deep value comparison
}

// ============================================================================
// PrimvarSet implementation
// ============================================================================

void PrimvarSet::set(const Primvar& primvar) {
    // Check if primvar already exists
    for (auto& pv : primvars_) {
        if (pv.name() == primvar.name()) {
            pv = primvar;
            return;
        }
    }
    primvars_.push_back(primvar);
}

void PrimvarSet::set(Primvar&& primvar) {
    std::string name = primvar.name();
    for (auto& pv : primvars_) {
        if (pv.name() == name) {
            pv = std::move(primvar);
            return;
        }
    }
    primvars_.push_back(std::move(primvar));
}

const Primvar* PrimvarSet::get(std::string_view name) const {
    for (const auto& pv : primvars_) {
        if (pv.name() == name) {
            return &pv;
        }
    }
    return nullptr;
}

Primvar* PrimvarSet::get(std::string_view name) {
    for (auto& pv : primvars_) {
        if (pv.name() == name) {
            return &pv;
        }
    }
    return nullptr;
}

bool PrimvarSet::has(std::string_view name) const {
    return get(name) != nullptr;
}

bool PrimvarSet::remove(std::string_view name) {
    for (auto it = primvars_.begin(); it != primvars_.end(); ++it) {
        if (it->name() == name) {
            primvars_.erase(it);
            return true;
        }
    }
    return false;
}

void PrimvarSet::clear() {
    primvars_.clear();
}

std::vector<std::string> PrimvarSet::names() const {
    std::vector<std::string> result;
    result.reserve(primvars_.size());
    for (const auto& pv : primvars_) {
        result.push_back(pv.name());
    }
    return result;
}

const Primvar* PrimvarSet::get_texcoords(std::string_view name) const {
    // First try the specified name
    if (const Primvar* pv = get(name)) {
        return pv;
    }
    // Fall back to common names
    if (name != "st") {
        if (const Primvar* pv = get("st")) return pv;
    }
    if (name != "uv") {
        if (const Primvar* pv = get("uv")) return pv;
    }
    return nullptr;
}

const Primvar* PrimvarSet::get_display_color() const {
    return get("displayColor");
}

const Primvar* PrimvarSet::get_display_opacity() const {
    return get("displayOpacity");
}

std::vector<std::string> PrimvarSet::validate(size_t num_faces, size_t num_vertices,
                                              size_t num_face_vertices) const {
    std::vector<std::string> errors;
    for (const auto& pv : primvars_) {
        std::string err = pv.validate(num_faces, num_vertices, num_face_vertices);
        if (!err.empty()) {
            errors.push_back(std::move(err));
        }
    }
    return errors;
}

// ============================================================================
// Utility functions
// ============================================================================

Result<Primvar> extract_primvar(const Prim& prim, std::string_view name) {
    std::string primvar_name = "primvars:" + std::string(name);

    // Get the main primvar attribute
    const Attribute* attr = prim.get_attribute(primvar_name);
    if (!attr) {
        return make_error("Primvar '" + std::string(name) + "' not found");
    }

    Primvar primvar(std::string(name), *attr);

    // Check for interpolation metadata
    std::string interp_name = primvar_name + ":interpolation";
    const Attribute* interp_attr = prim.get_attribute(interp_name);
    if (interp_attr && interp_attr->has_default()) {
        auto result = interp_attr->get_default();
        if (result) {
            const Token* token = result.value().as_token();
            if (token) {
                auto interp = interpolation_from_string(token->str());
                if (interp) {
                    primvar.set_interpolation(*interp);
                }
            }
        }
    }

    // Check for indices
    std::string indices_name = primvar_name + ":indices";
    const Attribute* indices_attr = prim.get_attribute(indices_name);
    if (indices_attr && indices_attr->has_default()) {
        auto result = indices_attr->get_default();
        if (result) {
            const Value& val = result.value();
            if (val.is_array() && val.type_id() == TypeId::Int32) {
                size_t count = val.array_size();
                std::vector<int32_t> indices(count);
                auto arr_view = val.as_int32_array();
                if (arr_view.data) {
                    const int32_t* data = static_cast<const int32_t*>(arr_view.data);
                    for (size_t i = 0; i < count; ++i) {
                        indices[i] = data[i];
                    }
                    primvar.set_indices(std::move(indices));
                }
            }
        }
    }

    return primvar;
}

Result<PrimvarSet> extract_primvars(const Prim& prim) {
    PrimvarSet result;

    // Find all properties starting with "primvars:"
    for (const auto& prop_name : prim.property_names()) {
        if (prop_name.rfind("primvars:", 0) != 0) continue;

        // Skip indices and interpolation sub-properties
        if (prop_name.find(":indices") != std::string::npos) continue;
        if (prop_name.find(":interpolation") != std::string::npos) continue;

        // Extract the primvar name (after "primvars:")
        std::string pv_name = prop_name.substr(9); // strlen("primvars:") = 9

        auto primvar_result = extract_primvar(prim, pv_name);
        if (primvar_result) {
            result.set(std::move(primvar_result).value());
        }
    }

    return result;
}

Result<Primvar> flatten_indexed_primvar(const Primvar& primvar) {
    if (!primvar.is_indexed()) {
        // Already non-indexed, return a copy
        return primvar;
    }

    auto flattened = compute_flattened_values(primvar);
    if (!flattened) {
        return make_error(flattened.error().message);
    }

    // Create new attribute with flattened values
    Attribute new_attr(primvar.attribute().type_id());
    new_attr.set_default(std::move(flattened).value());

    Primvar result(primvar.name(), new_attr, primvar.interpolation());
    result.set_element_size(primvar.element_size());
    // No indices for flattened primvar

    return result;
}

Result<Value> compute_flattened_values(const Primvar& primvar) {
    if (!primvar.is_indexed()) {
        return primvar.value();
    }

    if (!primvar.has_value()) {
        return make_error("Primvar has no value");
    }

    Value src = primvar.value();
    if (!src.is_array()) {
        return make_error("Cannot flatten non-array primvar");
    }

    const std::vector<int32_t>& indices = primvar.indices();
    TypeId type = src.type_id();

    // Handle common types
    // This is a simplified implementation - a full implementation would handle all types
    switch (type) {
        case TypeId::Float: {
            auto arr = src.as_float_array();
            const float* data = static_cast<const float*>(arr.data);
            if (!data) return make_error("Failed to access float array");
            std::vector<float> result(indices.size());
            for (size_t i = 0; i < indices.size(); ++i) {
                result[i] = data[indices[i]];
            }
            return Value::from_float_array(result.data(), result.size());
        }
        case TypeId::Float2: {
            auto arr = src.as_float2_array();
            const float* data = static_cast<const float*>(arr.data);
            if (!data) return make_error("Failed to access float2 array");
            std::vector<float> result(indices.size() * 2);
            for (size_t i = 0; i < indices.size(); ++i) {
                result[i * 2 + 0] = data[indices[i] * 2 + 0];
                result[i * 2 + 1] = data[indices[i] * 2 + 1];
            }
            return Value::from_float2_array(result.data(), indices.size());
        }
        case TypeId::Float3: {
            auto arr = src.as_float3_array();
            const float* data = static_cast<const float*>(arr.data);
            if (!data) return make_error("Failed to access float3 array");
            std::vector<float> result(indices.size() * 3);
            for (size_t i = 0; i < indices.size(); ++i) {
                result[i * 3 + 0] = data[indices[i] * 3 + 0];
                result[i * 3 + 1] = data[indices[i] * 3 + 1];
                result[i * 3 + 2] = data[indices[i] * 3 + 2];
            }
            return Value::from_float3_array(result.data(), indices.size());
        }
        case TypeId::Float4: {
            auto arr = src.as_float4_array();
            const float* data = static_cast<const float*>(arr.data);
            if (!data) return make_error("Failed to access float4 array");
            std::vector<float> result(indices.size() * 4);
            for (size_t i = 0; i < indices.size(); ++i) {
                result[i * 4 + 0] = data[indices[i] * 4 + 0];
                result[i * 4 + 1] = data[indices[i] * 4 + 1];
                result[i * 4 + 2] = data[indices[i] * 4 + 2];
                result[i * 4 + 3] = data[indices[i] * 4 + 3];
            }
            return Value::from_float4_array(result.data(), indices.size());
        }
        case TypeId::Int32: {
            auto arr = src.as_int32_array();
            const int32_t* data = static_cast<const int32_t*>(arr.data);
            if (!data) return make_error("Failed to access int32 array");
            std::vector<int32_t> result(indices.size());
            for (size_t i = 0; i < indices.size(); ++i) {
                result[i] = data[indices[i]];
            }
            return Value::from_int32_array(result.data(), result.size());
        }
        default:
            return make_error("Unsupported primvar type for flattening: " +
                              std::to_string(static_cast<int>(type)));
    }
}

} // namespace v1
} // namespace lightusd
