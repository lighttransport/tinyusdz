// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.

#include "timesamples-pprint.hh"

#include <sstream>
#include <cstring>
#include <map>

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
};;;;;

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

// Unified print function for simple POD types
template<typename T>
typename std::enable_if<std::is_arithmetic<T>::value && !is_value_type<T>::value, void>::type
print_type(OutputAdapter& out, const uint8_t* data) {
  T value;
  std::memcpy(&value, data, sizeof(T));
  out << value;
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
// Handles both POD types and value types uniformly
#define DISPATCH_POD_TYPE(TYPE_ID_NAME, CPP_TYPE, PRINT_FUNC) \
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
void print_pod_value_dispatch(OutputAdapter& out, const uint8_t* data, uint32_t type_id) {
  using namespace value;

  switch (type_id) {
    DISPATCH_POD_TYPE(TYPE_ID_BOOL, bool, print_type)
    DISPATCH_POD_TYPE(TYPE_ID_CHAR, char, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_CHAR2, char, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_CHAR3, char, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_CHAR4, char, 4)
    DISPATCH_POD_TYPE(TYPE_ID_UCHAR, uint8_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UCHAR2, uint8_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UCHAR3, uint8_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UCHAR4, uint8_t, 4)
    DISPATCH_POD_TYPE(TYPE_ID_SHORT, int16_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_SHORT2, int16_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_SHORT3, int16_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_SHORT4, int16_t, 4)
    DISPATCH_POD_TYPE(TYPE_ID_USHORT, uint16_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_USHORT2, uint16_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_USHORT3, uint16_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_USHORT4, uint16_t, 4)
    DISPATCH_POD_TYPE(TYPE_ID_INT32, int32_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_INT2, int32_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_INT3, int32_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_INT4, int32_t, 4)
    DISPATCH_POD_TYPE(TYPE_ID_UINT32, uint32_t, print_type)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UINT2, uint32_t, 2)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UINT3, uint32_t, 3)
    DISPATCH_VECTOR_TYPE(TYPE_ID_UINT4, uint32_t, 4)
    DISPATCH_POD_TYPE(TYPE_ID_INT64, int64_t, print_type)
    DISPATCH_POD_TYPE(TYPE_ID_UINT64, uint64_t, print_type)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF, half)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF2, half2)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF3, half3)
    DISPATCH_VALUE_TYPE(TYPE_ID_HALF4, half4)
    DISPATCH_POD_TYPE(TYPE_ID_FLOAT, float, print_type)
    DISPATCH_VALUE_TYPE(TYPE_ID_FLOAT2, float2)
    DISPATCH_VALUE_TYPE(TYPE_ID_FLOAT3, float3)
    DISPATCH_VALUE_TYPE(TYPE_ID_FLOAT4, float4)
    DISPATCH_POD_TYPE(TYPE_ID_DOUBLE, double, print_type)
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
      out.write("[Unknown POD type: ");
      out.write(static_cast<int>(type_id));
      out.write("]");
      break;
  }
}

