// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Read-only array views for next::Value.

#pragma once

#include "value.hh"

namespace tinyusdz {
namespace next {

template <typename T>
struct ArrayView {
  const T* data = nullptr;
  size_t size = 0;      // flat scalar count, not USD element count
  bool borrowed = false;

  bool empty() const { return size == 0; }
  const T* begin() const { return data; }
  const T* end() const { return data + size; }
  const T& operator[](size_t i) const { return data[i]; }
};

template <typename T>
struct ArrayScratch {
  std::vector<T> storage;
};

bool GetFloatArrayView(const Value& value, ArrayScratch<float>* scratch,
                       ArrayView<float>* out);
bool GetDoubleArrayView(const Value& value, ArrayScratch<double>* scratch,
                        ArrayView<double>* out);
bool GetIntArrayView(const Value& value, ArrayScratch<int32_t>* scratch,
                     ArrayView<int32_t>* out);
bool GetInt64ArrayView(const Value& value, ArrayScratch<int64_t>* scratch,
                       ArrayView<int64_t>* out);
bool GetUIntArrayView(const Value& value, ArrayScratch<uint32_t>* scratch,
                      ArrayView<uint32_t>* out);
bool GetUInt64ArrayView(const Value& value, ArrayScratch<uint64_t>* scratch,
                        ArrayView<uint64_t>* out);

}  // namespace next
}  // namespace tinyusdz
