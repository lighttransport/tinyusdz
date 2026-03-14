// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.

#include "timesamples-pprint.hh"

#include <sstream>
#include <cstring>
#include <map>
#include "str-util.hh"

#ifdef TINYUSDZ_ENABLE_THREAD
#include <thread>
#include <vector>
#endif

#include "value-types.hh"
#include "value-pprint.hh"
#include "pprinter.hh"
#include "logger.hh"
#include "timesamples.hh"
#include "stream-writer.hh"
#include "typed-array.hh"

namespace tinyusdz {

///
/// Configuration for threaded printing
///
struct ThreadedPrintConfig {
  /// Minimum number of samples to use threading (default: 1024)
  size_t thread_threshold = 1024;

  /// Number of threads to use (0 = auto-detect from hardware)
  unsigned int num_threads = 0;

  /// Get the actual number of threads to use
  unsigned int get_num_threads() const {
#ifdef TINYUSDZ_ENABLE_THREAD
    if (num_threads > 0) {
      return num_threads;
    }
    // Use hardware_concurrency() - let the hardware dictate thread count
    // More threads = less data per thread = better performance in our implementation
    unsigned int hw_threads = std::thread::hardware_concurrency();
    return (hw_threads > 0) ? hw_threads : 4;  // Fallback to 4 if can't detect
#else
    return 1;
#endif
  }
};

// Global configuration (can be customized)
static ThreadedPrintConfig g_threaded_print_config;

namespace {

// ============================================================================
// Type Traits and Unified Print System
// ============================================================================

// Output abstraction - allows same code to work with StreamWriter or string
class OutputAdapter {
public:
  virtual ~OutputAdapter() = default;
  virtual void write(const std::string& s) = 0;
  virtual void write(double d) = 0;
  virtual void write(float f) = 0;
  virtual void write(int i) = 0;