#undef DISPATCH_POD_TYPE
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

    // Cast to TypedArrayImpl<T>*
    auto* impl = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);

    
    if (!impl) {
        return "[InternalError. nullptr]";
    }

    TUSDZ_LOG_I("impl->size : " << impl->size());

    // Create TypedArray with dedup flag to prevent deletion
    TypedArray<T> typed_array(impl, true);

    TUSDZ_LOG_I("typed_array.size = " << typed_array.size());

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
void print_pod_value(StreamWriter& writer, const uint8_t* data) {
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

// ============================================================================
// Active Functions (used by the codebase)
// ============================================================================

// TypedArray - stored as packed pointer (uint64_t)
// Attempt to reconstruct TypedArray and print its contents to StreamWriter
template<typename T>
bool try_print_typed_array_value(StreamWriter& writer, const uint8_t* packed_ptr_data) {
    uint64_t packed_value;
    std::memcpy(&packed_value, packed_ptr_data, sizeof(uint64_t));

    //TUSDZ_LOG_I("try_print_typed_array_value: packed_value=0x" << std::hex << packed_value << std::dec);

    // Extract pointer from packed value (lower 48 bits)
    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

    //TUSDZ_LOG_I("try_print_typed_array_value: after mask ptr_bits=0x" << std::hex << ptr_bits << std::dec);

    // Sign-extend from 48 bits to 64 bits for canonical address
    if (ptr_bits & (1ULL << 47)) {
        ptr_bits |= 0xFFFF000000000000ULL;
        //TUSDZ_LOG_I("try_print_typed_array_value: sign-extended ptr_bits=0x" << std::hex << ptr_bits << std::dec);
    }

    if (ptr_bits == 0) {
        return false;  // Return false to indicate failure
    }

    // Cast to TypedArrayImpl<T>*
    auto* impl = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);

    // Check if impl looks valid by examining first few bytes
    // TypedArrayImpl should have a vtable pointer and size field
    if (impl == nullptr) {
        return false;
    }

    // Try to inspect the impl structure to understand what we're dealing with
    //TUSDZ_LOG_I("Inspecting TypedArrayImpl<" << typeid(T).name() << ">* at 0x" << std::hex << ptr_bits << std::dec);

    // Check if impl is valid by trying to access it
    //TUSDZ_LOG_I("impl->is_view() = " << impl->is_view());
    //TUSDZ_LOG_I("impl->size() = " << impl->size());
    //TUSDZ_LOG_I("impl->empty() = " << impl->empty());
    //TUSDZ_LOG_I("impl->data() = 0x" << std::hex << reinterpret_cast<uintptr_t>(impl->data()) << std::dec);

    // Also check the storage vector size
    //TUSDZ_LOG_I("impl->storage().size() = " << impl->storage().size());

    // DEBUG: Try to access the data pointer directly to see if it contains valid data
    const T* data_ptr = impl->data();
    (void)data_ptr;  // Suppress unused warning when debug logging is disabled
    #if 0  // Disabled debug code
    if (data_ptr != nullptr) {
        //TUSDZ_LOG_I("Trying to read first element from data_ptr...");
        // Try to read first few bytes to see if they look reasonable
        const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(data_ptr);
        //TUSDZ_LOG_I("First 16 bytes at data_ptr: "
        //            << std::hex
        //            << static_cast<int>(byte_ptr[0]) << " " << static_cast<int>(byte_ptr[1]) << " " << static_cast<int>(byte_ptr[2]) << " " << static_cast<int>(byte_ptr[3]) << " "
        //            << static_cast<int>(byte_ptr[4]) << " " << static_cast<int>(byte_ptr[5]) << " " << static_cast<int>(byte_ptr[6]) << " " << static_cast<int>(byte_ptr[7]) << " "
        //            << static_cast<int>(byte_ptr[8]) << " " << static_cast<int>(byte_ptr[9]) << " " << static_cast<int>(byte_ptr[10]) << " " << static_cast<int>(byte_ptr[11]) << " "
        //            << static_cast<int>(byte_ptr[12]) << " " << static_cast<int>(byte_ptr[13]) << " " << static_cast<int>(byte_ptr[14]) << " " << static_cast<int>(byte_ptr[15])
        //            << std::dec);
    }
    #endif

    // Create TypedArray with dedup flag to prevent deletion
    TypedArray<T> typed_array(impl, true);

    // Create a view to access the data
    TypedArrayView<const T> view(typed_array);

    //TUSDZ_LOG_I("TypedArrayView size: " << view.size());

    // If size is 0, this might not be the right type - return false to try next type
    if (view.size() == 0) {
        return false;  // Try next type
    }

    // Always use brackets for arrays (USD spec requires brackets for all arrays)
    writer.write("[");

    size_t max_elements = view.size();

    for (size_t i = 0; i < max_elements; ++i) {
        if (i > 0) writer.write(", ");

        // Write the value using operator<< via stringstream
        std::stringstream ss;
        ss << view[i];
        writer.write(ss.str());
    }

    //if (view.size() > max_elements) {
    //    writer.write(", ... (");
    //    writer.write(static_cast<int>(view.size()));
    //    writer.write(" total)");
    //}

    writer.write("]");
    return true;
}

