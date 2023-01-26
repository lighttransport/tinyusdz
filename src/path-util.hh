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
/// Currently ToUnixishPath converts backslash character to forward slash character.
///
/// /home/tinyusdz => C:/Users/tinyusdz
/// C:\\Users\\tinyusdz => C:/Users/tinyusdz
///
Path ToUnixishPath(const Path &path);

} // namespace path
} // namespace tinyusdz
