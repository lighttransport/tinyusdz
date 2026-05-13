// SPDX-License-Identifier: MIT
// Copyright 2024-Present Syoyo Fujita.

///
/// Simple but fast string library.
///

#include <vector>
#include <string>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "tiny-container.hh"
#include "value-types.hh"

namespace tinyusdz {

// default: Up to 1G char
static size_t strlen(const char *s, size_t max_len = 1024u*1024u*1024u) {
  if (!s) {
    return 0;
  }

  size_t i = 0;
  while(i < max_len) {
    if (s[i] == '\0') {
      return i;
    }
    i++;
  }

  return i;
}


template<size_t N = 8>
struct tstring_n {
  // 8 = enough size to store pointer address
  static_assert(N >= 8, "N must be 8 or larger.");

 public:

  tstring_n() {}
  ~tstring_n() {
    _delete_string();
  }

  tstring_n(const char *s) { 
    _copy_string(s);
  }

  tstring_n(const std::string &s) : tstring_n(s.c_str()) { 
  }

  tstring_n(const tstring_n &rhs) : tstring_n(rhs.c_str()) {
  }

  tstring_n(tstring_n &&rhs) {

    _delete_string();
    
    _u = std::exchange(rhs._u, nullptr);
    _len = std::exchange(rhs._len, 0);
  }

  tstring_n &operator=(const tstring_n &rhs) {
    if (this == &rhs) {
      return *this;
    }

    _copy_string(rhs.c_str());

    return *this;
  }

  tstring_n &operator=(tstring_n &&rhs) noexcept {
    if (this == &rhs) {
      return *this;
    }

    _delete_string();
    
    _u = std::exchange(rhs._u, nullptr);
    _len = std::exchange(rhs._len, 0);

    return *this;
  }

  const char *c_str() const {
    if (_len > N) {
      return reinterpret_cast<const char *>(_u._ptr);
    } else {
      return reinterpret_cast<const char *>(_u._buf);
    }
  }

  size_t size() const {
    return _len;
  }

  std::string to_std_string() {
    const char *p;
    if (_len > N) {
      p = reinterpret_cast<const char *>(_u._ptr);
    } else {
      p = reinterpret_cast<const char *>(_u._buf);
    }
    std::string s(p, _len);
    return s;
  }

 private:
  void _delete_string() {
    if ((_len > N) && (_u._ptr)) {
      delete[] _u._ptr;
    }
    memset(_u._buf, 0, 8);
    _len = 0;
  }

  void _copy_string(const char *s) {

    _delete_string();

    //if (_len > 0) {
    //  if (_len >= N) {
    //    //char *p = reinterpret_cast<char *>(_buf);
    //    //delete[] p;
    //    //memset(_buf, 0, 8);
    //  }
    //}

    //char *dst = reinterpret_cast<char *>(_buf);

    _len = strlen(s);    
    if (_len > N) {
      char *dst = new char[_len+1];
      memcpy(dst, s, _len);
      dst[_len] = '\0';

      _u._ptr = dst;
    } else {
      memcpy(_u._buf, s, _len);
      _u._buf[_len] = '\0';
    }

  }

  // TODO: Ues custom vector class.
  union {
    char _buf[N+1]{};
    char *_ptr;
  } _u;

  size_t _len{0};
};

using tstring = tstring_n<>;

// just a retain the pointer address.
struct tstring_view {
 public:

  constexpr tstring_view() {}
  ~tstring_view() {
    _s = nullptr;
  }

  tstring_view(const char *s) { 
    _len = strlen(s);    
    _s = s;
  }

  constexpr tstring_view(const char *s, size_t n) { 
    _len = n;
    _s = s;
  }

  tstring_view(const std::string &s) : tstring_view(s.c_str()) { 
  }

  tstring_view(const tstring &s) : tstring_view(s.c_str()) { 
  }

  bool operator==(const tstring_view &rhs) {
    if (_len != rhs.size()) {
      return false;
    }

    for (size_t i = 0; i < _len; i++) {
      if (_s[i] != rhs.c_str()[i]) {
        return false;
      }
    }   
    
    return true;
  }

  bool operator!=(const tstring_view &rhs) {
    return !(*this == rhs);
  }

  const char *c_str() const {
    return _s;
  }

  size_t size() const {
    return _len;
  }

  // C++20-like API
  bool starts_with( const tstring_view &sv ) const noexcept
  {
    size_t sv_size = sv.size();

    if (_len < sv_size) {
      return false;
    }
    const char *sv_str = sv.c_str();

    for (size_t i = 0; i < sv_size; i++) {
      if (_s[i] != sv_str[i]) {
        return false;
      }
    }   
    return true;
  }

  bool starts_with( const char *s) const {
    size_t s_size = strlen(s);

    if (_len < s_size) {
      return false;
    }
    for (size_t i = 0; i < s_size; i++) {
      if (_s[i] != s[i]) {
        return false;
      }
    }   
    return true;
  }

