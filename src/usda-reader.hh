// SPDX-License-Identifier: MIT
#pragma once

#include <math.h>
#include "tinyusdz.hh"
#include "stream-reader.hh"

#include "ascii-parser.hh"

namespace tinyusdz {

namespace usda {

//enum LoadState {
//  LOAD_STATE_TOPLEVEL,   // toplevel .usda input
//  LOAD_STATE_SUBLAYER,   // .usda is read by 'subLayers'
//  LOAD_STATE_REFERENCE,  // .usda is read by `references`
//  LOAD_STATE_PAYLOAD,    // .usda is read by `payload`
//};


///
/// Test if input file is USDA format.
///
bool IsUSDA(const std::string &filename, size_t max_filesize = 0);

class USDAReader {
 public:
  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  USDAReader() = delete;
  USDAReader(tinyusdz::StreamReader *sr);

  USDAReader(const USDAReader &rhs) = delete;
  USDAReader(USDAReader &&rhs) = delete;

  ~USDAReader();


  ///
  /// Base filesystem directory to search asset files.
  ///
  void SetBaseDir(const std::string &base_dir);

  ///
  /// Check if header data is USDA
  ///
  bool CheckHeader();

  ///
  /// Reader entry point
  ///
  bool Read(ascii::LoadState state = ascii::LoadState::TOPLEVEL);

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
  /// Get warning message(warnings in `Parse`)
  ///
  std::string GetWarning();

  ///
  /// Reconstruct Stage from loaded USD scene data.
  /// Must be called after `Read`
  ///
  bool ReconstructStage();

  ///
  /// Get as stage(scene graph). Must call `ReconstructStage` beforehand.
  ///
  const Stage& GetStage() const;

 private:
#if defined(TINYUSDZ_DISABLE_MODULE_USDA_READER)
  Stage *_empty_stage{nullptr};
#else
  class Impl;
  Impl *_impl{nullptr};
#endif

};

} // namespace usda

} // namespace tinyusdz