  template<typename T>
  OutputAdapter& operator<<(const T& value) {
    std::stringstream ss;
    ss << value;
    write(ss.str());
    return *this;
  }
};

class StringOutputAdapter : public OutputAdapter {
  std::stringstream ss_;
public:
  void write(const std::string& s) override { ss_ << s; }
  void write(double d) override { ss_ << d; }
  void write(float f) override { ss_ << f; }
  void write(int i) override { ss_ << i; }
  std::string str() const { return ss_.str(); }
};

class StreamWriterAdapter : public OutputAdapter {
  StreamWriter& writer_;
public:
  explicit StreamWriterAdapter(StreamWriter& w) : writer_(w) {}
  void write(const std::string& s) override { writer_.write(s); }
  void write(double d) override { writer_.write(d); }
  void write(float f) override { writer_.write(f); }
  void write(int i) override { writer_ << i; }
};

// Type traits for value types (those with operator<<)
template<typename T>
struct is_value_type : std::false_type {};

// Specialize for all value types that have operator<<
#define DECLARE_VALUE_TYPE(TYPE) \
  template<> struct is_value_type<TYPE> : std::true_type {}

DECLARE_VALUE_TYPE(value::half);
DECLARE_VALUE_TYPE(value::half2);
DECLARE_VALUE_TYPE(value::half3);
DECLARE_VALUE_TYPE(value::half4);
DECLARE_VALUE_TYPE(value::float2);
DECLARE_VALUE_TYPE(value::float3);
DECLARE_VALUE_TYPE(value::float4);
DECLARE_VALUE_TYPE(value::double2);
DECLARE_VALUE_TYPE(value::double3);
DECLARE_VALUE_TYPE(value::double4);
DECLARE_VALUE_TYPE(value::quath);
DECLARE_VALUE_TYPE(value::quatf);
DECLARE_VALUE_TYPE(value::quatd);
DECLARE_VALUE_TYPE(value::matrix2f);
DECLARE_VALUE_TYPE(value::matrix3f);
DECLARE_VALUE_TYPE(value::matrix4f);
DECLARE_VALUE_TYPE(value::matrix2d);
DECLARE_VALUE_TYPE(value::matrix3d);
DECLARE_VALUE_TYPE(value::matrix4d);
DECLARE_VALUE_TYPE(value::color3h);
DECLARE_VALUE_TYPE(value::color3f);
DECLARE_VALUE_TYPE(value::color3d);
DECLARE_VALUE_TYPE(value::color4h);
DECLARE_VALUE_TYPE(value::color4f);
DECLARE_VALUE_TYPE(value::color4d);
DECLARE_VALUE_TYPE(value::point3h);
DECLARE_VALUE_TYPE(value::point3f);
DECLARE_VALUE_TYPE(value::point3d);
DECLARE_VALUE_TYPE(value::normal3h);
DECLARE_VALUE_TYPE(value::normal3f);
DECLARE_VALUE_TYPE(value::normal3d);
DECLARE_VALUE_TYPE(value::vector3h);
DECLARE_VALUE_TYPE(value::vector3f);
DECLARE_VALUE_TYPE(value::vector3d);
DECLARE_VALUE_TYPE(value::texcoord2h);
DECLARE_VALUE_TYPE(value::texcoord2f);
DECLARE_VALUE_TYPE(value::texcoord2d);
DECLARE_VALUE_TYPE(value::texcoord3h);
DECLARE_VALUE_TYPE(value::texcoord3f);
DECLARE_VALUE_TYPE(value::texcoord3d);
DECLARE_VALUE_TYPE(value::frame4d);

#undef DECLARE_VALUE_TYPE

// Unified print function for value types (those with operator<<)
template<typename T>
typename std::enable_if<is_value_type<T>::value, void>::type
print_type(OutputAdapter& out, const uint8_t* data) {
  T value;
  std::memcpy(&value, data, sizeof(T));
  std::stringstream ss;
  ss << value;
  out.write(ss.str());
}

// Unified print function for simple binary-serializable types
template<typename T>
typename std::enable_if<std::is_arithmetic<T>::value && !is_value_type<T>::value, void>::type
print_type(OutputAdapter& out, const uint8_t* data) {
  T value;
  std::memcpy(&value, data, sizeof(T));
  out << value;
}

// Specialization for bool - normalize to 0 or 1 for safety
template<>
void print_type<bool>(OutputAdapter& out, const uint8_t* data) {
  uint8_t value;
  std::memcpy(&value, data, sizeof(uint8_t));
  // Normalize: any non-zero value becomes 1 (true)
  out.write(value != 0 ? 1 : 0);
}

// Specialization for uint8_t - print as integer, not char
// Note: USD stores bool arrays as uint8_t (TYPE_ID_UCHAR), so we print the actual value.
// Well-formed USD files should only have 0/1 for bool arrays anyway.
template<>
void print_type<uint8_t>(OutputAdapter& out, const uint8_t* data) {
  uint8_t value;
  std::memcpy(&value, data, sizeof(uint8_t));
  out.write(static_cast<int>(value));
}

// Specialization for char - print as integer, not char
template<>
void print_type<char>(OutputAdapter& out, const uint8_t* data) {
  char value;
  std::memcpy(&value, data, sizeof(char));
  out.write(static_cast<int>(value));
}

// Specialization for double - print with full precision using dtoa
template<>
void print_type<double>(OutputAdapter& out, const uint8_t* data) {
  double value;
  std::memcpy(&value, data, sizeof(double));
  char buf[384];
  char *end = dtoa(value, buf);
  *end = '\0';
  out.write(std::string(buf));
}

// Specialization for float - print with full precision using dtoa
template<>
void print_type<float>(OutputAdapter& out, const uint8_t* data) {
  float value;
  std::memcpy(&value, data, sizeof(float));
  char buf[384];
  char *end = dtoa(value, buf);
  *end = '\0';
  out.write(std::string(buf));
}

// Unified print function for vector types
template<typename T, size_t N>
void print_vector(OutputAdapter& out, const uint8_t* data) {
  T values[N];
  std::memcpy(&values, data, sizeof(T) * N);
  out.write("(");
  for (size_t i = 0; i < N; ++i) {
    if (i > 0) out.write(", ");
    out << values[i];
  }
  out.write(")");
}

// Specializations for char types (print as int)
template<>
void print_vector<char, 2>(OutputAdapter& out, const uint8_t* data) {
  char values[2];
  std::memcpy(&values, data, sizeof(char) * 2);
  out.write("(");
  out.write(int(values[0]));
  out.write(", ");
  out.write(int(values[1]));
  out.write(")");
}

template<>
void print_vector<char, 3>(OutputAdapter& out, const uint8_t* data) {
  char values[3];
  std::memcpy(&values, data, sizeof(char) * 3);
  out.write("(");
  out.write(int(values[0]));
  out.write(", ");
  out.write(int(values[1]));
  out.write(", ");
  out.write(int(values[2]));
  out.write(")");
}

template<>
void print_vector<char, 4>(OutputAdapter& out, const uint8_t* data) {
  char values[4];
  std::memcpy(&values, data, sizeof(char) * 4);
  out.write("(");
  out.write(int(values[0]));
  out.write(", ");
  out.write(int(values[1]));
  out.write(", ");
  out.write(int(values[2]));
  out.write(", ");
  out.write(int(values[3]));
  out.write(")");
}

template<>
void print_vector<uint8_t, 2>(OutputAdapter& out, const uint8_t* data) {
  uint8_t values[2];
  std::memcpy(&values, data, sizeof(uint8_t) * 2);
  out.write("(");
  out.write(int(values[0]));
  out.write(", ");
  out.write(int(values[1]));
  out.write(")");
}

template<>
void print_vector<uint8_t, 3>(OutputAdapter& out, const uint8_t* data) {
  uint8_t values[3];
  std::memcpy(&values, data, sizeof(uint8_t) * 3);
  out.write("(");
  out.write(int(values[0]));
  out.write(", ");
  out.write(int(values[1]));
  out.write(", ");
  out.write(int(values[2]));
  out.write(")");
}

template<>
void print_vector<uint8_t, 4>(OutputAdapter& out, const uint8_t* data) {
  uint8_t values[4];
  std::memcpy(&values, data, sizeof(uint8_t) * 4);
  out.write("(");
  out.write(int(values[0]));
  out.write(", ");
  out.write(int(values[1]));
  out.write(", ");
  out.write(int(values[2]));
  out.write(", ");
  out.write(int(values[3]));
  out.write(")");
}

// ============================================================================
// Centralized Type Dispatch System
// ============================================================================

// Macro to reduce repetition in switch statements
// Handles both binary-serializable types and value types uniformly
#define DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_NAME, CPP_TYPE, PRINT_FUNC) \
  case value::TYPE_ID_NAME: \
    PRINT_FUNC<CPP_TYPE>(out, data); \
    break;

#define DISPATCH_VALUE_TYPE(TYPE_ID_NAME, VALUE_TYPE) \
  case value::TYPE_ID_NAME: \
    print_type<value::VALUE_TYPE>(out, data); \
    break;

#define DISPATCH_VECTOR_TYPE(TYPE_ID_NAME, CPP_TYPE, DIM) \
  case value::TYPE_ID_NAME: \
    print_vector<CPP_TYPE, DIM>(out, data); \
    break;

// Centralized print dispatch using OutputAdapter
void print_binary_serializable_value_dispatch(OutputAdapter& out, const uint8_t* data, uint32_t type_id) {
  using namespace value;

  // Strip array bit - we're printing a single element
  type_id = type_id & (~TYPE_ID_1D_ARRAY_BIT);

  switch (type_id) {
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_BOOL, bool, print_type)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_CHAR, char, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_CHAR2, char, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_CHAR3, char, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_CHAR4, char, 4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_UCHAR, uint8_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UCHAR2, uint8_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UCHAR3, uint8_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UCHAR4, uint8_t, 4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_SHORT, int16_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_SHORT2, int16_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_SHORT3, int16_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_SHORT4, int16_t, 4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_USHORT, uint16_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_USHORT2, uint16_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_USHORT3, uint16_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_USHORT4, uint16_t, 4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_INT32, int32_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_INT2, int32_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_INT3, int32_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_INT4, int32_t, 4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_UINT32, uint32_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UINT2, uint32_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UINT3, uint32_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UINT4, uint32_t, 4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_INT64, int64_t, print_type)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_UINT64, uint64_t, print_type)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF, half)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF2, half2)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF3, half3)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF4, half4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_FLOAT, float, print_type)
    DISPATCH_VALUE_TYPE(TYPE_ID_FLOAT2, float2)
    DISPATCH_VALUE_TYPE(TYPE_ID_FLOAT3, float3)
    DISPATCH_VALUE_TYPE(TYPE_ID_FLOAT4, float4)
    DISPATCH_BINARY_SERIALIZABLE_TYPE(TYPE_ID_DOUBLE, double, print_type)
    DISPATCH_VALUE_TYPE(TYPE_ID_DOUBLE2, double2)
    DISPATCH_VALUE_TYPE(TYPE_ID_DOUBLE3, double3)
    DISPATCH_VALUE_TYPE(TYPE_ID_DOUBLE4, double4)
    DISPATCH_VALUE_TYPE(TYPE_ID_QUATH, quath)
    DISPATCH_VALUE_TYPE(TYPE_ID_QUATF, quatf)
    DISPATCH_VALUE_TYPE(TYPE_ID_QUATD, quatd)
    DISPATCH_VALUE_TYPE(TYPE_ID_MATRIX2F, matrix2f)
    DISPATCH_VALUE_TYPE(TYPE_ID_MATRIX3F, matrix3f)
    DISPATCH_VALUE_TYPE(TYPE_ID_MATRIX4F, matrix4f)
    DISPATCH_VALUE_TYPE(TYPE_ID_MATRIX2D, matrix2d)
    DISPATCH_VALUE_TYPE(TYPE_ID_MATRIX3D, matrix3d)
    DISPATCH_VALUE_TYPE(TYPE_ID_MATRIX4D, matrix4d)
    DISPATCH_VALUE_TYPE(TYPE_ID_COLOR3H, color3h)
    DISPATCH_VALUE_TYPE(TYPE_ID_COLOR3F, color3f)
    DISPATCH_VALUE_TYPE(TYPE_ID_COLOR3D, color3d)
    DISPATCH_VALUE_TYPE(TYPE_ID_COLOR4H, color4h)
    DISPATCH_VALUE_TYPE(TYPE_ID_COLOR4F, color4f)
    DISPATCH_VALUE_TYPE(TYPE_ID_COLOR4D, color4d)
    DISPATCH_VALUE_TYPE(TYPE_ID_POINT3H, point3h)
    DISPATCH_VALUE_TYPE(TYPE_ID_POINT3F, point3f)
    DISPATCH_VALUE_TYPE(TYPE_ID_POINT3D, point3d)
    DISPATCH_VALUE_TYPE(TYPE_ID_NORMAL3H, normal3h)
    DISPATCH_VALUE_TYPE(TYPE_ID_NORMAL3F, normal3f)
    DISPATCH_VALUE_TYPE(TYPE_ID_NORMAL3D, normal3d)
    DISPATCH_VALUE_TYPE(TYPE_ID_VECTOR3H, vector3h)
    DISPATCH_VALUE_TYPE(TYPE_ID_VECTOR3F, vector3f)
    DISPATCH_VALUE_TYPE(TYPE_ID_VECTOR3D, vector3d)
    DISPATCH_VALUE_TYPE(TYPE_ID_FRAME4D, frame4d)
    DISPATCH_VALUE_TYPE(TYPE_ID_TEXCOORD2H, texcoord2h)
    DISPATCH_VALUE_TYPE(TYPE_ID_TEXCOORD2F, texcoord2f)
    DISPATCH_VALUE_TYPE(TYPE_ID_TEXCOORD2D, texcoord2d)
    DISPATCH_VALUE_TYPE(TYPE_ID_TEXCOORD3H, texcoord3h)
    DISPATCH_VALUE_TYPE(TYPE_ID_TEXCOORD3F, texcoord3f)
    DISPATCH_VALUE_TYPE(TYPE_ID_TEXCOORD3D, texcoord3d)
    default:
      out.write("[Unknown binary-serializable type: ");
      out.write(static_cast<int>(type_id));
      out.write("]");
      break;
  }
}

