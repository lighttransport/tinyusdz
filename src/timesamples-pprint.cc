// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.

#include "timesamples-pprint.hh"

#include <sstream>
#include <cstring>

#include "value-types.hh"
#include "value-pprint.hh"
#include "pprinter.hh"
#include "logger.hh"
#include "timesamples.hh"
#include "stream-writer.hh"
#include "typed-array.hh"

namespace tinyusdz {

namespace {

// Helper function to convert raw bytes to typed value and print
template<typename T>
std::string print_pod_value(const uint8_t* data) {
    T value;
    std::memcpy(&value, data, sizeof(T));

    std::stringstream ss;
    ss << value;
    return ss.str();
}

// Specialization for float
std::string print_float(const uint8_t* data) {
    float value;
    std::memcpy(&value, data, sizeof(float));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

// Specialization for double
std::string print_double(const uint8_t* data) {
    double value;
    std::memcpy(&value, data, sizeof(double));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

// Specialization for vector types
template<typename T>
std::string print_vector2(const uint8_t* data) {
    T value[2];
    std::memcpy(&value, data, sizeof(T) * 2);
    std::stringstream ss;
    ss << "(" << value[0] << ", " << value[1] << ")";
    return ss.str();
}

template<typename T>
std::string print_vector3(const uint8_t* data) {
    T value[3];
    std::memcpy(&value, data, sizeof(T) * 3);
    std::stringstream ss;
    ss << "(" << value[0] << ", " << value[1] << ", " << value[2] << ")";
    return ss.str();
}

template<typename T>
std::string print_vector4(const uint8_t* data) {
    T value[4];
    std::memcpy(&value, data, sizeof(T) * 4);
    std::stringstream ss;
    ss << "(" << value[0] << ", " << value[1] << ", " << value[2] << ", " << value[3] << ")";
    return ss.str();
}

// Float vector specializations - use value types directly
std::string print_float2(const uint8_t* data) {
    value::float2 value;
    std::memcpy(&value, data, sizeof(value::float2));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string print_float3(const uint8_t* data) {
    value::float3 value;
    std::memcpy(&value, data, sizeof(value::float3));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string print_float4(const uint8_t* data) {
    value::float4 value;
    std::memcpy(&value, data, sizeof(value::float4));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

// Double vector specializations - use value types directly
std::string print_double2(const uint8_t* data) {
    value::double2 value;
    std::memcpy(&value, data, sizeof(value::double2));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string print_double3(const uint8_t* data) {
    value::double3 value;
    std::memcpy(&value, data, sizeof(value::double3));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string print_double4(const uint8_t* data) {
    value::double4 value;
    std::memcpy(&value, data, sizeof(value::double4));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

// Half (float16) support
std::string print_half(const uint8_t* data) {
    value::half value;
    std::memcpy(&value, data, sizeof(value::half));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string print_half2(const uint8_t* data) {
    value::half2 value;
    std::memcpy(&value, data, sizeof(value::half2));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string print_half3(const uint8_t* data) {
    value::half3 value;
    std::memcpy(&value, data, sizeof(value::half3));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

std::string print_half4(const uint8_t* data) {
    value::half4 value;
    std::memcpy(&value, data, sizeof(value::half4));
    std::stringstream ss;
    ss << value;
    return ss.str();
}

// Char vector types (treat as byte)
std::string print_char2(const uint8_t* data) {
    char value[2];
    std::memcpy(&value, data, sizeof(char) * 2);
    std::stringstream ss;
    ss << "(" << int(value[0]) << ", " << int(value[1]) << ")";
    return ss.str();
}

std::string print_char3(const uint8_t* data) {
    char value[3];
    std::memcpy(&value, data, sizeof(char) * 3);
    std::stringstream ss;
    ss << "(" << int(value[0]) << ", " << int(value[1]) << ", " << int(value[2]) << ")";
    return ss.str();
}

std::string print_char4(const uint8_t* data) {
    char value[4];
    std::memcpy(&value, data, sizeof(char) * 4);
    std::stringstream ss;
    ss << "(" << int(value[0]) << ", " << int(value[1]) << ", " << int(value[2]) << ", " << int(value[3]) << ")";
    return ss.str();
}

std::string print_uchar2(const uint8_t* data) {
    uint8_t value[2];
    std::memcpy(&value, data, sizeof(uint8_t) * 2);
    std::stringstream ss;
    ss << "(" << int(value[0]) << ", " << int(value[1]) << ")";
    return ss.str();
}

std::string print_uchar3(const uint8_t* data) {
    uint8_t value[3];
    std::memcpy(&value, data, sizeof(uint8_t) * 3);
    std::stringstream ss;
    ss << "(" << int(value[0]) << ", " << int(value[1]) << ", " << int(value[2]) << ")";
    return ss.str();
}

std::string print_uchar4(const uint8_t* data) {
    uint8_t value[4];
    std::memcpy(&value, data, sizeof(uint8_t) * 4);
    std::stringstream ss;
    ss << "(" << int(value[0]) << ", " << int(value[1]) << ", " << int(value[2]) << ", " << int(value[3]) << ")";
    return ss.str();
}

// Matrix types
std::string print_matrix2f(const uint8_t* data) {
    value::matrix2f m;
    std::memcpy(&m, data, sizeof(value::matrix2f));
    std::stringstream ss;
    ss << m;
    return ss.str();
}

std::string print_matrix3f(const uint8_t* data) {
    value::matrix3f m;
    std::memcpy(&m, data, sizeof(value::matrix3f));
    std::stringstream ss;
    ss << m;
    return ss.str();
}

std::string print_matrix4f(const uint8_t* data) {
    value::matrix4f m;
    std::memcpy(&m, data, sizeof(value::matrix4f));
    std::stringstream ss;
    ss << m;
    return ss.str();
}

std::string print_matrix2d(const uint8_t* data) {
    value::matrix2d m;
    std::memcpy(&m, data, sizeof(value::matrix2d));
    std::stringstream ss;
    ss << m;
    return ss.str();
}

std::string print_matrix3d(const uint8_t* data) {
    value::matrix3d m;
    std::memcpy(&m, data, sizeof(value::matrix3d));
    std::stringstream ss;
    ss << m;
    return ss.str();
}

std::string print_matrix4d(const uint8_t* data) {
    value::matrix4d m;
    std::memcpy(&m, data, sizeof(value::matrix4d));
    std::stringstream ss;
    ss << m;
    return ss.str();
}

// Quaternion types
std::string print_quath(const uint8_t* data) {
    value::quath q;
    std::memcpy(&q, data, sizeof(value::quath));
    std::stringstream ss;
    ss << q;
    return ss.str();
}

std::string print_quatf(const uint8_t* data) {
    value::quatf q;
    std::memcpy(&q, data, sizeof(value::quatf));
    std::stringstream ss;
    ss << q;
    return ss.str();
}

std::string print_quatd(const uint8_t* data) {
    value::quatd q;
    std::memcpy(&q, data, sizeof(value::quatd));
    std::stringstream ss;
    ss << q;
    return ss.str();
}

// Color types
std::string print_color3h(const uint8_t* data) {
    value::color3h c;
    std::memcpy(&c, data, sizeof(value::color3h));
    std::stringstream ss;
    ss << c;
    return ss.str();
}

std::string print_color3f(const uint8_t* data) {
    value::color3f c;
    std::memcpy(&c, data, sizeof(value::color3f));
    std::stringstream ss;
    ss << c;
    return ss.str();
}

std::string print_color3d(const uint8_t* data) {
    value::color3d c;
    std::memcpy(&c, data, sizeof(value::color3d));
    std::stringstream ss;
    ss << c;
    return ss.str();
}

std::string print_color4h(const uint8_t* data) {
    value::color4h c;
    std::memcpy(&c, data, sizeof(value::color4h));
    std::stringstream ss;
    ss << c;
    return ss.str();
}

std::string print_color4f(const uint8_t* data) {
    value::color4f c;
    std::memcpy(&c, data, sizeof(value::color4f));
    std::stringstream ss;
    ss << c;
    return ss.str();
}

std::string print_color4d(const uint8_t* data) {
    value::color4d c;
    std::memcpy(&c, data, sizeof(value::color4d));
    std::stringstream ss;
    ss << c;
    return ss.str();
}

// Texture coordinate types
std::string print_texcoord2h(const uint8_t* data) {
    value::texcoord2h t;
    std::memcpy(&t, data, sizeof(value::texcoord2h));
    std::stringstream ss;
    ss << t;
    return ss.str();
}

std::string print_texcoord2f(const uint8_t* data) {
    value::texcoord2f t;
    std::memcpy(&t, data, sizeof(value::texcoord2f));
    std::stringstream ss;
    ss << t;
    return ss.str();
}

std::string print_texcoord2d(const uint8_t* data) {
    value::texcoord2d t;
    std::memcpy(&t, data, sizeof(value::texcoord2d));
    std::stringstream ss;
    ss << t;
    return ss.str();
}

std::string print_texcoord3h(const uint8_t* data) {
    value::texcoord3h t;
    std::memcpy(&t, data, sizeof(value::texcoord3h));
    std::stringstream ss;
    ss << t;
    return ss.str();
}

std::string print_texcoord3f(const uint8_t* data) {
    value::texcoord3f t;
    std::memcpy(&t, data, sizeof(value::texcoord3f));
    std::stringstream ss;
    ss << t;
    return ss.str();
}

std::string print_texcoord3d(const uint8_t* data) {
    value::texcoord3d t;
    std::memcpy(&t, data, sizeof(value::texcoord3d));
    std::stringstream ss;
    ss << t;
    return ss.str();
}

// TypedArray - stored as packed pointer (uint64_t)
// Attempt to reconstruct TypedArray and print its contents
template<typename T>
std::string try_print_typed_array(const uint8_t* packed_ptr_data) {
    uint64_t packed_value;
    std::memcpy(&packed_value, packed_ptr_data, sizeof(uint64_t));

    // Extract pointer from packed value (lower 48 bits)
    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

    // Sign-extend from 48 bits to 64 bits for canonical address
    if (ptr_bits & (1ULL << 47)) {
        ptr_bits |= 0xFFFF000000000000ULL;
    }

    if (ptr_bits == 0) {
        return "";  // Return empty to indicate failure
    }

    // Cast to TypedArrayImpl<T>*
    auto* impl = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);

    // Create TypedArray with dedup flag to prevent deletion
    TypedArray<T> typed_array(impl, true);

    // Create a view to access the data
    TypedArrayView<const T> view(typed_array);

    if (view.size() == 0) {
        return "[]";
    }

    std::stringstream ss;
    ss << "[";

    // Limit output to first 10 elements for readability
    size_t max_elements = std::min(view.size(), size_t(10));

    for (size_t i = 0; i < max_elements; ++i) {
        if (i > 0) ss << ", ";

        // In C++14, we can't use if constexpr, so just output directly
        // The operator<< should work for all types we're likely to encounter
        ss << view[i];
    }

    if (view.size() > max_elements) {
        ss << ", ... (" << view.size() << " total)";
    }

    ss << "]";
    return ss.str();
}

// Geometry types
std::string print_point3h(const uint8_t* data) {
    value::point3h p;
    std::memcpy(&p, data, sizeof(value::point3h));
    std::stringstream ss;
    ss << p;
    return ss.str();
}

std::string print_point3f(const uint8_t* data) {
    value::point3f p;
    std::memcpy(&p, data, sizeof(value::point3f));
    std::stringstream ss;
    ss << p;
    return ss.str();
}

std::string print_point3d(const uint8_t* data) {
    value::point3d p;
    std::memcpy(&p, data, sizeof(value::point3d));
    std::stringstream ss;
    ss << p;
    return ss.str();
}

std::string print_normal3h(const uint8_t* data) {
    value::normal3h n;
    std::memcpy(&n, data, sizeof(value::normal3h));
    std::stringstream ss;
    ss << n;
    return ss.str();
}

std::string print_normal3f(const uint8_t* data) {
    value::normal3f n;
    std::memcpy(&n, data, sizeof(value::normal3f));
    std::stringstream ss;
    ss << n;
    return ss.str();
}

std::string print_normal3d(const uint8_t* data) {
    value::normal3d n;
    std::memcpy(&n, data, sizeof(value::normal3d));
    std::stringstream ss;
    ss << n;
    return ss.str();
}

std::string print_vector3h(const uint8_t* data) {
    value::vector3h v;
    std::memcpy(&v, data, sizeof(value::vector3h));
    std::stringstream ss;
    ss << v;
    return ss.str();
}

std::string print_vector3f(const uint8_t* data) {
    value::vector3f v;
    std::memcpy(&v, data, sizeof(value::vector3f));
    std::stringstream ss;
    ss << v;
    return ss.str();
}

std::string print_vector3d(const uint8_t* data) {
    value::vector3d v;
    std::memcpy(&v, data, sizeof(value::vector3d));
    std::stringstream ss;
    ss << v;
    return ss.str();
}

std::string print_frame4d(const uint8_t* data) {
    value::frame4d f;
    std::memcpy(&f, data, sizeof(value::frame4d));
    std::stringstream ss;
    ss << f;
    return ss.str();
}

// StreamWriter versions of print functions
template<typename T>
void print_pod_value(StreamWriter& writer, const uint8_t* data) {
    TUSDZ_LOG_I("pod_value\n");
    T value;
    std::memcpy(&value, data, sizeof(T));
    writer << value;
}

void print_float(StreamWriter& writer, const uint8_t* data) {
    float value;
    std::memcpy(&value, data, sizeof(float));
    writer.write(value);
}

void print_double(StreamWriter& writer, const uint8_t* data) {
    double value;
    std::memcpy(&value, data, sizeof(double));
    writer.write(value);
}

template<typename T>
void print_vector2(StreamWriter& writer, const uint8_t* data) {
    T value[2];
    std::memcpy(&value, data, sizeof(T) * 2);
    writer << "(" << value[0] << ", " << value[1] << ")";
}

template<typename T>
void print_vector3(StreamWriter& writer, const uint8_t* data) {
    T value[3];
    std::memcpy(&value, data, sizeof(T) * 3);
    writer << "(" << value[0] << ", " << value[1] << ", " << value[2] << ")";
}

template<typename T>
void print_vector4(StreamWriter& writer, const uint8_t* data) {
    T value[4];
    std::memcpy(&value, data, sizeof(T) * 4);
    writer << "(" << value[0] << ", " << value[1] << ", " << value[2] << ", " << value[3] << ")";
}

// Write value types directly using their operator<<
template<typename ValueType>
void print_value_type(StreamWriter& writer, const uint8_t* data) {
    ValueType value;
    std::memcpy(&value, data, sizeof(ValueType));
    // Use a temporary stringstream since value types have operator<< for ostream
    std::stringstream ss;
    ss << value;
    writer.write(ss.str());
}

// TypedArray - stored as packed pointer (uint64_t)
// Attempt to reconstruct TypedArray and print its contents to StreamWriter
template<typename T>
bool try_print_typed_array_value(StreamWriter& writer, const uint8_t* packed_ptr_data) {
    uint64_t packed_value;
    std::memcpy(&packed_value, packed_ptr_data, sizeof(uint64_t));

    // Extract pointer from packed value (lower 48 bits)
    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

    // Sign-extend from 48 bits to 64 bits for canonical address
    if (ptr_bits & (1ULL << 47)) {
        ptr_bits |= 0xFFFF000000000000ULL;
    }

    if (ptr_bits == 0) {
        return false;  // Return false to indicate failure
    }

    // Cast to TypedArrayImpl<T>*
    auto* impl = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);

    // Create TypedArray with dedup flag to prevent deletion
    TypedArray<T> typed_array(impl, true);

    // Create a view to access the data
    TypedArrayView<const T> view(typed_array);

    if (view.size() == 0) {
        writer.write("[]");
        return true;
    }

    writer.write("[");

    // Limit output to first 10 elements for readability
    size_t max_elements = std::min(view.size(), size_t(10));

    for (size_t i = 0; i < max_elements; ++i) {
        if (i > 0) writer.write(", ");

        // Write the value using operator<< via stringstream
        std::stringstream ss;
        ss << view[i];
        writer.write(ss.str());
    }

    if (view.size() > max_elements) {
        writer.write(", ... (");
        writer.write(static_cast<int>(view.size()));
        writer.write(" total)");
    }

    writer.write("]");
    return true;
}

void print_typed_array_value(StreamWriter& writer, const uint8_t* data) {
    // Try common types in order of likelihood
    bool success = false;

    // Try float array
    success = try_print_typed_array_value<float>(writer, data);
    if (success) return;

    // Try double array
    success = try_print_typed_array_value<double>(writer, data);
    if (success) return;

    // Try int array
    success = try_print_typed_array_value<int>(writer, data);
    if (success) return;

    // Try float3 array
    success = try_print_typed_array_value<value::float3>(writer, data);
    if (success) return;

    // Try float2 array
    success = try_print_typed_array_value<value::float2>(writer, data);
    if (success) return;

    // Try double3 array
    success = try_print_typed_array_value<value::double3>(writer, data);
    if (success) return;

    // If we can't determine the type, print a generic representation
    uint64_t packed_value;
    std::memcpy(&packed_value, data, sizeof(uint64_t));

    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;
    if (ptr_bits & (1ULL << 47)) {
        ptr_bits |= 0xFFFF000000000000ULL;
    }

    bool is_dedup = (packed_value & (1ULL << 63)) != 0;

    if (ptr_bits == 0) {
        writer.write("[]");
    } else {
        writer.write("[TypedArray@0x");
        std::stringstream ss;
        ss << std::hex << ptr_bits;
        writer.write(ss.str());
        if (is_dedup) {
            writer.write(" (dedup)");
        }
        writer.write("]");
    }
}

} // namespace

// TypedArray printing function - moved outside anonymous namespace to be accessible
std::string print_typed_array(const uint8_t* data) {
    // Try common types in order of likelihood
    std::string result;

    // Try float array
    result = try_print_typed_array<float>(data);
    if (!result.empty()) return result;

    // Try double array
    result = try_print_typed_array<double>(data);
    if (!result.empty()) return result;

    // Try int array
    result = try_print_typed_array<int>(data);
    if (!result.empty()) return result;

    // Try float3 array
    result = try_print_typed_array<value::float3>(data);
    if (!result.empty()) return result;

    // Try float2 array
    result = try_print_typed_array<value::float2>(data);
    if (!result.empty()) return result;

    // Try double3 array
    result = try_print_typed_array<value::double3>(data);
    if (!result.empty()) return result;

    // If we can't determine the type, print a generic representation
    uint64_t packed_value;
    std::memcpy(&packed_value, data, sizeof(uint64_t));

    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;
    if (ptr_bits & (1ULL << 47)) {
        ptr_bits |= 0xFFFF000000000000ULL;
    }

    bool is_dedup = (packed_value & (1ULL << 63)) != 0;

    std::stringstream ss;
    if (ptr_bits == 0) {
        ss << "[]";
    } else {
        ss << "[TypedArray@0x" << std::hex << ptr_bits;
        if (is_dedup) {
            ss << " (dedup)";
        }
        ss << "]";
    }
    return ss.str();
}

std::string pprint_pod_value_by_type(const uint8_t* data, uint32_t type_id) {
    using namespace value;

    switch (type_id) {
        case TYPE_ID_BOOL:
            return print_pod_value<bool>(data);
        case TYPE_ID_CHAR:
            return print_pod_value<char>(data);
        case TYPE_ID_CHAR2:
            return print_char2(data);
        case TYPE_ID_CHAR3:
            return print_char3(data);
        case TYPE_ID_CHAR4:
            return print_char4(data);
        case TYPE_ID_UCHAR:
            return print_pod_value<uint8_t>(data);
        case TYPE_ID_UCHAR2:
            return print_uchar2(data);
        case TYPE_ID_UCHAR3:
            return print_uchar3(data);
        case TYPE_ID_UCHAR4:
            return print_uchar4(data);
        case TYPE_ID_SHORT:
            return print_pod_value<int16_t>(data);
        case TYPE_ID_SHORT2:
            return print_vector2<int16_t>(data);
        case TYPE_ID_SHORT3:
            return print_vector3<int16_t>(data);
        case TYPE_ID_SHORT4:
            return print_vector4<int16_t>(data);
        case TYPE_ID_USHORT:
            return print_pod_value<uint16_t>(data);
        case TYPE_ID_USHORT2:
            return print_vector2<uint16_t>(data);
        case TYPE_ID_USHORT3:
            return print_vector3<uint16_t>(data);
        case TYPE_ID_USHORT4:
            return print_vector4<uint16_t>(data);
        case TYPE_ID_INT32:
            return print_pod_value<int32_t>(data);
        case TYPE_ID_INT2:
            return print_vector2<int32_t>(data);
        case TYPE_ID_INT3:
            return print_vector3<int32_t>(data);
        case TYPE_ID_INT4:
            return print_vector4<int32_t>(data);
        case TYPE_ID_UINT32:
            return print_pod_value<uint32_t>(data);
        case TYPE_ID_UINT2:
            return print_vector2<uint32_t>(data);
        case TYPE_ID_UINT3:
            return print_vector3<uint32_t>(data);
        case TYPE_ID_UINT4:
            return print_vector4<uint32_t>(data);
        case TYPE_ID_INT64:
            return print_pod_value<int64_t>(data);
        case TYPE_ID_UINT64:
            return print_pod_value<uint64_t>(data);
        case TYPE_ID_HALF:
            return print_half(data);
        case TYPE_ID_HALF2:
            return print_half2(data);
        case TYPE_ID_HALF3:
            return print_half3(data);
        case TYPE_ID_HALF4:
            return print_half4(data);
        case TYPE_ID_FLOAT:
            return print_float(data);
        case TYPE_ID_FLOAT2:
            return print_float2(data);
        case TYPE_ID_FLOAT3:
            return print_float3(data);
        case TYPE_ID_FLOAT4:
            return print_float4(data);
        case TYPE_ID_DOUBLE:
            return print_double(data);
        case TYPE_ID_DOUBLE2:
            return print_double2(data);
        case TYPE_ID_DOUBLE3:
            return print_double3(data);
        case TYPE_ID_DOUBLE4:
            return print_double4(data);
        case TYPE_ID_QUATH:
            return print_quath(data);
        case TYPE_ID_QUATF:
            return print_quatf(data);
        case TYPE_ID_QUATD:
            return print_quatd(data);
        case TYPE_ID_MATRIX2F:
            return print_matrix2f(data);
        case TYPE_ID_MATRIX3F:
            return print_matrix3f(data);
        case TYPE_ID_MATRIX4F:
            return print_matrix4f(data);
        case TYPE_ID_MATRIX2D:
            return print_matrix2d(data);
        case TYPE_ID_MATRIX3D:
            return print_matrix3d(data);
        case TYPE_ID_MATRIX4D:
            return print_matrix4d(data);
        case TYPE_ID_COLOR3H:
            return print_color3h(data);
        case TYPE_ID_COLOR3F:
            return print_color3f(data);
        case TYPE_ID_COLOR3D:
            return print_color3d(data);
        case TYPE_ID_COLOR4H:
            return print_color4h(data);
        case TYPE_ID_COLOR4F:
            return print_color4f(data);
        case TYPE_ID_COLOR4D:
            return print_color4d(data);
        case TYPE_ID_POINT3H:
            return print_point3h(data);
        case TYPE_ID_POINT3F:
            return print_point3f(data);
        case TYPE_ID_POINT3D:
            return print_point3d(data);
        case TYPE_ID_NORMAL3H:
            return print_normal3h(data);
        case TYPE_ID_NORMAL3F:
            return print_normal3f(data);
        case TYPE_ID_NORMAL3D:
            return print_normal3d(data);
        case TYPE_ID_VECTOR3H:
            return print_vector3h(data);
        case TYPE_ID_VECTOR3F:
            return print_vector3f(data);
        case TYPE_ID_VECTOR3D:
            return print_vector3d(data);
        case TYPE_ID_FRAME4D:
            return print_frame4d(data);
        case TYPE_ID_TEXCOORD2H:
            return print_texcoord2h(data);
        case TYPE_ID_TEXCOORD2F:
            return print_texcoord2f(data);
        case TYPE_ID_TEXCOORD2D:
            return print_texcoord2d(data);
        case TYPE_ID_TEXCOORD3H:
            return print_texcoord3h(data);
        case TYPE_ID_TEXCOORD3F:
            return print_texcoord3f(data);
        case TYPE_ID_TEXCOORD3D:
            return print_texcoord3d(data);
        case TYPE_ID_TYPED_ARRAY_TIMESAMPLE_VALUE:
            return print_typed_array(data);
        default:
            return "[Unknown POD type: " + std::to_string(type_id) + "]";
    }
}

void pprint_pod_value_by_type(StreamWriter& writer, const uint8_t* data, uint32_t type_id) {
    using namespace value;

    switch (type_id) {
        case TYPE_ID_BOOL:
            print_pod_value<bool>(writer, data);
            break;
        case TYPE_ID_CHAR:
            print_pod_value<char>(writer, data);
            break;
        case TYPE_ID_CHAR2:
            writer << "(" << int(data[0]) << ", " << int(data[1]) << ")";
            break;
        case TYPE_ID_CHAR3:
            writer << "(" << int(data[0]) << ", " << int(data[1]) << ", " << int(data[2]) << ")";
            break;
        case TYPE_ID_CHAR4:
            writer << "(" << int(data[0]) << ", " << int(data[1]) << ", " << int(data[2]) << ", " << int(data[3]) << ")";
            break;
        case TYPE_ID_UCHAR:
            print_pod_value<uint8_t>(writer, data);
            break;
        case TYPE_ID_UCHAR2:
            writer << "(" << int(data[0]) << ", " << int(data[1]) << ")";
            break;
        case TYPE_ID_UCHAR3:
            writer << "(" << int(data[0]) << ", " << int(data[1]) << ", " << int(data[2]) << ")";
            break;
        case TYPE_ID_UCHAR4:
            writer << "(" << int(data[0]) << ", " << int(data[1]) << ", " << int(data[2]) << ", " << int(data[3]) << ")";
            break;
        case TYPE_ID_SHORT:
            print_pod_value<int16_t>(writer, data);
            break;
        case TYPE_ID_SHORT2:
            print_vector2<int16_t>(writer, data);
            break;
        case TYPE_ID_SHORT3:
            print_vector3<int16_t>(writer, data);
            break;
        case TYPE_ID_SHORT4:
            print_vector4<int16_t>(writer, data);
            break;
        case TYPE_ID_USHORT:
            print_pod_value<uint16_t>(writer, data);
            break;
        case TYPE_ID_USHORT2:
            print_vector2<uint16_t>(writer, data);
            break;
        case TYPE_ID_USHORT3:
            print_vector3<uint16_t>(writer, data);
            break;
        case TYPE_ID_USHORT4:
            print_vector4<uint16_t>(writer, data);
            break;
        case TYPE_ID_INT32:
            print_pod_value<int32_t>(writer, data);
            break;
        case TYPE_ID_INT2:
            print_vector2<int32_t>(writer, data);
            break;
        case TYPE_ID_INT3:
            print_vector3<int32_t>(writer, data);
            break;
        case TYPE_ID_INT4:
            print_vector4<int32_t>(writer, data);
            break;
        case TYPE_ID_UINT32:
            print_pod_value<uint32_t>(writer, data);
            break;
        case TYPE_ID_UINT2:
            print_vector2<uint32_t>(writer, data);
            break;
        case TYPE_ID_UINT3:
            print_vector3<uint32_t>(writer, data);
            break;
        case TYPE_ID_UINT4:
            print_vector4<uint32_t>(writer, data);
            break;
        case TYPE_ID_INT64:
            print_pod_value<int64_t>(writer, data);
            break;
        case TYPE_ID_UINT64:
            print_pod_value<uint64_t>(writer, data);
            break;
        case TYPE_ID_HALF:
            print_value_type<value::half>(writer, data);
            break;
        case TYPE_ID_HALF2:
            print_value_type<value::half2>(writer, data);
            break;
        case TYPE_ID_HALF3:
            print_value_type<value::half3>(writer, data);
            break;
        case TYPE_ID_HALF4:
            print_value_type<value::half4>(writer, data);
            break;
        case TYPE_ID_FLOAT:
            print_float(writer, data);
            break;
        case TYPE_ID_FLOAT2:
            print_value_type<value::float2>(writer, data);
            break;
        case TYPE_ID_FLOAT3:
            print_value_type<value::float3>(writer, data);
            break;
        case TYPE_ID_FLOAT4:
            print_value_type<value::float4>(writer, data);
            break;
        case TYPE_ID_DOUBLE:
            print_double(writer, data);
            break;
        case TYPE_ID_DOUBLE2:
            print_value_type<value::double2>(writer, data);
            break;
        case TYPE_ID_DOUBLE3:
            print_value_type<value::double3>(writer, data);
            break;
        case TYPE_ID_DOUBLE4:
            print_value_type<value::double4>(writer, data);
            break;
        case TYPE_ID_QUATH:
            print_value_type<value::quath>(writer, data);
            break;
        case TYPE_ID_QUATF:
            print_value_type<value::quatf>(writer, data);
            break;
        case TYPE_ID_QUATD:
            print_value_type<value::quatd>(writer, data);
            break;
        case TYPE_ID_MATRIX2F:
            print_value_type<value::matrix2f>(writer, data);
            break;
        case TYPE_ID_MATRIX3F:
            print_value_type<value::matrix3f>(writer, data);
            break;
        case TYPE_ID_MATRIX4F:
            print_value_type<value::matrix4f>(writer, data);
            break;
        case TYPE_ID_MATRIX2D:
            print_value_type<value::matrix2d>(writer, data);
            break;
        case TYPE_ID_MATRIX3D:
            print_value_type<value::matrix3d>(writer, data);
            break;
        case TYPE_ID_MATRIX4D:
            print_value_type<value::matrix4d>(writer, data);
            break;
        case TYPE_ID_COLOR3H:
            print_value_type<value::color3h>(writer, data);
            break;
        case TYPE_ID_COLOR3F:
            print_value_type<value::color3f>(writer, data);
            break;
        case TYPE_ID_COLOR3D:
            print_value_type<value::color3d>(writer, data);
            break;
        case TYPE_ID_COLOR4H:
            print_value_type<value::color4h>(writer, data);
            break;
        case TYPE_ID_COLOR4F:
            print_value_type<value::color4f>(writer, data);
            break;
        case TYPE_ID_COLOR4D:
            print_value_type<value::color4d>(writer, data);
            break;
        case TYPE_ID_POINT3H:
            print_value_type<value::point3h>(writer, data);
            break;
        case TYPE_ID_POINT3F:
            print_value_type<value::point3f>(writer, data);
            break;
        case TYPE_ID_POINT3D:
            print_value_type<value::point3d>(writer, data);
            break;
        case TYPE_ID_NORMAL3H:
            print_value_type<value::normal3h>(writer, data);
            break;
        case TYPE_ID_NORMAL3F:
            print_value_type<value::normal3f>(writer, data);
            break;
        case TYPE_ID_NORMAL3D:
            print_value_type<value::normal3d>(writer, data);
            break;
        case TYPE_ID_VECTOR3H:
            print_value_type<value::vector3h>(writer, data);
            break;
        case TYPE_ID_VECTOR3F:
            print_value_type<value::vector3f>(writer, data);
            break;
        case TYPE_ID_VECTOR3D:
            print_value_type<value::vector3d>(writer, data);
            break;
        case TYPE_ID_FRAME4D:
            print_value_type<value::frame4d>(writer, data);
            break;
        case TYPE_ID_TEXCOORD2H:
            print_value_type<value::texcoord2h>(writer, data);
            break;
        case TYPE_ID_TEXCOORD2F:
            print_value_type<value::texcoord2f>(writer, data);
            break;
        case TYPE_ID_TEXCOORD2D:
            print_value_type<value::texcoord2d>(writer, data);
            break;
        case TYPE_ID_TEXCOORD3H:
            print_value_type<value::texcoord3h>(writer, data);
            break;
        case TYPE_ID_TEXCOORD3F:
            print_value_type<value::texcoord3f>(writer, data);
            break;
        case TYPE_ID_TEXCOORD3D:
            print_value_type<value::texcoord3d>(writer, data);
            break;
        case TYPE_ID_TYPED_ARRAY_TIMESAMPLE_VALUE:
            print_typed_array_value(writer, data);
            break;
        default:
            writer << "[Unknown POD type: " << type_id << "]";
            break;
    }
}

size_t get_pod_type_size(uint32_t type_id) {
    using namespace value;

    switch (type_id) {
        case TYPE_ID_BOOL:
            return sizeof(bool);
        case TYPE_ID_CHAR:
            return sizeof(char);
        case TYPE_ID_CHAR2:
            return sizeof(char) * 2;
        case TYPE_ID_CHAR3:
            return sizeof(char) * 3;
        case TYPE_ID_CHAR4:
            return sizeof(char) * 4;
        case TYPE_ID_UCHAR:
            return sizeof(uint8_t);
        case TYPE_ID_UCHAR2:
            return sizeof(uint8_t) * 2;
        case TYPE_ID_UCHAR3:
            return sizeof(uint8_t) * 3;
        case TYPE_ID_UCHAR4:
            return sizeof(uint8_t) * 4;
        case TYPE_ID_SHORT:
            return sizeof(int16_t);
        case TYPE_ID_SHORT2:
            return sizeof(int16_t) * 2;
        case TYPE_ID_SHORT3:
            return sizeof(int16_t) * 3;
        case TYPE_ID_SHORT4:
            return sizeof(int16_t) * 4;
        case TYPE_ID_USHORT:
            return sizeof(uint16_t);
        case TYPE_ID_USHORT2:
            return sizeof(uint16_t) * 2;
        case TYPE_ID_USHORT3:
            return sizeof(uint16_t) * 3;
        case TYPE_ID_USHORT4:
            return sizeof(uint16_t) * 4;
        case TYPE_ID_INT32:
            return sizeof(int32_t);
        case TYPE_ID_INT2:
            return sizeof(int32_t) * 2;
        case TYPE_ID_INT3:
            return sizeof(int32_t) * 3;
        case TYPE_ID_INT4:
            return sizeof(int32_t) * 4;
        case TYPE_ID_UINT32:
            return sizeof(uint32_t);
        case TYPE_ID_UINT2:
            return sizeof(uint32_t) * 2;
        case TYPE_ID_UINT3:
            return sizeof(uint32_t) * 3;
        case TYPE_ID_UINT4:
            return sizeof(uint32_t) * 4;
        case TYPE_ID_INT64:
            return sizeof(int64_t);
        case TYPE_ID_UINT64:
            return sizeof(uint64_t);
        case TYPE_ID_HALF:
            return sizeof(value::half);
        case TYPE_ID_HALF2:
            return sizeof(value::half2);
        case TYPE_ID_HALF3:
            return sizeof(value::half3);
        case TYPE_ID_HALF4:
            return sizeof(value::half4);
        case TYPE_ID_FLOAT:
            return sizeof(float);
        case TYPE_ID_FLOAT2:
            return sizeof(float) * 2;
        case TYPE_ID_FLOAT3:
            return sizeof(float) * 3;
        case TYPE_ID_FLOAT4:
            return sizeof(float) * 4;
        case TYPE_ID_DOUBLE:
            return sizeof(double);
        case TYPE_ID_DOUBLE2:
            return sizeof(double) * 2;
        case TYPE_ID_DOUBLE3:
            return sizeof(double) * 3;
        case TYPE_ID_DOUBLE4:
            return sizeof(double) * 4;
        case TYPE_ID_QUATH:
            return sizeof(value::quath);
        case TYPE_ID_QUATF:
            return sizeof(value::quatf);
        case TYPE_ID_QUATD:
            return sizeof(value::quatd);
        case TYPE_ID_MATRIX2F:
            return sizeof(value::matrix2f);
        case TYPE_ID_MATRIX3F:
            return sizeof(value::matrix3f);
        case TYPE_ID_MATRIX4F:
            return sizeof(value::matrix4f);
        case TYPE_ID_MATRIX2D:
            return sizeof(value::matrix2d);
        case TYPE_ID_MATRIX3D:
            return sizeof(value::matrix3d);
        case TYPE_ID_MATRIX4D:
            return sizeof(value::matrix4d);
        case TYPE_ID_COLOR3H:
            return sizeof(value::color3h);
        case TYPE_ID_COLOR3F:
            return sizeof(value::color3f);
        case TYPE_ID_COLOR3D:
            return sizeof(value::color3d);
        case TYPE_ID_COLOR4H:
            return sizeof(value::color4h);
        case TYPE_ID_COLOR4F:
            return sizeof(value::color4f);
        case TYPE_ID_COLOR4D:
            return sizeof(value::color4d);
        case TYPE_ID_POINT3H:
            return sizeof(value::point3h);
        case TYPE_ID_POINT3F:
            return sizeof(value::point3f);
        case TYPE_ID_POINT3D:
            return sizeof(value::point3d);
        case TYPE_ID_NORMAL3H:
            return sizeof(value::normal3h);
        case TYPE_ID_NORMAL3F:
            return sizeof(value::normal3f);
        case TYPE_ID_NORMAL3D:
            return sizeof(value::normal3d);
        case TYPE_ID_VECTOR3H:
            return sizeof(value::vector3h);
        case TYPE_ID_VECTOR3F:
            return sizeof(value::vector3f);
        case TYPE_ID_VECTOR3D:
            return sizeof(value::vector3d);
        case TYPE_ID_FRAME4D:
            return sizeof(value::frame4d);
        case TYPE_ID_TEXCOORD2H:
            return sizeof(value::texcoord2h);
        case TYPE_ID_TEXCOORD2F:
            return sizeof(value::texcoord2f);
        case TYPE_ID_TEXCOORD2D:
            return sizeof(value::texcoord2d);
        case TYPE_ID_TEXCOORD3H:
            return sizeof(value::texcoord3h);
        case TYPE_ID_TEXCOORD3F:
            return sizeof(value::texcoord3f);
        case TYPE_ID_TEXCOORD3D:
            return sizeof(value::texcoord3d);
        case TYPE_ID_TYPED_TIMESAMPLE_VALUE:
            return sizeof(uint64_t);
        case TYPE_ID_TYPED_ARRAY_TIMESAMPLE_VALUE:
            return sizeof(uint64_t);
        default:
            return 0;  // Unknown type
    }
}

void pprint_pod_timesamples(StreamWriter& writer, const PODTimeSamples& samples, uint32_t indent) {
    // Write opening brace
    writer.write("{\n");

    if (samples.empty()) {
        writer.write(pprint::Indent(indent));
        writer.write("}");
        return;
    }

    // Get element size for this type
    size_t element_size = get_pod_type_size(samples.type_id());
    if (element_size == 0) {
        writer.write(pprint::Indent(indent + 1));
        writer.write("[Error: Unknown type_id ");
        writer.write(samples.type_id());
        writer.write("]\n");
        writer.write(pprint::Indent(indent));
        writer.write("}");
        return;
    }

    // Make sure samples are updated (sorted)
    if (samples._dirty) {
        samples.update();
    }

    const std::vector<double>& times = samples.get_times();
    const Buffer<16>& blocked = samples.get_blocked();
    const Buffer<16>& values = samples.get_values();

    // Check if using offset table (new optimized storage)
    if (!samples._offsets.empty()) {
        // Verify offset table is correct size
        if (samples._offsets.size() != times.size()) {
            writer.write(pprint::Indent(indent + 1));
            writer.write("[Error: Offset table size mismatch: offsets=");
            writer.write(static_cast<int>(samples._offsets.size()));
            writer.write(" times=");
            writer.write(static_cast<int>(times.size()));
            writer.write("]\n");
            writer.write(pprint::Indent(indent));
            writer.write("}");
            return;
        }

        // Using offset table - blocked values don't consume space
        for (size_t i = 0; i < times.size(); ++i) {
            writer.write(pprint::Indent(indent + 1));
            writer.write(times[i]);
            writer.write(": ");

            if (blocked[i] || samples._offsets[i] == SIZE_MAX) {
                writer.write("None");
            } else {
                // Get pointer to value data using offset
                const uint8_t* value_data = values.data() + samples._offsets[i];
                pprint_pod_value_by_type(writer, value_data, samples.type_id());
            }

            writer.write(",");  // USDA allows trailing comma
            writer.write("\n");
        }
    } else {
        // Legacy storage - blocked values still consume space but are skipped
        size_t value_offset = 0;
        for (size_t i = 0; i < times.size(); ++i) {
            writer.write(pprint::Indent(indent + 1));
            writer.write(times[i]);
            writer.write(": ");

            if (blocked[i]) {
                writer.write("None");
            } else {
                // Get pointer to value data for this sample
                const uint8_t* value_data = values.data() + value_offset;
                pprint_pod_value_by_type(writer, value_data, samples.type_id());
                value_offset += element_size;
            }

            writer.write(",");  // USDA allows trailing comma
            writer.write("\n");
        }
    }

    writer.write(pprint::Indent(indent));
    writer.write("}");
}

std::string pprint_pod_timesamples(const PODTimeSamples& samples, uint32_t indent) {
    // Use StreamWriter internally for efficiency
    StreamWriter writer;
    pprint_pod_timesamples(writer, samples, indent);
    return writer.str();
}

void pprint_timesamples(StreamWriter& writer, const value::TimeSamples& samples, uint32_t indent) {
    // Write opening brace
    writer.write("{\n");

    if (samples.empty()) {
        writer.write(pprint::Indent(indent));
        writer.write("}");
        return;
    }

    // Check if using POD storage
    if (samples.is_using_pod()) {
        // Delegate to POD implementation
        const auto& pod_samples = samples.get_pod_samples();

        // Get type information
        uint32_t type_id = pod_samples.type_id();
        size_t element_size = get_pod_type_size(type_id);

        if (element_size == 0) {
            writer.write(pprint::Indent(indent + 1));
            writer.write("/* Unknown type_id: ");
            writer.write(type_id);
            writer.write(" */\n");
            writer.write(pprint::Indent(indent));
            writer.write("}");
            return;
        }

        // Get arrays
        const auto& times = pod_samples.get_times();
        const auto& blocked = pod_samples.get_blocked();
        const auto& values = pod_samples.get_values();

        TUSDZ_LOG_I("times.size " << times.size());
        TUSDZ_LOG_I("blocked.size " << blocked.size());
        TUSDZ_LOG_I("values.size " << values.size());

        // Write samples - handle offset table if present
        if (!pod_samples._offsets.empty()) {
            // Using offset table - blocked values don't consume space
            for (size_t i = 0; i < times.size(); ++i) {
                writer.write(pprint::Indent(indent + 1));
                writer.write(times[i]);
                writer.write(": ");

                if (blocked[i] || pod_samples._offsets[i] == SIZE_MAX) {
                    writer.write("None");
                } else {
                    // Get pointer to value data using offset
                    const uint8_t* value_ptr = values.data() + pod_samples._offsets[i];
                    pprint_pod_value_by_type(writer, value_ptr, type_id);
                }

                if (i < times.size() - 1) {
                    writer.write(",");
                }
                writer.write("\n");
            }
        } else {
            // Legacy: blocked values still counted in offset calculation
            size_t value_offset = 0;
            for (size_t i = 0; i < times.size(); ++i) {
                TUSDZ_LOG_I("times[" << i << "] = " << times[i]);
                writer.write(pprint::Indent(indent + 1));
                writer.write(times[i]);
                writer.write(": ");

                if (blocked[i]) {
                    writer.write("None");
                } else {
                    // Get pointer to value data
                    const uint8_t* value_ptr = values.data() + value_offset;
                    pprint_pod_value_by_type(writer, value_ptr, type_id);
                    value_offset += element_size;
                }

                if (i < times.size() - 1) {
                    writer.write(",");
                }
                writer.write("\n");
            }
        }
    } else {
        // Non-POD path: use regular samples
        const auto& samples_vec = samples.get_samples();

        for (size_t i = 0; i < samples_vec.size(); ++i) {
            const auto& sample = samples_vec[i];

            writer.write(pprint::Indent(indent + 1));
            writer.write(sample.t);
            writer.write(": ");

            if (sample.blocked || sample.value.is_none()) {
                writer.write("None");
            } else {
                // Pretty print the value using its pprint method
                std::string value_str = pprint_value(sample.value, indent + 1);

                // Remove leading/trailing whitespace for inline display
                size_t start = value_str.find_first_not_of(" \t\n\r");
                size_t end = value_str.find_last_not_of(" \t\n\r");

                if (start != std::string::npos && end != std::string::npos) {
                    writer.write(value_str.substr(start, end - start + 1));
                } else {
                    writer.write(value_str);
                }
            }

            if (i < samples_vec.size() - 1) {
                writer.write(",");
            }
            writer.write("\n");
        }
    }

    writer.write(pprint::Indent(indent));
    writer.write("}");
}

std::string pprint_timesamples(const value::TimeSamples& samples, uint32_t indent) {
    // Use StreamWriter internally for efficiency
    StreamWriter writer;
    pprint_timesamples(writer, samples, indent);
    return writer.str();
}

} // namespace tinyusdz
