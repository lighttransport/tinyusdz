// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value class implementation

#include "value.hh"
#include "type-info.hh"

#include <cstring>
#include <new>

namespace tinyusdz {
namespace next {

namespace {

// ============================================================
// Type storage helpers
// ============================================================

// String-like types stored in SBO using std::string
struct StringStorage {
  std::string value;
};

// Array types stored on heap
struct FloatArrayStorage {
  std::vector<float> data;
};

struct IntArrayStorage {
  std::vector<int32_t> data;
};

struct DoubleArrayStorage {
  std::vector<double> data;
};

struct UIntArrayStorage {
  std::vector<uint32_t> data;
};

// View storage - non-owning pointer to external data
struct ArrayViewStorage {
  const void* data;
  size_t count;
  size_t elem_size;  // Size of each element in bytes
};

// Check if type uses string storage
bool UsesStringStorage(TypeId id) {
  return id == TypeId::String || id == TypeId::Token || id == TypeId::AssetPath;
}

// Check if type requires heap allocation
bool RequiresHeap(TypeId id, bool is_array) {
  if (is_array) return true;
  if (id == TypeId::Dictionary) return true;
  return false;
}

}  // anonymous namespace

// ============================================================
// Constructors and destructor
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
  v.type_id_ = TypeId::Matrix2f;
  std::memcpy(v.storage_, data, 4 * sizeof(float));
  return v;
}

Value Value::MakeMatrix3f(const float* data) {
  Value v;
  v.type_id_ = TypeId::Matrix3f;
  std::memcpy(v.storage_, data, 9 * sizeof(float));
  return v;
}

Value Value::MakeMatrix4f(const float* data) {
  Value v;
  v.type_id_ = TypeId::Matrix4f;
  std::memcpy(v.storage_, data, 16 * sizeof(float));
  return v;
}

Value Value::MakeMatrix2d(const double* data) {
  Value v;
  v.type_id_ = TypeId::Matrix2d;
  std::memcpy(v.storage_, data, 4 * sizeof(double));
  return v;
}

Value Value::MakeMatrix3d(const double* data) {
  Value v;
  v.type_id_ = TypeId::Matrix3d;
  std::memcpy(v.storage_, data, 9 * sizeof(double));
  return v;
}

Value Value::MakeMatrix4d(const double* data) {
  Value v;
  v.type_id_ = TypeId::Matrix4d;
  std::memcpy(v.storage_, data, 16 * sizeof(double));
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

Value Value::MakePoint3f(float x, float y, float z) {
  Value v;
  v.type_id_ = TypeId::Point3f;
  float data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakePoint3d(double x, double y, double z) {
  Value v;
  v.type_id_ = TypeId::Point3d;
  double data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeVector3f(float x, float y, float z) {
  Value v;
  v.type_id_ = TypeId::Vector3f;
  float data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeVector3d(double x, double y, double z) {
  Value v;
  v.type_id_ = TypeId::Vector3d;
  double data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeNormal3f(float x, float y, float z) {
  Value v;
  v.type_id_ = TypeId::Normal3f;
  float data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeNormal3d(double x, double y, double z) {
  Value v;
  v.type_id_ = TypeId::Normal3d;
  double data[3] = {x, y, z};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeColor3f(float r, float g, float b) {
  Value v;
  v.type_id_ = TypeId::Color3f;
  float data[3] = {r, g, b};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeColor4f(float r, float g, float b, float a) {
  Value v;
  v.type_id_ = TypeId::Color4f;
  float data[4] = {r, g, b, a};
  std::memcpy(v.storage_, data, sizeof(data));
  return v;
}

Value Value::MakeTexcoord2f(float u, float v_coord) {
  Value val;
  val.type_id_ = TypeId::Texcoord2f;
  float data[2] = {u, v_coord};
  std::memcpy(val.storage_, data, sizeof(data));
  return val;
}

Value Value::MakeFromRaw(TypeId type_id, const void* data) {
  Value v;
  v.type_id_ = type_id;

  if (UsesStringStorage(type_id)) {
    new (v.storage_) StringStorage{std::string(static_cast<const char*>(data))};
  } else {
    size_t size = GetTypeSize(type_id);
    if (size > 0 && size <= kSBOSize) {
      std::memcpy(v.storage_, data, size);
    }
  }
  return v;
}

// ============================================================
// Array factory functions
// ============================================================

Value Value::MakeFloatArray(const std::vector<float>& data) {
  Value v;
  v.type_id_ = TypeId::Float;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  auto* storage = new FloatArrayStorage{data};
  std::memcpy(v.storage_, &storage, sizeof(storage));
  return v;
}

Value Value::MakeFloatArray(std::vector<float>&& data) {
  Value v;
  v.type_id_ = TypeId::Float;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  auto* storage = new FloatArrayStorage{std::move(data)};
  std::memcpy(v.storage_, &storage, sizeof(storage));
  return v;
}

Value Value::MakeIntArray(const std::vector<int32_t>& data) {
  Value v;
  v.type_id_ = TypeId::Int;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  auto* storage = new IntArrayStorage{data};
  std::memcpy(v.storage_, &storage, sizeof(storage));
  return v;
}

Value Value::MakeIntArray(std::vector<int32_t>&& data) {
  Value v;
  v.type_id_ = TypeId::Int;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  auto* storage = new IntArrayStorage{std::move(data)};
  std::memcpy(v.storage_, &storage, sizeof(storage));
  return v;
}

Value Value::MakeFloat3Array(const std::vector<float>& data) {
  Value v;
  v.type_id_ = TypeId::Float3;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size() / 3);
  auto* storage = new FloatArrayStorage{data};
  std::memcpy(v.storage_, &storage, sizeof(storage));
  return v;
}

Value Value::MakeFloat3Array(std::vector<float>&& data) {
  Value v;
  v.type_id_ = TypeId::Float3;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size() / 3);
  auto* storage = new FloatArrayStorage{std::move(data)};
  std::memcpy(v.storage_, &storage, sizeof(storage));
  return v;
}

// ============================================================
// Zero-copy array view factories
// ============================================================

Value Value::MakeFloatArrayView(const float* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Float;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(float)};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeIntArrayView(const int32_t* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Int;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(int32_t)};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeUIntArrayView(const uint32_t* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::UInt;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(uint32_t)};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeDoubleArrayView(const double* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Double;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(double)};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeFloat2ArrayView(const float* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Float2;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(float) * 2};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeFloat3ArrayView(const float* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Float3;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(float) * 3};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeFloat4ArrayView(const float* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Float4;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(float) * 4};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeDouble3ArrayView(const double* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Double3;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(double) * 3};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeInt2ArrayView(const int32_t* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Int2;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(int32_t) * 2};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeInt3ArrayView(const int32_t* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Int3;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(int32_t) * 3};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeInt4ArrayView(const int32_t* data, size_t count) {
  Value v;
  v.type_id_ = TypeId::Int4;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, sizeof(int32_t) * 4};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

Value Value::MakeArrayView(TypeId elem_type, const void* data, size_t count) {
  size_t elem_size = GetTypeSize(elem_type);
  if (elem_size == 0) return Value();

  Value v;
  v.type_id_ = elem_type;
  v.is_array_ = true;
  v.is_view_ = true;
  v.array_size_ = static_cast<uint32_t>(count);
  ArrayViewStorage view{data, count, elem_size};
  std::memcpy(v.storage_, &view, sizeof(view));
  return v;
}

// ============================================================
// Queries and accessors
// ============================================================

void Value::clear() {
  destroy();
  type_id_ = TypeId::Invalid;
  is_array_ = false;
  is_view_ = false;
  array_size_ = 0;
}

bool Value::uses_heap() const {
  return is_array_ || type_id_ == TypeId::Dictionary;
}

void Value::destroy() {
  if (type_id_ == TypeId::Invalid) return;

  if (is_array_) {
    // Views don't own their data - nothing to delete
    if (!is_view_) {
      void* ptr;
      std::memcpy(&ptr, storage_, sizeof(ptr));
      if (type_id_ == TypeId::Float || type_id_ == TypeId::Float3 ||
          type_id_ == TypeId::Float2 || type_id_ == TypeId::Float4) {
        delete static_cast<FloatArrayStorage*>(ptr);
      } else if (type_id_ == TypeId::Int || type_id_ == TypeId::Int2 ||
                 type_id_ == TypeId::Int3 || type_id_ == TypeId::Int4) {
        delete static_cast<IntArrayStorage*>(ptr);
      } else if (type_id_ == TypeId::UInt) {
        delete static_cast<UIntArrayStorage*>(ptr);
      } else if (type_id_ == TypeId::Double || type_id_ == TypeId::Double3) {
        delete static_cast<DoubleArrayStorage*>(ptr);
      }
    }
  } else if (UsesStringStorage(type_id_)) {
    reinterpret_cast<StringStorage*>(storage_)->~StringStorage();
  }

  type_id_ = TypeId::Invalid;
  is_array_ = false;
  is_view_ = false;
  array_size_ = 0;
}

void Value::copy_from(const Value& other) {
  type_id_ = other.type_id_;
  is_array_ = other.is_array_;
  is_view_ = other.is_view_;
  array_size_ = other.array_size_;

  if (other.is_array_) {
    if (other.is_view_) {
      // Copy the view storage (just pointers, not the data)
      std::memcpy(storage_, other.storage_, sizeof(ArrayViewStorage));
    } else {
      void* ptr;
      std::memcpy(&ptr, other.storage_, sizeof(ptr));
      if (other.type_id_ == TypeId::Float || other.type_id_ == TypeId::Float3 ||
          other.type_id_ == TypeId::Float2 || other.type_id_ == TypeId::Float4) {
        auto* new_storage = new FloatArrayStorage{static_cast<FloatArrayStorage*>(ptr)->data};
        std::memcpy(storage_, &new_storage, sizeof(new_storage));
      } else if (other.type_id_ == TypeId::Int || other.type_id_ == TypeId::Int2 ||
                 other.type_id_ == TypeId::Int3 || other.type_id_ == TypeId::Int4) {
        auto* new_storage = new IntArrayStorage{static_cast<IntArrayStorage*>(ptr)->data};
        std::memcpy(storage_, &new_storage, sizeof(new_storage));
      } else if (other.type_id_ == TypeId::UInt) {
        auto* new_storage = new UIntArrayStorage{static_cast<UIntArrayStorage*>(ptr)->data};
        std::memcpy(storage_, &new_storage, sizeof(new_storage));
      } else if (other.type_id_ == TypeId::Double || other.type_id_ == TypeId::Double3) {
        auto* new_storage = new DoubleArrayStorage{static_cast<DoubleArrayStorage*>(ptr)->data};
        std::memcpy(storage_, &new_storage, sizeof(new_storage));
      }
    }
  } else if (UsesStringStorage(other.type_id_)) {
    new (storage_) StringStorage{reinterpret_cast<const StringStorage*>(other.storage_)->value};
  } else {
    std::memcpy(storage_, other.storage_, kSBOSize);
  }
}

void Value::move_from(Value&& other) noexcept {
  type_id_ = other.type_id_;
  is_array_ = other.is_array_;
  is_view_ = other.is_view_;
  array_size_ = other.array_size_;

  if (other.is_array_) {
    if (other.is_view_) {
      // Copy the view storage (just pointers)
      std::memcpy(storage_, other.storage_, sizeof(ArrayViewStorage));
    } else {
      // Just copy the pointer - no need to allocate
      std::memcpy(storage_, other.storage_, sizeof(void*));
    }
  } else if (UsesStringStorage(other.type_id_)) {
    new (storage_) StringStorage{std::move(reinterpret_cast<StringStorage*>(other.storage_)->value)};
    reinterpret_cast<StringStorage*>(other.storage_)->~StringStorage();
  } else {
    std::memcpy(storage_, other.storage_, kSBOSize);
  }

  other.type_id_ = TypeId::Invalid;
  other.is_array_ = false;
  other.is_view_ = false;
  other.array_size_ = 0;
}

void* Value::data_ptr() {
  if (is_array_) {
    void* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    return ptr;
  }
  return storage_;
}

const void* Value::data_ptr() const {
  if (is_array_) {
    void* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    return ptr;
  }
  return storage_;
}

const void* Value::raw_data() const {
  return data_ptr();
}

void* Value::raw_data() {
  return data_ptr();
}

// ============================================================
// Type-safe accessors
// ============================================================

#define DEFINE_SCALAR_ACCESSOR(type, type_id_val, ret_type) \
  const ret_type* Value::as_##type() const { \
    if (type_id_ != TypeId::type_id_val || is_array_) return nullptr; \
    return reinterpret_cast<const ret_type*>(storage_); \
  } \
  ret_type* Value::as_##type() { \
    if (type_id_ != TypeId::type_id_val || is_array_) return nullptr; \
    return reinterpret_cast<ret_type*>(storage_); \
  }

DEFINE_SCALAR_ACCESSOR(bool, Bool, bool)
DEFINE_SCALAR_ACCESSOR(int, Int, int32_t)
DEFINE_SCALAR_ACCESSOR(uint, UInt, uint32_t)
DEFINE_SCALAR_ACCESSOR(int64, Int64, int64_t)
DEFINE_SCALAR_ACCESSOR(uint64, UInt64, uint64_t)
DEFINE_SCALAR_ACCESSOR(float, Float, float)
DEFINE_SCALAR_ACCESSOR(double, Double, double)

#undef DEFINE_SCALAR_ACCESSOR

const std::string* Value::as_string() const {
  if (type_id_ != TypeId::String || is_array_) return nullptr;
  return &reinterpret_cast<const StringStorage*>(storage_)->value;
}

std::string* Value::as_string() {
  if (type_id_ != TypeId::String || is_array_) return nullptr;
  return &reinterpret_cast<StringStorage*>(storage_)->value;
}

const std::string* Value::as_token() const {
  if (type_id_ != TypeId::Token || is_array_) return nullptr;
  return &reinterpret_cast<const StringStorage*>(storage_)->value;
}

const std::string* Value::as_asset_path() const {
  if (type_id_ != TypeId::AssetPath || is_array_) return nullptr;
  return &reinterpret_cast<const StringStorage*>(storage_)->value;
}

// Vector accessors
const int32_t* Value::as_int2() const {
  if (type_id_ != TypeId::Int2 || is_array_) return nullptr;
  return reinterpret_cast<const int32_t*>(storage_);
}

const int32_t* Value::as_int3() const {
  if (type_id_ != TypeId::Int3 || is_array_) return nullptr;
  return reinterpret_cast<const int32_t*>(storage_);
}

const int32_t* Value::as_int4() const {
  if (type_id_ != TypeId::Int4 || is_array_) return nullptr;
  return reinterpret_cast<const int32_t*>(storage_);
}

const float* Value::as_float2() const {
  if (type_id_ != TypeId::Float2 || is_array_) return nullptr;
  return reinterpret_cast<const float*>(storage_);
}

const float* Value::as_float3() const {
  if (type_id_ != TypeId::Float3 && type_id_ != TypeId::Point3f &&
      type_id_ != TypeId::Vector3f && type_id_ != TypeId::Normal3f &&
      type_id_ != TypeId::Color3f) return nullptr;
  if (is_array_) return nullptr;
  return reinterpret_cast<const float*>(storage_);
}

const float* Value::as_float4() const {
  if (type_id_ != TypeId::Float4 && type_id_ != TypeId::Quatf &&
      type_id_ != TypeId::Color4f) return nullptr;
  if (is_array_) return nullptr;
  return reinterpret_cast<const float*>(storage_);
}

const double* Value::as_double2() const {
  if (type_id_ != TypeId::Double2 || is_array_) return nullptr;
  return reinterpret_cast<const double*>(storage_);
}

const double* Value::as_double3() const {
  if (type_id_ != TypeId::Double3 && type_id_ != TypeId::Point3d &&
      type_id_ != TypeId::Vector3d && type_id_ != TypeId::Normal3d &&
      type_id_ != TypeId::Color3d) return nullptr;
  if (is_array_) return nullptr;
  return reinterpret_cast<const double*>(storage_);
}

const double* Value::as_double4() const {
  if (type_id_ != TypeId::Double4 && type_id_ != TypeId::Quatd &&
      type_id_ != TypeId::Color4d) return nullptr;
  if (is_array_) return nullptr;
  return reinterpret_cast<const double*>(storage_);
}

// Matrix accessors
const float* Value::as_matrix2f() const {
  if (type_id_ != TypeId::Matrix2f || is_array_) return nullptr;
  return reinterpret_cast<const float*>(storage_);
}

const float* Value::as_matrix3f() const {
  if (type_id_ != TypeId::Matrix3f || is_array_) return nullptr;
  return reinterpret_cast<const float*>(storage_);
}

const float* Value::as_matrix4f() const {
  if (type_id_ != TypeId::Matrix4f || is_array_) return nullptr;
  return reinterpret_cast<const float*>(storage_);
}

const double* Value::as_matrix2d() const {
  if (type_id_ != TypeId::Matrix2d || is_array_) return nullptr;
  return reinterpret_cast<const double*>(storage_);
}

const double* Value::as_matrix3d() const {
  if (type_id_ != TypeId::Matrix3d || is_array_) return nullptr;
  return reinterpret_cast<const double*>(storage_);
}

const double* Value::as_matrix4d() const {
  if (type_id_ != TypeId::Matrix4d || is_array_) return nullptr;
  return reinterpret_cast<const double*>(storage_);
}

// Array accessors (only work for owned arrays, not views)
const std::vector<float>* Value::as_float_array() const {
  if ((type_id_ != TypeId::Float && type_id_ != TypeId::Float3) || !is_array_ || is_view_) return nullptr;
  void* ptr;
  std::memcpy(&ptr, storage_, sizeof(ptr));
  return &static_cast<FloatArrayStorage*>(ptr)->data;
}

std::vector<float>* Value::as_float_array() {
  if ((type_id_ != TypeId::Float && type_id_ != TypeId::Float3) || !is_array_ || is_view_) return nullptr;
  void* ptr;
  std::memcpy(&ptr, storage_, sizeof(ptr));
  return &static_cast<FloatArrayStorage*>(ptr)->data;
}

const std::vector<int32_t>* Value::as_int_array() const {
  if (type_id_ != TypeId::Int || !is_array_ || is_view_) return nullptr;
  void* ptr;
  std::memcpy(&ptr, storage_, sizeof(ptr));
  return &static_cast<IntArrayStorage*>(ptr)->data;
}

std::vector<int32_t>* Value::as_int_array() {
  if (type_id_ != TypeId::Int || !is_array_ || is_view_) return nullptr;
  void* ptr;
  std::memcpy(&ptr, storage_, sizeof(ptr));
  return &static_cast<IntArrayStorage*>(ptr)->data;
}

// ============================================================
// Array view accessors
// ============================================================

FloatArrayView Value::float_array_view() const {
  if (!is_array_) return FloatArrayView();
  if (type_id_ != TypeId::Float && type_id_ != TypeId::Float3 &&
      type_id_ != TypeId::Float2 && type_id_ != TypeId::Float4) {
    return FloatArrayView();
  }

  if (is_view_) {
    ArrayViewStorage view;
    std::memcpy(&view, storage_, sizeof(view));
    size_t elem_count = (type_id_ == TypeId::Float) ? view.count :
                        (type_id_ == TypeId::Float2) ? view.count * 2 :
                        (type_id_ == TypeId::Float3) ? view.count * 3 : view.count * 4;
    return FloatArrayView(static_cast<const float*>(view.data), elem_count);
  } else {
    void* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    const auto& data = static_cast<FloatArrayStorage*>(ptr)->data;
    return FloatArrayView(data.data(), data.size());
  }
}

Int32ArrayView Value::int_array_view() const {
  if (!is_array_) return Int32ArrayView();
  if (type_id_ != TypeId::Int && type_id_ != TypeId::Int2 &&
      type_id_ != TypeId::Int3 && type_id_ != TypeId::Int4) {
    return Int32ArrayView();
  }

  if (is_view_) {
    ArrayViewStorage view;
    std::memcpy(&view, storage_, sizeof(view));
    size_t elem_count = (type_id_ == TypeId::Int) ? view.count :
                        (type_id_ == TypeId::Int2) ? view.count * 2 :
                        (type_id_ == TypeId::Int3) ? view.count * 3 : view.count * 4;
    return Int32ArrayView(static_cast<const int32_t*>(view.data), elem_count);
  } else {
    void* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    const auto& data = static_cast<IntArrayStorage*>(ptr)->data;
    return Int32ArrayView(data.data(), data.size());
  }
}

UInt32ArrayView Value::uint_array_view() const {
  if (!is_array_ || type_id_ != TypeId::UInt) return UInt32ArrayView();

  if (is_view_) {
    ArrayViewStorage view;
    std::memcpy(&view, storage_, sizeof(view));
    return UInt32ArrayView(static_cast<const uint32_t*>(view.data), view.count);
  } else {
    void* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    const auto& data = static_cast<UIntArrayStorage*>(ptr)->data;
    return UInt32ArrayView(data.data(), data.size());
  }
}

DoubleArrayView Value::double_array_view() const {
  if (!is_array_) return DoubleArrayView();
  if (type_id_ != TypeId::Double && type_id_ != TypeId::Double3) {
    return DoubleArrayView();
  }

  if (is_view_) {
    ArrayViewStorage view;
    std::memcpy(&view, storage_, sizeof(view));
    size_t elem_count = (type_id_ == TypeId::Double) ? view.count : view.count * 3;
    return DoubleArrayView(static_cast<const double*>(view.data), elem_count);
  } else {
    void* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    const auto& data = static_cast<DoubleArrayStorage*>(ptr)->data;
    return DoubleArrayView(data.data(), data.size());
  }
}

const void* Value::array_data() const {
  if (!is_array_) return nullptr;

  if (is_view_) {
    ArrayViewStorage view;
    std::memcpy(&view, storage_, sizeof(view));
    return view.data;
  } else {
    void* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    if (type_id_ == TypeId::Float || type_id_ == TypeId::Float3 ||
        type_id_ == TypeId::Float2 || type_id_ == TypeId::Float4) {
      return static_cast<FloatArrayStorage*>(ptr)->data.data();
    } else if (type_id_ == TypeId::Int || type_id_ == TypeId::Int2 ||
               type_id_ == TypeId::Int3 || type_id_ == TypeId::Int4) {
      return static_cast<IntArrayStorage*>(ptr)->data.data();
    } else if (type_id_ == TypeId::UInt) {
      return static_cast<UIntArrayStorage*>(ptr)->data.data();
    } else if (type_id_ == TypeId::Double || type_id_ == TypeId::Double3) {
      return static_cast<DoubleArrayStorage*>(ptr)->data.data();
    }
  }
  return nullptr;
}

size_t Value::array_element_count() const {
  if (!is_array_) return 0;

  if (is_view_) {
    ArrayViewStorage view;
    std::memcpy(&view, storage_, sizeof(view));
    return view.count;
  }
  return array_size_;
}

bool Value::make_owned() {
  if (!is_array_ || !is_view_) return false;

  ArrayViewStorage view;
  std::memcpy(&view, storage_, sizeof(view));

  // Create owned copy based on type
  if (type_id_ == TypeId::Float || type_id_ == TypeId::Float2 ||
      type_id_ == TypeId::Float3 || type_id_ == TypeId::Float4) {
    size_t total_floats = view.count * (view.elem_size / sizeof(float));
    std::vector<float> owned_data(total_floats);
    std::memcpy(owned_data.data(), view.data, total_floats * sizeof(float));
    auto* storage = new FloatArrayStorage{std::move(owned_data)};
    std::memcpy(storage_, &storage, sizeof(storage));
  } else if (type_id_ == TypeId::Int || type_id_ == TypeId::Int2 ||
             type_id_ == TypeId::Int3 || type_id_ == TypeId::Int4) {
    size_t total_ints = view.count * (view.elem_size / sizeof(int32_t));
    std::vector<int32_t> owned_data(total_ints);
    std::memcpy(owned_data.data(), view.data, total_ints * sizeof(int32_t));
    auto* storage = new IntArrayStorage{std::move(owned_data)};
    std::memcpy(storage_, &storage, sizeof(storage));
  } else if (type_id_ == TypeId::UInt) {
    std::vector<uint32_t> owned_data(view.count);
    std::memcpy(owned_data.data(), view.data, view.count * sizeof(uint32_t));
    auto* storage = new UIntArrayStorage{std::move(owned_data)};
    std::memcpy(storage_, &storage, sizeof(storage));
  } else if (type_id_ == TypeId::Double || type_id_ == TypeId::Double3) {
    size_t total_doubles = view.count * (view.elem_size / sizeof(double));
    std::vector<double> owned_data(total_doubles);
    std::memcpy(owned_data.data(), view.data, total_doubles * sizeof(double));
    auto* storage = new DoubleArrayStorage{std::move(owned_data)};
    std::memcpy(storage_, &storage, sizeof(storage));
  } else {
    return false;
  }

  is_view_ = false;
  return true;
}

// ============================================================
// Comparison
// ============================================================

bool Value::operator==(const Value& other) const {
  if (type_id_ != other.type_id_) return false;
  if (is_array_ != other.is_array_) return false;
  if (is_array_ && array_size_ != other.array_size_) return false;
  if (type_id_ == TypeId::Invalid) return true;

  if (is_array_) {
    if (type_id_ == TypeId::Float || type_id_ == TypeId::Float3) {
      return *as_float_array() == *other.as_float_array();
    } else if (type_id_ == TypeId::Int) {
      return *as_int_array() == *other.as_int_array();
    }
    return false;
  }

  if (UsesStringStorage(type_id_)) {
    return reinterpret_cast<const StringStorage*>(storage_)->value ==
           reinterpret_cast<const StringStorage*>(other.storage_)->value;
  }

  size_t size = GetTypeSize(type_id_);
  if (size > 0) {
    return std::memcmp(storage_, other.storage_, size) == 0;
  }

  return false;
}

// ============================================================
// Hashing
// ============================================================

namespace {

// FNV-1a hash for arbitrary byte sequences
inline uint64_t fnv1a_hash(const uint8_t* data, size_t len) {
  constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
  constexpr uint64_t FNV_PRIME = 1099511628211ULL;

  uint64_t hash = FNV_OFFSET;
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= FNV_PRIME;
  }
  return hash;
}

}  // namespace

uint64_t Value::hash() const {
  if (type_id_ == TypeId::Invalid) return 0;

  // Include type in hash
  uint64_t h = static_cast<uint64_t>(type_id_) | (is_array_ ? 0x100 : 0);

  if (is_array_) {
    // Hash array contents
    if (type_id_ == TypeId::Float || type_id_ == TypeId::Float3) {
      const auto* arr = as_float_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(float));
      }
    } else if (type_id_ == TypeId::Int) {
      const auto* arr = as_int_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(int32_t));
      }
    }
    return h;
  }

