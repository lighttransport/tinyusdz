// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "value-pprint.hh"

#include <sstream>

#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "usdGeom.hh"
#include "usdLux.hh"
#include "value-types.hh"

#ifdef TINYUSDZ_CHUNKED_SW
#include "stream-writer.hh"
#include "tiny-string.hh"
#include <cstring>
#include <cstdio>
#endif

//
#include "common-macros.inc"

// For fast int/float to ascii
// Default disabled.
//#define TINYUSDZ_LOCAL_USE_JEAIII_ITOA

#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
#include "external/jeaiii_to_text.h"
#endif

// dtoa_milo does not work well for float types
// (e.g. it prints float 0.01 as 0.009999999997),
// so use floaxie for float types
// TODO: Use floaxie also for double?
#include "external/dtoa_milo.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/floaxie/floaxie/ftoa.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef TINYUSDZ_PPRINT_OPT
#include "external/dragonbox/dragonbox_to_chars.h"
#include <type_traits>
#include <limits>
#include <cstring>
#include <array>

namespace tinyusdz {

// Forward declaration for half_to_float
namespace value {
  float half_to_float(const value::half &h);
}

namespace detail {

// Chunked buffer for minimizing allocations during float array printing
class ChunkedBuffer {
public:
  static constexpr size_t kChunkSize = 4096;  // 4KB chunks
  
  ChunkedBuffer() {
    chunks_.reserve(16);  // Reserve space for 16 chunks initially
    newChunk();
  }
  
  ~ChunkedBuffer() = default;
  
  ChunkedBuffer(const ChunkedBuffer&) = delete;
  ChunkedBuffer& operator=(const ChunkedBuffer&) = delete;
  ChunkedBuffer(ChunkedBuffer&&) = default;
  ChunkedBuffer& operator=(ChunkedBuffer&&) = default;
  
  // Append const char* data with known length
  void append(const char* data, size_t len) {
    if (current_pos_ + len <= kChunkSize) {
      // Fits in current chunk
      std::memcpy(chunks_.back().data() + current_pos_, data, len);
      current_pos_ += len;
    } else {
      // Need to split across chunks or create new chunk
      size_t remaining = len;
      size_t offset = 0;
      
      while (remaining > 0) {
        size_t available = kChunkSize - current_pos_;
        size_t to_copy = std::min(remaining, available);
        
        std::memcpy(chunks_.back().data() + current_pos_, data + offset, to_copy);
        current_pos_ += to_copy;
        remaining -= to_copy;
        offset += to_copy;
        
        if (remaining > 0) {
          newChunk();
        }
      }
    }
    total_size_ += len;
  }
  
  // Append single character
  void append(char c) {
    if (current_pos_ >= kChunkSize) {
      newChunk();
    }
    chunks_.back()[current_pos_++] = c;
    total_size_++;
  }
  
  // Append const char* (null-terminated)
  void append(const char* str) {
    append(str, std::strlen(str));
  }
  
  // Convert to std::string
  std::string to_string() const {
    std::string result;
    result.reserve(total_size_);
    
    for (size_t i = 0; i < chunks_.size() - 1; ++i) {
      result.append(chunks_[i].data(), kChunkSize);
    }
    
    // Last chunk may be partially filled
    if (!chunks_.empty()) {
      result.append(chunks_.back().data(), current_pos_);
    }
    
    return result;
  }
  
  // Stream-like operator<< for const char*
  ChunkedBuffer& operator<<(const char* str) {
    append(str);
    return *this;
  }
  
  // Stream-like operator<< for char
  ChunkedBuffer& operator<<(char c) {
    append(c);
    return *this;
  }
  
  // Stream-like operator<< for float (optimized)
  ChunkedBuffer& operator<<(float f) {
    char buf[32];  // Enough for any float representation
    char* end = jkj::dragonbox::to_chars(f, buf);
    append(buf, end - buf);
    return *this;
  }
  
  // Stream-like operator<< for double (optimized)  
  ChunkedBuffer& operator<<(double d) {
    char buf[32];  // Enough for any double representation
    char* end = jkj::dragonbox::to_chars(d, buf);
    append(buf, end - buf);
    return *this;
  }
  
  // Fast integer to string conversion
  template<typename T>
  typename std::enable_if<std::is_integral<T>::value, ChunkedBuffer&>::type
  operator<<(T value) {
    char buf[32];  // Enough for any 64-bit integer
    char* p = buf + sizeof(buf);
    char* end = p;
    
    bool negative = false;
    if (std::is_signed<T>::value && value < 0) {
      negative = true;
      // Handle minimum value edge case
      if (value == std::numeric_limits<T>::min()) {
        // Special case for minimum value
        const char* min_str = nullptr;
        if (sizeof(T) == 4) {
          min_str = "-2147483648";
        } else if (sizeof(T) == 8) {
          min_str = "-9223372036854775808";
        }
        if (min_str) {
          append(min_str);
          return *this;
        }
      }
      value = -value;
    }
    
    // Convert to string backwards
    typename std::make_unsigned<T>::type uvalue = value;
    do {
      *--p = '0' + (uvalue % 10);
      uvalue /= 10;
    } while (uvalue > 0);
    
    if (negative) {
      *--p = '-';
    }
    
    append(p, end - p);
    return *this;
  }
  
  // Optimized float array types (float2, float3, float4)
  template<size_t N>
  ChunkedBuffer& operator<<(const std::array<float, N>& v) {
    append('(');
    for (size_t i = 0; i < N; i++) {
      if (i > 0) append(", ");
      *this << v[i];  // Uses dragonbox float conversion
    }
    append(')');
    return *this;
  }
  
  // Optimized double array types (double2, double3, double4)
  template<size_t N>
  ChunkedBuffer& operator<<(const std::array<double, N>& v) {
    append('(');
    for (size_t i = 0; i < N; i++) {
      if (i > 0) append(", ");
      *this << v[i];  // Uses dragonbox double conversion
    }
    append(')');
    return *this;
  }
  
