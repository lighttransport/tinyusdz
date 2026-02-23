// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA writer implementation

#include "lightusd/usda_writer.hh"
#include "lightusd/value.hh"
#include "lightusd/token.hh"
#include "lightusd/path.hh"
#include "lightusd/timesamples.hh"
#include "lightusd/attribute.hh"
#include "lightusd/relationship.hh"
#include "lightusd/property.hh"
#include "lightusd/prim.hh"
#include "lightusd/stage.hh"
#include "lightusd/composition.hh"
#include "lightusd/variant.hh"

#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace lightusd {
namespace v1 {

// ============================================================================
// UsdaWriter Implementation
// ============================================================================

UsdaWriter::UsdaWriter()
    : options_(UsdaFormatOptions::defaults()) {
}

UsdaWriter::UsdaWriter(const UsdaFormatOptions& options)
    : options_(options) {
}

std::string UsdaWriter::make_indent(int depth) const {
    std::string result;
    result.reserve(options_.indent_string.size() * depth);
    for (int i = 0; i < depth; ++i) {
        result += options_.indent_string;
    }
    return result;
}

std::string UsdaWriter::escape_string(const std::string& s) const {
    std::string result;
    result.reserve(s.size() + 2);
    result += '"';
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    result += '"';
    return result;
}

// ============================================================================
// Type Formatting
// ============================================================================

const char* UsdaWriter::format_type_name(TypeId type) {
    // Strip array bit for lookup
    TypeId base = static_cast<TypeId>(static_cast<uint32_t>(type) & ~static_cast<uint32_t>(TypeId::ArrayBit));
    const TypeDescriptor* desc = get_type_descriptor(base);
    return desc ? desc->name : "unknown";
}

const char* UsdaWriter::format_specifier(Specifier spec) {
    switch (spec) {
        case Specifier::Def:   return "def";
        case Specifier::Over:  return "over";
        case Specifier::Class: return "class";
    }
    return "def";
}

// ============================================================================
// Scalar Value Formatting
// ============================================================================

std::string UsdaWriter::format_scalar(const Value& value) const {
    std::ostringstream oss;
    oss << std::setprecision(options_.float_precision);

    TypeId type = value.type_id();

    // Bool
    if (const bool* b = value.as_bool()) {
        return *b ? "true" : "false";
    }

    // Integers
    if (const int32_t* i = value.as_int32()) {
        return std::to_string(*i);
    }
    if (const int64_t* i = value.as_int64()) {
        return std::to_string(*i);
    }
    if (const uint32_t* u = value.as_uint32()) {
        return std::to_string(*u);
    }
    if (const uint64_t* u = value.as_uint64()) {
        return std::to_string(*u);
    }

    // Floating point
    if (const float* f = value.as_float()) {
        oss << *f;
        return oss.str();
    }
    if (const double* d = value.as_double()) {
        oss << std::setprecision(options_.double_precision) << *d;
        return oss.str();
    }

    // Timecode
    if (const double* t = value.as_timecode()) {
        oss << std::setprecision(options_.double_precision) << *t;
        return oss.str();
    }

    // String types
    if (const std::string* s = value.as_string()) {
        return escape_string(*s);
    }
    if (const Token* t = value.as_token()) {
        return escape_string(t->str());
    }
    if (const std::string* s = value.as_asset_path()) {
        return "@" + *s + "@";
    }
    if (const Path* p = value.as_path()) {
        return "<" + p->full_path() + ">";
    }

    return "None";
}

// ============================================================================
// Tuple/Vector Value Formatting
// ============================================================================

std::string UsdaWriter::format_tuple(const Value& value) const {
    std::ostringstream oss;
    oss << std::setprecision(options_.float_precision);

    // Integer vectors
    if (const int32_t* v = value.as_int2()) {
        oss << "(" << v[0] << ", " << v[1] << ")";
        return oss.str();
    }
    if (const int32_t* v = value.as_int3()) {
        oss << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
        return oss.str();
    }
    if (const int32_t* v = value.as_int4()) {
        oss << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
        return oss.str();
    }

    // Float vectors
    if (const float* v = value.as_float2()) {
        oss << "(" << v[0] << ", " << v[1] << ")";
        return oss.str();
    }
    if (const float* v = value.as_float3()) {
        oss << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
        return oss.str();
    }
    if (const float* v = value.as_float4()) {
        oss << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
        return oss.str();
    }

    // Double vectors
    oss << std::setprecision(options_.double_precision);
    if (const double* v = value.as_double2()) {
        oss << "(" << v[0] << ", " << v[1] << ")";
        return oss.str();
    }
    if (const double* v = value.as_double3()) {
        oss << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
        return oss.str();
    }
    if (const double* v = value.as_double4()) {
        oss << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
        return oss.str();
    }

    // Quaternions (output as tuple: x, y, z, w)
    if (const float* q = value.as_quatf()) {
        oss << "(" << q[0] << ", " << q[1] << ", " << q[2] << ", " << q[3] << ")";
        return oss.str();
    }
    if (const double* q = value.as_quatd()) {
        oss << std::setprecision(options_.double_precision);
        oss << "(" << q[0] << ", " << q[1] << ", " << q[2] << ", " << q[3] << ")";
        return oss.str();
    }

    return "";
}

// ============================================================================
// Matrix Value Formatting
// ============================================================================

std::string UsdaWriter::format_matrix(const Value& value) const {
    std::ostringstream oss;

    // Float matrices
    if (const float* m = value.as_matrix2f()) {
        oss << std::setprecision(options_.float_precision);
        oss << "( (" << m[0] << ", " << m[1] << "), ";
        oss << "(" << m[2] << ", " << m[3] << ") )";
        return oss.str();
    }
    if (const float* m = value.as_matrix3f()) {
        oss << std::setprecision(options_.float_precision);
        oss << "( (" << m[0] << ", " << m[1] << ", " << m[2] << "), ";
        oss << "(" << m[3] << ", " << m[4] << ", " << m[5] << "), ";
        oss << "(" << m[6] << ", " << m[7] << ", " << m[8] << ") )";
        return oss.str();
    }
    if (const float* m = value.as_matrix4f()) {
        oss << std::setprecision(options_.float_precision);
        oss << "( ";
        for (int row = 0; row < 4; ++row) {
            oss << "(";
            for (int col = 0; col < 4; ++col) {
                if (col > 0) oss << ", ";
                oss << m[row * 4 + col];
            }
            oss << ")";
            if (row < 3) oss << ", ";
        }
        oss << " )";
        return oss.str();
    }

    // Double matrices
    oss << std::setprecision(options_.double_precision);
    if (const double* m = value.as_matrix2d()) {
        oss << "( (" << m[0] << ", " << m[1] << "), ";
        oss << "(" << m[2] << ", " << m[3] << ") )";
        return oss.str();
    }
    if (const double* m = value.as_matrix3d()) {
        oss << "( (" << m[0] << ", " << m[1] << ", " << m[2] << "), ";
        oss << "(" << m[3] << ", " << m[4] << ", " << m[5] << "), ";
        oss << "(" << m[6] << ", " << m[7] << ", " << m[8] << ") )";
        return oss.str();
    }
    if (const double* m = value.as_matrix4d()) {
        oss << "( ";
        for (int row = 0; row < 4; ++row) {
            oss << "(";
            for (int col = 0; col < 4; ++col) {
                if (col > 0) oss << ", ";
                oss << m[row * 4 + col];
            }
            oss << ")";
            if (row < 3) oss << ", ";
        }
        oss << " )";
        return oss.str();
    }

    return "";
}

// ============================================================================
// Array Value Formatting
// ============================================================================

std::string UsdaWriter::format_array(const Value& value) const {
    std::ostringstream oss;
    oss << std::setprecision(options_.float_precision);

    TypeId type = value.type_id();
    TypeId elem_type = static_cast<TypeId>(static_cast<uint32_t>(type) & ~static_cast<uint32_t>(TypeId::ArrayBit));

    // Int32 array
    {
        Value::ArrayView view = value.as_int32_array();
        if (view.data && view.count > 0) {
            const int32_t* data = static_cast<const int32_t*>(view.data);
            oss << "[";
            for (size_t i = 0; i < view.count; ++i) {
                if (i > 0) oss << ", ";
                oss << data[i];
            }
            oss << "]";
            return oss.str();
        }
    }

    // Float array
    {
        Value::ArrayView view = value.as_float_array();
        if (view.data && view.count > 0 && elem_type == TypeId::Float) {
            const float* data = static_cast<const float*>(view.data);
            oss << "[";
            for (size_t i = 0; i < view.count; ++i) {
                if (i > 0) oss << ", ";
                oss << data[i];
            }
            oss << "]";
            return oss.str();
        }
    }

    // Float2 array
    {
        Value::ArrayView view = value.as_float2_array();
        if (view.data && view.count > 0 && (elem_type == TypeId::Float2 || elem_type == TypeId::TexCoord2f)) {
            const float* data = static_cast<const float*>(view.data);
            oss << "[";
            for (size_t i = 0; i < view.count; ++i) {
                if (i > 0) oss << ", ";
                oss << "(" << data[i*2] << ", " << data[i*2+1] << ")";
            }
            oss << "]";
            return oss.str();
        }
    }

    // Float3 array (including role types)
    {
        Value::ArrayView view = value.as_float3_array();
        if (view.data && view.count > 0) {
            const float* data = static_cast<const float*>(view.data);
            oss << "[";
            for (size_t i = 0; i < view.count; ++i) {
                if (i > 0) oss << ", ";
                oss << "(" << data[i*3] << ", " << data[i*3+1] << ", " << data[i*3+2] << ")";
            }
            oss << "]";
            return oss.str();
        }
    }

    // Float4 array
    {
        Value::ArrayView view = value.as_float4_array();
        if (view.data && view.count > 0) {
            const float* data = static_cast<const float*>(view.data);
            oss << "[";
            for (size_t i = 0; i < view.count; ++i) {
                if (i > 0) oss << ", ";
                oss << "(" << data[i*4] << ", " << data[i*4+1] << ", " << data[i*4+2] << ", " << data[i*4+3] << ")";
            }
            oss << "]";
            return oss.str();
        }
    }

    // Double array
    {
        Value::ArrayView view = value.as_double_array();
        if (view.data && view.count > 0 && elem_type == TypeId::Double) {
            const double* data = static_cast<const double*>(view.data);
            oss << std::setprecision(options_.double_precision);
            oss << "[";
            for (size_t i = 0; i < view.count; ++i) {
                if (i > 0) oss << ", ";
                oss << data[i];
            }
            oss << "]";
            return oss.str();
        }
    }

    // Empty array fallback
    return "[]";
}

// ============================================================================
// Value Formatting
// ============================================================================

std::string UsdaWriter::format(const Value& value) const {
    TypeId type = value.type_id();

    // Handle null and blocked
    if (value.is_null() || type == TypeId::Null) {
        return "None";
    }
    if (value.is_none() || type == TypeId::ValueBlock) {
        return "None";
    }

    // Arrays
    if (value.is_array()) {
        return format_array(value);
    }

    // Try tuple/vector format
    std::string tuple_str = format_tuple(value);
    if (!tuple_str.empty()) {
        return tuple_str;
    }

    // Try matrix format
    std::string matrix_str = format_matrix(value);
    if (!matrix_str.empty()) {
        return matrix_str;
    }

    // Scalar format
    return format_scalar(value);
}

std::string UsdaWriter::format(const Value& value, TypeId /*type_hint*/) const {
    // Currently type_hint is not used, but could be for explicit casting
    return format(value);
}

// ============================================================================
// Token/Path Formatting
// ============================================================================

std::string UsdaWriter::format(const Token& token) const {
    return escape_string(token.str());
}

std::string UsdaWriter::format(const Path& path) const {
    return "<" + path.full_path() + ">";
}

// ============================================================================
// TimeSamples Formatting
// ============================================================================

std::string UsdaWriter::format(const TimeSamples& ts, int depth) const {
    std::ostringstream oss;
    std::string ind = make_indent(depth);
    std::string ind1 = make_indent(depth + 1);

    oss << "{" << options_.newline;

    for (size_t i = 0; i < ts.size(); ++i) {
        auto time_result = ts.get_time(i);
        if (!time_result.ok()) continue;

        oss << ind1 << std::setprecision(options_.double_precision) << time_result.value() << ": ";

        if (ts.is_blocked(i)) {
            oss << "None";
        } else {
            auto val_result = ts.get_sample(i);
            if (val_result.ok()) {
                oss << format(val_result.value());
            } else {
                oss << "None";
            }
        }

        oss << "," << options_.newline;
    }

    oss << ind << "}";
    return oss.str();
}

// ============================================================================
// Attribute Formatting
// ============================================================================

std::string UsdaWriter::format(const Attribute& attr, const std::string& name, int depth) const {
    std::ostringstream oss;
    std::string ind = make_indent(depth);

    // Type and name
    const char* type_name = attr.type_name();
    bool is_array = attr.is_array_type();

    if (attr.variability() == Variability::Uniform) {
        oss << ind << "uniform ";
    } else {
        oss << ind;
    }

    oss << type_name;
    if (is_array) {
        oss << "[]";
    }
    oss << " " << name;

    // Connections
    if (attr.has_connection()) {
        oss << ".connect = ";
        const auto& conns = attr.connections();
        if (conns.size() == 1) {
            oss << "<" << conns[0].full_path() << ">";
        } else {
            oss << "[";
            for (size_t i = 0; i < conns.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "<" << conns[i].full_path() << ">";
            }
            oss << "]";
        }
    }
    // Time samples
    else if (attr.has_timesamples()) {
        const TimeSamples* ts = attr.timesamples();
        if (ts && !ts->empty()) {
            oss << ".timeSamples = " << format(*ts, depth);
        }
    }
    // Default value
    else if (attr.has_default()) {
        auto val_result = attr.get_default();
        if (val_result.ok()) {
            oss << " = " << format(val_result.value());
        }
    }
    // Blocked
    else if (attr.is_blocked()) {
        oss << " = None";
    }

    return oss.str();
}

// ============================================================================
// Relationship Formatting
// ============================================================================

std::string UsdaWriter::format(const Relationship& rel, const std::string& name, int depth) const {
    std::ostringstream oss;
    std::string ind = make_indent(depth);

    oss << ind << "rel " << name;

    if (rel.has_targets()) {
        const auto& targets = rel.targets();
        if (targets.size() == 1) {
            oss << " = <" << targets[0].full_path() << ">";
        } else {
            oss << " = [";
            for (size_t i = 0; i < targets.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "<" << targets[i].full_path() << ">";
            }
            oss << "]";
        }
    }

    return oss.str();
}

// ============================================================================
// Property Formatting
// ============================================================================

std::string UsdaWriter::format(const Property& prop, const std::string& name, int depth) const {
    if (prop.is_attribute()) {
        const Attribute* attr = prop.as_attribute();
        if (attr && attr->is_authored()) {
            return format(*attr, name, depth);
        }
    } else if (prop.is_relationship()) {
        const Relationship* rel = prop.as_relationship();
        if (rel && rel->is_authored()) {
            return format(*rel, name, depth);
        }
    }
    return "";
}

// ============================================================================
// Prim Formatting
// ============================================================================

// Helper to format a reference list
static void format_reference_list(std::ostringstream& oss,
                                  const std::vector<Reference>& refs,
                                  const std::string& ind,
                                  const std::string& qualifier,
                                  const std::string& key,
                                  const std::string& newline) {
    if (refs.empty()) return;

    oss << ind << qualifier << key << " = [" << newline;
    for (size_t i = 0; i < refs.size(); ++i) {
        oss << ind << "    ";
        if (!refs[i].asset_path.empty()) {
            oss << "@" << refs[i].asset_path << "@";
        }
        if (refs[i].prim_path.is_valid()) {
            oss << "<" << refs[i].prim_path.full_path() << ">";
        }
        if (!refs[i].layer_offset.is_identity()) {
            oss << " (offset = " << refs[i].layer_offset.offset
                << "; scale = " << refs[i].layer_offset.scale << ")";
        }
        if (i + 1 < refs.size()) {
            oss << ",";
        }
        oss << newline;
    }
    oss << ind << "]" << newline;
}

// Helper to format a payload list
static void format_payload_list(std::ostringstream& oss,
                                const std::vector<Payload>& payloads,
                                const std::string& ind,
                                const std::string& qualifier,
                                const std::string& key,
                                const std::string& newline) {
    if (payloads.empty()) return;

    oss << ind << qualifier << key << " = [" << newline;
    for (size_t i = 0; i < payloads.size(); ++i) {
        oss << ind << "    ";
        if (!payloads[i].asset_path.empty()) {
            oss << "@" << payloads[i].asset_path << "@";
        }
        if (payloads[i].prim_path.is_valid()) {
            oss << "<" << payloads[i].prim_path.full_path() << ">";
        }
        if (!payloads[i].layer_offset.is_identity()) {
            oss << " (offset = " << payloads[i].layer_offset.offset
                << "; scale = " << payloads[i].layer_offset.scale << ")";
        }
        if (i + 1 < payloads.size()) {
            oss << ",";
        }
        oss << newline;
    }
    oss << ind << "]" << newline;
}

// Helper to format a path list for inherits/specializes
static void format_path_list(std::ostringstream& oss,
                             const std::vector<Path>& paths,
                             const std::string& ind,
                             const std::string& qualifier,
                             const std::string& key,
                             const std::string& newline) {
    if (paths.empty()) return;

    oss << ind << qualifier << key << " = [";
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "<" << paths[i].full_path() << ">";
    }
    oss << "]" << newline;
}

