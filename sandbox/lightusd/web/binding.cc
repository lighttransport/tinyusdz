// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Emscripten JavaScript bindings

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "lightusd/lightusd.hh"
#include "lightusd/render_data.hh"

#include <string>
#include <vector>
#include <sstream>

using namespace emscripten;
using namespace lightusd;

// ============================================================================
// Helper functions
// ============================================================================

namespace {

// Convert Value to JavaScript object
val value_to_js(const Value& v) {
    switch (v.type_id()) {
        case TypeId::Bool:
            if (auto p = v.as_bool()) return val(*p);
            break;
        case TypeId::Int32:
            if (auto p = v.as_int32()) return val(*p);
            break;
        case TypeId::Int64:
            if (auto p = v.as_int64()) return val(static_cast<double>(*p));
            break;
        case TypeId::UInt32:
            if (auto p = v.as_uint32()) return val(*p);
            break;
        case TypeId::UInt64:
            if (auto p = v.as_uint64()) return val(static_cast<double>(*p));
            break;
        case TypeId::Float:
            if (auto p = v.as_float()) return val(*p);
            break;
        case TypeId::Double:
            if (auto p = v.as_double()) return val(*p);
            break;
        case TypeId::String:
            if (auto p = v.as_string()) return val(*p);
            break;
        case TypeId::Token:
            if (auto p = v.as_token()) return val(p->str());
            break;
        case TypeId::Float2:
            if (auto p = v.as_float2()) {
                val arr = val::array();
                arr.call<void>("push", p[0]);
                arr.call<void>("push", p[1]);
                return arr;
            }
            break;
        case TypeId::Float3:
            if (auto p = v.as_float3()) {
                val arr = val::array();
                arr.call<void>("push", p[0]);
                arr.call<void>("push", p[1]);
                arr.call<void>("push", p[2]);
                return arr;
            }
            break;
        case TypeId::Float4:
            if (auto p = v.as_float4()) {
                val arr = val::array();
                arr.call<void>("push", p[0]);
                arr.call<void>("push", p[1]);
                arr.call<void>("push", p[2]);
                arr.call<void>("push", p[3]);
                return arr;
            }
            break;
        case TypeId::Double2:
            if (auto p = v.as_double2()) {
                val arr = val::array();
                arr.call<void>("push", p[0]);
                arr.call<void>("push", p[1]);
                return arr;
            }
            break;
        case TypeId::Double3:
            if (auto p = v.as_double3()) {
                val arr = val::array();
                arr.call<void>("push", p[0]);
                arr.call<void>("push", p[1]);
                arr.call<void>("push", p[2]);
                return arr;
            }
            break;
        case TypeId::Double4:
            if (auto p = v.as_double4()) {
                val arr = val::array();
                arr.call<void>("push", p[0]);
                arr.call<void>("push", p[1]);
                arr.call<void>("push", p[2]);
                arr.call<void>("push", p[3]);
                return arr;
            }
            break;
        default:
            break;
    }
    return val::null();
}

// Get type name as string
std::string get_type_name(TypeId id) {
    const TypeDescriptor* desc = get_type_descriptor(id);
    if (desc) return desc->name;
    return "unknown";
}

} // anonymous namespace

// ============================================================================
// Token wrapper
// ============================================================================

class TokenWrapper {
public:
    TokenWrapper() {}
    TokenWrapper(const std::string& s) : token_(s) {}
    TokenWrapper(const Token& t) : token_(t) {}

    std::string str() const { return token_.str(); }
    bool empty() const { return token_.empty(); }
    bool equals(const TokenWrapper& other) const { return token_ == other.token_; }

    const Token& get() const { return token_; }

private:
    Token token_;
};

// ============================================================================
// Path wrapper
// ============================================================================

class PathWrapper {
public:
    PathWrapper() {}
    PathWrapper(const std::string& s) : path_(s) {}
    PathWrapper(const Path& p) : path_(p) {}