  // Optimized integer array types (int2, int3, int4, uint2, uint3, uint4)
  template<size_t N, typename T>
  typename std::enable_if<std::is_integral<T>::value, ChunkedBuffer&>::type
  operator<<(const std::array<T, N>& v) {
    append('(');
    for (size_t i = 0; i < N; i++) {
      if (i > 0) append(", ");
      *this << v[i];  // Uses optimized integer conversion
    }
    append(')');
    return *this;
  }
  
  // Optimized half array types (half2, half3, half4) - convert to float first
  template<size_t N>
  ChunkedBuffer& operator<<(const std::array<tinyusdz::value::half, N>& v) {
    append('(');
    for (size_t i = 0; i < N; i++) {
      if (i > 0) append(", ");
      float f = tinyusdz::value::half_to_float(v[i]);
      *this << f;  // Uses dragonbox float conversion
    }
    append(')');
    return *this;
  }
  
  // Forward declarations for matrix types
  struct matrix2f;
  struct matrix3f; 
  struct matrix4f;
  struct matrix2d;
  struct matrix3d;
  struct matrix4d;
  
  // Optimized matrix2f printing
  ChunkedBuffer& operator<<(const tinyusdz::value::matrix2f& m) {
    append("( (");
    for (int i = 0; i < 2; i++) {
      if (i > 0) append("), (");
      for (int j = 0; j < 2; j++) {
        if (j > 0) append(", ");
        *this << m.m[i][j];
      }
    }
    append(") )");
    return *this;
  }
  
  // Optimized matrix3f printing
  ChunkedBuffer& operator<<(const tinyusdz::value::matrix3f& m) {
    append("( (");
    for (int i = 0; i < 3; i++) {
      if (i > 0) append("), (");
      for (int j = 0; j < 3; j++) {
        if (j > 0) append(", ");
        *this << m.m[i][j];
      }
    }
    append(") )");
    return *this;
  }
  
  // Optimized matrix4f printing
  ChunkedBuffer& operator<<(const tinyusdz::value::matrix4f& m) {
    append("( (");
    for (int i = 0; i < 4; i++) {
      if (i > 0) append("), (");
      for (int j = 0; j < 4; j++) {
        if (j > 0) append(", ");
        *this << m.m[i][j];
      }
    }
    append(") )");
    return *this;
  }
  
  // Optimized matrix2d printing
  ChunkedBuffer& operator<<(const tinyusdz::value::matrix2d& m) {
    append("( (");
    for (int i = 0; i < 2; i++) {
      if (i > 0) append("), (");
      for (int j = 0; j < 2; j++) {
        if (j > 0) append(", ");
        *this << m.m[i][j];
      }
    }
    append(") )");
    return *this;
  }
  
  // Optimized matrix3d printing
  ChunkedBuffer& operator<<(const tinyusdz::value::matrix3d& m) {
    append("( (");
    for (int i = 0; i < 3; i++) {
      if (i > 0) append("), (");
      for (int j = 0; j < 3; j++) {
        if (j > 0) append(", ");
        *this << m.m[i][j];
      }
    }
    append(") )");
    return *this;
  }
  
  // Optimized matrix4d printing
  ChunkedBuffer& operator<<(const tinyusdz::value::matrix4d& m) {
    append("( (");
    for (int i = 0; i < 4; i++) {
      if (i > 0) append("), (");
      for (int j = 0; j < 4; j++) {
        if (j > 0) append(", ");
        *this << m.m[i][j];
      }
    }
    append(") )");
    return *this;
  }
  
  size_t size() const { return total_size_; }
  bool empty() const { return total_size_ == 0; }
  
private:
  void newChunk() {
    chunks_.emplace_back();
    current_pos_ = 0;
  }
  
  std::vector<std::array<char, kChunkSize>> chunks_;
  size_t current_pos_ = 0;
  size_t total_size_ = 0;
};

}  // namespace detail
}  // namespace tinyusdz
#endif  // TINYUSDZ_PPRINT_OPT

