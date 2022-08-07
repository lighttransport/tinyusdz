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
class USDCReader {
 public:
  USDCReader(StreamReader *sr, int num_threads = -1);
  ~USDCReader();

  bool ReadUSDC();

  bool ReconstructStage(Stage *stage);

  // Approximated memory usage in [mb]
  size_t GetMemoryUsage() const;

  std::string GetError();
  std::string GetWarning();

 private:
  class Impl;
  Impl *impl_{};
};

}  // namespace usdc
}  // namespace tinyusdz