    bool is_valid() const { return path_.is_valid(); }
    bool is_empty() const { return path_.is_empty(); }
    bool is_absolute() const { return path_.is_absolute(); }
    bool is_root() const { return path_.is_root(); }
    bool is_prim_path() const { return path_.is_prim_path(); }
    bool is_property_path() const { return path_.is_property_path(); }

    std::string prim_part() const { return path_.prim_part(); }
    std::string prop_part() const { return path_.prop_part(); }
    std::string full_path() const { return path_.full_path(); }
    std::string element_name() const { return path_.element_name(); }

    PathWrapper parent() const { return PathWrapper(path_.parent()); }
    PathWrapper append_child(const std::string& name) const {
        return PathWrapper(path_.append_child(name));
    }
    PathWrapper append_property(const std::string& name) const {
        return PathWrapper(path_.append_property(name));
    }

    // Variant selection
    PathWrapper append_variant_selection(const std::string& vs, const std::string& vn) const {
        return PathWrapper(path_.append_variant_selection(vs, vn));
    }
    bool has_variant_selections() const { return path_.has_variant_selections(); }
    PathWrapper strip_variant_selections() const {
        return PathWrapper(path_.strip_variant_selections());
    }

    bool equals(const PathWrapper& other) const { return path_ == other.path_; }

    const Path& get() const { return path_; }

private:
    Path path_;
};

// ============================================================================
// Value wrapper
// ============================================================================

class ValueWrapper {
public:
    ValueWrapper() {}
    ValueWrapper(const Value& v) : value_(v) {}

    std::string type_name() const { return get_type_name(value_.type_id()); }
    bool is_array() const { return value_.is_array(); }
    size_t array_size() const { return value_.array_size(); }

    val to_js() const { return value_to_js(value_); }

    // Factory methods
    static ValueWrapper from_bool(bool v) { return ValueWrapper(Value::from_bool(v)); }
    static ValueWrapper from_int(int v) { return ValueWrapper(Value::from_int32(v)); }
    static ValueWrapper from_float(float v) { return ValueWrapper(Value::from_float(v)); }
    static ValueWrapper from_double(double v) { return ValueWrapper(Value::from_double(v)); }
    static ValueWrapper from_string(const std::string& v) { return ValueWrapper(Value::from_string(v)); }
    static ValueWrapper from_float3(float x, float y, float z) {
        return ValueWrapper(Value::from_float3(x, y, z));
    }

    const Value& get() const { return value_; }

private:
    Value value_;
};

// ============================================================================
// Attribute wrapper
// ============================================================================

class AttributeWrapper {
public:
    AttributeWrapper() {}
    AttributeWrapper(const Attribute& a) : attr_(a) {}

    std::string name() const { return attr_.name(); }
    std::string type_name() const { return get_type_name(attr_.type_id()); }
    bool has_value() const { return attr_.has_value(); }
    bool has_time_samples() const { return attr_.has_time_samples(); }

    ValueWrapper value() const {
        if (attr_.has_value()) {
            return ValueWrapper(attr_.value());
        }
        return ValueWrapper();
    }

    ValueWrapper value_at_time(double time) const {
        auto result = attr_.get_value_at_time(time);
        if (result.ok()) {
            return ValueWrapper(result.value());
        }
        return ValueWrapper();
    }

    val time_sample_times() const {
        val arr = val::array();
        if (attr_.has_time_samples()) {
            const auto& ts = attr_.time_samples();
            for (double t : ts.times()) {
                arr.call<void>("push", t);
            }
        }
        return arr;
    }

private:
    Attribute attr_;
};

// ============================================================================
// Prim wrapper
// ============================================================================

class PrimWrapper {
public:
    PrimWrapper() {}
    PrimWrapper(const Prim& p) : prim_(p) {}
    PrimWrapper(const Prim* p) : prim_(p ? *p : Prim()) {}

    std::string name() const { return prim_.name(); }
    std::string type_name() const { return prim_.type_name(); }
    PathWrapper path() const { return PathWrapper(prim_.path()); }

