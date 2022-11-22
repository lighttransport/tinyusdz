#pragma once

// utility to apply function to a Prim.
// Internal use expected(not intended for Public Tydra API).

#include <functional>

namespace tinyusdz {

// forward decl.
class Stage;
class Prim;
struct GPrim;

namespace tydra {

bool ApplyToGPrim(
  const Stage &stage, const Prim &prim,
  std::function<bool(const Stage &stage, const GPrim *gprim)> fn);

} // namespace tydra
} // namespace tinyusdz
