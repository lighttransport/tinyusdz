#pragma once

#include "tinyusdz.hh"

namespace tinyusdz {
namespace usdc {

///
/// Save scene as USDC(binary)
///
/// @param[in] filename USDC filename
/// @param[in] scene Scene
/// @param[out] warn Warning message
/// @param[out] err Error message
///
/// @return true upon success.
///
bool SaveAsUSDC(const std::string &filename, const Scene &scene, std::string *warn, std::string *err);

} // namespace usdc
} // namespace tinyusdz