  bool ends_with( const tstring_view &sv ) const noexcept
  {
    size_t sv_size = sv.size();

    if (_len < sv_size) {
      return false;
    }
    const char *sv_str = sv.c_str();

    for (size_t i = 0; i < sv_size; i++) {
      if (_s[_len - i - 1] != sv_str[sv_size - i - 1]) {
        return false;
      }
    }   
    return true;
  }

  bool ends_with( const char *s) const {
    size_t s_size = strlen(s);

    if (_len < s_size) {
      return false;
    }
    for (size_t i = 0; i < s_size; i++) {
      if (_s[_len - i - 1] != s[s_size - i - 1]) {
        return false;
      }
    }   
    return true;
  }

  bool contains( const char *s) const {
    size_t s_size = strlen(s);
    if (s_size == 0) {
      return false;
    }

    if (_len < s_size) {
      return false;
    }

    size_t i_s{0};
    for (size_t i = 0; i < _len; i++) {
      if (_s[i] == s[0]) {
        i_s = i;
        break;
      }
    }

    if (_len < i_s + s_size) {
      return false; 
    }

    for (size_t i = i_s; i < i_s + s_size; i++) {
      if (_s[i] != s[i - i_s]) {
        return false;
      }
    }   

    return true;
  }

  bool contains( const tstring_view &sv) const {
    return contains(sv.c_str());
  }

 private:
  // TODO: Ues custom vector class.
  const char *_s{nullptr}; // end with '\0'
  size_t _len{0};
};

// Simple std::ostringstream like class
class tostringstream
{
 public:
    
    tostringstream &operator<<( const tstring &str );
    tostringstream &operator<<( const tstring_view &str );

    void write(const char *p, const size_t n);

    uint64_t size() const {
      return binary_.size();
    }

    std::string str() const;
    tstring tstr() const;

    
    const char *data() const { return binary_.data(); }

 private:
  const std::vector<char> binary_;
  mutable uint64_t idx_{0};
};

namespace str {

bool parse_int(const tstring_view &sv, int32_t *ret);
bool parse_int64(const tstring_view &sv, int64_t *ret);

bool parse_uint(const tstring_view &sv, uint32_t *ret);
bool parse_uint64(const tstring_view &sv, uint64_t *ret);

bool parse_float(const tstring_view &sv, float *ret);
bool parse_double(const tstring_view &sv, double *ret);

bool parse_int_array(const tstring_view &sv, std::vector<int32_t> *result);
bool parse_half_array(const tstring_view &sv, std::vector<tinyusdz::value::half> *result);
bool parse_float_array(const tstring_view &sv, std::vector<float> *result);
bool parse_double_array(const tstring_view &sv, std::vector<double> *result);
bool parse_token_array(const tstring_view &sv, std::vector<tinyusdz::value::token> *result);
bool parse_string_array(const tstring_view &sv, std::vector<tinyusdz::value::StringData> *result);
bool parse_std_string_array(const tstring_view &sv, std::vector<std::string> *result);

// Compound-type array parsers
bool parse_half2_array(const tstring_view &sv, std::vector<tinyusdz::value::half2> *result);
bool parse_half3_array(const tstring_view &sv, std::vector<tinyusdz::value::half3> *result);
bool parse_half4_array(const tstring_view &sv, std::vector<tinyusdz::value::half4> *result);
bool parse_float2_array(const tstring_view &sv, std::vector<tinyusdz::value::float2> *result);
bool parse_float3_array(const tstring_view &sv, std::vector<tinyusdz::value::float3> *result);
bool parse_float4_array(const tstring_view &sv, std::vector<tinyusdz::value::float4> *result);
bool parse_point3f_array(const tstring_view &sv, std::vector<tinyusdz::value::point3f> *result);
bool parse_normal3f_array(const tstring_view &sv, std::vector<tinyusdz::value::normal3f> *result);
bool parse_double2_array(const tstring_view &sv, std::vector<tinyusdz::value::double2> *result);
bool parse_double3_array(const tstring_view &sv, std::vector<tinyusdz::value::double3> *result);
bool parse_double4_array(const tstring_view &sv, std::vector<tinyusdz::value::double4> *result);
bool parse_quath_array(const tstring_view &sv, std::vector<tinyusdz::value::quath> *result);
bool parse_quatf_array(const tstring_view &sv, std::vector<tinyusdz::value::quatf> *result);
bool parse_quatd_array(const tstring_view &sv, std::vector<tinyusdz::value::quatd> *result);
bool parse_matrix2f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix2f> *result);
bool parse_matrix3f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix3f> *result);
bool parse_matrix4f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix4f> *result);
bool parse_matrix2d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix2d> *result);
bool parse_matrix3d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix3d> *result);
bool parse_matrix4d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix4d> *result);

}

} // namespace tinyusdz
