#pragma once

// utility to apply function to a Prim.
// Internal use expected(not intended for Public Tydra API).

#include <functional>

namespace tinyusdz {

// forward decl.
class Stage;
class Prim;
class MaterialBinding;
struct GPrim;
struct Xformable;

namespace tydra {

bool ApplyToGPrim(
  const Stage &stage, const Prim &prim,
  std::function<bool(const Stage &stage, const GPrim *gprim)> fn);

//
// Prim which inherits MaterialBinding, i.e, GPrim and GeomSubset.
// 
bool ApplyToMaterialBinding(
  const Stage &stage, const Prim &prim,
  std::function<bool(const Stage &stage, const MaterialBinding *mb)> fn);

bool ApplyToXformable(
  const Stage &stage, const Prim &prim,
  std::function<bool(const Stage &stage, const Xformable *xformable)> fn);

} // namespace tydra
} // namespace tinyusdz
