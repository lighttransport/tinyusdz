// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Array reading operations for Crate reader
#pragma once

#include <vector>
#include "stream-reader.hh"
#include "typed-array.hh"
#include "value-types.hh"
#include "memory-budget.hh"
#include "list-op.hh"

namespace tinyusdz {
namespace crate {

class CrateArrayReader {
 public:
  CrateArrayReader(StreamReader* sr, MemoryBudgetManager& memory_manager)
      : _sr(sr), memory_manager_(memory_manager) {}

  // Integer array reading
  template <typename T>
  bool ReadIntArray(bool is_compressed, std::vector<T>* d);
  
  template <typename T>
  bool ReadIntArrayTyped(bool is_compressed, TypedArray<T>* d);
  
  // Floating point array reading
  bool ReadHalfArray(bool is_compressed, std::vector<value::half>* d);
  bool ReadFloatArray(bool is_compressed, std::vector<float>* d);
  bool ReadDoubleArray(bool is_compressed, std::vector<double>* d);
  
  // Typed floating point arrays
  bool ReadFloatArrayTyped(bool is_compressed, TypedArray<float>* d);
  bool ReadDoubleArrayTyped(bool is_compressed, TypedArray<double>* d);
  
  // Vector reading
  bool ReadDoubleVector(std::vector<double>* d);
  
  // Time samples
  bool ReadTimeSamples(value::TimeSamples* d);
  
  // String array
  bool ReadStringArray(std::vector<std::string>* d);
  
  // Generic array reading
  template <typename T>
  bool ReadArray(std::vector<T>* d);
  
  // ListOp array reading
  template <typename T>
  bool ReadListOp(ListOp<T>* d);
  
  // Compressed integer reading
  template <typename Int>
  bool ReadCompressedInts(Int* out, size_t m_length);

  // Error handling
  void PushError(const std::string& msg) { _err += msg + "\n"; }
  void PushWarn(const std::string& msg) { _warn += msg + "\n"; }
  std::string GetError() const { return _err; }
  std::string GetWarning() const { return _warn; }

 private:
  StreamReader* _sr;
  MemoryBudgetManager& memory_manager_;
  std::string _err;
  std::string _warn;
};

}  // namespace crate
}  // namespace tinyusdz