#include "tiny-string.hh"

#if defined(TINYUSDZ_USE_THREAD)
#include <thread>
#include <atomic>
#include <mutex>
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#define nssv_CONFIG_USR_SV_OPERATOR  0

// TODO(syoyo): Use C++17 std::string_view when compiled with C++-17 compiler

// clang and gcc
#if defined(__EXCEPTIONS) || defined(__cpp_exceptions)

#ifdef nsel_CONFIG_NO_EXCEPTIONS
#undef nsel_CONFIG_NO_EXCEPTIONS
#endif
#ifdef nssv_CONFIG_NO_EXCEPTIONS
#undef nssv_CONFIG_NO_EXCEPTIONS
#endif

#define nsel_CONFIG_NO_EXCEPTIONS 0
#define nssv_CONFIG_NO_EXCEPTIONS 0
#else
// -fno-exceptions
#if !defined(nsel_CONFIG_NO_EXCEPTIONS)
#define nsel_CONFIG_NO_EXCEPTIONS 1
#endif

#define nssv_CONFIG_NO_EXCEPTIONS 1
#endif
#include "nonstd/string_view.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

namespace str {

bool print_float_array(std::vector<float> &v,
  std::string &dst, const char delimiter) {


  // TODO
  (void)v;
  (void)dst;
  (void)delimiter;

  return false;
}


}


} // namespace tinyusdz
