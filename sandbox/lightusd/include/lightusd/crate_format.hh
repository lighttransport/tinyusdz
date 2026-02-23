// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// USDC (Crate) file format definitions
// Based on Pixar's USD Crate format specification

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

namespace lightusd {
namespace v1 {
namespace crate {

// =============================================================================
// Constants
// =============================================================================

constexpr char kCrateMagic[] = "PXR-USDC";
constexpr size_t kCrateMagicSize = 8;
constexpr size_t kSectionNameMaxLength = 15;
constexpr size_t kMinCompressedArraySize = 16;

// Software version info embedded in crate files
struct CrateVersion {
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;

    bool operator>=(const CrateVersion& other) const {
        if (major != other.major) return major > other.major;
        if (minor != other.minor) return minor > other.minor;
        return patch >= other.patch;
    }

    std::string to_string() const {
        return std::to_string(major) + "." +
               std::to_string(minor) + "." +
               std::to_string(patch);
    }
};

// Minimum supported version
constexpr CrateVersion kMinSupportedVersion = {0, 0, 1};
// Current version we write
constexpr CrateVersion kCurrentVersion = {0, 9, 0};

// =============================================================================
// Crate Data Types
// =============================================================================

// Data type IDs matching Pixar's crateDataType.h
enum class CrateDataTypeId : int32_t {
    Invalid = 0,

    Bool = 1,
    UChar = 2,
    Int = 3,
    UInt = 4,
    Int64 = 5,
    UInt64 = 6,

    Half = 7,
    Float = 8,
    Double = 9,

    String = 10,
    Token = 11,
    AssetPath = 12,

    Matrix2d = 13,
    Matrix3d = 14,
    Matrix4d = 15,

    Quatd = 16,
    Quatf = 17,
    Quath = 18,

    Vec2d = 19,
    Vec2f = 20,
    Vec2h = 21,
    Vec2i = 22,

    Vec3d = 23,
    Vec3f = 24,
    Vec3h = 25,
    Vec3i = 26,

    Vec4d = 27,
    Vec4f = 28,
    Vec4h = 29,
    Vec4i = 30,

    Dictionary = 31,
    TokenListOp = 32,
    StringListOp = 33,
    PathListOp = 34,
    ReferenceListOp = 35,
    IntListOp = 36,
    Int64ListOp = 37,
    UIntListOp = 38,
    UInt64ListOp = 39,

    PathVector = 40,
    TokenVector = 41,

    Specifier = 42,
    Permission = 43,
    Variability = 44,

    VariantSelectionMap = 45,
    TimeSamples = 46,
    Payload = 47,
    DoubleVector = 48,
    LayerOffsetVector = 49,
    StringVector = 50,
    ValueBlock = 51,
    Value = 52,
    UnregisteredValue = 53,
    UnregisteredValueListOp = 54,
    PayloadListOp = 55,
    TimeCode = 56,

    NumDataTypes
};

/// Get human-readable name for a crate data type
const char* crate_data_type_name(CrateDataTypeId id);
const char* crate_data_type_name(int32_t id);

// =============================================================================
// Index Types
// =============================================================================

/// Base index type for table indexing
struct Index {
    uint32_t value = ~0u;

    Index() = default;
    explicit Index(uint32_t v) : value(v) {}

    bool operator==(const Index& other) const { return value == other.value; }
    bool operator!=(const Index& other) const { return value != other.value; }
    bool operator<(const Index& other) const { return value < other.value; }
    bool is_valid() const { return value != ~0u; }
};

struct TokenIndex : Index { using Index::Index; };
struct StringIndex : Index { using Index::Index; };
struct FieldIndex : Index { using Index::Index; };
struct FieldSetIndex : Index { using Index::Index; };
struct PathIndex : Index { using Index::Index; };

// =============================================================================
// ValueRep - Lazy value representation
// =============================================================================

/// Value representation in crate file format.
/// Consists of 2 bytes type info and 6 bytes payload.
/// Values may be stored inline (for small values) or at an offset in the file.
struct ValueRep {
    uint64_t data = 0;

    // Bit positions
    static constexpr uint64_t kIsArrayBit = 1ull << 63;
    static constexpr uint64_t kIsInlinedBit = 1ull << 62;
    static constexpr uint64_t kIsCompressedBit = 1ull << 61;
    static constexpr uint64_t kPayloadMask = (1ull << 48) - 1;

    ValueRep() = default;
    explicit ValueRep(uint64_t d) : data(d) {}

    ValueRep(int32_t type, bool is_inlined, bool is_array, uint64_t payload)
        : data(combine(type, is_inlined, is_array, payload)) {}

    // Type accessors
    int32_t type() const { return static_cast<int32_t>((data >> 48) & 0xFF); }
    CrateDataTypeId type_id() const { return static_cast<CrateDataTypeId>(type()); }

    void set_type(int32_t t) {
        data &= ~(0xFFull << 48);
        data |= (static_cast<uint64_t>(t) << 48);
    }

    // Flag accessors
    bool is_array() const { return (data & kIsArrayBit) != 0; }
    bool is_inlined() const { return (data & kIsInlinedBit) != 0; }
    bool is_compressed() const { return (data & kIsCompressedBit) != 0; }

