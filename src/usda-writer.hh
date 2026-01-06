///
/// @file usda-writer.hh  
/// @brief USDA (USD ASCII) writer interface
///
/// Production-ready writer for exporting USD scenes to ASCII format.
/// Supports full round-trip preservation of USD data structures.
///
#pragma once

#include "tinyusdz.hh"

namespace tinyusdz {
namespace usda {

///
/// Save scene as USDA(ASCII)
///
/// @param[in] filename USDA filename(UTF-8). WideChar(Unicode) represented as std::string is supported on Windows.
/// @param[in] stage Stage(scene graph).
/// @param[out] warn Warning message
/// @param[out] err Error message
///
/// @return true upon success.
///
bool SaveAsUSDA(const std::string &filename, const Stage &stage, std::string *warn, std::string *err);

#if defined(_WIN32)
// WideChar(UNICODE) filename version.
bool SaveAsUSDA(const std::wstring &filename, const Stage &stage, std::string *warn, std::string *err);
#endif

} // namespace usda
} // namespace tinyusdz