namespace tinyusdz {

namespace {

#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
void itoa(uint32_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int32_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(uint64_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
void itoa(int64_t n, char *b) { *jeaiii::to_text_from_integer(b, n) = '\0'; }
#endif

inline std::string dtos(const float v) {
  char buf[floaxie::max_buffer_size<float>()];
  size_t n = floaxie::ftoa(v, buf);

  return std::string(buf, buf + n);
}

inline std::string dtos(const double v) {
  char buf[128];
  dtoa_milo(v, buf);

  return std::string(buf);
}

}  // namespace

}  // namespace tinyusdz

namespace std {

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half &v) {
  os << tinyusdz::value::half_to_float(v);
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::half4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

// treat char vector type as byte
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::char2 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::char3 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::char4 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ", "
     << int(v[3]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uchar2 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uchar3 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uchar4 &v) {
  os << "(" << int(v[0]) << ", " << int(v[1]) << ", " << int(v[2]) << ", "
     << int(v[3]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::short2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::short3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::short4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::ushort2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::ushort3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::ushort4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::int4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint2 &v) {
  os << "(" << v[0] << ", " << v[1] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint3 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::uint4 &v) {
  os << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float2 &v) {
  os << "(" << tinyusdz::dtos(v[0]) << ", " << tinyusdz::dtos(v[1]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float3 &v) {
  os << "(" << tinyusdz::dtos(v[0]) << ", " << tinyusdz::dtos(v[1]) << ", "
     << tinyusdz::dtos(v[2]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::float4 &v) {
  os << "(" << tinyusdz::dtos(v[0]) << ", " << tinyusdz::dtos(v[1]) << ", "
     << tinyusdz::dtos(v[2]) << ", " << tinyusdz::dtos(v[3]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double2 &v) {
  os << "(" << tinyusdz::dtos(v[0]) << ", " << tinyusdz::dtos(v[1]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double3 &v) {
  os << "(" << tinyusdz::dtos(v[0]) << ", " << tinyusdz::dtos(v[1]) << ", "
     << tinyusdz::dtos(v[2]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::double4 &v) {
  os << "(" << tinyusdz::dtos(v[0]) << ", " << tinyusdz::dtos(v[1]) << ", "
     << tinyusdz::dtos(v[2]) << ", " << tinyusdz::dtos(v[3]) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3f &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::vector3d &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3f &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::normal3d &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3h &v) {
  os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3f &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::point3d &v) {
  os << "(" << tinyusdz::dtos(v.x) << ", " << tinyusdz::dtos(v.y) << ", "
     << tinyusdz::dtos(v.z) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3h &v) {
  os << "(" << tinyusdz::value::half_to_float(v.r) << ", "
     << tinyusdz::value::half_to_float(v.g) << ", "
     << tinyusdz::value::half_to_float(v.b) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3f &v) {
  os << "(" << tinyusdz::dtos(v.r) << ", " << tinyusdz::dtos(v.g) << ", "
     << tinyusdz::dtos(v.b) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color3d &v) {
  os << "(" << tinyusdz::dtos(v.r) << ", " << tinyusdz::dtos(v.g) << ", "
     << tinyusdz::dtos(v.b) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4h &v) {
  os << "(" << tinyusdz::value::half_to_float(v.r) << ", "
     << tinyusdz::value::half_to_float(v.g) << ", "
     << tinyusdz::value::half_to_float(v.b) << ", "
     << tinyusdz::value::half_to_float(v.a) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4f &v) {
  os << "(" << tinyusdz::dtos(v.r) << ", " << tinyusdz::dtos(v.g) << ", "
     << tinyusdz::dtos(v.b) << ", " << tinyusdz::dtos(v.a) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::color4d &v) {
  os << "(" << tinyusdz::dtos(v.r) << ", " << tinyusdz::dtos(v.g) << ", "
     << tinyusdz::dtos(v.b) << ", " << tinyusdz::dtos(v.a) << ")";
  return os;
}

// pxrUSD prints quateron in [w, x, y, z] order
// https://github.com/PixarAnimationStudios/USD/blob/3abc46452b1271df7650e9948fef9f0ce602e3b2/pxr/base/gf/quatf.h#L287
std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quath &v) {
  os << "(" << v.real << ", " << v.imag[0] << ", " << v.imag[1] << ", "
     << v.imag[2] << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatf &v) {
  os << "(" << tinyusdz::dtos(v.real) << ", " << tinyusdz::dtos(v.imag[0])
     << ", " << tinyusdz::dtos(v.imag[1]) << ", " << tinyusdz::dtos(v.imag[2])
     << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os, const tinyusdz::value::quatd &v) {
  os << "(" << tinyusdz::dtos(v.real) << ", " << tinyusdz::dtos(v.imag[0])
     << ", " << tinyusdz::dtos(v.imag[1]) << ", " << tinyusdz::dtos(v.imag[2])
     << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2h &v) {
  os << "(" << v.s << ", " << v.t << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2f &v) {
  os << "(" << tinyusdz::dtos(v.s) << ", " << tinyusdz::dtos(v.t) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord2d &v) {
  os << "(" << tinyusdz::dtos(v.s) << ", " << tinyusdz::dtos(v.t) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3h &v) {
  os << "(" << v.s << ", " << v.t << ", " << v.r << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3f &v) {
  os << "(" << tinyusdz::dtos(v.s) << ", " << tinyusdz::dtos(v.t) << ", "
     << tinyusdz::dtos(v.r) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const tinyusdz::value::texcoord3d &v) {
  os << "(" << tinyusdz::dtos(v.s) << ", " << tinyusdz::dtos(v.t) << ", "
     << tinyusdz::dtos(v.r) << ")";
  return os;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix2f &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix3f &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix4f &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << ", " << tinyusdz::dtos(m.m[0][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << ", " << tinyusdz::dtos(m.m[1][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ", " << tinyusdz::dtos(m.m[2][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[3][0]) << ", " << tinyusdz::dtos(m.m[3][1])
      << ", " << tinyusdz::dtos(m.m[3][2]) << ", " << tinyusdz::dtos(m.m[3][3])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix2d &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix3d &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::matrix4d &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << ", " << tinyusdz::dtos(m.m[0][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << ", " << tinyusdz::dtos(m.m[1][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ", " << tinyusdz::dtos(m.m[2][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[3][0]) << ", " << tinyusdz::dtos(m.m[3][1])
      << ", " << tinyusdz::dtos(m.m[3][2]) << ", " << tinyusdz::dtos(m.m[3][3])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::frame4d &m) {
  ofs << "( ";

  ofs << "(" << tinyusdz::dtos(m.m[0][0]) << ", " << tinyusdz::dtos(m.m[0][1])
      << ", " << tinyusdz::dtos(m.m[0][2]) << ", " << tinyusdz::dtos(m.m[0][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[1][0]) << ", " << tinyusdz::dtos(m.m[1][1])
      << ", " << tinyusdz::dtos(m.m[1][2]) << ", " << tinyusdz::dtos(m.m[1][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[2][0]) << ", " << tinyusdz::dtos(m.m[2][1])
      << ", " << tinyusdz::dtos(m.m[2][2]) << ", " << tinyusdz::dtos(m.m[2][3])
      << "), ";
  ofs << "(" << tinyusdz::dtos(m.m[3][0]) << ", " << tinyusdz::dtos(m.m[3][1])
      << ", " << tinyusdz::dtos(m.m[3][2]) << ", " << tinyusdz::dtos(m.m[3][3])
      << ")";

  ofs << " )";

  return ofs;
}

std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::token &tok) {
  ofs << tinyusdz::quote(tok.str());

  return ofs;
}

#if 0
std::ostream &operator<<(std::ostream &ofs, const tinyusdz::value::dict &m) {
  ofs << "{\n";
  for (const auto &item : m) {
    ofs << item.first << " = " << tinyusdz::value::pprint_any(item.second)
        << "\n";
  }
  ofs << "}";

  return ofs;
}
#endif

std::ostream &operator<<(std::ostream &ofs,
                         const tinyusdz::value::AssetPath &asset) {
  std::string in_s = asset.GetAssetPath();

  if (in_s.empty()) {
    ofs << "@@";
  } else {
    std::string quote_str = "@";

    std::string s;

    if (tinyusdz::contains(in_s, '@')) {
      // Escape '@@@'(to '\@@@') if the input path contains '@@@'
      for (size_t i = 0; i < in_s.length(); i++) {
        if ((i + 2) < in_s.length()) {
          if (in_s[i] == '@' && in_s[i + 1] == '@' && in_s[i + 2] == '@') {
            s += "\\@@@";
            i += 2;
          } else {
            s += in_s[i];
          }
        }
      }

      quote_str = "@@@";
    } else {
      s = in_s;
    }

    // Do not escape backslash for asset path
    ofs << quote_str << s << quote_str;
  }

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<double> &v) {
#ifdef TINYUSDZ_PPRINT_OPT
  // Use chunked buffer with dragonbox for optimal performance
  tinyusdz::detail::ChunkedBuffer buf;
  buf << '[';
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      buf << ", ";
    }
    buf << v[i];  // Uses dragonbox via ChunkedBuffer::operator<<(double)
  }
  buf << ']';
  
  ofs << buf.to_string();
#else
  // Not sure what is the HARD-LIMT buffer length for dtoa_milo,
  // but according to std::numeric_limits<double>::digits10(=15),
  // 32 should be sufficient, but allocate 128 just in case
  char buf[128];

  // TODO: multi-threading for further performance gain?

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    dtoa_milo(v[i], buf);
    ofs << std::string(buf);
  }
  ofs << "]";
#endif

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<float> &v) {
#ifdef TINYUSDZ_PPRINT_OPT
  // Use chunked buffer with dragonbox for optimal performance
  tinyusdz::detail::ChunkedBuffer buf;
  buf << '[';
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      buf << ", ";
    }
    buf << v[i];  // Uses dragonbox via ChunkedBuffer::operator<<(float)
  }
  buf << ']';
  
  ofs << buf.to_string();
#else
  // Use floaxie
  char buf[128];

  // TODO: multi-threading for further performance gain?

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
    floaxie::ftoa(v[i], buf);
    ofs << std::string(buf);
  }
  ofs << "]";
#endif

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<int32_t> &v) {
#ifdef TINYUSDZ_PPRINT_OPT
  // Use chunked buffer with optimized integer conversion
  tinyusdz::detail::ChunkedBuffer buf;
  buf << '[';
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      buf << ", ";
    }
    buf << v[i];  // Uses optimized integer conversion
  }
  buf << ']';
  
  ofs << buf.to_string();
#else
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  // numeric_limits<uint64_t>::digits10 is 19, so 32 should suffice.
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";
#endif

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<uint32_t> &v) {
#ifdef TINYUSDZ_PPRINT_OPT
  // Use chunked buffer with optimized integer conversion
  tinyusdz::detail::ChunkedBuffer buf;
  buf << '[';
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      buf << ", ";
    }
    buf << v[i];  // Uses optimized integer conversion
  }
  buf << ']';
  
  ofs << buf.to_string();
#else
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";
#endif

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<int64_t> &v) {
#ifdef TINYUSDZ_PPRINT_OPT
  // Use chunked buffer with optimized integer conversion
  tinyusdz::detail::ChunkedBuffer buf;
  buf << '[';
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      buf << ", ";
    }
    buf << v[i];  // Uses optimized integer conversion
  }
  buf << ']';
  
  ofs << buf.to_string();
#else
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  // numeric_limits<uint64_t>::digits10 is 19, so 32 should suffice.
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";
#endif

  return ofs;
}

template <>
std::ostream &operator<<(std::ostream &ofs, const std::vector<uint64_t> &v) {
#ifdef TINYUSDZ_PPRINT_OPT
  // Use chunked buffer with optimized integer conversion
  tinyusdz::detail::ChunkedBuffer buf;
  buf << '[';
  
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      buf << ", ";
    }
    buf << v[i];  // Uses optimized integer conversion
  }
  buf << ']';
  
  ofs << buf.to_string();
#else
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
  char buf[32];
#endif

  ofs << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      ofs << ", ";
    }
#if defined(TINYUSDZ_LOCAL_USE_JEAIII_ITOA)
    tinyusdz::itoa(v[i], buf);
    ofs << buf;
#else
    ofs << v[i];
#endif
  }
  ofs << "]";
#endif

  return ofs;
}

}  // namespace std

namespace tinyusdz {
namespace value {

// Simple brute-force way..
// TODO: Use std::function or some template technique?
// NOTE: Use dedicated path for `float` and `double`

#define CASE_EXPR_LIST(__FUNC) \
  __FUNC(bool)                 \
  __FUNC(half)                 \
  __FUNC(half2)                \
  __FUNC(half3)                \
  __FUNC(half4)                \
  __FUNC(int32_t)              \
  __FUNC(uint32_t)             \
  __FUNC(int2)                 \
  __FUNC(int3)                 \
  __FUNC(int4)                 \
  __FUNC(uint2)                \
  __FUNC(uint3)                \
  __FUNC(uint4)                \
  __FUNC(int64_t)              \
  __FUNC(uint64_t)             \
  __FUNC(float2)               \
  __FUNC(float3)               \
  __FUNC(float4)               \
  __FUNC(double2)              \
  __FUNC(double3)              \
  __FUNC(double4)              \
  __FUNC(matrix2d)             \
  __FUNC(matrix3d)             \
  __FUNC(matrix4d)             \
  __FUNC(quath)                \
  __FUNC(quatf)                \
  __FUNC(quatd)                \
  __FUNC(normal3h)             \
  __FUNC(normal3f)             \
  __FUNC(normal3d)             \
  __FUNC(vector3h)             \
  __FUNC(vector3f)             \
  __FUNC(vector3d)             \
  __FUNC(point3h)              \
  __FUNC(point3f)              \
  __FUNC(point3d)              \
  __FUNC(color3f)              \
  __FUNC(color3d)              \
  __FUNC(color4f)              \
  __FUNC(color4d)              \
  __FUNC(texcoord2h)           \
  __FUNC(texcoord2f)           \
  __FUNC(texcoord2d)           \
  __FUNC(texcoord3h)           \
  __FUNC(texcoord3f)           \
  __FUNC(texcoord3d)

#define CASE_GPRIM_LIST(__FUNC) \
  __FUNC(Model)                 \
  __FUNC(Scope)                 \
  __FUNC(Xform)                 \
  __FUNC(GeomMesh)              \
  __FUNC(GeomSphere)            \
  __FUNC(GeomSubset)            \
  __FUNC(GeomPoints)            \
  __FUNC(GeomCube)              \
  __FUNC(GeomCylinder)          \
  __FUNC(GeomCapsule)           \
  __FUNC(GeomCone)              \
  __FUNC(GeomBasisCurves)       \
  __FUNC(GeomNurbsCurves)       \
  __FUNC(GeomCamera)            \
  __FUNC(GeomPointInstancer)        \
  __FUNC(SphereLight)           \
  __FUNC(DomeLight)             \
  __FUNC(DiskLight)             \
  __FUNC(DistantLight)          \
  __FUNC(CylinderLight)         \
  __FUNC(SkelRoot)              \
  __FUNC(Skeleton)              \
  __FUNC(SkelAnimation)         \
  __FUNC(BlendShape)            \
  __FUNC(Material)              \
  __FUNC(Shader)

#if 0  // remove
// std::ostream &operator<<(std::ostream &os, const any_value &v) {
// std::ostream &operator<<(std::ostream &os, const linb::any &v) {
std::string pprint_any(const linb::any &v, const uint32_t indent,
                       bool closing_brace) {
#define BASETYPE_CASE_EXPR(__ty)         \
  case TypeTraits<__ty>::type_id(): {    \
    os << linb::any_cast<const __ty>(v); \
    break;                               \
  }

#define PRIMTYPE_CASE_EXPR(__ty)                                           \
  case TypeTraits<__ty>::type_id(): {                                      \
    os << to_string(linb::any_cast<const __ty>(v), indent, closing_brace); \
    break;                                                                 \
  }

#define ARRAY1DTYPE_CASE_EXPR(__ty)                   \
  case TypeTraits<std::vector<__ty>>::type_id(): {    \
    os << linb::any_cast<const std::vector<__ty>>(v); \
    break;                                            \
  }

#define ARRAY2DTYPE_CASE_EXPR(__ty)                                \
  case TypeTraits<std::vector<std::vector<__ty>>>::type_id(): {    \
    os << linb::any_cast<const std::vector<std::vector<__ty>>>(v); \
    break;                                                         \
  }

  std::stringstream os;

  switch (v.type_id()) {
    // no `bool` type for 1D and 2D array
    BASETYPE_CASE_EXPR(bool)

    // no std::vector<dict> and std::vector<std::vector<dict>>, ...
    BASETYPE_CASE_EXPR(dict)

    // base type
    CASE_EXPR_LIST(BASETYPE_CASE_EXPR)

    // 1D array
    CASE_EXPR_LIST(ARRAY1DTYPE_CASE_EXPR)

    // 2D array
    CASE_EXPR_LIST(ARRAY2DTYPE_CASE_EXPR)
    // Assume no 2D array of string-like data.

    // GPrim
    CASE_GPRIM_LIST(PRIMTYPE_CASE_EXPR)

    // token, str: wrap with '"'
    case TypeTraits<value::token>::type_id(): {
      os << quote(linb::any_cast<const value::token>(v).str());
      break;
    }
    case TypeTraits<std::vector<value::token>>::type_id(): {
      const std::vector<value::token> &lst =
          linb::any_cast<const std::vector<value::token>>(v);
      std::vector<std::string> vs;
      std::transform(lst.begin(), lst.end(), std::back_inserter(vs),
                     [](const value::token &tok) { return tok.str(); });

      os << quote(vs);
      break;
    }
    case TypeTraits<std::string>::type_id(): {
      os << quote(linb::any_cast<const std::string>(v));
      break;
    }
    case TypeTraits<std::vector<std::string>>::type_id(): {
      const std::vector<std::string> &vs =
          linb::any_cast<const std::vector<std::string>>(v);
      os << quote(vs);
      break;
    }
    case TypeTraits<value::ValueBlock>::type_id(): {
      os << "None";
      break;
    }

    // TODO: List-up all case and remove `default` clause.
    default: {
      os << "ANY_PPRINT: TODO: (type: " << v.type_name() << ") ";
    }
  }

#undef BASETYPE_CASE_EXPR
#undef PRIMTYPE_CASE_EXPR
#undef ARRAY1DTYPE_CASE_EXPR
#undef ARRAY2DTYPE_CASE_EXPR

  return os.str();
}
#endif

std::string pprint_value(const value::Value &v, const uint32_t indent,
                         bool closing_brace) {
#define BASETYPE_CASE_EXPR(__ty)                           \
  case TypeTraits<__ty>::type_id(): {                      \
    auto p = v.as<__ty>();                                 \
    if (p) {                                               \
      os << (*p);                                          \
    } else {                                               \
      os << "[InternalError: Base type TypeId mismatch.]"; \
    }                                                      \
    break;                                                 \
  }

#define PRIMTYPE_CASE_EXPR(__ty)                           \
  case TypeTraits<__ty>::type_id(): {                      \
    auto p = v.as<__ty>();                                 \
    if (p) {                                               \
      os << to_string(*p, indent, closing_brace);          \
    } else {                                               \
      os << "[InternalError: Prim type TypeId mismatch.]"; \
    }                                                      \
    break;                                                 \
  }

#define ARRAY1DTYPE_CASE_EXPR(__ty)                      \
  case TypeTraits<std::vector<__ty>>::type_id(): {       \
    auto p = v.as<std::vector<__ty>>();                  \
    if (p) {                                             \
      os << (*p);                                        \
    } else {                                             \
      os << "[InternalError: 1D type TypeId mismatch.]"; \
    }                                                    \
    break;                                               \
  }

  std::stringstream os;

  switch (v.type_id()) {
    // base type
    CASE_EXPR_LIST(BASETYPE_CASE_EXPR)

    case TypeTraits<float>::type_id(): {
      auto p = v.as<float>();
      if (p) {
        os << dtos(*p);
      } else {
        os << "[InternalError: TypeId mismatch(`float` expected).]";
      }
      break;
    }

    case TypeTraits<double>::type_id(): {
      auto p = v.as<double>();
      if (p) {
        os << dtos(*p);
      } else {
        os << "[InternalError: TypeId mismatch(`double` expected).]";
      }
      break;
    }

      // 1D array
      CASE_EXPR_LIST(ARRAY1DTYPE_CASE_EXPR)

    case TypeTraits<std::vector<float>>::type_id(): {
      auto p = v.as<std::vector<float>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: TypeId mismatch(`float[]` expected).]";
      }
      break;
    }