    bool has_children() const { return prim_.child_count() > 0; }
    size_t child_count() const { return prim_.child_count(); }

    val child_names() const {
        val arr = val::array();
        for (const auto& name : prim_.child_names()) {
            arr.call<void>("push", name);
        }
        return arr;
    }

    PrimWrapper child(const std::string& name) const {
        const Prim* c = prim_.child(name);
        return PrimWrapper(c);
    }

    PrimWrapper child_at(size_t index) const {
        const Prim* c = prim_.child(index);
        return PrimWrapper(c);
    }

    bool has_properties() const { return prim_.property_count() > 0; }
    size_t property_count() const { return prim_.property_count(); }

    val property_names() const {
        val arr = val::array();
        for (const auto& name : prim_.property_names()) {
            arr.call<void>("push", name);
        }
        return arr;
    }

    AttributeWrapper attribute(const std::string& name) const {
        const Attribute* a = prim_.attribute(name);
        if (a) return AttributeWrapper(*a);
        return AttributeWrapper();
    }

    // Variant sets
    bool has_variant_sets() const { return prim_.variant_set_count() > 0; }
    size_t variant_set_count() const { return prim_.variant_set_count(); }

    val variant_set_names() const {
        val arr = val::array();
        for (const auto& name : prim_.variant_set_names()) {
            arr.call<void>("push", name);
        }
        return arr;
    }

    std::string get_variant_selection(const std::string& vs_name) const {
        return prim_.get_variant_selection(vs_name);
    }

    // Metadata
    std::string kind() const { return prim_.kind(); }
    std::string purpose() const { return prim_.purpose(); }
    bool is_active() const { return prim_.active(); }
    bool is_instanceable() const { return prim_.instanceable(); }

    bool is_valid() const { return !prim_.name().empty(); }

private:
    Prim prim_;
};

// ============================================================================
// Stage wrapper
// ============================================================================

class StageWrapper {
public:
    StageWrapper() {}
    StageWrapper(const Stage& s) : stage_(s) {}

    bool is_valid() const { return stage_.root_prim_count() > 0 || !stage_.default_prim().empty(); }

    std::string default_prim() const { return stage_.default_prim(); }
    std::string up_axis() const { return stage_.up_axis(); }
    double meters_per_unit() const { return stage_.meters_per_unit(); }
    double time_codes_per_second() const { return stage_.time_codes_per_second(); }
    double frames_per_second() const { return stage_.frames_per_second(); }
    double start_time_code() const { return stage_.start_time_code(); }
    double end_time_code() const { return stage_.end_time_code(); }

    size_t root_prim_count() const { return stage_.root_prim_count(); }

    val root_prim_names() const {
        val arr = val::array();
        for (size_t i = 0; i < stage_.root_prim_count(); ++i) {
            const Prim* p = stage_.root_prim(i);
            if (p) arr.call<void>("push", p->name());
        }
        return arr;
    }

    PrimWrapper root_prim(size_t index) const {
        return PrimWrapper(stage_.root_prim(index));
    }

    PrimWrapper root_prim_by_name(const std::string& name) const {
        return PrimWrapper(stage_.root_prim(name));
    }

    PrimWrapper prim_at_path(const std::string& path_str) const {
        Path path(path_str);
        return PrimWrapper(stage_.prim_at_path(path));
    }

    // Export to USDA string
    std::string to_usda() const {
        UsdaWriter writer;
        return writer.write_stage(stage_);
    }

    const Stage& get() const { return stage_; }

private:
    Stage stage_;
};

// ============================================================================
// USDA Reader wrapper
// ============================================================================

class UsdaReaderResult {
public:
    UsdaReaderResult() : ok_(false) {}
    UsdaReaderResult(const Stage& stage) : ok_(true), stage_(stage) {}
    UsdaReaderResult(const std::string& error) : ok_(false), error_(error) {}

    bool ok() const { return ok_; }
    std::string error() const { return error_; }
    StageWrapper stage() const { return StageWrapper(stage_); }

private:
    bool ok_;
    Stage stage_;
    std::string error_;
};

