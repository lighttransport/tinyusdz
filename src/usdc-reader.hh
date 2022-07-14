// SPDX-License-Identifier: MIT
// Copyright 2022-Present Syoyo Fujita.
//
#pragma once

//#include "prim-types.hh"
//#include "crate-format.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"

namespace tinyusdz {
namespace usdc {

///
/// USDC(Crate) reader
///
class Reader {
 public:
  Reader(StreamReader *sr, int num_threads = -1) ;
  ~Reader();

  bool ReadUSDC();

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const;

  bool ReconstructScene(Scene *scene);

  std::string GetError();
  std::string GetWarning();

 private:
  class Impl;
  Impl *impl_;

};

} // namespace usdc
} // namespace tinyusdz
