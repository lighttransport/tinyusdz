// SPDX-License-Identifier: MIT
// Copyright 2022-Present Syoyo Fujita.
//
#pragma once

#include "stream-reader.hh"
#include "tinyusdz.hh"

namespace tinyusdz {
namespace usdc {

///
/// USDC(Crate) reader
///

struct USDCReaderConfig {
  int32_t numThreads = -1; // -1 = use system's # of threads
  uint32_t kMaxFieldValuePairs = 4096;
};

class USDCReader {
 public:
  USDCReader(StreamReader *sr,
             const USDCReaderConfig &config = USDCReaderConfig());
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