std::string UsdaWriter::format(const Prim& prim, int depth) const {
    std::ostringstream oss;
    std::string ind = make_indent(depth);
    std::string ind1 = make_indent(depth + 1);
    std::string ind2 = make_indent(depth + 2);

    // Prim header
    oss << ind << format_specifier(prim.specifier());
    if (!prim.type_name().empty()) {
        oss << " " << prim.type_name();
    }
    oss << " \"" << prim.name() << "\"";

    // Prim metadata
    bool has_metadata = prim.metadata_count() > 0 || !prim.is_active() ||
                        prim.is_instanceable() || prim.has_references() ||
                        prim.has_payloads() || prim.has_inherits() ||
                        prim.has_specializes() || !prim.variant_selections().empty() ||
                        prim.asset_info_count() > 0;
    if (has_metadata) {
        oss << " (" << options_.newline;

        // active (only if false)
        if (!prim.is_active()) {
            oss << ind1 << "active = false" << options_.newline;
        }

        // instanceable
        if (prim.is_instanceable()) {
            oss << ind1 << "instanceable = true" << options_.newline;
        }

        // inherits
        const PathList& inherits_list = prim.inherits();
        format_path_list(oss, inherits_list.prepended_items(), ind1, "prepend ", "inherits", options_.newline);
        format_path_list(oss, inherits_list.explicit_items(), ind1, "", "inherits", options_.newline);
        format_path_list(oss, inherits_list.appended_items(), ind1, "append ", "inherits", options_.newline);

        // specializes
        const PathList& spec_list = prim.specializes();
        format_path_list(oss, spec_list.prepended_items(), ind1, "prepend ", "specializes", options_.newline);
        format_path_list(oss, spec_list.explicit_items(), ind1, "", "specializes", options_.newline);
        format_path_list(oss, spec_list.appended_items(), ind1, "append ", "specializes", options_.newline);

        // references
        const ReferenceList& refs = prim.references();
        format_reference_list(oss, refs.prepended_items(), ind1, "prepend ", "references", options_.newline);
        format_reference_list(oss, refs.explicit_items(), ind1, "", "references", options_.newline);
        format_reference_list(oss, refs.appended_items(), ind1, "append ", "references", options_.newline);

        // payloads
        const PayloadList& payloads = prim.payloads();
        format_payload_list(oss, payloads.prepended_items(), ind1, "prepend ", "payloads", options_.newline);
        format_payload_list(oss, payloads.explicit_items(), ind1, "", "payloads", options_.newline);
        format_payload_list(oss, payloads.appended_items(), ind1, "append ", "payloads", options_.newline);

        // variantSets (list of variant set names)
        if (prim.variant_set_count() > 0) {
            oss << ind1 << "variantSets = [";
            std::vector<std::string> vs_names = prim.variant_set_names();
            for (size_t i = 0; i < vs_names.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "\"" << vs_names[i] << "\"";
            }
            oss << "]" << options_.newline;
        }

        // variants (selections)
        std::vector<VariantSelection> selections = prim.variant_selections();
        if (!selections.empty()) {
            oss << ind1 << "variants = {" << options_.newline;
            for (const auto& sel : selections) {
                oss << ind2 << sel.variant_set_name << " = \""
                    << sel.variant_name << "\"" << options_.newline;
            }
            oss << ind1 << "}" << options_.newline;
        }

        // assetInfo
        if (prim.asset_info_count() > 0) {
            oss << ind1 << "assetInfo = {" << options_.newline;
            std::vector<std::string> ai_keys = prim.asset_info_keys();
            std::sort(ai_keys.begin(), ai_keys.end());
            for (const std::string& key : ai_keys) {
                const Value* val = prim.get_asset_info(key);
                if (val) {
                    oss << ind2 << format_type_name(val->type_id()) << " " << key
                        << " = " << format(*val) << options_.newline;
                }
            }
            oss << ind1 << "}" << options_.newline;
        }

        // Output metadata in sorted order
        std::vector<std::string> meta_keys = prim.metadata_keys();
        std::sort(meta_keys.begin(), meta_keys.end());

        for (const auto& key : meta_keys) {
            const Value* val = prim.get_metadata(key);
            if (!val) continue;

            oss << ind1 << key << " = ";

            // Format value based on type
            if (const bool* b = val->as_bool()) {
                oss << (*b ? "true" : "false");
            } else if (const std::string* s = val->as_string()) {
                oss << escape_string(*s);
            } else if (const Token* t = val->as_token()) {
                oss << "\"" << t->str() << "\"";
            } else {
                oss << format(*val);
            }
            oss << options_.newline;
        }

        oss << ind << ")";
    }

    // Check for content
    bool has_props = prim.property_count() > 0;
    bool has_children = prim.child_count() > 0;
    bool has_variant_sets = prim.variant_set_count() > 0;
    bool has_content = has_props || has_children || has_variant_sets;

    if (!options_.include_empty_prims && !has_content) {
        oss << options_.newline << ind << "{" << options_.newline << ind << "}" << options_.newline;
        return oss.str();
    }

    oss << options_.newline << ind << "{" << options_.newline;

    // Properties
    std::vector<std::string> prop_names = prim.property_names();
    if (options_.sort_properties) {
        std::sort(prop_names.begin(), prop_names.end());
    }

    for (const auto& prop_name : prop_names) {
        const Property* prop = prim.get_property(prop_name);
        if (!prop) continue;

        std::string prop_str = format(*prop, prop_name, depth + 1);
        if (!prop_str.empty()) {
            oss << prop_str << options_.newline;
        }
    }

    // VariantSets
    std::vector<std::string> vs_names = prim.variant_set_names();
    for (const auto& vs_name : vs_names) {
        const VariantSet* vs = prim.get_variant_set(vs_name);
        if (!vs) continue;

        if (has_props) {
            oss << options_.newline;
        }

        oss << ind1 << "variantSet \"" << vs_name << "\" = {" << options_.newline;

        std::vector<std::string> var_names = vs->variant_names();
        for (const auto& var_name : var_names) {
            const Variant* var = vs->get_variant(var_name);
            if (!var) continue;

            oss << ind2 << "\"" << var_name << "\" {" << options_.newline;

            // Output variant content (properties and children)
            if (var->has_content()) {
                const Prim* content = var->content();
                if (content) {
                    // Output variant properties
                    std::vector<std::string> var_prop_names = content->property_names();
                    for (const auto& pn : var_prop_names) {
                        const Property* p = content->get_property(pn);
                        if (p) {
                            std::string ps = format(*p, pn, depth + 3);
                            if (!ps.empty()) {
                                oss << ps << options_.newline;
                            }
                        }
                    }
                    // Output variant children
                    std::vector<std::string> var_child_names = content->child_names();
                    for (const auto& cn : var_child_names) {
                        const Prim* child = content->child(cn);
                        if (child) {
                            oss << options_.newline;
                            oss << format(*child, depth + 3);
                        }
                    }
                }
            }

            oss << ind2 << "}" << options_.newline;
        }

        oss << ind1 << "}" << options_.newline;
    }

    // Children
    std::vector<std::string> child_names_vec = prim.child_names();
    if (options_.sort_children) {
        std::sort(child_names_vec.begin(), child_names_vec.end());
    }

    for (size_t i = 0; i < child_names_vec.size(); ++i) {
        const Prim* child = prim.child(child_names_vec[i]);
        if (child) {
            if (i > 0 || has_props || has_variant_sets) {
                oss << options_.newline;
            }
            oss << format(*child, depth + 1);
        }
    }

    oss << ind << "}" << options_.newline;
    return oss.str();
}

