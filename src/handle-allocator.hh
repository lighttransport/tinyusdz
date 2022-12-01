// SPDX-License-Identifier: Apache 2.0
// Copyright 2022-Present Light Transport Entertainment Inc.
#pragma once

#include <cstdint>
//#include <cassert>
#include <limits>
#include <vector>

namespace tinyusdz {

///
/// Simple handle resource manager
/// Assume T is an unsigned integer type.
/// TODO(LTE): Allocate handle for a given value range. e.g. [minVal, maxVal)
///
template<typename T = uint32_t>
class HandleAllocator {
public:
  // id = 0 is reserved.
  HandleAllocator() : counter_(static_cast<T>(1)){}
  //~HandleAllocator(){}

  /// Allocates handle object.
  bool Allocate(T *dst) {

    if (!dst) {
      return false;
    }

    T handle = 0;

    if (!freeList_.empty()) {
      // Reuse previously issued handle.
      handle = freeList_.back();
      freeList_.pop_back();
      (*dst) = handle;
      return true;
    }

    handle = counter_;
    if ((handle >= static_cast<T>(1)) && (handle < std::numeric_limits<T>::max())) {
      counter_++;
      (*dst) = handle;
      return true;
    }

    return false;
  }

  /// Release handle object.
  bool Release(const T handle) {
    if (handle == counter_ - static_cast<T>(1)) {
      if (counter_ > static_cast<T>(1)) {
        counter_--;
      }
    } else {
      if (handle >= static_cast<T>(1)) {
        freeList_.push_back(handle);
      } else {
        // invalid handle
        return false;
      }
    }

    return true;
  }

private:
  std::vector<T> freeList_;
  T counter_{};
};

} // namespace tinyusdz
