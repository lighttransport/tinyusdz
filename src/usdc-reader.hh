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
  uint32_t kMaxPrimNestLevel = 256;
  uint32_t kMaxFieldValuePairs = 4096;
  uint32_t kMaxTokenLength = 4096; // Max length of `token`
  uint32_t kMaxStringLength = 1024*1024*64; // Max length of `string` data
  uint32_t kMaxElementSize = 512; // Max allowed value for `elementSize`
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
