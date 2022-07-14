// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
#pragma once

#include <string>
#include <unordered_set>

#include "crate-format.hh"
#include "stream-reader.hh"

namespace tinyusdz {
namespace crate {

///
/// Crate(binary data) reader
///
class Reader {
 private:
  Reader() = delete;

 public:
  Reader(StreamReader *sr, int num_threads = -1);
  ~Reader();

  bool ReadBootStrap();
  bool ReadTOC();

  ///
  /// Read TOC section
  ///
  bool ReadSection(crate::Section *s);

  // Read known sections
  bool ReadPaths();
  bool ReadTokens();
  bool ReadStrings();
  bool ReadFields();
  bool ReadFieldSets();
  bool ReadSpecs();

  bool BuildLiveFieldSets();

  std::string GetError();
  std::string GetWarning();

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const;

  /// -------------------------------------
  /// Following Methods are valid after successfull parsing of Crate data.
  ///

  const value::token GetToken(crate::Index token_index);
  const value::token GetToken(crate::Index token_index) const;

  size_t NumPaths();
  Path GetPath(crate::Index index);

 private:
  class Impl;
  Impl *_impl;
};

}  // namespace crate
}  // namespace tinyusdz