    case TypeTraits<std::vector<double>>::type_id(): {
      auto p = v.as<std::vector<double>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: TypeId mismatch(`double[]` expected).]";
      }
      break;
    }

      // 2D array
      // CASE_EXPR_LIST(ARRAY2DTYPE_CASE_EXPR)

      // GPrim
      CASE_GPRIM_LIST(PRIMTYPE_CASE_EXPR)

    // dict and customData
    case TypeTraits<CustomDataType>::type_id(): {
      auto p = v.as<CustomDataType>();
      if (p) {
        os << print_customData(*p, "", indent);
      } else {
        os << "[InternalError: Dict type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<value::AssetPath>::type_id(): {
      auto p = v.as<value::AssetPath>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: asset type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<value::AssetPath>>::type_id(): {
      auto p = v.as<std::vector<value::AssetPath>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: asset[] type TypeId mismatch.]";
      }
      break;
    }

    case TypeTraits<value::token>::type_id(): {
      auto p = v.as<value::token>();
      if (p) {
        os << buildEscapedAndQuotedStringForUSDA(p->str());
      } else {
        os << "[InternalError: Token type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<value::token>>::type_id(): {
      auto p = v.get_value<std::vector<value::token>>();
      if (p) {
        std::vector<std::string> vs;
        std::transform(p->begin(), p->end(), std::back_inserter(vs),
                       [](const value::token &tok) {
                         return buildEscapedAndQuotedStringForUSDA(tok.str());
                       });

        os << vs;
      } else {
        os << "[InternalError: `token[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::string>::type_id(): {
      auto p = v.as<std::string>();
      if (p) {
        os << buildEscapedAndQuotedStringForUSDA(*p);
      } else {
        os << "[InternalError: `string` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<value::StringData>::type_id(): {
      auto p = v.as<value::StringData>();
      if (p) {
        os << (*p);  // FIXME: Call buildEscapedAndQuotedStringForUSDA() here?
      } else {
        os << "[InternalError: `string` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<std::string>>::type_id(): {
      auto p = v.as<std::vector<std::string>>();
      if (p) {
        std::vector<std::string> ss;
        for (const auto &item : *p) {
          ss.push_back(buildEscapedAndQuotedStringForUSDA(item));
        }
        os << ss;  // Use operator<<(std::vector<std::string>)
      } else {
        os << "[InternalError: `string[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<value::StringData>>::type_id(): {
      auto p = v.as<std::vector<value::StringData>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: `string[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<value::ValueBlock>::type_id(): {
      if (v.as<value::ValueBlock>()) {
        os << "None";
      } else {
        os << "[InternalError: ValueBlock type TypeId mismatch.]";
      }
      break;
    }
    // TODO: List-up all case and remove `default` clause.
    default: {
      os << "VALUE_PPRINT: TODO: (type: " << v.type_name() << ") ";
    }
  }

#undef BASETYPE_CASE_EXPR
#undef PRIMTYPE_CASE_EXPR
#undef ARRAY1DTYPE_CASE_EXPR
#undef ARRAY2DTYPE_CASE_EXPR

  return os.str();
}

#undef CASE_EXPR_LIST
#undef CASE_GPRIM_LIST

}  // namespace value

std::string to_string(bool v) {
  if (v) {
    return "true";
  } else {
    return "false";
  }
}

std::string to_string(int32_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(uint32_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(int64_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(uint64_t v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const tinyusdz::value::half4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

std::string to_string(const value::char2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::char3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::char4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::short2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::short3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::short4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::int2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::int3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::int4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::uint2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::uint3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::uint4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::float2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::float3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::float4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::double2 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::double3 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::double4 &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord2h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord2f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord2d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::texcoord3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::StringData &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::token &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const std::string &s) {
  // TODO: Escape `"` character.

  // Escape backslash
  return quote(escapeBackslash(s));
}
std::string to_string(const value::quath &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::quatf &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::quatd &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix2f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix4f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix2d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::matrix4d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::frame4d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::normal3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::normal3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::normal3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::vector3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::vector3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::vector3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::point3h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::point3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::point3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color3f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color3d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color4h &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color4f &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}
std::string to_string(const value::color4d &v) {
  std::stringstream ss;
  ss << v;
  return ss.str();
}

#ifdef TINYUSDZ_CHUNKED_SW
#include <cstring> // for strlen
#include <cstdio>  // for snprintf
#include <cinttypes> // for PRId64, PRIu64

//
// ChunkedStreamWriter operator<< implementations
//

// Basic types - optimized to avoid std::string allocations
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const char* str) {
  if (str) {
    sw.writeN(reinterpret_cast<const uint8_t*>(str), strlen(str));
  }
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const std::string &str) {
  sw.writeN(reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::tstring &str) {
  sw.writeN(reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::tstring_view &str) {
  sw.writeN(reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, char c) {
  sw.write1(static_cast<uint8_t>(c));
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, bool b) {
  sw << (b ? "true" : "false");
  return sw;
}

// Optimized integer to string conversion - avoid std::string allocation
namespace {
  // Forward declarations for template specializations
  inline void write_int32_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, int32_t i);
  inline void write_uint32_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, uint32_t i);
  inline void write_int64_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, int64_t i);
  inline void write_uint64_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, uint64_t i);
  inline void write_float_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, float f);
  inline void write_double_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, double d);

  inline void write_int32_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, int32_t i) {
    char buf[16]; // enough for int32
    int len = snprintf(buf, sizeof(buf), "%d", i);
    if (len > 0 && len < sizeof(buf)) {
      sw.writeN(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
  }

  inline void write_uint32_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, uint32_t i) {
    char buf[16]; // enough for uint32
    int len = snprintf(buf, sizeof(buf), "%u", i);
    if (len > 0 && len < sizeof(buf)) {
      sw.writeN(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
  }

  inline void write_int64_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, int64_t i) {
    char buf[24]; // enough for int64
    int len = snprintf(buf, sizeof(buf), "%" PRId64, i);
    if (len > 0 && len < sizeof(buf)) {
      sw.writeN(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
  }

  inline void write_uint64_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, uint64_t i) {
    char buf[24]; // enough for uint64
    int len = snprintf(buf, sizeof(buf), "%" PRIu64, i);
    if (len > 0 && len < sizeof(buf)) {
      sw.writeN(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
  }

  inline void write_float_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, float f) {
    char buf[32]; // enough for float
    int len = snprintf(buf, sizeof(buf), "%g", f);
    if (len > 0 && len < sizeof(buf)) {
      sw.writeN(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
  }

  inline void write_double_to_chunked_stream(tinyusdz::ChunkedStreamWriter &sw, double d) {
    char buf[32]; // enough for double
    int len = snprintf(buf, sizeof(buf), "%g", d);
    if (len > 0 && len < sizeof(buf)) {
      sw.writeN(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
    }
  }
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, int32_t i) {
  write_int32_to_chunked_stream(sw, i);
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, uint32_t i) {
  write_uint32_to_chunked_stream(sw, i);
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, int64_t i) {
  write_int64_to_chunked_stream(sw, i);
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, uint64_t i) {
  write_uint64_to_chunked_stream(sw, i);
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, float f) {
  write_float_to_chunked_stream(sw, f);
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, double d) {
  write_double_to_chunked_stream(sw, d);
  return sw;
}

// Vector types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::char2 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::char3 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::char4 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::uchar2 &v) {
  sw << "(" << static_cast<uint32_t>(v.x) << ", " << static_cast<uint32_t>(v.y) << ")";
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::uchar3 &v) {
  sw << "(" << static_cast<uint32_t>(v.x) << ", " << static_cast<uint32_t>(v.y) << ", " << static_cast<uint32_t>(v.z) << ")";
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::uchar4 &v) {
  sw << "(" << static_cast<uint32_t>(v.x) << ", " << static_cast<uint32_t>(v.y) << ", " << static_cast<uint32_t>(v.z) << ", " << static_cast<uint32_t>(v.w) << ")";
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::short2 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::short3 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::short4 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::ushort2 &v) {
  sw << "(" << v.x << ", " << v.y << ")";
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::ushort3 &v) {
  sw << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::ushort4 &v) {
  sw << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::int2 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::int3 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::int4 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::uint2 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::uint3 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::uint4 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::half &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::half2 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::half3 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::half4 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::float2 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::float3 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::float4 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::double2 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::double3 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::double4 &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// Point types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::point3h &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::point3f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::point3d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// Normal types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::normal3h &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::normal3f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::normal3d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// Vector types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::vector3h &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::vector3f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::vector3d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// Color types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::color3h &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::color3f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::color3d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::color4h &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::color4f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::color4d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// TexCoord types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::texcoord2h &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::texcoord2f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::texcoord2d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::texcoord3h &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::texcoord3f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::texcoord3d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// Quaternion types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::quath &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::quatf &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::quatd &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// Matrix types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::matrix2f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::matrix3f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::matrix4f &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::matrix2d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::matrix3d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::matrix4d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::frame4d &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

// Token and string types
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::token &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::StringData &v) {
  std::string s = tinyusdz::to_string(v);
  sw << s;
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::AssetPath &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

// For complex types that don't have to_string implementations, fall back to stringstream
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::dict &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::value::TimeSamples &ts) {
  std::stringstream ss;
  ss << ts;
  sw << ss.str();
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::Path &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::Reference &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::Payload &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::LayerOffset &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::SubLayer &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const tinyusdz::Collection &v) {
  std::stringstream ss;
  ss << v;
  sw << ss.str();
  return sw;
}

// Specialized array implementations - optimized for performance
template <>
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const std::vector<double> &v) {
  sw << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      sw << ", ";
    }
    write_double_to_chunked_stream(sw, v[i]);
  }
  sw << "]";
  return sw;
}

template <>
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const std::vector<float> &v) {
  sw << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      sw << ", ";
    }
    write_float_to_chunked_stream(sw, v[i]);
  }
  sw << "]";
  return sw;
}

template <>
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const std::vector<int32_t> &v) {
  sw << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      sw << ", ";
    }
    write_int32_to_chunked_stream(sw, v[i]);
  }
  sw << "]";
  return sw;
}

