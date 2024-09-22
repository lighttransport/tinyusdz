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

#include "tiny-container.hh"

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
struct tstring {
  static_assert(N >= 8, "N must be 8 or larger.");

 public:

  tstring() {}
  ~tstring() {
    _delete_string();
  }

  tstring(const char *s) { 
    _copy_string(s);
  }

  tstring(const std::string &s) : tstring(s.c_str()) { 
  }

  tstring(const tstring &rhs) : tstring(rhs.c_str()) {
  }

  tstring(tstring &&rhs) {

    _delete_string();
    
    _buf = std::exchange(rhs._buf, nullptr);
    _len = std::exchange(rhs._len, 0);
  }

  tstring &operator=(const tstring &rhs) {
    if (this == &rhs) {
      return *this;
    }

    _copy_string(rhs.c_str());

    return *this;
  }

  tstring &operator=(tstring &&rhs) noexcept {
    if (this == &rhs) {
      return *this;
    }

    _delete_string();
    
    _buf = std::exchange(rhs._buf, nullptr);
    _len = std::exchange(rhs._len, 0);

    return *this;
  }

  const char *c_str() const {
    return reinterpret_cast<char *>(_buf);
  }

  size_t size() const {
    return _len;
  }

  std::string to_std_string() {
    char *p = reinterpret_cast<char *>(_buf);
    std::string s(p, _len);
    return s;
  }

 private:
  void _delete_string() {
    if (_len >= N) {
      char *p = reinterpret_cast<char *>(_buf);
      delete[] p;
    }
    memset(_buf, 0, 8);
    _len = 0;
  }

  void _copy_string(const char *s) {

    if (_len > 0) {
      if (_len >= N) {
        char *p = reinterpret_cast<char *>(_buf);
        delete[] p;
        memset(_buf, 0, 8);
      }
    }

    char *dst = reinterpret_cast<char *>(_buf);

    _len = strlen(s);    
    if (_len >= N) {
      dst = new char[_len+1];
    } else {
      memcpy(dst, s, _len);
    }
    dst[_len] = '\0';

  }

  // TODO: Ues custom vector class.
  //char *_s{nullptr}; // end with '\0'
  char _buf[N]{};
  size_t _len{0};
};

// just a retain the pointer address.
struct tstring_view {
 public:

  constexpr tstring_view() {}
  ~tstring_view() {
    _s = nullptr;
  }

  constexpr tstring_view(const char *s) { 
    _len = strlen(s);    
    _s = s;
  }

  constexpr tstring_view(const std::string &s) : tstring_view(s.c_str()) { 
  }

  const char *c_str() const {
    return _s;
  }

  size_t size() const {
    return _len;
  }

 private:
  // TODO: Ues custom vector class.
  const char *_s{nullptr}; // end with '\0'
  size_t _len{0};
};

namespace str {

bool parse_int(const tstring_view &sv, int32_t *ret);
bool parse_int64(const tstring_view &sv, int64_t *ret);

bool parse_uint(const tstring_view &sv, uint32_t *ret);
bool parse_uint64(const tstring_view &sv, uint64_t *ret);

bool parse_float(const tstring_view &sv, float *ret);
bool parse_double(const tstring_view &sv, double *ret);

bool parse_int_arary(const tstring_view &sv, std::vector<int32_t> *result, const char delimiter = ',');
bool parse_float_arary(const tstring_view &sv, std::vector<float> *result, const char delimiter = ',');
bool parse_double_arary(const tstring_view &sv, std::vector<double> *result, const char delimiter = ',');

bool print_float_array(std::vector<float> &v,
  std::string &dst, const char delimiter = ',');


}

} // namespace tinyusdz
