// SPDX-License-Identifier: MIT
//
// Built-in MaterialX XML import plugIn.
// Import only. Export is not supported(yet).
//
// example usage
//
// def "mesh" (
//   prepend references = @bunny.obj@
// )
// {
//    ...
// }

#pragma once

#include <string>

namespace tinyusdz {
namespace usdMtlx {

class UsdMtlx
{
  // TODO
};

bool ReadMaterialXFromString(const std::string &str, UsdMtlx *mtlx, std::string *err = nullptr);
bool ReadMaterialXFromFile(const std::string &filepath, UsdMtlx *mtlx, std::string *err = nullptr);


} // namespace usdMtlx
} // namespace tinyusdz
