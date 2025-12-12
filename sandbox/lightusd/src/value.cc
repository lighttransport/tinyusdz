// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Value implementation

#include "lightusd/value.hh"
#include "lightusd/token.hh"
#include "lightusd/path.hh"
#include <cstring>
#include <algorithm>

namespace lightusd {
namespace v1 {

// ============================================================================
// Constructor/Destructor
// ============================================================================

Value::Value()
    : type_id_(TypeId::Null)
    , flags_(0) {
    std::memset(&storage_, 0, sizeof(storage_));
}

Value::~Value() {
    destroy();
}

Value::Value(const Value& other)
    : type_id_(TypeId::Null)
    , flags_(0) {
    std::memset(&storage_, 0, sizeof(storage_));
    copy_from(other);
}

Value::Value(Value&& other) noexcept
    : type_id_(TypeId::Null)
    , flags_(0) {
    std::memset(&storage_, 0, sizeof(storage_));
    move_from(std::move(other));
}

Value& Value::operator=(const Value& other) {
    if (this != &other) {
        destroy();
        copy_from(other);
    }
    return *this;
}

Value& Value::operator=(Value&& other) noexcept {
    if (this != &other) {
        destroy();
        move_from(std::move(other));
    }
    return *this;
}

// ============================================================================
// Internal Helpers
// ============================================================================

void Value::destroy() {
    if (is_heap() && storage_.heap_) {
        TypeId base = get_base_type(type_id_);

        // Handle heap-allocated types
        switch (base) {
            case TypeId::String:
            case TypeId::AssetPath:
                delete static_cast<std::string*>(storage_.heap_);
                break;
            case TypeId::Token:
                delete static_cast<Token*>(storage_.heap_);
                break;
            case TypeId::Path:
                delete static_cast<Path*>(storage_.heap_);
                break;
            case TypeId::Matrix4d:
            case TypeId::Matrix4f:
            case TypeId::Matrix3d:
            case TypeId::Matrix3f:
                // Matrices are POD, just free memory
                delete[] static_cast<uint8_t*>(storage_.heap_);
                break;
            default:
                // Arrays
                if (is_array_type(type_id_)) {
                    delete[] static_cast<uint8_t*>(storage_.heap_);
                }
                break;
        }
        storage_.heap_ = nullptr;
    }

    type_id_ = TypeId::Null;
    flags_ = 0;
}

void Value::copy_from(const Value& other) {
    type_id_ = other.type_id_;
    flags_ = other.flags_;

    if (!other.is_heap()) {
        // Inline storage - simple copy
        std::memcpy(&storage_, &other.storage_, sizeof(storage_));
    } else {
        // Heap storage - deep copy
        TypeId base = get_base_type(type_id_);
        switch (base) {
            case TypeId::String:
            case TypeId::AssetPath:
                storage_.heap_ = new std::string(*static_cast<const std::string*>(other.storage_.heap_));
                break;
            case TypeId::Token:
                storage_.heap_ = new Token(*static_cast<const Token*>(other.storage_.heap_));
                break;
            case TypeId::Path:
                storage_.heap_ = new Path(*static_cast<const Path*>(other.storage_.heap_));
                break;
            default: {
                // POD arrays/matrices - copy raw bytes
                const TypeDescriptor* desc = get_type_descriptor(base);
                size_t size = desc ? desc->size : 0;
                if (is_array_type(type_id_)) {
                    size *= (flags_ & kArraySizeMask);
                }
                if (size > 0) {
                    storage_.heap_ = new uint8_t[size];
                    std::memcpy(storage_.heap_, other.storage_.heap_, size);
                }
                break;
            }
        }
    }
}

void Value::move_from(Value&& other) noexcept {
    type_id_ = other.type_id_;
    flags_ = other.flags_;
    storage_ = other.storage_;

    other.type_id_ = TypeId::Null;
    other.flags_ = 0;
    std::memset(&other.storage_, 0, sizeof(other.storage_));
}

// ============================================================================
// Factory Methods - Null/None
// ============================================================================

Value Value::make_null() {
    Value v;
    v.type_id_ = TypeId::Null;
    return v;
}

Value Value::make_none() {
    Value v;
    v.type_id_ = TypeId::ValueBlock;
    return v;
}

// ============================================================================
// Factory Methods - Scalars
// ============================================================================

Value Value::from_bool(bool val) {
    Value v;
    v.type_id_ = TypeId::Bool;
    v.storage_.inline_[0] = val ? 1 : 0;
    return v;
}

Value Value::from_int32(int32_t val) {
    Value v;
    v.type_id_ = TypeId::Int32;
    std::memcpy(v.storage_.inline_, &val, sizeof(val));
    return v;
}

Value Value::from_int64(int64_t val) {
    Value v;
    v.type_id_ = TypeId::Int64;
    std::memcpy(v.storage_.inline_, &val, sizeof(val));
    return v;
}

Value Value::from_uint32(uint32_t val) {
    Value v;
    v.type_id_ = TypeId::UInt32;
    std::memcpy(v.storage_.inline_, &val, sizeof(val));
    return v;
}

Value Value::from_uint64(uint64_t val) {
    Value v;
    v.type_id_ = TypeId::UInt64;
    std::memcpy(v.storage_.inline_, &val, sizeof(val));
    return v;
}

Value Value::from_half(uint16_t val) {
    Value v;
    v.type_id_ = TypeId::Half;
    std::memcpy(v.storage_.inline_, &val, sizeof(val));
    return v;
}

Value Value::from_float(float val) {
    Value v;
    v.type_id_ = TypeId::Float;
    std::memcpy(v.storage_.inline_, &val, sizeof(val));
    return v;
}

Value Value::from_double(double val) {
    Value v;
    v.type_id_ = TypeId::Double;
    std::memcpy(v.storage_.inline_, &val, sizeof(val));
    return v;
}

// ============================================================================
// Factory Methods - Integer Vectors
// ============================================================================

Value Value::from_int2(int32_t x, int32_t y) {
    Value v;
    v.type_id_ = TypeId::Int2;
    int32_t data[2] = {x, y};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_int3(int32_t x, int32_t y, int32_t z) {
    Value v;
    v.type_id_ = TypeId::Int3;
    int32_t data[3] = {x, y, z};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_int4(int32_t x, int32_t y, int32_t z, int32_t w) {
    Value v;
    v.type_id_ = TypeId::Int4;
    int32_t data[4] = {x, y, z, w};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

// ============================================================================
// Factory Methods - Float Vectors
// ============================================================================

Value Value::from_float2(float x, float y) {
    Value v;
    v.type_id_ = TypeId::Float2;
    float data[2] = {x, y};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_float3(float x, float y, float z) {
    Value v;
    v.type_id_ = TypeId::Float3;
    float data[3] = {x, y, z};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_float4(float x, float y, float z, float w) {
    Value v;
    v.type_id_ = TypeId::Float4;
    float data[4] = {x, y, z, w};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

// ============================================================================
// Factory Methods - Double Vectors
// ============================================================================

Value Value::from_double2(double x, double y) {
    Value v;
    v.type_id_ = TypeId::Double2;
    double data[2] = {x, y};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_double3(double x, double y, double z) {
    Value v;
    v.type_id_ = TypeId::Double3;
    double data[3] = {x, y, z};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_double4(double x, double y, double z, double w) {
    Value v;
    v.type_id_ = TypeId::Double4;
    // 32 bytes - needs heap allocation (sizeof(double)*4 = 32, too large for inline - 24)
    v.set_heap(true);
    v.storage_.heap_ = new uint8_t[sizeof(double) * 4];
    double data[4] = {x, y, z, w};
    std::memcpy(v.storage_.heap_, data, sizeof(data));
    return v;
}

// ============================================================================
// Factory Methods - Matrices
// ============================================================================

Value Value::from_matrix2f(const float* m) {
    Value v;
    v.type_id_ = TypeId::Matrix2f;
    std::memcpy(v.storage_.inline_, m, sizeof(float) * 4);
    return v;
}

Value Value::from_matrix3f(const float* m) {
    Value v;
    v.type_id_ = TypeId::Matrix3f;
    // 36 bytes - needs heap
    v.set_heap(true);
    v.storage_.heap_ = new uint8_t[sizeof(float) * 9];
    std::memcpy(v.storage_.heap_, m, sizeof(float) * 9);
    return v;
}

Value Value::from_matrix4f(const float* m) {
    Value v;
    v.type_id_ = TypeId::Matrix4f;
    // 64 bytes - needs heap
    v.set_heap(true);
    v.storage_.heap_ = new uint8_t[sizeof(float) * 16];
    std::memcpy(v.storage_.heap_, m, sizeof(float) * 16);
    return v;
}

Value Value::from_matrix2d(const double* m) {
    Value v;
    v.type_id_ = TypeId::Matrix2d;
    // 32 bytes - needs heap
    v.set_heap(true);
    v.storage_.heap_ = new uint8_t[sizeof(double) * 4];
    std::memcpy(v.storage_.heap_, m, sizeof(double) * 4);
    return v;
}

Value Value::from_matrix3d(const double* m) {
    Value v;
    v.type_id_ = TypeId::Matrix3d;
    // 72 bytes - needs heap
    v.set_heap(true);
    v.storage_.heap_ = new uint8_t[sizeof(double) * 9];
    std::memcpy(v.storage_.heap_, m, sizeof(double) * 9);
    return v;
}

Value Value::from_matrix4d(const double* m) {
    Value v;
    v.type_id_ = TypeId::Matrix4d;
    // 128 bytes - needs heap
    v.set_heap(true);
    v.storage_.heap_ = new uint8_t[sizeof(double) * 16];
    std::memcpy(v.storage_.heap_, m, sizeof(double) * 16);
    return v;
}

// ============================================================================
// Factory Methods - Quaternions
// ============================================================================

Value Value::from_quatf(float x, float y, float z, float w) {
    Value v;
    v.type_id_ = TypeId::Quatf;
    float data[4] = {x, y, z, w};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_quatd(double x, double y, double z, double w) {
    Value v;
    v.type_id_ = TypeId::Quatd;
    // 32 bytes - needs heap
    v.set_heap(true);
    v.storage_.heap_ = new uint8_t[sizeof(double) * 4];
    double data[4] = {x, y, z, w};
    std::memcpy(v.storage_.heap_, data, sizeof(data));
    return v;
}

// ============================================================================
// Factory Methods - Strings
// ============================================================================

Value Value::from_string(const char* s) {
    Value v;
    v.type_id_ = TypeId::String;
    v.set_heap(true);
    v.storage_.heap_ = new std::string(s ? s : "");
    return v;
}

Value Value::from_string(const std::string& s) {
    Value v;
    v.type_id_ = TypeId::String;
    v.set_heap(true);
    v.storage_.heap_ = new std::string(s);
    return v;
}

Value Value::from_token(const Token& t) {
    Value v;
    v.type_id_ = TypeId::Token;
    v.set_heap(true);
    v.storage_.heap_ = new Token(t);
    return v;
}

Value Value::from_asset_path(const char* s) {
    Value v;
    v.type_id_ = TypeId::AssetPath;
    v.set_heap(true);
    v.storage_.heap_ = new std::string(s ? s : "");
    return v;
}

Value Value::from_asset_path(const std::string& s) {
    Value v;
    v.type_id_ = TypeId::AssetPath;
    v.set_heap(true);
    v.storage_.heap_ = new std::string(s);
    return v;
}

Value Value::from_path(const Path& p) {
    Value v;
    v.type_id_ = TypeId::Path;
    v.set_heap(true);
    v.storage_.heap_ = new Path(p);
    return v;
}

Value Value::from_timecode(double t) {
    Value v;
    v.type_id_ = TypeId::TimeCode;
    std::memcpy(v.storage_.inline_, &t, sizeof(t));
    return v;
}

// ============================================================================
// Factory Methods - Role Types
// ============================================================================

Value Value::from_color3f(float r, float g, float b) {
    Value v;
    v.type_id_ = TypeId::Color3f;
    float data[3] = {r, g, b};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_color4f(float r, float g, float b, float a) {
    Value v;
    v.type_id_ = TypeId::Color4f;
    float data[4] = {r, g, b, a};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_point3f(float x, float y, float z) {
    Value v;
    v.type_id_ = TypeId::Point3f;
    float data[3] = {x, y, z};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_vector3f(float x, float y, float z) {
    Value v;
    v.type_id_ = TypeId::Vector3f;
    float data[3] = {x, y, z};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_normal3f(float x, float y, float z) {
    Value v;
    v.type_id_ = TypeId::Normal3f;
    float data[3] = {x, y, z};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

Value Value::from_texcoord2f(float u, float vt) {
    Value v;
    v.type_id_ = TypeId::TexCoord2f;
    float data[2] = {u, vt};
    std::memcpy(v.storage_.inline_, data, sizeof(data));
    return v;
}

// ============================================================================
// Factory Methods - Arrays
// ============================================================================

Value Value::from_int32_array(const int32_t* data, size_t count) {
    Value v;
    v.type_id_ = make_array_type(TypeId::Int32);
    v.set_heap(true);
    v.flags_ |= (count & kArraySizeMask);
    size_t size = sizeof(int32_t) * count;
    v.storage_.heap_ = new uint8_t[size];
    std::memcpy(v.storage_.heap_, data, size);
    return v;
}

Value Value::from_float_array(const float* data, size_t count) {
    Value v;
    v.type_id_ = make_array_type(TypeId::Float);
    v.set_heap(true);
    v.flags_ |= (count & kArraySizeMask);
    size_t size = sizeof(float) * count;
    v.storage_.heap_ = new uint8_t[size];
    std::memcpy(v.storage_.heap_, data, size);
    return v;
}

Value Value::from_double_array(const double* data, size_t count) {
    Value v;
    v.type_id_ = make_array_type(TypeId::Double);
    v.set_heap(true);
    v.flags_ |= (count & kArraySizeMask);
    size_t size = sizeof(double) * count;
    v.storage_.heap_ = new uint8_t[size];
    std::memcpy(v.storage_.heap_, data, size);
    return v;
}

Value Value::from_float2_array(const float* data, size_t count) {
    Value v;
    v.type_id_ = make_array_type(TypeId::Float2);
    v.set_heap(true);
    v.flags_ |= (count & kArraySizeMask);
    size_t size = sizeof(float) * 2 * count;
    v.storage_.heap_ = new uint8_t[size];
    std::memcpy(v.storage_.heap_, data, size);
    return v;
}

Value Value::from_float3_array(const float* data, size_t count) {
    Value v;
    v.type_id_ = make_array_type(TypeId::Float3);
    v.set_heap(true);
    v.flags_ |= (count & kArraySizeMask);
    size_t size = sizeof(float) * 3 * count;
    v.storage_.heap_ = new uint8_t[size];
    std::memcpy(v.storage_.heap_, data, size);
    return v;
}

Value Value::from_float4_array(const float* data, size_t count) {
    Value v;
    v.type_id_ = make_array_type(TypeId::Float4);
    v.set_heap(true);
    v.flags_ |= (count & kArraySizeMask);
    size_t size = sizeof(float) * 4 * count;
    v.storage_.heap_ = new uint8_t[size];
    std::memcpy(v.storage_.heap_, data, size);
    return v;
}

Value Value::from_int32_array(const std::vector<int32_t>& arr) {
    return from_int32_array(arr.data(), arr.size());
}

Value Value::from_float_array(const std::vector<float>& arr) {
    return from_float_array(arr.data(), arr.size());
}

Value Value::from_string_array(const std::vector<std::string>& arr) {
    Value v;
    v.type_id_ = make_array_type(TypeId::String);
    v.set_heap(true);
    v.flags_ |= (arr.size() & kArraySizeMask);
    v.storage_.heap_ = new std::vector<std::string>(arr);
    return v;
}

// ============================================================================
// Type Queries
// ============================================================================

TypeId Value::type_id() const {
    return type_id_;
}

const char* Value::type_name() const {
    return get_type_name(type_id_);
}

bool Value::is_null() const {
    return type_id_ == TypeId::Null;
}

bool Value::is_none() const {
    return type_id_ == TypeId::ValueBlock;
}

bool Value::is_array() const {
    return is_array_type(type_id_);
}

size_t Value::array_size() const {
    if (!is_array_type(type_id_)) {
        return 0;
    }
    return flags_ & kArraySizeMask;
}

bool Value::is_numeric() const {
    const TypeDescriptor* desc = get_type_descriptor(type_id_);
    return desc && desc->is_numeric;
}

// ============================================================================
// Type-Specific Accessors
// ============================================================================

const bool* Value::as_bool() const {
    if (type_id_ != TypeId::Bool) return nullptr;
    return reinterpret_cast<const bool*>(storage_.inline_);
}

const int32_t* Value::as_int32() const {
    if (type_id_ != TypeId::Int32) return nullptr;
    return reinterpret_cast<const int32_t*>(storage_.inline_);
}

const int64_t* Value::as_int64() const {
    if (type_id_ != TypeId::Int64) return nullptr;
    return reinterpret_cast<const int64_t*>(storage_.inline_);
}

const uint32_t* Value::as_uint32() const {
    if (type_id_ != TypeId::UInt32) return nullptr;
    return reinterpret_cast<const uint32_t*>(storage_.inline_);
}

const uint64_t* Value::as_uint64() const {
    if (type_id_ != TypeId::UInt64) return nullptr;
    return reinterpret_cast<const uint64_t*>(storage_.inline_);
}

const uint16_t* Value::as_half() const {
    if (type_id_ != TypeId::Half) return nullptr;
    return reinterpret_cast<const uint16_t*>(storage_.inline_);
}

const float* Value::as_float() const {
    if (type_id_ != TypeId::Float) return nullptr;
    return reinterpret_cast<const float*>(storage_.inline_);
}

const double* Value::as_double() const {
    if (type_id_ != TypeId::Double) return nullptr;
    return reinterpret_cast<const double*>(storage_.inline_);
}

const int32_t* Value::as_int2() const {
    if (type_id_ != TypeId::Int2) return nullptr;
    return reinterpret_cast<const int32_t*>(storage_.inline_);
}

const int32_t* Value::as_int3() const {
    if (type_id_ != TypeId::Int3) return nullptr;
    return reinterpret_cast<const int32_t*>(storage_.inline_);
}

const int32_t* Value::as_int4() const {
    if (type_id_ != TypeId::Int4) return nullptr;
    return reinterpret_cast<const int32_t*>(storage_.inline_);
}

const float* Value::as_float2() const {
    if (type_id_ != TypeId::Float2 && type_id_ != TypeId::TexCoord2f) return nullptr;
    return reinterpret_cast<const float*>(storage_.inline_);
}

const float* Value::as_float3() const {
    TypeId base = type_id_;
    // Allow role types
    if (base != TypeId::Float3 && base != TypeId::Color3f &&
        base != TypeId::Point3f && base != TypeId::Vector3f &&
        base != TypeId::Normal3f) {
        return nullptr;
    }
    return reinterpret_cast<const float*>(storage_.inline_);
}

const float* Value::as_float4() const {
    TypeId base = type_id_;
    if (base != TypeId::Float4 && base != TypeId::Color4f) {
        return nullptr;
    }
    return reinterpret_cast<const float*>(storage_.inline_);
}

const double* Value::as_double2() const {
    if (type_id_ != TypeId::Double2) return nullptr;
    return reinterpret_cast<const double*>(storage_.inline_);
}

const double* Value::as_double3() const {
    if (type_id_ != TypeId::Double3) return nullptr;
    return reinterpret_cast<const double*>(storage_.inline_);
}

const double* Value::as_double4() const {
    if (type_id_ != TypeId::Double4) return nullptr;
    if (!is_heap()) return nullptr;
    return static_cast<const double*>(storage_.heap_);
}

const float* Value::as_matrix2f() const {
    if (type_id_ != TypeId::Matrix2f) return nullptr;
    return reinterpret_cast<const float*>(storage_.inline_);
}

const float* Value::as_matrix3f() const {
    if (type_id_ != TypeId::Matrix3f) return nullptr;
    return static_cast<const float*>(storage_.heap_);
}

const float* Value::as_matrix4f() const {
    if (type_id_ != TypeId::Matrix4f) return nullptr;
    return static_cast<const float*>(storage_.heap_);
}

const double* Value::as_matrix2d() const {
    if (type_id_ != TypeId::Matrix2d) return nullptr;
    return static_cast<const double*>(storage_.heap_);
}

const double* Value::as_matrix3d() const {
    if (type_id_ != TypeId::Matrix3d) return nullptr;
    return static_cast<const double*>(storage_.heap_);
}

const double* Value::as_matrix4d() const {
    if (type_id_ != TypeId::Matrix4d) return nullptr;
    return static_cast<const double*>(storage_.heap_);
}

const float* Value::as_quatf() const {
    if (type_id_ != TypeId::Quatf) return nullptr;
    return reinterpret_cast<const float*>(storage_.inline_);
}

const double* Value::as_quatd() const {
    if (type_id_ != TypeId::Quatd) return nullptr;
    return static_cast<const double*>(storage_.heap_);
}

const std::string* Value::as_string() const {
    if (type_id_ != TypeId::String) return nullptr;
    return static_cast<const std::string*>(storage_.heap_);
}

const Token* Value::as_token() const {
    if (type_id_ != TypeId::Token) return nullptr;
    return static_cast<const Token*>(storage_.heap_);
}

const std::string* Value::as_asset_path() const {
    if (type_id_ != TypeId::AssetPath) return nullptr;
    return static_cast<const std::string*>(storage_.heap_);
}

const Path* Value::as_path() const {
    if (type_id_ != TypeId::Path) return nullptr;
    return static_cast<const Path*>(storage_.heap_);
}

const double* Value::as_timecode() const {
    if (type_id_ != TypeId::TimeCode) return nullptr;
    return reinterpret_cast<const double*>(storage_.inline_);
}

// ============================================================================
// Array Accessors
// ============================================================================

Value::ArrayView Value::as_int32_array() const {
    if (type_id_ != make_array_type(TypeId::Int32)) return ArrayView();
    return ArrayView(storage_.heap_, flags_ & kArraySizeMask);
}

Value::ArrayView Value::as_float_array() const {
    if (type_id_ != make_array_type(TypeId::Float)) return ArrayView();
    return ArrayView(storage_.heap_, flags_ & kArraySizeMask);
}

Value::ArrayView Value::as_double_array() const {
    if (type_id_ != make_array_type(TypeId::Double)) return ArrayView();
    return ArrayView(storage_.heap_, flags_ & kArraySizeMask);
}

Value::ArrayView Value::as_float2_array() const {
    if (type_id_ != make_array_type(TypeId::Float2)) return ArrayView();
    return ArrayView(storage_.heap_, flags_ & kArraySizeMask);
}

Value::ArrayView Value::as_float3_array() const {
    TypeId expected = make_array_type(TypeId::Float3);
    TypeId point3f_arr = make_array_type(TypeId::Point3f);
    if (type_id_ != expected && type_id_ != point3f_arr) return ArrayView();
    return ArrayView(storage_.heap_, flags_ & kArraySizeMask);
}

Value::ArrayView Value::as_float4_array() const {
    if (type_id_ != make_array_type(TypeId::Float4)) return ArrayView();
    return ArrayView(storage_.heap_, flags_ & kArraySizeMask);
}

// ============================================================================
// Raw Access
// ============================================================================

const void* Value::raw_data() const {
    if (is_heap()) {
        return storage_.heap_;
    }
    return storage_.inline_;
}

void* Value::raw_data() {
    if (is_heap()) {
        return storage_.heap_;
    }
    return storage_.inline_;
}

// ============================================================================
// Comparison
// ============================================================================

bool Value::operator==(const Value& other) const {
    if (type_id_ != other.type_id_) {
        return false;
    }

    if (type_id_ == TypeId::Null) {
        return true;
    }

    if (type_id_ == TypeId::ValueBlock) {
        return true;
    }

    // Compare based on type
    const TypeDescriptor* desc = get_type_descriptor(type_id_);
    if (!desc) {
        return false;
    }

    // Handle heap-allocated non-array types
    TypeId base = get_base_type(type_id_);
    if (base == TypeId::String || base == TypeId::AssetPath) {
        return *static_cast<const std::string*>(storage_.heap_) ==
               *static_cast<const std::string*>(other.storage_.heap_);
    }
    if (base == TypeId::Token) {
        return *static_cast<const Token*>(storage_.heap_) ==
               *static_cast<const Token*>(other.storage_.heap_);
    }
    if (base == TypeId::Path) {
        return *static_cast<const Path*>(storage_.heap_) ==
               *static_cast<const Path*>(other.storage_.heap_);
    }

    // POD comparison
    size_t size = desc->size;
    if (is_array_type(type_id_)) {
        size_t count = flags_ & kArraySizeMask;
        size_t other_count = other.flags_ & kArraySizeMask;
        if (count != other_count) {
            return false;
        }
        size *= count;
    }

    const void* a = is_heap() ? storage_.heap_ : storage_.inline_;
    const void* b = other.is_heap() ? other.storage_.heap_ : other.storage_.inline_;

    return std::memcmp(a, b, size) == 0;
}

bool Value::operator!=(const Value& other) const {
    return !(*this == other);
}

// ============================================================================
// Utility
// ============================================================================

void Value::swap(Value& other) noexcept {
    std::swap(storage_, other.storage_);
    std::swap(type_id_, other.type_id_);
    std::swap(flags_, other.flags_);
}

void Value::clear() {
    destroy();
    type_id_ = TypeId::Null;
    flags_ = 0;
    std::memset(&storage_, 0, sizeof(storage_));
}

} // namespace v1
} // namespace lightusd
