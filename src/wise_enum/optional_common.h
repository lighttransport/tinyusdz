#pragma once

#ifdef WISE_ENUM_NO_EXCEPT

#include <cstdlib>
#define WISE_ENUM_OPTIONAL_BAD_ACCESS std::abort()

#else

#include <stdexcept>

namespace wise_enum {

struct bad_optional_access : std::exception {
  const char *what() const noexcept override {
    return "Error, attempt to access valueless optional!";
  }
};
} // namespace wise_enum

#define WISE_ENUM_OPTIONAL_BAD_ACCESS throw bad_optional_access{}

#endif
