// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// LightUSD Next - Value class implementation

#include "value.hh"
#include "value-common-inl.hh"
#include "../c/lightusd-core-c.h"
#include "array-storage.hh"
#include "type-info.hh"
#include "interpolation.hh"
#include "../crate/lazy-array.hh"
#include "../crate/crate-data-source.hh"

#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace lightusd {
namespace next {

// ============================================================

Value::Value() = default;

Value::~Value() {
  destroy();
}

Value::Value(const Value& other) {
  copy_from(other);
}

Value::Value(Value&& other) noexcept {
  move_from(std::move(other));
}

Value& Value::operator=(const Value& other) {
  if (this != &other) {
    destroy();
    copy_from(other);
  }
  return *this;
}

Value& Value::operator=(Value&& other) noexcept {
  if (this != &other) {
    destroy();
    move_from(std::move(other));
  }
  return *this;
}

// ============================================================
// Type-specific constructors
// ============================================================

Value::Value(bool v) : type_id_(TypeId::Bool) {
  std::memcpy(storage_, &v, sizeof(bool));
}

Value::Value(int32_t v) : type_id_(TypeId::Int) {
  std::memcpy(storage_, &v, sizeof(int32_t));
}

Value::Value(uint32_t v) : type_id_(TypeId::UInt) {
  std::memcpy(storage_, &v, sizeof(uint32_t));
}

Value::Value(int64_t v) : type_id_(TypeId::Int64) {
  std::memcpy(storage_, &v, sizeof(int64_t));
}

Value::Value(uint64_t v) : type_id_(TypeId::UInt64) {
  std::memcpy(storage_, &v, sizeof(uint64_t));
}

Value::Value(float v) : type_id_(TypeId::Float) {
  std::memcpy(storage_, &v, sizeof(float));
}

Value::Value(double v) : type_id_(TypeId::Double) {
  std::memcpy(storage_, &v, sizeof(double));
}

Value::Value(const char* v) : type_id_(TypeId::String) {
  new (storage_) StringStorage{std::string(v ? v : "")};
}

Value::Value(const std::string& v) : type_id_(TypeId::String) {
  new (storage_) StringStorage{v};
}

Value::Value(std::string&& v) : type_id_(TypeId::String) {
  new (storage_) StringStorage{std::move(v)};
}

// ============================================================
// Factory functions
// ============================================================

Value Value::MakeInt2(int32_t x, int32_t y) {
  Value v;
  v.type_id_ = TypeId::Int2;
  int32_t data[2] = {x, y};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeInt3(int32_t x, int32_t y, int32_t z) {
  Value v;
  v.type_id_ = TypeId::Int3;
  int32_t data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeInt4(int32_t x, int32_t y, int32_t z, int32_t w) {
  Value v;
  v.type_id_ = TypeId::Int4;
  int32_t data[4] = {x, y, z, w};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeFloat2(float x, float y) {
  Value v;
  v.type_id_ = TypeId::Float2;
  float data[2] = {x, y};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeFloat3(float x, float y, float z) {
  Value v;
  v.type_id_ = TypeId::Float3;
  float data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeFloat4(float x, float y, float z, float w) {
  Value v;
  v.type_id_ = TypeId::Float4;
  float data[4] = {x, y, z, w};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeDouble2(double x, double y) {
  Value v;
  v.type_id_ = TypeId::Double2;
  double data[2] = {x, y};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeDouble3(double x, double y, double z) {
  Value v;
  v.type_id_ = TypeId::Double3;
  double data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeDouble4(double x, double y, double z, double w) {
  Value v;
  v.type_id_ = TypeId::Double4;
  double data[4] = {x, y, z, w};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeQuatf(float x, float y, float z, float w) {
  Value v;
  v.type_id_ = TypeId::Quatf;
  float data[4] = {x, y, z, w};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeQuatd(double x, double y, double z, double w) {
  Value v;
  v.type_id_ = TypeId::Quatd;
  double data[4] = {x, y, z, w};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeMatrix2f(const float* data) {
  Value v;
  if (!data) return v;
  v.type_id_ = TypeId::Matrix2f;
  std::memcpy(v.storage_, data, 4 * sizeof(float));
  return v;
}

Value Value::MakeMatrix3f(const float* data) {
  Value v;
  if (!data) return v;
  v.type_id_ = TypeId::Matrix3f;
  std::memcpy(v.storage_, data, 9 * sizeof(float));
  return v;
}

Value Value::MakeMatrix4f(const float* data) {
  Value v;
  if (!data) return v;
  v.type_id_ = TypeId::Matrix4f;
  std::memcpy(v.storage_, data, 16 * sizeof(float));
  return v;
}

Value Value::MakeMatrix2d(const double* data) {
  Value v;
  if (!data) return v;
  v.type_id_ = TypeId::Matrix2d;
  std::memcpy(v.storage_, data, 4 * sizeof(double));
  return v;
}

Value Value::MakeMatrix3d(const double* data) {
  Value v;
  if (!data) return v;
  v.type_id_ = TypeId::Matrix3d;
  std::memcpy(v.storage_, data, 9 * sizeof(double));
  return v;
}

Value Value::MakeMatrix4d(const double* data) {
  Value v;
  if (!data) return v;
  v.type_id_ = TypeId::Matrix4d;
  std::memcpy(v.storage_, data, 16 * sizeof(double));
  return v;
}

Value Value::MakeStringLike(const std::string& s, TypeId type) {
  Value v;
  if (!UsesStringStorage(type)) return v;
  v.type_id_ = type;
  new (v.storage_) StringStorage{s};
  return v;
}

Value Value::MakeToken(const std::string& s) {
  Value v;
  v.type_id_ = TypeId::Token;
  new (v.storage_) StringStorage{s};
  return v;
}

Value Value::MakeToken(std::string&& s) {
  Value v;
  v.type_id_ = TypeId::Token;
  new (v.storage_) StringStorage{std::move(s)};
  return v;
}

Value Value::MakeAssetPath(const std::string& s) {
  Value v;
  v.type_id_ = TypeId::AssetPath;
  new (v.storage_) StringStorage{s};
  return v;
}

Value Value::MakeAssetPath(std::string&& s) {
  Value v;
  v.type_id_ = TypeId::AssetPath;
  new (v.storage_) StringStorage{std::move(s)};
  return v;
}

}  // namespace next
}  // namespace lightusd
