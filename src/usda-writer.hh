#pragma once

#include "tinyusdz.hh"

namespace tinyusdz {

///
/// Save scene as USDA(ASCII)
///
bool SaveAsUSDA(const std::string &filename, const Scene &scene, std::string *warn, std::string *err);

} // namespace tinyusdz
