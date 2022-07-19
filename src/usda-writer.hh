#pragma once

#include "tinyusdz.hh"

namespace tinyusdz {
namespace usda {

///
/// Save scene as USDA(ASCII)
///
/// @param[in] filename USDA filename
/// @param[in] scene High-level scene class. 
/// @param[out] warn Warning message
/// @param[out] err Error message
///
/// @return true upon success.
///
bool SaveAsUSDA(const std::string &filename, const HighLevelScene &scene, std::string *warn, std::string *err);

} // namespace usda
} // namespace tinyusdz
