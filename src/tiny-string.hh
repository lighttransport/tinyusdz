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
#include <iostream> // dbg

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
    
    _buf = std::exchange(rhs._buf, nullptr);
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
    
    _buf = std::exchange(rhs._buf, nullptr);
    _len = std::exchange(rhs._len, 0);

    return *this;
  }

  const char *c_str() const {
    if (_len >= N) {
      return *reinterpret_cast<const char **>(&_buf);
    } else {
      return reinterpret_cast<const char *>(_buf);
    }
  }

  size_t size() const {
    return _len;
  }

  std::string to_std_string() {
    const char *p;
    if (_len < N) {
      p = reinterpret_cast<const char *>(_buf);
    } else {
      p = *reinterpret_cast<const char **>(&_buf);
    }
    std::string s(p, _len);
    return s;
  }

 private:
  void _delete_string() {
    if (_len >= N) {
      char **p = reinterpret_cast<char **>(&_buf);
      delete[] (*p);
    }
    memset(_buf, 0, 8);
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
    if (_len >= N) {
      char *dst = new char[_len+1];
      memcpy(dst, s, _len);
      dst[_len] = '\0';

      *(reinterpret_cast<char **>(&_buf[0])) = dst;
    } else {
      memcpy(_buf, s, _len);
    }

  }

  // TODO: Ues custom vector class.
  //char *_s{nullptr}; // end with '\0'
  char _buf[N+1]{}; // store pointer address when _len > N
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

  constexpr tstring_view(const char *s) { 
    _len = strlen(s);    
    _s = s;
  }

  constexpr tstring_view(const std::string &s) : tstring_view(s.c_str()) { 
  }

  constexpr tstring_view(const tstring &s) : tstring_view(s.c_str()) { 
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

  // C++20
  bool starts_with( tstring_view sv ) const noexcept
  {
    size_t sv_size = sv.size();

    std::cout << "len " << _len << ", sv_size " << sv_size << "\n";
    if (_len < sv_size) {
      return false;
    }
    const char *sv_str = sv.c_str();
    std::cout << "sv_str " << sv_str << "\n";

    for (size_t i = 0; i < sv_size; i++) {
      if (_s[i] != sv_str[i]) {
        return false;
      }
    }   
    return true;
  }

  bool starts_with( const char *s) const {
    size_t s_size = strlen(s);

    std::cout << "len " << _len << ", s_size " << s_size << "\n";

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

  bool ends_with( tstring_view sv ) const noexcept
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

  bool contains( tstring_view sv) const {
    return contains(sv.c_str());
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