template <>
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const std::vector<uint32_t> &v) {
  sw << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      sw << ", ";
    }
    write_uint32_to_chunked_stream(sw, v[i]);
  }
  sw << "]";
  return sw;
}

template <>
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const std::vector<int64_t> &v) {
  sw << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      sw << ", ";
    }
    write_int64_to_chunked_stream(sw, v[i]);
  }
  sw << "]";
  return sw;
}

template <>
tinyusdz::ChunkedStreamWriter &operator<<(tinyusdz::ChunkedStreamWriter &sw, const std::vector<uint64_t> &v) {
  sw << "[";
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0) {
      sw << ", ";
    }
    write_uint64_to_chunked_stream(sw, v[i]);
  }
  sw << "]";
  return sw;
}

// tstring versions of to_string functions - efficient string handling
tinyusdz::tstring to_tstring(bool v) {
  return tinyusdz::tstring(v ? "true" : "false");
}

tinyusdz::tstring to_tstring(int32_t v) {
  char buf[16];
  int len = snprintf(buf, sizeof(buf), "%d", v);
  if (len > 0 && len < sizeof(buf)) {
    return tinyusdz::tstring(buf);
  }
  return tinyusdz::tstring();
}

tinyusdz::tstring to_tstring(uint32_t v) {
  char buf[16];
  int len = snprintf(buf, sizeof(buf), "%u", v);
  if (len > 0 && len < sizeof(buf)) {
    return tinyusdz::tstring(buf);
  }
  return tinyusdz::tstring();
}