UsdaReaderResult read_usda_string(const std::string& usda_content) {
    auto result = lightusd::read_usda_string(usda_content);
    if (result.ok()) {
        return UsdaReaderResult(result.stage);
    }
    return UsdaReaderResult(result.error);
}

// ============================================================================
// RenderMesh wrapper
// ============================================================================

class RenderMeshWrapper {
public:
    RenderMeshWrapper() {}
    RenderMeshWrapper(RenderMesh&& m) : mesh_(std::move(m)) {}

    std::string name() const { return mesh_.name; }
    std::string path() const { return mesh_.path; }
    uint32_t vertex_count() const { return mesh_.vertex_count(); }
    uint32_t triangle_count() const { return mesh_.triangle_count(); }
    bool is_valid() const { return mesh_.is_valid(); }
    bool double_sided() const { return mesh_.double_sided; }

    // Return typed arrays for efficient transfer to WebGPU
    val positions() const {
        return val(typed_memory_view(mesh_.positions.data.size(), mesh_.positions.data.data()));
    }

    val normals() const {
        if (mesh_.normals.empty()) return val::null();
        return val(typed_memory_view(mesh_.normals.data.size(), mesh_.normals.data.data()));
    }

    val texcoords() const {
        if (mesh_.texcoords0.empty()) return val::null();
        return val(typed_memory_view(mesh_.texcoords0.data.size(), mesh_.texcoords0.data.data()));
    }

    val tangents() const {
        if (mesh_.tangents.empty()) return val::null();
        return val(typed_memory_view(mesh_.tangents.data.size(), mesh_.tangents.data.data()));
    }

    val indices() const {
        return val(typed_memory_view(mesh_.indices.size(), mesh_.indices.data()));
    }

    // Bounding box
    val bounds_min() const {
        val arr = val::array();
        arr.call<void>("push", mesh_.bounds.min.x);
        arr.call<void>("push", mesh_.bounds.min.y);
        arr.call<void>("push", mesh_.bounds.min.z);
        return arr;
    }

    val bounds_max() const {
        val arr = val::array();
        arr.call<void>("push", mesh_.bounds.max.x);
        arr.call<void>("push", mesh_.bounds.max.y);
        arr.call<void>("push", mesh_.bounds.max.z);
        return arr;
    }

    // Transform (4x4 column-major)
    val transform() const {
        return val(typed_memory_view(16, mesh_.transform.m));
    }

    // Submesh info
    size_t submesh_count() const { return mesh_.submeshes.size(); }

    val submesh(size_t index) const {
        if (index >= mesh_.submeshes.size()) return val::null();
        const auto& sm = mesh_.submeshes[index];
        val obj = val::object();
        obj.set("indexOffset", sm.index_offset);
        obj.set("indexCount", sm.index_count);
        obj.set("materialIndex", sm.material_index);
        return obj;
    }

private:
    RenderMesh mesh_;
};

// ============================================================================
// RenderTexture wrapper
// ============================================================================

class RenderTextureWrapper {
public:
    RenderTextureWrapper() {}
    RenderTextureWrapper(RenderTexture&& t) : tex_(std::move(t)) {}

    std::string name() const { return tex_.name; }
    std::string uri() const { return tex_.uri; }
    std::string mime_type() const { return tex_.mime_type; }
    uint32_t width() const { return tex_.width; }
    uint32_t height() const { return tex_.height; }
    uint32_t channels() const { return tex_.channels; }
    bool is_hdr() const { return tex_.is_hdr; }
    bool is_valid() const { return tex_.is_valid(); }

    // Return raw file data for browser decode
    val file_data() const {
        if (tex_.file_data.empty()) return val::null();
        return val(typed_memory_view(tex_.file_data.size(), tex_.file_data.data()));
    }

