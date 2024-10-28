// SPDX-License-Identifier: MIT
// 
// Built-in .ply import plugIn.
// Import only. Writing scene data as .ply is not supported.
//
// example usage 
//
// def "points" (
//   prepend references = @bunny.ply@
// )
// {
//    ...
// }

#pragma once

#include <string>

//#include "tinyusdz.hh"

class GPrim;

namespace tinyusdz {

namespace usdPly {

bool ReadPlyFromString(const std::string &str, GPrim *prim, std::string *err = nullptr);
bool ReadPlyFromFile(const std::string &filepath, GPrim *prim, std::string *err = nullptr);

} // namespace usdPly

} // namespace tinyusdz
