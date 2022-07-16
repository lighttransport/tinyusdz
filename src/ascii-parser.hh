// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// USD ASCII parser

#pragma once

#include "tinyusdz.hh"
#include "stream-reader.hh"

namespace tinyusdz {

namespace ascii {

enum LoadState {
  LOAD_STATE_TOPLEVEL,   // toplevel .usda input
  LOAD_STATE_SUBLAYER,   // .usda is read by 'subLayers'
  LOAD_STATE_REFERENCE,  // .usda is read by `references`
  LOAD_STATE_PAYLOAD,    // .usda is read by `payload`
};


///
/// Test if input file is USDA ascii format.
///
bool IsUSDA(const std::string &filename, size_t max_filesize = 0);

class AsciiParser {
 public:
  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  AsciiParser();
  AsciiParser(tinyusdz::StreamReader *sr);

  AsciiParser(const AsciiParser &rhs) = delete;
  AsciiParser(AsciiParser &&rhs) = delete;

  ~AsciiParser();


  ///
  /// Base filesystem directory to search asset files.
  ///
  void SetBaseDir(const std::string &base_dir);

  ///
  /// Set ASCII data stream
  ///
  void SetStream(tinyusdz::StreamReader *sr);

  ///
  /// Check if header data is USDA
  ///
  bool CheckHeader();

  ///
  /// Parser entry point
  ///
  bool Parse(LoadState state = LOAD_STATE_TOPLEVEL);

#if 0
  ///
  ///
  ///
  std::string GetDefaultPrimName() const;

  ///
  /// Get parsed toplevel "def" nodes(GPrim)
  ///
  std::vector<GPrim> GetGPrims();
#endif

  ///
  /// Get error message(when `Parse` failed)
  ///
  std::string GetError();

  ///
  /// Get warning message(warnings in `Parse`)
  ///
  std::string GetWarning();

#if 0
  ///
  /// Get as scene
  ///
  const HighLevelScene& GetHighLevelScene() const;
#endif

 private:
  class Impl;
  Impl *_impl;

};

} // namespace ascii

} // namespace tinyusdz
