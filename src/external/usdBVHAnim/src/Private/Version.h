#pragma once

namespace usdBVHAnimPlugin {
// Concatenate the separate version components that have been defined by
// the build system into a single version string. Use a single STR macro
// to work around a bug in Doxygen, where it cannot concatenated string literals
#define STR(x) #x
#define VERSION_STRING(a, b, c) STR(a.b.c)
//! The version string for usdBVHAnimPlugin
char const* const c_ProjectVersion = VERSION_STRING(PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH);
} // namespace usdBVHAnimPlugin