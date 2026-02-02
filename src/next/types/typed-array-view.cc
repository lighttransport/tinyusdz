// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - TypedArrayView implementation

#include "typed-array-view.hh"
#include <cstring>

namespace tinyusdz {
namespace next {

ArrayData::ArrayData(ArrayData&& other) noexcept
    : kind_(other.kind_),
      type_size_(other.type_size_),
      count_(other.count_),
      owned_data_(other.owned_data_),
      owned_vector_(other.owned_vector_),
      owned_destructor_(other.owned_destructor_),
      view_data_(other.view_data_) {
  other.kind_ = ArrayStorageKind::Empty;
  other.owned_data_ = nullptr;
  other.owned_vector_ = nullptr;
  other.owned_destructor_ = nullptr;
  other.view_data_ = nullptr;
  other.count_ = 0;
}

ArrayData& ArrayData::operator=(ArrayData&& other) noexcept {
  if (this != &other) {
    clear();
    kind_ = other.kind_;
    type_size_ = other.type_size_;
    count_ = other.count_;
    owned_data_ = other.owned_data_;
    owned_vector_ = other.owned_vector_;
    owned_destructor_ = other.owned_destructor_;
    view_data_ = other.view_data_;

    other.kind_ = ArrayStorageKind::Empty;
    other.owned_data_ = nullptr;
    other.owned_vector_ = nullptr;
    other.owned_destructor_ = nullptr;
    other.view_data_ = nullptr;
    other.count_ = 0;
  }
  return *this;
}

void ArrayData::clear() {
  if (kind_ == ArrayStorageKind::Owned && owned_destructor_ && owned_vector_) {
    owned_destructor_(owned_vector_);
  }
  kind_ = ArrayStorageKind::Empty;
  type_size_ = 0;
  count_ = 0;
  owned_data_ = nullptr;
  owned_vector_ = nullptr;
  owned_destructor_ = nullptr;
  view_data_ = nullptr;
}

bool ArrayData::make_owned() {
  if (kind_ != ArrayStorageKind::View || !view_data_ || count_ == 0) {
    return false;
  }

  // Allocate new vector and copy data
  size_t total_bytes = count_ * type_size_;
  std::vector<uint8_t>* new_vec = new std::vector<uint8_t>(total_bytes);
  std::memcpy(new_vec->data(), view_data_, total_bytes);

  // Update state
  kind_ = ArrayStorageKind::Owned;
  owned_data_ = new_vec->data();
  owned_vector_ = new_vec;
  owned_destructor_ = [](void* p) { delete static_cast<std::vector<uint8_t>*>(p); };
  view_data_ = nullptr;

  return true;
}

}  // namespace next
}  // namespace tinyusdz