    // Return decoded RGBA8 data (if decoded in C++)
    val data_u8() const {
        if (tex_.data_u8.empty()) return val::null();
        return val(typed_memory_view(tex_.data_u8.size(), tex_.data_u8.data()));
    }

    // Return decoded float data (HDR)
    val data_f32() const {
        if (tex_.data_f32.empty()) return val::null();
        return val(typed_memory_view(tex_.data_f32.size(), tex_.data_f32.data()));
    }

    // Format as WebGPU string
    std::string webgpu_format() const {
        switch (tex_.format) {
            case TextureFormat::R8: return "r8unorm";
            case TextureFormat::RG8: return "rg8unorm";
            case TextureFormat::RGBA8: return "rgba8unorm";
            case TextureFormat::R16F: return "r16float";
            case TextureFormat::RG16F: return "rg16float";
            case TextureFormat::RGBA16F: return "rgba16float";
            case TextureFormat::R32F: return "r32float";
            case TextureFormat::RG32F: return "rg32float";
            case TextureFormat::RGBA32F: return "rgba32float";
            default: return "rgba8unorm";
        }
    }

private:
    RenderTexture tex_;
};

// ============================================================================
// RenderMaterial wrapper
// ============================================================================

class RenderMaterialWrapper {
public:
    RenderMaterialWrapper() {}
    RenderMaterialWrapper(RenderMaterial&& m) : mat_(std::move(m)) {}

    std::string name() const { return mat_.name; }
    std::string path() const { return mat_.path; }
    bool is_valid() const { return mat_.is_valid(); }

    // Base color (RGBA)
    val base_color() const {
        val arr = val::array();
        arr.call<void>("push", mat_.base_color.x);
        arr.call<void>("push", mat_.base_color.y);
        arr.call<void>("push", mat_.base_color.z);
        arr.call<void>("push", mat_.base_color.w);
        return arr;
    }

    int32_t base_color_texture() const { return mat_.base_color_texture; }

    float metallic() const { return mat_.metallic; }
    float roughness() const { return mat_.roughness; }
    int32_t metallic_roughness_texture() const { return mat_.metallic_roughness_texture; }

    int32_t normal_texture() const { return mat_.normal_texture; }
    float normal_scale() const { return mat_.normal_scale; }

    val emissive() const {
        val arr = val::array();
        arr.call<void>("push", mat_.emissive.x);
        arr.call<void>("push", mat_.emissive.y);
        arr.call<void>("push", mat_.emissive.z);
        return arr;
    }
    int32_t emissive_texture() const { return mat_.emissive_texture; }

    int32_t occlusion_texture() const { return mat_.occlusion_texture; }
    float occlusion_strength() const { return mat_.occlusion_strength; }

    bool double_sided() const { return mat_.double_sided; }
    float alpha_cutoff() const { return mat_.alpha_cutoff; }

private:
    RenderMaterial mat_;
};

// ============================================================================
// RenderScene wrapper
// ============================================================================

class RenderSceneWrapper {
public:
    RenderSceneWrapper() {}
    RenderSceneWrapper(RenderScene&& s) : scene_(std::move(s)) {}

    std::string name() const { return scene_.name; }
    std::string up_axis() const { return scene_.up_axis; }
    float meters_per_unit() const { return scene_.meters_per_unit; }
    bool is_valid() const { return scene_.is_valid(); }

    size_t mesh_count() const { return scene_.meshes.size(); }
    size_t material_count() const { return scene_.materials.size(); }
    size_t texture_count() const { return scene_.textures.size(); }
    size_t camera_count() const { return scene_.cameras.size(); }
    size_t light_count() const { return scene_.lights.size(); }

    RenderMeshWrapper mesh(size_t index) const {
        if (index >= scene_.meshes.size()) return RenderMeshWrapper();
        RenderMesh copy = scene_.meshes[index];
        return RenderMeshWrapper(std::move(copy));
    }

    RenderMaterialWrapper material(size_t index) const {
        if (index >= scene_.materials.size()) return RenderMaterialWrapper();
        RenderMaterial copy = scene_.materials[index];
        return RenderMaterialWrapper(std::move(copy));
    }