    void set_is_array(bool v) { if (v) data |= kIsArrayBit; else data &= ~kIsArrayBit; }
    void set_is_inlined(bool v) { if (v) data |= kIsInlinedBit; else data &= ~kIsInlinedBit; }
    void set_is_compressed(bool v) { if (v) data |= kIsCompressedBit; else data &= ~kIsCompressedBit; }

    // Payload (either inline value or file offset)
    uint64_t payload() const { return data & kPayloadMask; }

    void set_payload(uint64_t p) {
        data &= ~kPayloadMask;
        data |= (p & kPayloadMask);
    }

    // For inlined values, extract as specific types
    int32_t as_int32() const { return static_cast<int32_t>(payload()); }
    uint32_t as_uint32() const { return static_cast<uint32_t>(payload()); }
    float as_float() const {
        uint32_t bits = as_uint32();
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    // Comparison
    bool operator==(const ValueRep& other) const { return data == other.data; }
    bool operator!=(const ValueRep& other) const { return data != other.data; }

    // Debug string
    std::string to_string() const {
        std::ostringstream oss;
        oss << "ValueRep{type=" << type()
            << "(" << crate_data_type_name(type()) << ")"
            << ", array=" << is_array()
            << ", inlined=" << is_inlined()
            << ", compressed=" << is_compressed()
            << ", payload=" << payload() << "}";
        return oss.str();
    }

private:
    static uint64_t combine(int32_t type, bool is_inlined, bool is_array, uint64_t payload) {
        return (is_array ? kIsArrayBit : 0) |
               (is_inlined ? kIsInlinedBit : 0) |
               (static_cast<uint64_t>(type) << 48) |
               (payload & kPayloadMask);
    }
};

// =============================================================================
// File Structures
// =============================================================================

/// Section in the crate file
struct Section {
    char name[kSectionNameMaxLength + 1] = {0};
    int64_t start = 0;   // Byte offset to section data
    int64_t size = 0;    // Size of section data in bytes

    Section() = default;

    Section(const char* n, int64_t s, int64_t sz) : start(s), size(sz) {
        std::strncpy(name, n, kSectionNameMaxLength);
        name[kSectionNameMaxLength] = '\0';
    }

    std::string name_str() const { return std::string(name); }
};

/// Table of contents - list of sections
struct TableOfContents {
    std::vector<Section> sections;

    const Section* find_section(const char* name) const {
        for (const auto& s : sections) {
            if (std::strcmp(s.name, name) == 0) {
                return &s;
            }
        }
        return nullptr;
    }
};

/// Field - maps a token (field name) to a value
struct Field {
    TokenIndex token_index;
    ValueRep value_rep;
};

/// Spec type enumeration
enum class SpecType : int32_t {
    Unknown = 0,
    Attribute = 1,
    Connection = 2,
    Expression = 3,
    Mapper = 4,
    MapperArg = 5,
    Prim = 6,
    PseudoRoot = 7,
    Relationship = 8,
    RelationshipTarget = 9,
    Variant = 10,
    VariantSet = 11
};

const char* spec_type_name(SpecType type);

/// Spec - describes a path and its associated fields
struct Spec {
    PathIndex path_index;
    FieldSetIndex fieldset_index;
    SpecType spec_type = SpecType::Unknown;
};

static_assert(sizeof(Spec) == 12, "Spec must be 12 bytes");

// =============================================================================
// Lazy Value Wrapper
// =============================================================================

/// Lazy value that holds a ValueRep and decodes on demand
/// This allows deferring the expensive value reconstruction until actually needed
class LazyValue {
public:
    LazyValue() = default;
    explicit LazyValue(ValueRep rep) : rep_(rep), decoded_(false) {}

    /// Get the underlying ValueRep
    const ValueRep& rep() const { return rep_; }

    /// Check if this is a valid lazy value
    bool is_valid() const { return rep_.data != 0; }

    /// Check if value has been decoded
    bool is_decoded() const { return decoded_; }

    /// Mark as decoded (called by decoder)
    void mark_decoded() { decoded_ = true; }

    /// Type information
    CrateDataTypeId type_id() const { return rep_.type_id(); }
    bool is_array() const { return rep_.is_array(); }
    bool is_inlined() const { return rep_.is_inlined(); }
    bool is_compressed() const { return rep_.is_compressed(); }

    /// For inlined simple values, can get directly
    int32_t get_int32() const { return rep_.as_int32(); }
    uint32_t get_uint32() const { return rep_.as_uint32(); }
    float get_float() const { return rep_.as_float(); }
    bool get_bool() const { return rep_.payload() != 0; }

    /// File offset for non-inlined values
    uint64_t file_offset() const { return rep_.payload(); }

private:
    ValueRep rep_;
    bool decoded_ = false;
};

// =============================================================================
// Path representation in crate file
// =============================================================================

/// Encoded path element
struct PathElement {
    TokenIndex token;          // Name token
    TokenIndex jump_token;     // For sibling navigation
    bool has_child = false;
    bool has_sibling = false;
    bool is_prim_property_path = false;
};

} // namespace crate
} // namespace v1
} // namespace lightusd
