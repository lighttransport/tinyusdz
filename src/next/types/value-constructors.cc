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

Value::Value(bool v) {
  core_.type = static_cast<uint16_t>(TypeId::Bool);
  core_.kind = LU_VALUE_POD;
  std::memcpy(core_.store.bytes, &v, sizeof(bool));
}

Value::Value(int32_t v) {
  core_.type = static_cast<uint16_t>(TypeId::Int);
  core_.kind = LU_VALUE_POD;
  std::memcpy(core_.store.bytes, &v, sizeof(int32_t));
}

Value::Value(uint32_t v) {
  core_.type = static_cast<uint16_t>(TypeId::UInt);
  core_.kind = LU_VALUE_POD;
  std::memcpy(core_.store.bytes, &v, sizeof(uint32_t));
}

Value::Value(int64_t v) {
  core_.type = static_cast<uint16_t>(TypeId::Int64);
  core_.kind = LU_VALUE_POD;
  std::memcpy(core_.store.bytes, &v, sizeof(int64_t));
}

Value::Value(uint64_t v) {
  core_.type = static_cast<uint16_t>(TypeId::UInt64);
  core_.kind = LU_VALUE_POD;
  std::memcpy(core_.store.bytes, &v, sizeof(uint64_t));
}

Value::Value(float v) {
  core_.type = static_cast<uint16_t>(TypeId::Float);
  core_.kind = LU_VALUE_POD;
  std::memcpy(core_.store.bytes, &v, sizeof(float));
}

Value::Value(double v) {
  core_.type = static_cast<uint16_t>(TypeId::Double);
  core_.kind = LU_VALUE_POD;
  std::memcpy(core_.store.bytes, &v, sizeof(double));
}

Value::Value(const char* v) {
  core_.type = static_cast<uint16_t>(TypeId::String);
  core_.kind = LU_VALUE_POD;
  new (core_.store.bytes) StringStorage{std::string(v ? v : "")};
}

Value::Value(const std::string& v) {
  core_.type = static_cast<uint16_t>(TypeId::String);
  core_.kind = LU_VALUE_POD;
  new (core_.store.bytes) StringStorage{v};
}

Value::Value(std::string&& v) {
  core_.type = static_cast<uint16_t>(TypeId::String);
  core_.kind = LU_VALUE_POD;
  new (core_.store.bytes) StringStorage{std::move(v)};
}

// ============================================================
// Factory functions
// ============================================================

Value Value::MakeInt2(int32_t x, int32_t y) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Int2);
  int32_t data[2] = {x, y};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeInt3(int32_t x, int32_t y, int32_t z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Int3);
  int32_t data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeInt4(int32_t x, int32_t y, int32_t z, int32_t w) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Int4);
  int32_t data[4] = {x, y, z, w};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeFloat2(float x, float y) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float2);
  float data[2] = {x, y};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeFloat3(float x, float y, float z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float3);
  float data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeFloat4(float x, float y, float z, float w) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Float4);
  float data[4] = {x, y, z, w};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeDouble2(double x, double y) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Double2);
  double data[2] = {x, y};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeDouble3(double x, double y, double z) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Double3);
  double data[3] = {x, y, z};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeDouble4(double x, double y, double z, double w) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Double4);
  double data[4] = {x, y, z, w};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeQuatf(float x, float y, float z, float w) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Quatf);
  float data[4] = {x, y, z, w};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeQuatd(double x, double y, double z, double w) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Quatd);
  double data[4] = {x, y, z, w};
  AdoptPod(v, data, sizeof(data));
  return v;
}

Value Value::MakeMatrix2f(const float* data) {
  Value v;
  if (!data) return v;
  v.core_.type = static_cast<uint16_t>(TypeId::Matrix2f);
  AdoptPod(v, data, static_cast<size_t>(4) * sizeof(float));
  return v;
}

Value Value::MakeMatrix3f(const float* data) {
  Value v;
  if (!data) return v;
  v.core_.type = static_cast<uint16_t>(TypeId::Matrix3f);
  AdoptPod(v, data, static_cast<size_t>(9) * sizeof(float));
  return v;
}

Value Value::MakeMatrix4f(const float* data) {
  Value v;
  if (!data) return v;
  v.core_.type = static_cast<uint16_t>(TypeId::Matrix4f);
  AdoptPod(v, data, static_cast<size_t>(16) * sizeof(float));
  return v;
}

Value Value::MakeMatrix2d(const double* data) {
  Value v;
  if (!data) return v;
  v.core_.type = static_cast<uint16_t>(TypeId::Matrix2d);
  AdoptPod(v, data, static_cast<size_t>(4) * sizeof(double));
  return v;
}

Value Value::MakeMatrix3d(const double* data) {
  Value v;
  if (!data) return v;
  v.core_.type = static_cast<uint16_t>(TypeId::Matrix3d);
  AdoptPod(v, data, static_cast<size_t>(9) * sizeof(double));
  return v;
}

Value Value::MakeMatrix4d(const double* data) {
  Value v;
  if (!data) return v;
  v.core_.type = static_cast<uint16_t>(TypeId::Matrix4d);
  AdoptPod(v, data, static_cast<size_t>(16) * sizeof(double));
  return v;
}

Value Value::MakeStringLike(const std::string& s, TypeId type) {
  Value v;
  if (!UsesStringStorage(type)) return v;
  v.core_.type = static_cast<uint16_t>(type);
  new (v.core_.store.bytes) StringStorage{s};
  return v;
}

Value Value::MakeToken(const std::string& s) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Token);
  new (v.core_.store.bytes) StringStorage{s};
  return v;
}

Value Value::MakeToken(std::string&& s) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::Token);
  new (v.core_.store.bytes) StringStorage{std::move(s)};
  return v;
}

Value Value::MakeAssetPath(const std::string& s) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::AssetPath);
  new (v.core_.store.bytes) StringStorage{s};
  return v;
}

Value Value::MakeAssetPath(std::string&& s) {
  Value v;
  v.core_.type = static_cast<uint16_t>(TypeId::AssetPath);
  new (v.core_.store.bytes) StringStorage{std::move(s)};
  return v;
}

}  // namespace next
}  // namespace lightusd
