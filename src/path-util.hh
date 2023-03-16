// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// Utility functions for Path

#include "prim-types.hh"

namespace tinyusdz {
namespace pathutil {

///
/// Validate path. `err` will be filled when Path is an invalid one.
///
bool ValidatePath(const Path &path, std::string *err);

///
/// Concatinate two Paths.
///
Path ConcatPath(const Path &parent, const Path &child);

///
/// Replace '../' and produce absolute path
///
/// base_prim_path: /root/xform
///
/// ../bora => /root/bora
/// ../../bora => /bora
/// ../../../bora => NG(return false)
///
/// TODO: `../` in the middle of relative path(e.g. `/root/../bora`
///
/// @return true upon success to resolve relative path.
/// @return false when input paths are invalid or cannot resolve relative path.
///
bool ResolveRelativePath(const Path &base_prim_path, const Path &relative_path, Path *abs_path);

///
/// Currently ToUnixishPath converts backslash character to forward slash character.
///
/// /home/tinyusdz => C:/Users/tinyusdz
/// C:\\Users\\tinyusdz => C:/Users/tinyusdz
///
Path ToUnixishPath(const Path &path);


} // namespace path
} // namespace tinyusdz