tinyusdz::tstring to_tstring(int64_t v) {
  char buf[24];
  int len = snprintf(buf, sizeof(buf), "%" PRId64, v);
  if (len > 0 && len < sizeof(buf)) {
    return tinyusdz::tstring(buf);
  }
  return tinyusdz::tstring();
}

tinyusdz::tstring to_tstring(uint64_t v) {
  char buf[24];
  int len = snprintf(buf, sizeof(buf), "%" PRIu64, v);
  if (len > 0 && len < sizeof(buf)) {
    return tinyusdz::tstring(buf);
  }
  return tinyusdz::tstring();
}

tinyusdz::tstring to_tstring(float v) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%g", v);
  if (len > 0 && len < sizeof(buf)) {
    return tinyusdz::tstring(buf);
  }
  return tinyusdz::tstring();
}

tinyusdz::tstring to_tstring(double v) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%g", v);
  if (len > 0 && len < sizeof(buf)) {
    return tinyusdz::tstring(buf);
  }
  return tinyusdz::tstring();
}

// Vector type tstring conversions - delegate to std::string version for now
tinyusdz::tstring to_tstring(const value::char2 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::char3 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::char4 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::short2 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::short3 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::short4 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::int2 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::int3 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::int4 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::uint2 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::uint3 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::uint4 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::float2 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::float3 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::float4 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::double2 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::double3 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::double4 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::texcoord2h &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::texcoord2f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::texcoord2d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::texcoord3h &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::texcoord3f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::texcoord3d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::StringData &s) {
  return tinyusdz::tstring(to_string(s));
}

