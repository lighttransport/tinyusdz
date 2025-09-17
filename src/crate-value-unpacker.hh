// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Value unpacking operations for Crate reader
#pragma once

#include <vector>
#include <string>
#include "crate-format.hh"
#include "stream-reader.hh"
#include "value-types.hh"
#include "nonstd/expected.hpp"
#include "memory-budget.hh"

namespace tinyusdz {
namespace crate {

// Forward declarations
class CrateReader;

class CrateValueUnpacker {
 public:
  CrateValueUnpacker(CrateReader* reader, StreamReader* sr, MemoryBudgetManager& memory_manager)
      : _reader(reader), _sr(sr), memory_manager_(memory_manager) {}

  // Main unpacking functions
  bool UnpackInlinedValueRep(const crate::ValueRep& rep,
                             crate::CrateValue* value,
                             std::string* err);
  
  bool UnpackValueRep(const crate::ValueRep& rep,
                     crate::CrateValue* value,
                     std::string* err);
  
  // Array value unpacking
  nonstd::expected<bool, std::string> UnpackArrayValue(
      CrateDataTypeId dty, 
      crate::CrateValue* value_out);

  // Error handling
  void PushError(const std::string& msg) { _err += msg + "\n"; }
  std::string GetError() const { return _err; }

 private:
  CrateReader* _reader;
  StreamReader* _sr;
  MemoryBudgetManager& memory_manager_;
  std::string _err;
  
  // Helper methods for specific value types
  bool UnpackDictionary(crate::CrateValue* value);
  bool UnpackTimeSamples(crate::CrateValue* value);
  bool UnpackPathVector(crate::CrateValue* value);
  bool UnpackVariantSelectionMap(crate::CrateValue* value);
  bool UnpackCustomData(crate::CrateValue* value);
  
  // Array unpacking helpers
  template <typename T>
  bool UnpackNumericArray(CrateDataTypeId dty, bool is_compressed, 
                          crate::CrateValue* value);
  
  bool UnpackStringArray(crate::CrateValue* value);
  bool UnpackTokenArray(crate::CrateValue* value);
};

}  // namespace crate
}  // namespace tinyusdz