  // Hash string types
  if (UsesStringStorage(type_id_)) {
    const auto& s = reinterpret_cast<const StringStorage*>(storage_)->value;
    if (!s.empty()) {
      h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    return h;
  }

  // Hash scalar/vector types from storage
  size_t size = GetTypeSize(type_id_);
  if (size > 0) {
    h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(storage_), size);
  }

  return h;
}

const uint8_t* Value::raw_bytes(size_t* out_size) const {
  if (!out_size) return nullptr;
  *out_size = 0;

  if (type_id_ == TypeId::Invalid) return nullptr;

  if (is_array_) {
    if (type_id_ == TypeId::Float || type_id_ == TypeId::Float3) {
      const auto* arr = as_float_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(float);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (type_id_ == TypeId::Int) {
      const auto* arr = as_int_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(int32_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    }
    return nullptr;
  }

  if (UsesStringStorage(type_id_)) {
    const auto& s = reinterpret_cast<const StringStorage*>(storage_)->value;
    *out_size = s.size();
    return reinterpret_cast<const uint8_t*>(s.data());
  }

  size_t size = GetTypeSize(type_id_);
  if (size > 0) {
    *out_size = size;
    return reinterpret_cast<const uint8_t*>(storage_);
  }

  return nullptr;
}

}  // namespace next
}  // namespace tinyusdz