#undef DISPATCH_BINARY_SERIALIZABLE_TYPE
#undef DISPATCH_VALUE_TYPE
#undef DISPATCH_VECTOR_TYPE

// ============================================================================
// Active Helper Functions (used by the codebase)
// ============================================================================

// TypedArray helper - used by print_typed_array() function
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

    // Cast to TypedArray<T>*
    auto* impl = reinterpret_cast<TypedArray<T>*>(ptr_bits);

    // Create a view to access the data directly
    TypedArrayView<const T> view(*impl);

    if (view.size() == 0) {
        return "[]";
    }

    std::stringstream ss;
    ss << "[";

    size_t max_elements = view.size();

    for (size_t i = 0; i < max_elements; ++i) {
        if (i > 0) ss << ", ";

        // In C++14, we can't use if constexpr, so just output directly
        // The operator<< should work for all types we're likely to encounter
        ss << view[i];
    }

    //if (view.size() > max_elements) {
    //    ss << ", ... (" << view.size() << " total)";
    //}

    ss << "]";
    return ss.str();
}

// ============================================================================
// Legacy Print Functions (for backward compatibility)
// Now DISABLED - replaced by unified dispatch system above
// Can be removed in future cleanup
// ============================================================================

#if 0  // Disabled - replaced by unified dispatch system