    RenderTextureWrapper texture(size_t index) const {
        if (index >= scene_.textures.size()) return RenderTextureWrapper();
        RenderTexture copy = scene_.textures[index];
        return RenderTextureWrapper(std::move(copy));
    }

    // Scene bounds
    val bounds_min() const {
        val arr = val::array();
        arr.call<void>("push", scene_.bounds.min.x);
        arr.call<void>("push", scene_.bounds.min.y);
        arr.call<void>("push", scene_.bounds.min.z);
        return arr;
    }

    val bounds_max() const {
        val arr = val::array();
        arr.call<void>("push", scene_.bounds.max.x);
        arr.call<void>("push", scene_.bounds.max.y);
        arr.call<void>("push", scene_.bounds.max.z);
        return arr;
    }

    int32_t default_camera() const { return scene_.default_camera; }

private:
    RenderScene scene_;
};

// ============================================================================
// RenderConverter wrapper
// ============================================================================

class RenderConverterWrapper {
public:
    RenderConverterWrapper() {}

    // Convert Stage to RenderScene
    RenderSceneWrapper convert(const StageWrapper& stage, double time = 0.0,
                               bool triangulate = true, bool compute_normals = true,
                               bool compute_tangents = true) {
        RenderConverterConfig config;
        config.time = time;
        config.triangulate = triangulate;
        config.compute_normals = compute_normals;
        config.compute_tangents = compute_tangents;

        auto result = converter_.convert(stage.get(), config);
        if (result) {
            return RenderSceneWrapper(std::move(result).value());
        }
        last_error_ = result.error().message;
        return RenderSceneWrapper();
    }

    std::string error() const { return last_error_; }

private:
    RenderConverter converter_;
    std::string last_error_;
};

// ============================================================================
// Emscripten bindings
// ============================================================================

