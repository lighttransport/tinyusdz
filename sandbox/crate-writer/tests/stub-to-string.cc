// Stub implementations for to_string functions and Section constructor
#include <string>
#include <set>
#include <cstring>

// Include necessary headers for actual type definitions
#include "crate-format.hh"
#include "xform.hh"
#include "core/prim.hh"
#include "core/prim-enums.hh"

namespace tinyusdz {

// Stub implementations for to_string (to avoid pprinter.cc dependency)
std::string to_string(const XformOp::OpType &ty) {
  return "XformOp";  // Stub
}

std::string to_string(Kind k) {
  return "Kind";  // Stub
}

// Note: makeUniqueName is defined in str-util.cc, no need to stub it

// Section constructor implementation (missing from TinyUSDZ codebase)
namespace crate {
Section::Section(char const *n, int64_t s, int64_t sz) : start(s), size(sz) {
  memset(name, 0, sizeof(name));
  if (n) {
    strncpy(name, n, sizeof(name) - 1);
  }
}
} // namespace crate

} // namespace tinyusdz
