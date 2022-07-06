// SPDX-License-Identifier: MIT
// Copyright 2020-Present Syoyo Fujita.
//
#pragma once

#include "prim-types.hh"
#include "crate-format.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"

namespace tinyusdz {
namespace usdc {

class Parser {
 public:
  Parser(StreamReader *sr, int num_threads);
  ~Parser();

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

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const;
 
  bool BuildLiveFieldSets();

  ///
  /// Following APIs are valid after successfull parsing of Crate data.
  ///
  size_t NumPaths();
  Path GetPath(crate::Index index);

  bool ReconstructScene(Scene *scene);

  std::string GetError();
  std::string GetWarning();

 private:
  class Impl;
  Impl *impl_;

};

} // namespace usdc
} // namespace tinyusdz
