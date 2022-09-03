// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
// 
// Asset Resolution utilities
// https://graphics.pixar.com/usd/release/api/ar_page_front.html
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyusdz {

///
/// For easier language bindings(e.g. for C), we use simple callback function approach.
///
typedef int (*FSReadFile)(const char *filename, uint8_t **bytes, uint64_t *size, std::string *err);

// To avoid a confusion with Argumented Reality, we don't use Abbreviation `Ar` ;-)
class AssetResolutionResolver
{
 public:
  static void SetDefaultSearchPath(const std::string &p);

  ///
  /// Register user defined filesystem handler.
  ///
  void RegisterFilesystemHandler(FSReadFile readFn);

 private:

 static std::vector<std::string> search_paths;
};


} // namespace tinyusdz 