EMSCRIPTEN_BINDINGS(lightusd) {
    // Token
    class_<TokenWrapper>("Token")
        .constructor<>()
        .constructor<std::string>()
        .function("str", &TokenWrapper::str)
        .function("empty", &TokenWrapper::empty)
        .function("equals", &TokenWrapper::equals);

    // Path
    class_<PathWrapper>("Path")
        .constructor<>()
        .constructor<std::string>()
        .function("isValid", &PathWrapper::is_valid)
        .function("isEmpty", &PathWrapper::is_empty)
        .function("isAbsolute", &PathWrapper::is_absolute)
        .function("isRoot", &PathWrapper::is_root)
        .function("isPrimPath", &PathWrapper::is_prim_path)
        .function("isPropertyPath", &PathWrapper::is_property_path)
        .function("primPart", &PathWrapper::prim_part)
        .function("propPart", &PathWrapper::prop_part)
        .function("fullPath", &PathWrapper::full_path)
        .function("elementName", &PathWrapper::element_name)
        .function("parent", &PathWrapper::parent)
        .function("appendChild", &PathWrapper::append_child)
        .function("appendProperty", &PathWrapper::append_property)
        .function("appendVariantSelection", &PathWrapper::append_variant_selection)
        .function("hasVariantSelections", &PathWrapper::has_variant_selections)
        .function("stripVariantSelections", &PathWrapper::strip_variant_selections)
        .function("equals", &PathWrapper::equals);

    // Value
    class_<ValueWrapper>("Value")
        .constructor<>()
        .function("typeName", &ValueWrapper::type_name)
        .function("isArray", &ValueWrapper::is_array)
        .function("arraySize", &ValueWrapper::array_size)
        .function("toJS", &ValueWrapper::to_js)
        .class_function("fromBool", &ValueWrapper::from_bool)
        .class_function("fromInt", &ValueWrapper::from_int)
        .class_function("fromFloat", &ValueWrapper::from_float)
        .class_function("fromDouble", &ValueWrapper::from_double)
        .class_function("fromString", &ValueWrapper::from_string)
        .class_function("fromFloat3", &ValueWrapper::from_float3);

    // Attribute
    class_<AttributeWrapper>("Attribute")
        .constructor<>()
        .function("name", &AttributeWrapper::name)
        .function("typeName", &AttributeWrapper::type_name)
        .function("hasValue", &AttributeWrapper::has_value)
        .function("hasTimeSamples", &AttributeWrapper::has_time_samples)
        .function("value", &AttributeWrapper::value)
        .function("valueAtTime", &AttributeWrapper::value_at_time)
        .function("timeSampleTimes", &AttributeWrapper::time_sample_times);

    // Prim
    class_<PrimWrapper>("Prim")
        .constructor<>()
        .function("isValid", &PrimWrapper::is_valid)
        .function("name", &PrimWrapper::name)
        .function("typeName", &PrimWrapper::type_name)
        .function("path", &PrimWrapper::path)
        .function("hasChildren", &PrimWrapper::has_children)
        .function("childCount", &PrimWrapper::child_count)
        .function("childNames", &PrimWrapper::child_names)
        .function("child", &PrimWrapper::child)
        .function("childAt", &PrimWrapper::child_at)
        .function("hasProperties", &PrimWrapper::has_properties)
        .function("propertyCount", &PrimWrapper::property_count)
        .function("propertyNames", &PrimWrapper::property_names)
        .function("attribute", &PrimWrapper::attribute)
        .function("hasVariantSets", &PrimWrapper::has_variant_sets)
        .function("variantSetCount", &PrimWrapper::variant_set_count)
        .function("variantSetNames", &PrimWrapper::variant_set_names)
        .function("getVariantSelection", &PrimWrapper::get_variant_selection)
        .function("kind", &PrimWrapper::kind)
        .function("purpose", &PrimWrapper::purpose)
        .function("isActive", &PrimWrapper::is_active)
        .function("isInstanceable", &PrimWrapper::is_instanceable);

    // Stage
    class_<StageWrapper>("Stage")
        .constructor<>()
        .function("isValid", &StageWrapper::is_valid)
        .function("defaultPrim", &StageWrapper::default_prim)
        .function("upAxis", &StageWrapper::up_axis)
        .function("metersPerUnit", &StageWrapper::meters_per_unit)
        .function("timeCodesPerSecond", &StageWrapper::time_codes_per_second)
        .function("framesPerSecond", &StageWrapper::frames_per_second)
        .function("startTimeCode", &StageWrapper::start_time_code)
        .function("endTimeCode", &StageWrapper::end_time_code)
        .function("rootPrimCount", &StageWrapper::root_prim_count)
        .function("rootPrimNames", &StageWrapper::root_prim_names)
        .function("rootPrim", &StageWrapper::root_prim)
        .function("rootPrimByName", &StageWrapper::root_prim_by_name)
        .function("primAtPath", &StageWrapper::prim_at_path)
        .function("toUsda", &StageWrapper::to_usda);

    // USDA Reader result
    class_<UsdaReaderResult>("UsdaReaderResult")
        .function("ok", &UsdaReaderResult::ok)
        .function("error", &UsdaReaderResult::error)
        .function("stage", &UsdaReaderResult::stage);

    // Free function to read USDA
    function("readUsdaString", &read_usda_string);

    // Version info
    function("version", +[]() -> std::string {
        return lightusd::version_string();
    });

    // RenderMesh
    class_<RenderMeshWrapper>("RenderMesh")
        .constructor<>()
        .function("name", &RenderMeshWrapper::name)
        .function("path", &RenderMeshWrapper::path)
        .function("vertexCount", &RenderMeshWrapper::vertex_count)
        .function("triangleCount", &RenderMeshWrapper::triangle_count)
        .function("isValid", &RenderMeshWrapper::is_valid)
        .function("doubleSided", &RenderMeshWrapper::double_sided)
        .function("positions", &RenderMeshWrapper::positions)
        .function("normals", &RenderMeshWrapper::normals)
        .function("texcoords", &RenderMeshWrapper::texcoords)
        .function("tangents", &RenderMeshWrapper::tangents)
        .function("indices", &RenderMeshWrapper::indices)
        .function("boundsMin", &RenderMeshWrapper::bounds_min)
        .function("boundsMax", &RenderMeshWrapper::bounds_max)
        .function("transform", &RenderMeshWrapper::transform)
        .function("submeshCount", &RenderMeshWrapper::submesh_count)
        .function("submesh", &RenderMeshWrapper::submesh);

    // RenderTexture
    class_<RenderTextureWrapper>("RenderTexture")
        .constructor<>()
        .function("name", &RenderTextureWrapper::name)
        .function("uri", &RenderTextureWrapper::uri)
        .function("mimeType", &RenderTextureWrapper::mime_type)
        .function("width", &RenderTextureWrapper::width)
        .function("height", &RenderTextureWrapper::height)
        .function("channels", &RenderTextureWrapper::channels)
        .function("isHdr", &RenderTextureWrapper::is_hdr)
        .function("isValid", &RenderTextureWrapper::is_valid)
        .function("fileData", &RenderTextureWrapper::file_data)
        .function("dataU8", &RenderTextureWrapper::data_u8)
        .function("dataF32", &RenderTextureWrapper::data_f32)
        .function("webgpuFormat", &RenderTextureWrapper::webgpu_format);

    // RenderMaterial
    class_<RenderMaterialWrapper>("RenderMaterial")
        .constructor<>()
        .function("name", &RenderMaterialWrapper::name)
        .function("path", &RenderMaterialWrapper::path)
        .function("isValid", &RenderMaterialWrapper::is_valid)
        .function("baseColor", &RenderMaterialWrapper::base_color)
        .function("baseColorTexture", &RenderMaterialWrapper::base_color_texture)
        .function("metallic", &RenderMaterialWrapper::metallic)
        .function("roughness", &RenderMaterialWrapper::roughness)
        .function("metallicRoughnessTexture", &RenderMaterialWrapper::metallic_roughness_texture)
        .function("normalTexture", &RenderMaterialWrapper::normal_texture)
        .function("normalScale", &RenderMaterialWrapper::normal_scale)
        .function("emissive", &RenderMaterialWrapper::emissive)
        .function("emissiveTexture", &RenderMaterialWrapper::emissive_texture)
        .function("occlusionTexture", &RenderMaterialWrapper::occlusion_texture)
        .function("occlusionStrength", &RenderMaterialWrapper::occlusion_strength)
        .function("doubleSided", &RenderMaterialWrapper::double_sided)
        .function("alphaCutoff", &RenderMaterialWrapper::alpha_cutoff);

    // RenderScene
    class_<RenderSceneWrapper>("RenderScene")
        .constructor<>()
        .function("name", &RenderSceneWrapper::name)
        .function("upAxis", &RenderSceneWrapper::up_axis)
        .function("metersPerUnit", &RenderSceneWrapper::meters_per_unit)
        .function("isValid", &RenderSceneWrapper::is_valid)
        .function("meshCount", &RenderSceneWrapper::mesh_count)
        .function("materialCount", &RenderSceneWrapper::material_count)
        .function("textureCount", &RenderSceneWrapper::texture_count)
        .function("cameraCount", &RenderSceneWrapper::camera_count)
        .function("lightCount", &RenderSceneWrapper::light_count)
        .function("mesh", &RenderSceneWrapper::mesh)
        .function("material", &RenderSceneWrapper::material)
        .function("texture", &RenderSceneWrapper::texture)
        .function("boundsMin", &RenderSceneWrapper::bounds_min)
        .function("boundsMax", &RenderSceneWrapper::bounds_max)
        .function("defaultCamera", &RenderSceneWrapper::default_camera);

    // RenderConverter
    class_<RenderConverterWrapper>("RenderConverter")
        .constructor<>()
        .function("convert", &RenderConverterWrapper::convert)
        .function("error", &RenderConverterWrapper::error);
}
