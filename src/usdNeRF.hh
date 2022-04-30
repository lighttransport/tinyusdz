// SPDX-License-Identifier: MIT
// 
// NeRF model import plugIn.
// Import only.
//
// example usage 
//
// def NeRF "bunny" (
//   prepend references = @bunny.nerf@
// )
// {
//    ...
// }

#pragma once

#include "tinyusdz.hh"

namespace tinyusdz {
namespace usdNeRF {

bool ReadNeRFFromFile(const std::string &filepath, GPrim *prim, std::string *err = nullptr);

} // namespace usdNeRF
} // namespace tinyusdz
