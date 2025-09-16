// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Basic I/O operations for Crate reader
#pragma once

#include <string>
#include <vector>
#include "crate-format.hh"
#include "stream-reader.hh"
#include "value-types.hh"
#include "prim-types.hh"
#include "memory-budget.hh"

namespace tinyusdz {
namespace crate {

class CrateIOHelper {
 public:
  CrateIOHelper(StreamReader* sr, MemoryBudget& memory_manager)
      : _sr(sr), memory_manager_(memory_manager) {}

  // Basic reading operations
  bool ReadIndex(crate::Index* i);
  bool ReadIndices(std::vector<crate::Index>* indices);
  bool ReadString(std::string* s);
  bool ReadValueRep(crate::ValueRep* rep);
  
  // Reference and payload reading
  bool ReadReference(Reference* d);
  bool ReadPayload(Payload* d);
  bool ReadLayerOffset(LayerOffset* d);
  bool ReadLayerOffsetArray(std::vector<LayerOffset>* d);
  
  // Path reading
  bool ReadPathArray(std::vector<Path>* d);
  
  // ListOp reading
  bool ReadTokenListOp(ListOp<value::token>* d);
  bool ReadStringListOp(ListOp<std::string>* d);
  bool ReadPathListOp(ListOp<Path>* d);
  
  // Variant selection map
  bool ReadVariantSelectionMap(VariantSelectionMap* d);
  
  // Custom data
  bool ReadCustomData(CustomDataType* d);
  
  // Error handling
  void PushError(const std::string& msg) { _err += msg + "\n"; }
  void PushWarn(const std::string& msg) { _warn += msg + "\n"; }
  std::string GetError() const { return _err; }
  std::string GetWarning() const { return _warn; }
  void ClearError() { _err.clear(); }
  void ClearWarning() { _warn.clear(); }

 private:
  StreamReader* _sr;
  MemoryBudget& memory_manager_;
  std::string _err;
  std::string _warn;
};

}  // namespace crate
}  // namespace tinyusdz