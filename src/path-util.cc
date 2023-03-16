// SPDX-License-Identifier: Apache 2.0
// Copyright 2023-Present Light Transport Entertainment, Inc.

#include "path-util.hh"
#include "str-util.hh"
#include "prim-types.hh"
#include "common-macros.inc"

namespace tinyusdz {
namespace pathutil {

namespace {

// Remove sequential "../"
// Returns the number of "../" occurence to `num`
std::string RemoveRelativePrefix(const std::string &in_str, size_t &num) {
  constexpr size_t maxnum = 1024*1024;

  num = 0;
  std::string ret = in_str;
  while (!ret.empty() || (num < maxnum)) {
    if (startsWith(ret, "../")) {
      ret = removePrefix(ret, "../");
      num++;
    } else {
      break;
    }
  }

  return ret;
}

} // namespace

bool ResolveRelativePath(const Path &base_prim_path, const Path &relative_path, Path *abs_path) {

  if (!abs_path) {
    return false;
  }

  std::string relative_str = relative_path.prim_part();
  std::string base_str = base_prim_path.prim_part();

  if (startsWith(base_str, "/")) {
    // ok
  } else {
    return false;
  }

  std::string abs_dir;

  if (startsWith(relative_str, "./")) {

    std::string remainder = removePrefix(relative_str, "./");

    // "./../", "././", etc is not allowed at the moment.
    if (contains_str(remainder, ".")) {
      return false;
    } 

    abs_dir = base_str + "/" + remainder;
    
  } else if (startsWith(relative_str, "../")) {
    // ok
    size_t ndepth{0};
    std::string remainder = RemoveRelativePrefix(relative_str, ndepth);
    DCOUT("remainder = " << remainder << ", ndepth = " << ndepth);

    // "../" in subsequent position(e.g. `../bora/../dora`) is not allowed at the moment.
    if (contains_str(remainder, ".")) {
      return false;
    } 

    std::vector<std::string> base_dirs = split(base_str, "/");
    DCOUT("base_dirs.len = " << base_dirs.size());
    //if (base_dirs.size() < ndepth) {
    //  return false;
    //}

    if (base_dirs.size() == 0) { // "/"
      abs_dir = "/" + remainder;
    } else {
      int64_t n = int64_t(base_dirs.size()) - int64_t(ndepth);

      if (n <= 0) {
        abs_dir += "/" + remainder;
      } else {
        for (size_t i = 0; i < size_t(n); i++) {
          abs_dir += "/" + base_dirs[i];
        }
        abs_dir += "/" + remainder;
    
      }
    }
  } else {
    return false;
  }
  
  (*abs_path) = Path(abs_dir, relative_path.prop_part());

  // TODO
  return true;
}

} // namespace pathutil
} // namespace tinyusdz