// ============================================================================
// Stage Formatting
// ============================================================================

std::string UsdaWriter::format(const Stage& stage) const {
    std::ostringstream oss;

    // Header
    oss << "#usda 1.0" << options_.newline;
    oss << "(" << options_.newline;

    // Stage metadata
    std::string ind = options_.indent_string;

    // Sublayers (output first, before other metadata)
    if (stage.sublayer_count() > 0) {
        oss << ind << "subLayers = [" << options_.newline;
        std::string ind2 = ind + options_.indent_string;
        const std::vector<SubLayer>& sublayers = stage.sublayers();
        for (size_t i = 0; i < sublayers.size(); ++i) {
            const SubLayer& sl = sublayers[i];
            oss << ind2 << "@" << sl.asset_path << "@";
            if (!sl.layer_offset.is_identity()) {
                oss << " (offset = " << sl.layer_offset.offset
                    << "; scale = " << sl.layer_offset.scale << ")";
            }
            if (i + 1 < sublayers.size()) {
                oss << ",";
            }
            oss << options_.newline;
        }
        oss << ind << "]" << options_.newline;
    }

    // Common layer metadata (sorted alphabetically)
    std::string comment_str = stage.comment();
    if (!comment_str.empty()) {
        oss << ind << "comment = " << escape_string(comment_str) << options_.newline;
    }

    if (!stage.default_prim().empty()) {
        oss << ind << "defaultPrim = \"" << stage.default_prim() << "\"" << options_.newline;
    }

    std::string doc_str = stage.documentation();
    if (!doc_str.empty()) {
        oss << ind << "documentation = " << escape_string(doc_str) << options_.newline;
    }

    if (stage.end_time_code() > stage.start_time_code()) {
        oss << ind << "endTimeCode = " << std::setprecision(options_.double_precision)
            << stage.end_time_code() << options_.newline;
    }

    oss << ind << "framesPerSecond = " << stage.frames_per_second() << options_.newline;
    oss << ind << "metersPerUnit = " << stage.meters_per_unit() << options_.newline;

    std::string owner_str = stage.owner();
    if (!owner_str.empty()) {
        oss << ind << "owner = " << escape_string(owner_str) << options_.newline;
    }

    if (stage.end_time_code() > stage.start_time_code()) {
        oss << ind << "startTimeCode = " << std::setprecision(options_.double_precision)
            << stage.start_time_code() << options_.newline;
    }

    oss << ind << "upAxis = \"" << stage.up_axis() << "\"" << options_.newline;

    // Custom layer data
    if (stage.custom_layer_data_count() > 0) {
        oss << ind << "customLayerData = {" << options_.newline;
        std::string ind2 = ind + options_.indent_string;
        std::vector<std::string> keys = stage.custom_layer_data_keys();
        std::sort(keys.begin(), keys.end());
        for (const std::string& key : keys) {
            const Value* val = stage.get_custom_layer_data(key);
            if (val) {
                oss << ind2 << format_type_name(val->type_id()) << " " << key
                    << " = " << format(*val) << options_.newline;
            }
        }
        oss << ind << "}" << options_.newline;
    }

    // Generic metadata (keys not already output)
    std::vector<std::string> meta_keys = stage.metadata_keys();
    std::sort(meta_keys.begin(), meta_keys.end());
    for (const std::string& key : meta_keys) {
        // Skip convenience accessors - they're already output above
        if (key == "documentation" || key == "comment" || key == "owner") {
            continue;
        }
        const Value* val = stage.get_metadata(key);
        if (val) {
            oss << ind << key << " = " << format(*val) << options_.newline;
        }
    }

    oss << ")" << options_.newline << options_.newline;

    // Root prims
    for (size_t i = 0; i < stage.root_prim_count(); ++i) {
        if (i > 0) {
            oss << options_.newline;
        }
        const Prim* prim = stage.root_prim(i);
        if (prim) {
            oss << format(*prim, 0);
        }
    }

    return oss.str();
}

