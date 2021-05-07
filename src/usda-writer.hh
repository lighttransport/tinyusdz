#pragma once

#include "tinyusdz.hh"

namespace tinyusdz {
namespace usda {

///
/// Save scene as USDA(ASCII)
///
/// @param[in] filename USDA filename
/// @param[in] scene Scene
/// @param[out] warn Warning message
/// @param[out] err Error message
///
/// @return true upon success.
///
bool SaveAsUSDA(const std::string &filename, const Scene &scene, std::string *warn, std::string *err);

} // namespace usda
} // namespace tinyusdz