// Print TypedArray when the element type_id is known
void print_typed_array_value_by_type_id(StreamWriter& writer, const uint8_t* data, uint32_t type_id) {
    using namespace value;

    bool success = false;

    // Dispatch to the correct template instantiation based on type_id
    switch (type_id) {
        case TYPE_ID_FLOAT:
            success = try_print_typed_array_value<float>(writer, data);
            break;
        case TYPE_ID_DOUBLE:
            success = try_print_typed_array_value<double>(writer, data);
            break;
        case TYPE_ID_INT32:
            success = try_print_typed_array_value<int32_t>(writer, data);
            break;
        case TYPE_ID_FLOAT2:
            success = try_print_typed_array_value<value::float2>(writer, data);
            break;
        case TYPE_ID_FLOAT3:
            success = try_print_typed_array_value<value::float3>(writer, data);
            break;
        case TYPE_ID_FLOAT4:
            success = try_print_typed_array_value<value::float4>(writer, data);
            break;
        case TYPE_ID_DOUBLE2:
            success = try_print_typed_array_value<value::double2>(writer, data);
            break;
        case TYPE_ID_DOUBLE3:
            success = try_print_typed_array_value<value::double3>(writer, data);
            break;
        case TYPE_ID_DOUBLE4:
            success = try_print_typed_array_value<value::double4>(writer, data);
            break;
        case TYPE_ID_TEXCOORD2F:
            success = try_print_typed_array_value<value::texcoord2f>(writer, data);
            break;
        case TYPE_ID_TEXCOORD2D:
            success = try_print_typed_array_value<value::texcoord2d>(writer, data);
            break;
        case TYPE_ID_TEXCOORD2H:
            success = try_print_typed_array_value<value::texcoord2h>(writer, data);
            break;
        case TYPE_ID_TEXCOORD3F:
            success = try_print_typed_array_value<value::texcoord3f>(writer, data);
            break;
        case TYPE_ID_TEXCOORD3D:
            success = try_print_typed_array_value<value::texcoord3d>(writer, data);
            break;
        case TYPE_ID_TEXCOORD3H:
            success = try_print_typed_array_value<value::texcoord3h>(writer, data);
            break;
        case TYPE_ID_NORMAL3F:
            success = try_print_typed_array_value<value::normal3f>(writer, data);
            break;
        case TYPE_ID_NORMAL3D:
            success = try_print_typed_array_value<value::normal3d>(writer, data);
            break;
        case TYPE_ID_NORMAL3H:
            success = try_print_typed_array_value<value::normal3h>(writer, data);
            break;
        case TYPE_ID_POINT3F:
            success = try_print_typed_array_value<value::point3f>(writer, data);
            break;
        case TYPE_ID_POINT3D:
            success = try_print_typed_array_value<value::point3d>(writer, data);
            break;
        case TYPE_ID_POINT3H:
            success = try_print_typed_array_value<value::point3h>(writer, data);
            break;
        case TYPE_ID_COLOR3F:
            success = try_print_typed_array_value<value::color3f>(writer, data);
            break;
        case TYPE_ID_COLOR3D:
            success = try_print_typed_array_value<value::color3d>(writer, data);
            break;
        case TYPE_ID_COLOR3H:
            success = try_print_typed_array_value<value::color3h>(writer, data);
            break;
        case TYPE_ID_COLOR4F:
            success = try_print_typed_array_value<value::color4f>(writer, data);
            break;
        case TYPE_ID_COLOR4D:
            success = try_print_typed_array_value<value::color4d>(writer, data);
            break;
        case TYPE_ID_COLOR4H:
            success = try_print_typed_array_value<value::color4h>(writer, data);
            break;
        case TYPE_ID_VECTOR3F:
            success = try_print_typed_array_value<value::vector3f>(writer, data);
            break;
        case TYPE_ID_VECTOR3D:
            success = try_print_typed_array_value<value::vector3d>(writer, data);
            break;
        case TYPE_ID_VECTOR3H:
            success = try_print_typed_array_value<value::vector3h>(writer, data);
            break;
        case TYPE_ID_QUATH:
            success = try_print_typed_array_value<value::quath>(writer, data);
            break;
        case TYPE_ID_QUATF:
            success = try_print_typed_array_value<value::quatf>(writer, data);
            break;
        case TYPE_ID_QUATD:
            success = try_print_typed_array_value<value::quatd>(writer, data);
            break;
        case TYPE_ID_MATRIX2F:
            success = try_print_typed_array_value<value::matrix2f>(writer, data);
            break;
        case TYPE_ID_MATRIX3F:
            success = try_print_typed_array_value<value::matrix3f>(writer, data);
            break;
        case TYPE_ID_MATRIX4F:
            success = try_print_typed_array_value<value::matrix4f>(writer, data);
            break;
        case TYPE_ID_MATRIX2D:
            success = try_print_typed_array_value<value::matrix2d>(writer, data);
            break;
        case TYPE_ID_MATRIX3D:
            success = try_print_typed_array_value<value::matrix3d>(writer, data);
            break;
        case TYPE_ID_MATRIX4D:
            success = try_print_typed_array_value<value::matrix4d>(writer, data);
            break;
        default:
            // Unknown type_id
            success = false;
            break;
    }

    // If not successful or unknown type, print generic representation
    if (!success) {
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
}

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
void pprint_pod_value_by_type(StreamWriter& writer, const uint8_t* data, uint32_t type_id);
size_t get_pod_type_size(uint32_t type_id);

/// Helper function to print an array of POD values
/// @param writer Output writer
/// @param data Pointer to the first array element
/// @param type_id Type ID of the array elements
/// @param array_size Number of elements in the array
static void pprint_pod_array_by_type(StreamWriter& writer, const uint8_t* data, uint32_t type_id, size_t array_size) {
    size_t element_size = get_pod_type_size(type_id);
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
        pprint_pod_value_by_type(writer, element_ptr, type_id);
    }
    writer.write("]");
}