// ============================================================================
// Stream Output
// ============================================================================

void UsdaWriter::write(std::ostream& os, const Stage& stage) const {
    os << format(stage);
}

void UsdaWriter::write(std::ostream& os, const Prim& prim, int depth) const {
    os << format(prim, depth);
}

void UsdaWriter::write(std::ostream& os, const Value& value) const {
    os << format(value);
}

// ============================================================================
// to_string Functions
// ============================================================================

std::string to_string(TypeId type) {
    return UsdaWriter::format_type_name(type);
}

std::string to_string(Specifier spec) {
    return UsdaWriter::format_specifier(spec);
}

std::string to_string(const Value& value) {
    UsdaWriter writer;
    return writer.format(value);
}

std::string to_string(const Token& token) {
    UsdaWriter writer;
    return writer.format(token);
}

std::string to_string(const Path& path) {
    UsdaWriter writer;
    return writer.format(path);
}

std::string to_string(const TimeSamples& ts) {
    UsdaWriter writer;
    return writer.format(ts);
}

std::string to_string(const Attribute& attr, const std::string& name) {
    UsdaWriter writer;
    return writer.format(attr, name.empty() ? "attr" : name);
}

std::string to_string(const Relationship& rel, const std::string& name) {
    UsdaWriter writer;
    return writer.format(rel, name.empty() ? "rel" : name);
}