// Helper function to convert raw bytes to typed value and print
template<typename T>
std::string print_binary_serializable_value(const uint8_t* data) {
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

// Note: try_print_typed_array template has been moved to active section above (lines 340-391)
// and is still used by print_typed_array function outside the anonymous namespace

// Disabled - use print_typed_array_value_by_type_id instead which takes type_id parameter
#if 0
template<typename T>
std::string print_typed_array(const uint8_t* packed_ptr_data) {
    uint64_t packed_value;
    std::memcpy(&packed_value, packed_ptr_data, sizeof(uint64_t));

    TUSDZ_LOG_I("packed_value : " << packed_value);

    // Extract pointer from packed value (lower 48 bits)
    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

    // Sign-extend from 48 bits to 64 bits for canonical address
    if (ptr_bits & (1ULL << 47)) {
        ptr_bits |= 0xFFFF000000000000ULL;
    }

    if (ptr_bits == 0) {
        return "";  // Return empty to indicate failure
    }

    // Cast to TypedArray<T>*
    auto* impl = reinterpret_cast<TypedArray<T>*>(ptr_bits);

    
    if (!impl) {
        return "[InternalError. nullptr]";
    }

    TUSDZ_LOG_I("impl->size : " << impl->size());

    // Create a view to access the data directly
    TypedArrayView<const T> view(*impl);

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
#endif  // #if 0 - template print_typed_array

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
void print_binary_serializable_value(StreamWriter& writer, const uint8_t* data) {
    //TUSDZ_LOG_I("pod_value\n");
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

#endif  // #if 0 - Disabled legacy print functions



// Old version - commented out since we now use print_typed_array_value_by_type_id
// which knows the type_id and doesn't need to guess
#if 0
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

    // Try texcoord2f array (common in primvars)
    success = try_print_typed_array_value<value::texcoord2f>(writer, data);
    if (success) return;

    // Try texcoord2d array
    success = try_print_typed_array_value<value::texcoord2d>(writer, data);
    if (success) return;

    // Try texcoord2h array
    success = try_print_typed_array_value<value::texcoord2h>(writer, data);
    if (success) return;

    // Try normal3f array
    success = try_print_typed_array_value<value::normal3f>(writer, data);
    if (success) return;

    // Try point3f array
    success = try_print_typed_array_value<value::point3f>(writer, data);
    if (success) return;

    // Try color3f array
    success = try_print_typed_array_value<value::color3f>(writer, data);
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
#endif

} // namespace

// TypedArray printing function - moved outside anonymous namespace to be accessible
std::string print_typed_array(const uint8_t* data) {
    // Try common types in order of likelihood

    // Try float array
    {
        std::string result = try_print_typed_array<float>(data);
        if (!result.empty()) {
            return result;
        }
    }

    // Try double array
    {
        std::string result = try_print_typed_array<double>(data);
        if (!result.empty()) {
            return result;
        }
    }

    // Try int array
    {
        std::string result = try_print_typed_array<int>(data);
        if (!result.empty()) {
            return result;
        }
    }

    // Try float3 array
    {
        std::string result = try_print_typed_array<value::float3>(data);
        if (!result.empty()) {
            return result;
        }
    }

    // Try float2 array
    {
        std::string result = try_print_typed_array<value::float2>(data);
        if (!result.empty()) {
            return result;
        }
    }

    // Try double3 array
    {
        std::string result = try_print_typed_array<value::double3>(data);
        if (!result.empty()) {
            return result;
        }
    }

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

// Forward declarations
void pprint_binary_serializable_value_by_type(StreamWriter& writer, const uint8_t* data, uint32_t type_id);
size_t get_binary_serializable_type_size(uint32_t type_id);

/// Helper function to print an array of binary-serializable values
/// @param writer Output writer
/// @param data Pointer to the first array element
/// @param type_id Type ID of the array elements
/// @param array_size Number of elements in the array
static void pprint_binary_serializable_array_by_type(StreamWriter& writer, const uint8_t* data, uint32_t type_id, size_t array_size) {
    size_t element_size = get_binary_serializable_type_size(type_id);
    if (element_size == 0) {
        writer.write("/* Unknown type_id: ");
        writer.write(type_id);
        writer.write(" */");
        return;
    }

    writer.write("[");
    for (size_t i = 0; i < array_size; ++i) {
        if (i > 0) {
            writer.write(", ");
        }
        const uint8_t* element_ptr = data + (i * element_size);
        pprint_binary_serializable_value_by_type(writer, element_ptr, type_id);
    }
    writer.write("]");
}

std::string pprint_binary_serializable_value_by_type(const uint8_t* data, uint32_t type_id) {
    // Use unified dispatch system with string output adapter
    StringOutputAdapter adapter;
    print_binary_serializable_value_dispatch(adapter, data, type_id);
    return adapter.str();
}

void pprint_binary_serializable_value_by_type(StreamWriter& writer, const uint8_t* data, uint32_t type_id) {
    // Use unified dispatch system with StreamWriter adapter
    StreamWriterAdapter adapter(writer);
    print_binary_serializable_value_dispatch(adapter, data, type_id);
}

size_t get_binary_serializable_type_size(uint32_t type_id) {
    using namespace value;

    // Strip array bit - we want the element size
    type_id = type_id & (~TYPE_ID_1D_ARRAY_BIT);

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
#if 0
        case TYPE_ID_TYPED_TIMESAMPLE_VALUE:
            return sizeof(uint64_t);
        case TYPE_ID_TYPED_ARRAY_TIMESAMPLE_VALUE:
            return sizeof(uint64_t);
#endif
        default:
            return 0;  // Unknown type
    }
}


void pprint_timesamples(StreamWriter& writer, const value::TimeSamples& samples, uint32_t indent) {
    // Write opening brace
    writer.write("{\n");

    if (samples.empty()) {
        writer.write(pprint::Indent(indent));
        writer.write("}");
        return;
    }

    // Check if using unified storage (_times non-empty AND has actual data in buffers)
    // vs Sample-based storage (_samples vector)
    // Note: Some operations like add_value_array_sample() populate _times but store data
    // in _samples, so we need to check if unified storage buffers actually have data
    bool has_unified_data = !samples.get_times().empty() &&
                           (!samples.get_values().empty() ||
                            !samples.get_small_values().empty() ||
                            !samples.get_offsets().empty());

    if (has_unified_data) {

        // Phase 3: Access unified storage directly from TimeSamples
        // Note: TypedArray is no longer supported in Phase 3, so we skip that path

        // Get type information
        uint32_t type_id = samples.type_id();
        size_t element_size = get_binary_serializable_type_size(type_id);

        if (element_size == 0) {
            writer.write(pprint::Indent(indent + 1));
            writer.write("/* Unknown type_id: ");
            writer.write(type_id);
            writer.write(" */\n");
            writer.write(pprint::Indent(indent));
            writer.write("}");
            return;
        }

        // Get array size from TimeSamples directly (works for both binary storage and unified storage)
        size_t array_size = samples.get_array_size();

        // Get arrays from unified storage
        const auto& times = samples.get_times();
        const auto& blocked = samples.get_blocked();
        const auto& values = samples.get_values();
        const auto& offsets = samples.get_offsets();
        const auto& small_values = samples.get_small_values();
        const auto& array_counts = samples.get_array_counts();

        // Write samples - handle offset table if present
        if (!offsets.empty()) {

            // Phase 3: TypedArray path removed (not supported in unified storage)
            // Use regular printing for all binary-serializable types
            for (size_t i = 0; i < times.size(); ++i) {
                writer.write(pprint::Indent(indent + 1));
                writer.write(times[i]);
                writer.write(": ");

                // Check blocked array bounds before accessing
                bool is_blocked = (i < blocked.size()) ? blocked[i] : false;
                // Check offsets bounds as well - treat out-of-bounds as None
                bool offset_is_none = (i >= offsets.size()) || (offsets[i] == SIZE_MAX);
                if (is_blocked || offset_is_none) {
                    writer.write("None");
                } else {
                    // Resolve offset (may be encoded with dedup/array flags) and get resolved index
                    size_t byte_offset;
                    size_t resolved_idx = i;
                    if (!value::TimeSamples::resolve_offset_static(offsets, i, &byte_offset, nullptr, nullptr, 100, &resolved_idx)) {
                        writer.write("/* ERROR: failed to resolve offset */");
                    } else {
                        // Get pointer to value data using resolved byte offset
                        const uint8_t* value_ptr = values.data() + byte_offset;

                        // Check if this sample is an array (check array flag in offset)
                        bool is_array = samples.is_stl_array() || (offsets[i] & value::TimeSamples::OFFSET_ARRAY_FLAG);

                        if (is_array) {
                            // Get per-sample array count (with fallback to global array_size)
                            size_t per_sample_count = (resolved_idx < array_counts.size()) ? array_counts[resolved_idx] : array_size;
                            // Print all elements in the array
                            pprint_binary_serializable_array_by_type(writer, value_ptr, type_id, per_sample_count);
                        } else {
                            // Print single value
                            pprint_binary_serializable_value_by_type(writer, value_ptr, type_id);
                        }
                    }
                }

                if (i < times.size() - 1) {
                    writer.write(",");
                }
                writer.write("\n");
            }
        } else {
            // No offset table - using direct storage (either _values or _small_values)
            // Check if using small_values (for types sizeof <= 8) or values buffer (for types sizeof > 8)
            bool using_small_values = !small_values.empty();

            // Handle case where both storage types are empty but times is not (error case)
            if (values.empty() && small_values.empty() && !times.empty()) {
                for (size_t i = 0; i < times.size(); ++i) {
                    writer.write(pprint::Indent(indent + 1));
                    writer.write(times[i]);
                    writer.write(": /* empty value data */");
                    if (i < times.size() - 1) {
                        writer.write(",");
                    }
                    writer.write("\n");
                }
            } else if (using_small_values) {
                // Print small values (stored as uint64_t, one entry per sample)
                for (size_t i = 0; i < times.size(); ++i) {
                    writer.write(pprint::Indent(indent + 1));
                    writer.write(times[i]);
                    writer.write(": ");

                    // Check blocked array bounds before accessing
                    bool is_blocked = (i < blocked.size()) ? blocked[i] : false;
                    if (is_blocked) {
                        writer.write("None");
                    } else {
                        // Get value from small_values and print it
                        if (i < small_values.size()) {
                            uint64_t stored_value = small_values[i];
                            // Cast to typed pointer and print
                            const uint8_t* value_ptr = reinterpret_cast<const uint8_t*>(&stored_value);
                            pprint_binary_serializable_value_by_type(writer, value_ptr, type_id);
                        } else {
                            writer.write("/* ERROR: small_values index out of bounds */");
                        }
                    }

                    if (i < times.size() - 1) {
                        writer.write(",");
                    }
                    writer.write("\n");
                }
            } else {
            // Use values buffer (large types)
            size_t value_offset = 0;
            for (size_t i = 0; i < times.size(); ++i) {
                //TUSDZ_LOG_I("times[" << i << "] = " << times[i]);
                writer.write(pprint::Indent(indent + 1));
                writer.write(times[i]);
                writer.write(": ");

                // Check blocked array bounds before accessing
                bool is_blocked = (i < blocked.size()) ? blocked[i] : false;
                if (is_blocked) {
                    writer.write("None");
                } else {
                    // Get pointer to value data
                    const uint8_t* value_ptr = values.data() + value_offset;

                    // Check if this is an array type
                    bool is_array = samples.is_stl_array();

                    if (is_array) {
                        // Get per-sample array count (with fallback to global array_size)
                        size_t per_sample_count = (i < array_counts.size()) ? array_counts[i] : array_size;
                        // Print all elements in the array
                        pprint_binary_serializable_array_by_type(writer, value_ptr, type_id, per_sample_count);
                    } else {
                        // Print single value
                        pprint_binary_serializable_value_by_type(writer, value_ptr, type_id);
                    }
                    value_offset += element_size;
                }

                if (i < times.size() - 1) {
                    writer.write(",");
                }
                writer.write("\n");
            }
            } // end else for using_small_values check
        } // end else for offsets.empty() check
    } else {
        // Non-binary-storage path: use regular samples
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

void set_threaded_print_threshold(size_t threshold) {
    g_threaded_print_config.thread_threshold = threshold;
}

void set_threaded_print_num_threads(unsigned int num_threads) {
    g_threaded_print_config.num_threads = num_threads;
}

} // namespace tinyusdz