std::string pprint_pod_value_by_type(const uint8_t* data, uint32_t type_id) {
    // Use unified dispatch system with string output adapter
    StringOutputAdapter adapter;
    print_pod_value_dispatch(adapter, data, type_id);
    return adapter.str();
}

void pprint_pod_value_by_type(StreamWriter& writer, const uint8_t* data, uint32_t type_id) {
    // Use unified dispatch system with StreamWriter adapter
    StreamWriterAdapter adapter(writer);
    print_pod_value_dispatch(adapter, data, type_id);
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

#if 0  // Currently unused - disabled to use generic path
static void pprint_typed_array_timesamples_FLOAT2(StreamWriter& writer, const PODTimeSamples& samples, uint32_t indent) {

    const std::vector<double>& times = samples.get_times();
    const Buffer<16>& blocked = samples.get_blocked();
    const Buffer<16>& values = samples.get_values();

    size_t element_size = sizeof(uint64_t);

    if (!samples._offsets.empty()) {
      // TODO: Check samples._offsets.size() == times.size();
    }

    {
      size_t value_offset = 0;
      for (size_t i = 0; i < times.size(); ++i) {
          writer.write(pprint::Indent(indent + 1));
          writer.write(times[i]);
          writer.write(": ");

          if (blocked[i]) {
              writer.write("None");
          } else {

              if (!samples._offsets.empty()) {
                value_offset = samples._offsets[i];
              }

              // Get pointer to value data for this sample
              const uint8_t* value_data = values.data() + value_offset;

              // Use correct type based on type_id
              std::string s;
              if (samples.type_id() == value::TYPE_ID_TEXCOORD2F) {
                  s = print_typed_array<value::texcoord2f>(value_data);
              } else {
                  // TYPE_ID_FLOAT2
                  s = print_typed_array<value::float2>(value_data);
              }
              writer.write(s);
              if (samples._offsets.empty()) {
                value_offset += element_size;
              }
          }

          writer.write(",");  // USDA allows trailing comma
          writer.write("\n");
      }
    }
}
#endif  // #if 0 - pprint_typed_array_timesamples_FLOAT2

#if defined(TINYUSDZ_ENABLE_THREAD)
// Helper function to print a range of samples to a ChunkedStreamWriter
template<typename T>
static void pprint_typed_array_timesamples_range(
    ChunkedStreamWriter<4096>& chunk_writer,
    const PODTimeSamples& samples,
    uint32_t indent,
    size_t start_idx,
    size_t end_idx,
    std::map<uint64_t, std::string>& cached_strings) {

    const std::vector<double>& times = samples._times;
    const Buffer<16>& blocked = samples._blocked;
    const Buffer<16>& values = samples._values;

    size_t element_size = sizeof(uint64_t);
    size_t value_offset = 0;

    for (size_t i = start_idx; i < end_idx; ++i) {
        chunk_writer.write(pprint::Indent(indent + 1));
        chunk_writer.write(times[i]);
        chunk_writer.write(": ");

        if (blocked[i]) {
            chunk_writer.write("None");
        } else {
            if (!samples._offsets.empty()) {
                value_offset = samples._offsets[i];
            } else {
                // For non-offset tables, calculate based on global index
                value_offset = i * element_size;
            }

            // Get pointer to value data for this sample
            const uint8_t* value_data = values.data() + value_offset;

            // Extract pointer value from packed data
            uint64_t packed_value;
            std::memcpy(&packed_value, value_data, sizeof(uint64_t));
            uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

            // Sign-extend from 48 bits to 64 bits
            if (ptr_bits & (1ULL << 47)) {
                ptr_bits |= 0xFFFF000000000000ULL;
            }

            // Check cache first
            auto it = cached_strings.find(ptr_bits);
            if (it != cached_strings.end()) {
                // Reuse cached string
                chunk_writer.write(it->second);
            } else {
                // First occurrence - dereference TypedArray pointer and print
                // Dereference and print the actual TypedArray contents
                StreamWriter temp_writer;
                bool success = try_print_typed_array_value<T>(temp_writer, value_data);
                if (!success) {
                    temp_writer.write("[TypedArray print failed]");
                }

                std::string printed = temp_writer.str();
                chunk_writer.write(printed);

                // Cache the printed string
                cached_strings[ptr_bits] = printed;
            }
        }

        chunk_writer.write(",");  // USDA allows trailing comma
        chunk_writer.write("\n");
    }
}
#endif

// Templated efficient printing for typed array timesamples
// General template for most types
template<typename T>
static void pprint_typed_array_timesamples(StreamWriter& writer, const PODTimeSamples& samples, uint32_t indent) {
    const std::vector<double>& times = samples._times;
    const Buffer<16>& blocked = samples._blocked;
    const Buffer<16>& values = samples._values;

#ifdef TINYUSDZ_ENABLE_THREAD
    // Use threaded path for large arrays
    size_t num_samples = times.size();
    if (num_samples >= g_threaded_print_config.thread_threshold) {
        unsigned int num_threads = g_threaded_print_config.get_num_threads();
        size_t samples_per_thread = (num_samples + num_threads - 1) / num_threads;

        // Vector to hold ChunkedStreamWriters for each thread
        std::vector<ChunkedStreamWriter<4096>> thread_writers(num_threads);
        std::vector<std::thread> threads;
        std::vector<std::map<uint64_t, std::string>> thread_caches(num_threads);

        // Launch threads
        for (unsigned int t = 0; t < num_threads; ++t) {
            size_t start_idx = t * samples_per_thread;
            size_t end_idx = std::min(start_idx + samples_per_thread, num_samples);

            if (start_idx >= num_samples) break;

            threads.emplace_back([&, t, start_idx, end_idx]() {
                pprint_typed_array_timesamples_range<T>(
                    thread_writers[t],
                    samples,
                    indent,
                    start_idx,
                    end_idx,
                    thread_caches[t]
                );
            });
        }

        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        // Concat all thread results into a single ChunkedStreamWriter
        ChunkedStreamWriter<4096> final_chunked_writer;
        for (size_t t = 0; t < thread_writers.size(); ++t) {
            if (!thread_writers[t].empty()) {
                final_chunked_writer.concat(std::move(thread_writers[t]));
            }
        }

        // Convert to string and write to output writer
        writer.write(final_chunked_writer.str());
        return;
    }
#endif

    // Single-threaded path
    size_t element_size = sizeof(uint64_t);

    // Map to cache printed strings: pointer -> string
    std::map<uint64_t, std::string> cached_strings;

    size_t value_offset = 0;
    for (size_t i = 0; i < times.size(); ++i) {
        writer.write(pprint::Indent(indent + 1));
        writer.write(times[i]);
        writer.write(": ");

        if (blocked[i]) {
            writer.write("None");
        } else {
            if (!samples._offsets.empty()) {
                value_offset = samples._offsets[i];
            }

            // Get pointer to value data for this sample
            const uint8_t* value_data = values.data() + value_offset;

            // Extract pointer value from packed data
            uint64_t packed_value;
            std::memcpy(&packed_value, value_data, sizeof(uint64_t));
            uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

            // Sign-extend from 48 bits to 64 bits
            if (ptr_bits & (1ULL << 47)) {
                ptr_bits |= 0xFFFF000000000000ULL;
            }

            // Check cache first
            auto it = cached_strings.find(ptr_bits);
            if (it != cached_strings.end()) {
                // Reuse cached string
                writer.write(it->second);
            } else {
                // First occurrence - dereference TypedArray pointer and print
                size_t pos_before = writer.str().size();

                // Dereference and print the actual TypedArray contents
                bool success = try_print_typed_array_value<T>(writer, value_data);
                if (!success) {
                    writer.write("[TypedArray print failed]");
                }

                size_t pos_after = writer.str().size();

                // Cache the printed string
                std::string printed = writer.str().substr(pos_before, pos_after - pos_before);
                cached_strings[ptr_bits] = printed;
            }

            if (samples._offsets.empty()) {
                value_offset += element_size;
            }
        }

        writer.write(",");  // USDA allows trailing comma
        writer.write("\n");
    }
}

// Specialization for bool type - stored as uint8_t
template<>
void pprint_typed_array_timesamples<bool>(StreamWriter& writer, const PODTimeSamples& samples, uint32_t indent) {
    const std::vector<double>& times = samples._times;
    const Buffer<16>& blocked = samples._blocked;
    const Buffer<16>& values = samples._values;

    size_t element_size = sizeof(uint64_t);

    // Map to cache printed strings: pointer -> string
    std::map<uint64_t, std::string> cached_strings;

    size_t value_offset = 0;
    for (size_t i = 0; i < times.size(); ++i) {
        writer.write(pprint::Indent(indent + 1));
        writer.write(times[i]);
        writer.write(": ");

        if (blocked[i]) {
            writer.write("None");
        } else {
            if (!samples._offsets.empty()) {
                value_offset = samples._offsets[i];
            }

            // Get pointer to value data for this sample
            const uint8_t* value_data = values.data() + value_offset;

            // Extract pointer value from packed data
            uint64_t packed_value;
            std::memcpy(&packed_value, value_data, sizeof(uint64_t));
            uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

            // Sign-extend from 48 bits to 64 bits
            if (ptr_bits & (1ULL << 47)) {
                ptr_bits |= 0xFFFF000000000000ULL;
            }

            // Check cache first
            auto it = cached_strings.find(ptr_bits);
            if (it != cached_strings.end()) {
                // Reuse cached string
                writer.write(it->second);
            } else {
                // First occurrence - dereference TypedArray pointer and print
                // For bool, we treat it as uint8_t storage
                size_t pos_before = writer.str().size();

                // Try printing as uint8_t (bool's storage type)
                bool success = try_print_typed_array_value<uint8_t>(writer, value_data);
                if (!success) {
                    writer.write("[TypedArray<bool> print failed]");
                }

                size_t pos_after = writer.str().size();

                // Cache the printed string
                std::string printed = writer.str().substr(pos_before, pos_after - pos_before);
                cached_strings[ptr_bits] = printed;
            }

            if (samples._offsets.empty()) {
                value_offset += element_size;
            }
        }

        writer.write(",");  // USDA allows trailing comma
        writer.write("\n");
    }
}

// Dispatch function to call the correct template based on type_id
static bool pprint_typed_array_timesamples_dispatch(StreamWriter& writer, const PODTimeSamples& samples, uint32_t indent) {
    using namespace value;

    switch (samples.type_id()) {
        case TYPE_ID_BOOL:
            pprint_typed_array_timesamples<bool>(writer, samples, indent);
            return true;
        case TYPE_ID_FLOAT:
            pprint_typed_array_timesamples<float>(writer, samples, indent);
            return true;
        case TYPE_ID_DOUBLE:
            pprint_typed_array_timesamples<double>(writer, samples, indent);
            return true;
        case TYPE_ID_INT32:
            pprint_typed_array_timesamples<int32_t>(writer, samples, indent);
            return true;
        case TYPE_ID_FLOAT2:
            pprint_typed_array_timesamples<value::float2>(writer, samples, indent);
            return true;
        case TYPE_ID_FLOAT3:
            pprint_typed_array_timesamples<value::float3>(writer, samples, indent);
            return true;
        case TYPE_ID_FLOAT4:
            pprint_typed_array_timesamples<value::float4>(writer, samples, indent);
            return true;
        case TYPE_ID_DOUBLE2:
            pprint_typed_array_timesamples<value::double2>(writer, samples, indent);
            return true;
        case TYPE_ID_DOUBLE3:
            pprint_typed_array_timesamples<value::double3>(writer, samples, indent);
            return true;
        case TYPE_ID_DOUBLE4:
            pprint_typed_array_timesamples<value::double4>(writer, samples, indent);
            return true;
        case TYPE_ID_TEXCOORD2F:
            pprint_typed_array_timesamples<value::texcoord2f>(writer, samples, indent);
            return true;
        case TYPE_ID_TEXCOORD2D:
            pprint_typed_array_timesamples<value::texcoord2d>(writer, samples, indent);
            return true;
        case TYPE_ID_TEXCOORD2H:
            pprint_typed_array_timesamples<value::texcoord2h>(writer, samples, indent);
            return true;
        case TYPE_ID_TEXCOORD3F:
            pprint_typed_array_timesamples<value::texcoord3f>(writer, samples, indent);
            return true;
        case TYPE_ID_TEXCOORD3D:
            pprint_typed_array_timesamples<value::texcoord3d>(writer, samples, indent);
            return true;
        case TYPE_ID_TEXCOORD3H:
            pprint_typed_array_timesamples<value::texcoord3h>(writer, samples, indent);
            return true;
        case TYPE_ID_NORMAL3F:
            pprint_typed_array_timesamples<value::normal3f>(writer, samples, indent);
            return true;
        case TYPE_ID_NORMAL3D:
            pprint_typed_array_timesamples<value::normal3d>(writer, samples, indent);
            return true;
        case TYPE_ID_NORMAL3H:
            pprint_typed_array_timesamples<value::normal3h>(writer, samples, indent);
            return true;
        case TYPE_ID_POINT3F:
            pprint_typed_array_timesamples<value::point3f>(writer, samples, indent);
            return true;
        case TYPE_ID_POINT3D:
            pprint_typed_array_timesamples<value::point3d>(writer, samples, indent);
            return true;
        case TYPE_ID_POINT3H:
            pprint_typed_array_timesamples<value::point3h>(writer, samples, indent);
            return true;
        case TYPE_ID_COLOR3F:
            pprint_typed_array_timesamples<value::color3f>(writer, samples, indent);
            return true;
        case TYPE_ID_COLOR3D:
            pprint_typed_array_timesamples<value::color3d>(writer, samples, indent);
            return true;
        case TYPE_ID_COLOR3H:
            pprint_typed_array_timesamples<value::color3h>(writer, samples, indent);
            return true;
        case TYPE_ID_COLOR4F:
            pprint_typed_array_timesamples<value::color4f>(writer, samples, indent);
            return true;
        case TYPE_ID_COLOR4D:
            pprint_typed_array_timesamples<value::color4d>(writer, samples, indent);
            return true;
        case TYPE_ID_COLOR4H:
            pprint_typed_array_timesamples<value::color4h>(writer, samples, indent);
            return true;
        case TYPE_ID_VECTOR3F:
            pprint_typed_array_timesamples<value::vector3f>(writer, samples, indent);
            return true;
        case TYPE_ID_VECTOR3D:
            pprint_typed_array_timesamples<value::vector3d>(writer, samples, indent);
            return true;
        case TYPE_ID_VECTOR3H:
            pprint_typed_array_timesamples<value::vector3h>(writer, samples, indent);
            return true;
        case TYPE_ID_QUATH:
            pprint_typed_array_timesamples<value::quath>(writer, samples, indent);
            return true;
        case TYPE_ID_QUATF:
            pprint_typed_array_timesamples<value::quatf>(writer, samples, indent);
            return true;
        case TYPE_ID_QUATD:
            pprint_typed_array_timesamples<value::quatd>(writer, samples, indent);
            return true;
        case TYPE_ID_MATRIX2F:
            pprint_typed_array_timesamples<value::matrix2f>(writer, samples, indent);
            return true;
        case TYPE_ID_MATRIX3F:
            pprint_typed_array_timesamples<value::matrix3f>(writer, samples, indent);
            return true;
        case TYPE_ID_MATRIX4F:
            pprint_typed_array_timesamples<value::matrix4f>(writer, samples, indent);
            return true;
        case TYPE_ID_MATRIX2D:
            pprint_typed_array_timesamples<value::matrix2d>(writer, samples, indent);
            return true;
        case TYPE_ID_MATRIX3D:
            pprint_typed_array_timesamples<value::matrix3d>(writer, samples, indent);
            return true;
        case TYPE_ID_MATRIX4D:
            pprint_typed_array_timesamples<value::matrix4d>(writer, samples, indent);
            return true;
        default:
            // Type not supported by optimized path
            return false;
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
    size_t element_size = samples._is_typed_array ? sizeof(uint64_t) : get_pod_type_size(samples.type_id());
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

    bool printed_array = false;
    if (samples._is_typed_array) {
      // Route to optimized template-based path
      printed_array = pprint_typed_array_timesamples_dispatch(writer, samples, indent);
    }

    if (!printed_array) {
      // Fallback
      const std::vector<double>& times = samples._times;
      const Buffer<16>& blocked = samples._blocked;
      const Buffer<16>& values = samples._values;

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

          // Optimization for TypedArray: cache printed strings by pointer value
          if (samples._is_typed_array) {
              // Map to cache printed strings: pointer -> string
              std::map<uint64_t, std::string> cached_strings;

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

                      //TUSDZ_LOG_I("pprint_pod_timesamples: i=" << i << " offset=" << samples._offsets[i] << " value_data=0x" << std::hex << reinterpret_cast<uintptr_t>(value_data) << std::dec);

                      // Extract pointer value from packed data
                      uint64_t packed_value;
                      std::memcpy(&packed_value, value_data, sizeof(uint64_t));
                      //TUSDZ_LOG_I("pprint_pod_timesamples: packed_value=0x" << std::hex << packed_value << std::dec);
                      uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;

                      // Sign-extend from 48 bits to 64 bits
                      if (ptr_bits & (1ULL << 47)) {
                          ptr_bits |= 0xFFFF000000000000ULL;
                      }

                      // Check cache first
                      auto it = cached_strings.find(ptr_bits);
                      if (it != cached_strings.end()) {
                          // Reuse cached string
                          writer.write(it->second);
                      } else {
                          // First occurrence - dereference TypedArray pointer and print
                          size_t pos_before = writer.str().size();

                          // Dereference and print the actual TypedArray contents using known type_id
                          print_typed_array_value_by_type_id(writer, value_data, samples.type_id());

                          size_t pos_after = writer.str().size();

                          // Cache the printed string
                          std::string printed = writer.str().substr(pos_before, pos_after - pos_before);
                          cached_strings[ptr_bits] = printed;
                      }
                  }

                  writer.write(",");  // USDA allows trailing comma
                  writer.write("\n");
              }
          } else {
              // Non-TypedArray path: use regular printing
              for (size_t i = 0; i < times.size(); ++i) {
                  writer.write(pprint::Indent(indent + 1));
                  writer.write(times[i]);
                  writer.write(": ");

                  if (blocked[i] || samples._offsets[i] == SIZE_MAX) {
                      writer.write("None");
                  } else {
                      // Get pointer to value data using offset
                      const uint8_t* value_data = values.data() + (samples._offsets[i] & PODTimeSamples::OFFSET_VALUE_MASK);

                      // Check if this sample is an array (either global flag or per-sample flag)
                      bool is_array = samples._is_stl_array || (samples._offsets[i] & PODTimeSamples::OFFSET_ARRAY_FLAG);

                      if (is_array) {
                          // Print all elements in the array
                          pprint_pod_array_by_type(writer, value_data, samples.type_id(), samples._array_size);
                      } else {
                          // Print single value
                          pprint_pod_value_by_type(writer, value_data, samples.type_id());
                      }
                  }

                  writer.write(",");  // USDA allows trailing comma
                  writer.write("\n");
              }
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

                  // Check if this is an array type
                  bool is_array = samples._is_stl_array;

                  if (is_array) {
                      // Print all elements in the array
                      pprint_pod_array_by_type(writer, value_data, samples.type_id(), samples._array_size);
                  } else {
                      // Print single value
                      pprint_pod_value_by_type(writer, value_data, samples.type_id());
                  }
                  value_offset += element_size;
              }

              writer.write(",");  // USDA allows trailing comma
              writer.write("\n");
          }
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

        // Phase 3: Access unified storage directly from TimeSamples
        // Note: TypedArray is no longer supported in Phase 3, so we skip that path

        // Get type information
        uint32_t type_id = samples.type_id();
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

        // Get array size from TimeSamples directly (works for both POD storage and unified storage)
        size_t array_size = samples.get_array_size();

        // Get arrays from unified storage
        const auto& times = samples.get_times();
        const auto& blocked = samples.get_blocked();
        const auto& values = samples.get_values();
        const auto& offsets = samples.get_offsets();

        //TUSDZ_LOG_I("times.size " << times.size());
        //TUSDZ_LOG_I("blocked.size " << blocked.size());
        //TUSDZ_LOG_I("values.size " << values.size());

        // Write samples - handle offset table if present
        if (!offsets.empty()) {

            // Phase 3: TypedArray path removed (not supported in unified storage)
            // Use regular printing for all POD types
            for (size_t i = 0; i < times.size(); ++i) {
                writer.write(pprint::Indent(indent + 1));
                writer.write(times[i]);
                writer.write(": ");

                if (blocked[i] || offsets[i] == SIZE_MAX) {
                    writer.write("None");
                } else {
                    // Resolve offset (may be encoded with dedup/array flags)
                    size_t byte_offset;
                    if (!PODTimeSamples::resolve_offset_static(offsets, i, &byte_offset)) {
                        writer.write("/* ERROR: failed to resolve offset */");
                    } else {
                        // Get pointer to value data using resolved byte offset
                        const uint8_t* value_ptr = values.data() + byte_offset;

                        // Check if this sample is an array (check array flag in offset)
                        bool is_array = samples.is_stl_array() || (offsets[i] & PODTimeSamples::OFFSET_ARRAY_FLAG);

                        if (is_array) {
                            // Print all elements in the array
                            pprint_pod_array_by_type(writer, value_ptr, type_id, array_size);
                        } else {
                            // Print single value
                            pprint_pod_value_by_type(writer, value_ptr, type_id);
                        }
                    }
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
                //TUSDZ_LOG_I("times[" << i << "] = " << times[i]);
                writer.write(pprint::Indent(indent + 1));
                writer.write(times[i]);
                writer.write(": ");

                if (blocked[i]) {
                    writer.write("None");
                } else {
                    // Get pointer to value data
                    const uint8_t* value_ptr = values.data() + value_offset;

                    // Check if this is an array type
                    bool is_array = samples.is_stl_array();

                    if (is_array) {
                        // Print all elements in the array
                        pprint_pod_array_by_type(writer, value_ptr, type_id, array_size);
                    } else {
                        // Print single value
                        pprint_pod_value_by_type(writer, value_ptr, type_id);
                    }
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

void set_threaded_print_threshold(size_t threshold) {
    g_threaded_print_config.thread_threshold = threshold;
}

void set_threaded_print_num_threads(unsigned int num_threads) {
    g_threaded_print_config.num_threads = num_threads;
}

} // namespace tinyusdz