std::string to_string(const Property& prop, const std::string& name) {
    UsdaWriter writer;
    return writer.format(prop, name.empty() ? "prop" : name);
}

std::string to_string(const Prim& prim) {
    UsdaWriter writer;
    return writer.format(prim);
}

std::string to_string(const Stage& stage) {
    UsdaWriter writer;
    return writer.format(stage);
}

// ============================================================================
// Stream Operators
// ============================================================================

std::ostream& operator<<(std::ostream& os, TypeId type) {
    return os << to_string(type);
}

std::ostream& operator<<(std::ostream& os, Specifier spec) {
    return os << to_string(spec);
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
    return os << to_string(value);
}

std::ostream& operator<<(std::ostream& os, const Token& token) {
    return os << to_string(token);
}

std::ostream& operator<<(std::ostream& os, const Path& path) {
    return os << to_string(path);
}

std::ostream& operator<<(std::ostream& os, const TimeSamples& ts) {
    return os << to_string(ts);
}

std::ostream& operator<<(std::ostream& os, const Attribute& attr) {
    return os << to_string(attr);
}

std::ostream& operator<<(std::ostream& os, const Relationship& rel) {
    return os << to_string(rel);
}

std::ostream& operator<<(std::ostream& os, const Property& prop) {
    return os << to_string(prop);
}

std::ostream& operator<<(std::ostream& os, const Prim& prim) {
    return os << to_string(prim);
}

std::ostream& operator<<(std::ostream& os, const Stage& stage) {
    return os << to_string(stage);
}

} // namespace v1
} // namespace lightusd
