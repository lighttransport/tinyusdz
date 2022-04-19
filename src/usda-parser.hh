// SPDX-License-Identifier: MIT
#pragma once

#include "tinyusdz.hh"
#include "stream-reader.hh"

namespace tinyusdz {

namespace usda {

enum LoadState {
  LOAD_STATE_TOPLEVEL,   // toplevel .usda input
  LOAD_STATE_SUBLAYER,   // .usda is read by 'subLayers'
  LOAD_STATE_REFERENCE,  // .usda is read by `references`
  LOAD_STATE_PAYLOAD,    // .usda is read by `payload`
};


///
/// Test if input file is USDA format.
///
bool IsUSDA(const std::string &filename, size_t max_filesize = 0);

class USDAParser {
 public:
  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  USDAParser() = delete;
  USDAParser(tinyusdz::StreamReader *sr);

  USDAParser(const USDAParser &rhs) = delete;
  USDAParser(USDAParser &&rhs) = delete;

  ~USDAParser();


  ///
  /// Base filesystem directory to search asset files.
  ///
  void SetBaseDir(const std::string &base_dir);

  ///
  /// Check if header data is USDA
  ///
  bool CheckHeader();

  ///
  /// Parser entry point
  ///
  bool Parse(LoadState state = LOAD_STATE_TOPLEVEL);

  ///
  ///
  ///
  std::string GetDefaultPrimName() const;

  ///
  /// Get parsed toplevel "def" nodes(GPrim)
  ///
  std::vector<GPrim> GetGPrims();

  ///
  /// Get error message(when `Parse` failed)
  ///
  std::string GetError();

  /// 
  /// Get as scene
  ///
  const Scene& GetScene() const;
  
 private:
  class Impl;
  Impl *_impl;

};

} // namespace usda

} // namespace tinyusdz