tinyusdz::tstring to_tstring(const value::token &s) {
  return tinyusdz::tstring(to_string(s));
}

tinyusdz::tstring to_tstring(const std::string &s) {
  return tinyusdz::tstring(to_string(s));
}

tinyusdz::tstring to_tstring(const value::quath &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::quatf &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::quatd &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::matrix2f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::matrix3f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::matrix4f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::matrix2d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::matrix3d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::matrix4d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::frame4d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::half &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::half2 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::half3 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::half4 &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::normal3h &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::normal3f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::normal3d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::vector3h &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::vector3f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::vector3d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::point3h &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::point3f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::point3d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::color3f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::color3d &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::color4h &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::color4f &v) {
  return tinyusdz::tstring(to_string(v));
}

tinyusdz::tstring to_tstring(const value::color4d &v) {
  return tinyusdz::tstring(to_string(v));
}

// Template array function implementation
template <typename T>
tinyusdz::tstring to_tstring_array(const std::vector<T> &v, size_t N) {
  // Use ChunkedStreamWriter for efficient array formatting
  ChunkedStreamWriter sw;
  sw << "[";

  if ((N == 0) || ((N * 2) >= v.size())) {
    for (size_t i = 0; i < v.size(); i++) {
      if (i > 0) {
        sw << ", ";
      }
      sw << v[i];
    }
  } else {
    size_t head_end = (std::min)(N, v.size());
    size_t tail_start = (std::max)(v.size() - N, head_end);

    for (size_t i = 0; i < head_end; i++) {
      if (i > 0) {
        sw << ", ";
      }
      sw << v[i];
    }

    sw << ", ..., ";

    for (size_t i = tail_start; i < v.size(); i++) {
      if (i > tail_start) {
        sw << ", ";
      }
      sw << v[i];
    }
  }
  sw << "]";

  // Convert to tstring - ensure null termination
  std::vector<uint8_t> data = sw.flatten();
  data.push_back('\0'); // Ensure null termination
  return tinyusdz::tstring(reinterpret_cast<const char*>(data.data()));
}

// Explicit template instantiations for common types
template tinyusdz::tstring to_tstring_array<float>(const std::vector<float> &v, size_t N);
template tinyusdz::tstring to_tstring_array<double>(const std::vector<double> &v, size_t N);
template tinyusdz::tstring to_tstring_array<int32_t>(const std::vector<int32_t> &v, size_t N);
template tinyusdz::tstring to_tstring_array<uint32_t>(const std::vector<uint32_t> &v, size_t N);
template tinyusdz::tstring to_tstring_array<int64_t>(const std::vector<int64_t> &v, size_t N);
template tinyusdz::tstring to_tstring_array<uint64_t>(const std::vector<uint64_t> &v, size_t N);

#endif // TINYUSDZ_CHUNKED_SW

}  // namespace tinyusdz
