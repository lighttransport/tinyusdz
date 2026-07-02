// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Value class implementation

#include "value.hh"
#include "type-info.hh"
#include "token.hh"
#include "../crate/lazy-array.hh"
#include "../crate/crate-data-source.hh"

#include <atomic>
#include <cstring>
#include <memory>
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

// Array types are heap-allocated and held by an intrusive COW handle in the SBO
// slot: copying a Value bumps the box refcount (no element copy); the first
// mutable access detaches (clones) if the buffer is shared. This is the VtArray
// _DetachIfNotUnique pattern — the dominant composition/flatten memory win for
// materialized (USDA / eager-crate) arrays, which previously deep-copied on
// every compose/Clone/CopyLocalOpinions.
//
// Intrusive-refcounted COW box for array element buffers. This is next-core
// Value redesign step 1: it replaces `shared_ptr<ArrayBox>` (16 bytes +
// a separate control-block allocation + a vtable) with the box's OWN atomic
// refcount and a per-element-type function-pointer table (clone/destroy) — no
// vtable, and an 8-byte owning handle. Semantics (VtArray-style copy-on-write)
// are unchanged; the smaller handle is what lets the future compact Value fit an
// array reference in a 16-byte inline payload (see doc/next-value-redesign.md).
struct ArrayBox {
  std::atomic<uint32_t> rc;
  ArrayBox* (*clone_fn)(const ArrayBox*);  // deep-copy (COW detach)
  void (*destroy_fn)(ArrayBox*);           // delete as the concrete type
 protected:
  ArrayBox(ArrayBox* (*c)(const ArrayBox*), void (*d)(ArrayBox*))
      : rc(1), clone_fn(c), destroy_fn(d) {}
};

template <class T>
struct VecArrayStorage : ArrayBox {
  std::vector<T> data;
  VecArrayStorage() : ArrayBox(&clone_impl, &destroy_impl) {}
  explicit VecArrayStorage(const std::vector<T>& d)
      : ArrayBox(&clone_impl, &destroy_impl), data(d) {}
  explicit VecArrayStorage(std::vector<T>&& d)
      : ArrayBox(&clone_impl, &destroy_impl), data(std::move(d)) {}
  static ArrayBox* clone_impl(const ArrayBox* b) {
    return new VecArrayStorage<T>(static_cast<const VecArrayStorage<T>*>(b)->data);
  }
  static void destroy_impl(ArrayBox* b) {
    delete static_cast<VecArrayStorage<T>*>(b);
  }
};

using FloatArrayStorage = VecArrayStorage<float>;
using IntArrayStorage = VecArrayStorage<int32_t>;
using DoubleArrayStorage = VecArrayStorage<double>;
using Int64ArrayStorage = VecArrayStorage<int64_t>;
using UIntArrayStorage = VecArrayStorage<uint32_t>;
using UInt64ArrayStorage = VecArrayStorage<uint64_t>;
using BoolArrayStorage = VecArrayStorage<uint8_t>;  // 0/1 values
using TokenArrayStorage = VecArrayStorage<TfToken>;  // interned token elements

inline void array_box_retain(ArrayBox* b) {
  if (b) b->rc.fetch_add(1, std::memory_order_relaxed);
}
inline void array_box_release(ArrayBox* b) {
  if (b && b->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) b->destroy_fn(b);
}

// Owning, copy-on-write handle to an ArrayBox. 8 bytes (one pointer) — half the
// size of the former shared_ptr. Mirrors just enough of the shared_ptr surface
// (get()/operator->/use_count()/copy=retain/move) that the Value call sites are
// unchanged; the SBO slot for an array Value holds one, placement-constructed.
class ArrayHandle {
 public:
  ArrayHandle() : box_(nullptr) {}
  // Adopts an already-refcounted box (rc == 1); does NOT bump the count.
  explicit ArrayHandle(ArrayBox* b) : box_(b) {}
  ArrayHandle(const ArrayHandle& o) : box_(o.box_) { array_box_retain(box_); }
  ArrayHandle(ArrayHandle&& o) noexcept : box_(o.box_) { o.box_ = nullptr; }
  ArrayHandle& operator=(const ArrayHandle& o) {
    if (this != &o) { array_box_retain(o.box_); array_box_release(box_); box_ = o.box_; }
    return *this;
  }
  ArrayHandle& operator=(ArrayHandle&& o) noexcept {
    if (this != &o) { array_box_release(box_); box_ = o.box_; o.box_ = nullptr; }
    return *this;
  }
  ~ArrayHandle() { array_box_release(box_); }
  ArrayBox* get() const { return box_; }
  ArrayBox* operator->() const { return box_; }
  uint32_t use_count() const {
    return box_ ? box_->rc.load(std::memory_order_acquire) : 0;
  }
  // COW: clone the buffer if shared, so a subsequent mutation is private.
  void detach_if_shared() {
    if (box_ && box_->rc.load(std::memory_order_acquire) > 1) {
      ArrayBox* nb = box_->clone_fn(box_);  // rc == 1
      array_box_release(box_);
      box_ = nb;
    }
  }
 private:
  ArrayBox* box_;
};

inline ArrayHandle* ArraySlot(char* s) {
  return reinterpret_cast<ArrayHandle*>(s);
}
inline const ArrayHandle* ArraySlot(const char* s) {
  return reinterpret_cast<const ArrayHandle*>(s);
}

// Copy-on-write: before handing out a mutable view, clone the buffer if it is
// shared with another Value so the mutation is private.
inline void DetachArray(char* s) { ArraySlot(s)->detach_if_shared(); }

// COW box for scalar POD payloads that exceed the (shrunk) 32-byte SBO — i.e.
// the large matrices (matrix3f/4f/3d/4d). Value redesign step 3. Scalars have no
// mutable accessor (they are replaced wholesale), so this is share-only COW: a
// copy just bumps the intrusive refcount, no detach needed. `alignas(16)` makes
// the 8-byte header round up to 16, so the payload is 16-aligned (safe for the
// double matrices). The SBO slot holds the ScalarBox* (8 bytes).
struct alignas(16) ScalarBox {
  std::atomic<uint32_t> rc;
  uint32_t size;
  void* data() { return this + 1; }
  const void* data() const { return this + 1; }
};
inline ScalarBox* scalar_box_alloc(const void* src, size_t bytes) {
  auto* b = static_cast<ScalarBox*>(::operator new(sizeof(ScalarBox) + bytes));
  new (&b->rc) std::atomic<uint32_t>(1);
  b->size = static_cast<uint32_t>(bytes);
  std::memcpy(b->data(), src, bytes);
  return b;
}
inline void scalar_box_retain(ScalarBox* b) {
  if (b) b->rc.fetch_add(1, std::memory_order_relaxed);
}
inline void scalar_box_release(ScalarBox* b) {
  if (b && b->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    ::operator delete(b);
  }
}
inline ScalarBox*& ScalarSlot(char* s) {
  return *reinterpret_cast<ScalarBox**>(s);
}
inline ScalarBox* ScalarSlot(const char* s) {
  return *reinterpret_cast<ScalarBox* const*>(s);
}

// A scalar POD type whose payload does not fit in the SBO (only matrices today).
// Strings/tokens/arrays/dicts have their own storage and are checked earlier, so
// callers only apply this to the "plain POD scalar" residual.
inline bool IsBoxedScalar(TypeId id) { return GetTypeSize(id) > Value::kSBOSize; }

// Pointer to a scalar's payload: the box buffer if boxed, else inline storage.
inline const void* scalar_data(TypeId id, const char* storage) {
  return IsBoxedScalar(id) ? ScalarSlot(storage)->data()
                           : static_cast<const void*>(storage);
}

// Dictionary values are held by a shared_ptr<Dict> in the SBO slot — the same
// copy-on-write model as arrays (cheap copy during composition; detach on first
// mutation). A nested dict is itself just another shared_ptr handle, so the
// recursive structure never threatens the SBO size.
using DictHandle = std::shared_ptr<Dict>;
inline DictHandle* DictSlot(char* s) {
  return reinterpret_cast<DictHandle*>(s);
}
inline const DictHandle* DictSlot(const char* s) {
  return reinterpret_cast<const DictHandle*>(s);
}
inline void DetachDict(char* s) {
  DictHandle& h = *DictSlot(s);
  if (h.use_count() > 1) h = std::make_shared<Dict>(*h);
}

// Check if type uses string storage. NOTE: Token is NOT here — a scalar token is
// stored as an inline 4-byte interned TfToken id (Value redesign step 2), so it
// flows through the inline-POD copy/move/destroy paths and gets explicit
// equality/hash/raw_bytes branches. String / AssetPath keep std::string storage.
bool UsesStringStorage(TypeId id) {
  return id == TypeId::String || id == TypeId::AssetPath;
}

// Array element types stored as a flat std::vector<float> (FloatArrayStorage):
// scalar floats plus all float vector/quat/matrix/color types.
bool IsFloatBackedArray(TypeId id) {
  switch (id) {
    case TypeId::Float:
    case TypeId::Float2:
    case TypeId::Float3:
    case TypeId::Float4:
    case TypeId::Point3f:
    case TypeId::Vector3f:
    case TypeId::Normal3f:
    case TypeId::Color3f:
    case TypeId::Color4f:
    case TypeId::Texcoord2f:
    case TypeId::Texcoord3f:
    case TypeId::Quatf:
    case TypeId::Matrix2f:
    case TypeId::Matrix3f:
    case TypeId::Matrix4f:
    // Half element types materialize into a float buffer (no 16-bit storage).
    case TypeId::Half:
    case TypeId::Half2:
    case TypeId::Half3:
    case TypeId::Half4:
    case TypeId::Point3h:
    case TypeId::Vector3h:
    case TypeId::Normal3h:
    case TypeId::Color3h:
    case TypeId::Color4h:
    case TypeId::Texcoord2h:
    case TypeId::Texcoord3h:
    case TypeId::Quath:
      return true;
    default:
      return false;
  }
}

// Array element types stored as a flat std::vector<double> (DoubleArrayStorage).
bool IsDoubleBackedArray(TypeId id) {
  switch (id) {
    case TypeId::Double:
    case TypeId::Double2:
    case TypeId::Double3:
    case TypeId::Double4:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d:
    case TypeId::Color3d:
    case TypeId::Color4d:
    case TypeId::Texcoord2d:
    case TypeId::Texcoord3d:
    case TypeId::Quatd:
    case TypeId::Matrix2d:
    case TypeId::Matrix3d:
    case TypeId::Matrix4d:
      return true;
    default:
      return false;
  }
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

Value::Value(std::string_view v) : type_id_(TypeId::String) {
  new (storage_) StringStorage{std::string(v)};
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
  v.type_id_ = TypeId::Matrix3f;  // 36 B > SBO -> boxed
  ScalarSlot(v.storage_) = scalar_box_alloc(data, 9 * sizeof(float));
  return v;
}

Value Value::MakeMatrix4f(const float* data) {
  Value v;
  v.type_id_ = TypeId::Matrix4f;  // 64 B > SBO -> boxed
  ScalarSlot(v.storage_) = scalar_box_alloc(data, 16 * sizeof(float));
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
  v.type_id_ = TypeId::Matrix3d;  // 72 B > SBO -> boxed
  ScalarSlot(v.storage_) = scalar_box_alloc(data, 9 * sizeof(double));
  return v;
}

Value Value::MakeMatrix4d(const double* data) {
  Value v;
  v.type_id_ = TypeId::Matrix4d;  // 128 B > SBO -> boxed
  ScalarSlot(v.storage_) = scalar_box_alloc(data, 16 * sizeof(double));
  return v;
}

// A scalar token is an interned TfToken (4-byte id) stored inline in the SBO —
// dedup + O(1) compare, and no per-token std::string. as_token() returns the
// stable table string so callers/the writer are unchanged.
Value Value::MakeToken(const std::string& s) {
  Value v;
  v.type_id_ = TypeId::Token;
  new (v.storage_) TfToken(s);
  return v;
}

Value Value::MakeToken(const char* s) {
  Value v;
  v.type_id_ = TypeId::Token;
  new (v.storage_) TfToken(s);
  return v;
}

Value Value::MakeToken(std::string_view s) {
  Value v;
  v.type_id_ = TypeId::Token;
  new (v.storage_) TfToken(s);
  return v;
}

Value Value::MakeToken(std::string&& s) {
  Value v;
  v.type_id_ = TypeId::Token;
  new (v.storage_) TfToken(s);
  return v;
}

Value Value::MakeAssetPath(const std::string& s) {
  Value v;
  v.type_id_ = TypeId::AssetPath;
  new (v.storage_) StringStorage{s};
  return v;
}

Value Value::MakeAssetPath(const char* s) {
  Value v;
  v.type_id_ = TypeId::AssetPath;
  new (v.storage_) StringStorage{std::string(s ? s : "")};
  return v;
}

Value Value::MakeAssetPath(std::string_view s) {
  Value v;
  v.type_id_ = TypeId::AssetPath;
  new (v.storage_) StringStorage{std::string(s)};
  return v;
}

Value Value::MakeAssetPath(std::string&& s) {
  Value v;
  v.type_id_ = TypeId::AssetPath;
  new (v.storage_) StringStorage{std::move(s)};
  return v;
}

Value Value::MakeDictionary() {
  Value v;
  v.type_id_ = TypeId::Dictionary;
  new (v.storage_) DictHandle(std::make_shared<Dict>());
  return v;
}

Value Value::MakeDictionary(Dict&& d) {
  Value v;
  v.type_id_ = TypeId::Dictionary;
  new (v.storage_) DictHandle(std::make_shared<Dict>(std::move(d)));
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
    if (size > kSBOSize) {
      ScalarSlot(v.storage_) = scalar_box_alloc(data, size);  // oversized -> box
    } else if (size > 0) {
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
  new (v.storage_) ArrayHandle(new FloatArrayStorage(data));
  return v;
}

Value Value::MakeFloatArray(std::vector<float>&& data) {
  Value v;
  v.type_id_ = TypeId::Float;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new FloatArrayStorage(std::move(data)));
  return v;
}

Value Value::MakeIntArray(const std::vector<int32_t>& data) {
  Value v;
  v.type_id_ = TypeId::Int;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new IntArrayStorage(data));
  return v;
}

Value Value::MakeIntArray(std::vector<int32_t>&& data) {
  Value v;
  v.type_id_ = TypeId::Int;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new IntArrayStorage(std::move(data)));
  return v;
}

Value Value::MakeFloat2Array(const std::vector<float>& data) {
  Value v;
  v.type_id_ = TypeId::Float2;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size() / 2);
  new (v.storage_) ArrayHandle(new FloatArrayStorage(data));
  return v;
}

Value Value::MakeFloat2Array(std::vector<float>&& data) {
  Value v;
  v.type_id_ = TypeId::Float2;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size() / 2);
  new (v.storage_) ArrayHandle(new FloatArrayStorage(std::move(data)));
  return v;
}

Value Value::MakeFloat3Array(const std::vector<float>& data) {
  Value v;
  v.type_id_ = TypeId::Float3;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size() / 3);
  new (v.storage_) ArrayHandle(new FloatArrayStorage(data));
  return v;
}

Value Value::MakeFloat3Array(std::vector<float>&& data) {
  Value v;
  v.type_id_ = TypeId::Float3;
  v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size() / 3);
  new (v.storage_) ArrayHandle(new FloatArrayStorage(std::move(data)));
  return v;
}

// New array types
Value Value::MakeDoubleArray(const std::vector<double>& data) {
  Value v; v.type_id_ = TypeId::Double; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new DoubleArrayStorage(data)); return v;
}
Value Value::MakeDoubleArray(std::vector<double>&& data) {
  Value v; v.type_id_ = TypeId::Double; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new DoubleArrayStorage(std::move(data))); return v;
}
Value Value::MakeInt64Array(const std::vector<int64_t>& data) {
  Value v; v.type_id_ = TypeId::Int64; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new Int64ArrayStorage(data)); return v;
}
Value Value::MakeInt64Array(std::vector<int64_t>&& data) {
  Value v; v.type_id_ = TypeId::Int64; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new Int64ArrayStorage(std::move(data))); return v;
}
Value Value::MakeUIntArray(const std::vector<uint32_t>& data) {
  Value v; v.type_id_ = TypeId::UInt; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new UIntArrayStorage(data)); return v;
}
Value Value::MakeUIntArray(std::vector<uint32_t>&& data) {
  Value v; v.type_id_ = TypeId::UInt; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new UIntArrayStorage(std::move(data))); return v;
}
Value Value::MakeUInt64Array(const std::vector<uint64_t>& data) {
  Value v; v.type_id_ = TypeId::UInt64; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new UInt64ArrayStorage(data)); return v;
}
Value Value::MakeUInt64Array(std::vector<uint64_t>&& data) {
  Value v; v.type_id_ = TypeId::UInt64; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new UInt64ArrayStorage(std::move(data))); return v;
}
Value Value::MakeBoolArray(const std::vector<bool>& data) {
  Value v; v.type_id_ = TypeId::Bool; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  std::vector<uint8_t> tmp(data.size());
  for (size_t i = 0; i < data.size(); i++) tmp[i] = data[i] ? 1 : 0;
  new (v.storage_) ArrayHandle(new BoolArrayStorage(std::move(tmp))); return v;
}
Value Value::MakeBoolArrayFromBytes(std::vector<uint8_t>&& data) {
  Value v; v.type_id_ = TypeId::Bool; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  for (uint8_t& b : data) b = b ? uint8_t{1} : uint8_t{0};
  new (v.storage_) ArrayHandle(new BoolArrayStorage(std::move(data))); return v;
}
// Both overloads intern each element into a TfToken (dedup + no per-element
// std::string). The std::vector<std::string> input API is unchanged.
static std::vector<TfToken> InternTokenVec(const std::vector<std::string>& data) {
  std::vector<TfToken> toks;
  toks.reserve(data.size());
  for (const std::string& s : data) toks.emplace_back(s);
  return toks;
}
Value Value::MakeTokenArray(const std::vector<std::string>& data) {
  Value v; v.type_id_ = TypeId::Token; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new TokenArrayStorage(InternTokenVec(data)));
  return v;
}
Value Value::MakeTokenArray(std::vector<std::string>&& data) {
  Value v; v.type_id_ = TypeId::Token; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new TokenArrayStorage(InternTokenVec(data)));
  return v;
}
Value Value::MakeTokenArray(std::vector<TfToken>&& data) {
  Value v; v.type_id_ = TypeId::Token; v.is_array_ = true;
  v.array_size_ = static_cast<uint32_t>(data.size());
  new (v.storage_) ArrayHandle(new TokenArrayStorage(std::move(data)));
  return v;
}

Value Value::MakeFloatCompArray(std::vector<float>&& data, TypeId elem_type,
                                uint32_t comps_per_elem) {
  Value v;
  v.type_id_ = elem_type;
  v.is_array_ = true;
  v.array_size_ = comps_per_elem
                      ? static_cast<uint32_t>(data.size() / comps_per_elem)
                      : 0;
  new (v.storage_) ArrayHandle(new FloatArrayStorage(std::move(data)));
  return v;
}

Value Value::MakeDoubleCompArray(std::vector<double>&& data, TypeId elem_type,
                                 uint32_t comps_per_elem) {
  Value v;
  v.type_id_ = elem_type;
  v.is_array_ = true;
  v.array_size_ = comps_per_elem
                      ? static_cast<uint32_t>(data.size() / comps_per_elem)
                      : 0;
  new (v.storage_) ArrayHandle(new DoubleArrayStorage(std::move(data)));
  return v;
}

namespace {

template <class T>
void MakeDeferredStorage(char* slot, Value::DeferredArrayFill* out_fill) {
  auto* box = new VecArrayStorage<T>();  // rc == 1, adopted by the SBO handle
  new (slot) ArrayHandle(box);
  out_fill->vec = &box->data;
  // keepalive keeps the box alive independent of the committed Value: take a
  // second intrusive ref and release it via the type-erased shared_ptr deleter.
  array_box_retain(box);  // rc == 2
  out_fill->keepalive = std::shared_ptr<void>(
      box, [](void* p) { array_box_release(static_cast<ArrayBox*>(p)); });
}

}  // namespace

Value Value::MakeDeferredArray(TypeId elem_type, ArrayScalarKind kind,
                               uint32_t elem_count,
                               DeferredArrayFill* out_fill) {
  Value v;
  v.type_id_ = elem_type;
  v.is_array_ = true;
  // Final size now; the payload arrives later through *out_fill. See the
  // header for the no-reads-before-fill contract.
  v.array_size_ = elem_count;
  switch (kind) {
    case ArrayScalarKind::Float:
      MakeDeferredStorage<float>(v.storage_, out_fill);
      break;
    case ArrayScalarKind::Double:
      MakeDeferredStorage<double>(v.storage_, out_fill);
      break;
    case ArrayScalarKind::Int32:
      MakeDeferredStorage<int32_t>(v.storage_, out_fill);
      break;
    case ArrayScalarKind::UInt32:
      MakeDeferredStorage<uint32_t>(v.storage_, out_fill);
      break;
    case ArrayScalarKind::Int64:
      MakeDeferredStorage<int64_t>(v.storage_, out_fill);
      break;
    case ArrayScalarKind::UInt64:
      MakeDeferredStorage<uint64_t>(v.storage_, out_fill);
      break;
  }
  return v;
}

// ============================================================
// Queries and accessors
// ============================================================

void Value::clear() {
  destroy();
  type_id_ = TypeId::Invalid;
  is_array_ = false;
  array_size_ = 0;
}

bool Value::uses_heap() const {
  return is_array_ || type_id_ == TypeId::Dictionary;
}

void Value::destroy() {
  if (is_lazy_) {
    LazyArrayRef* ptr;
    std::memcpy(&ptr, storage_, sizeof(ptr));
    delete ptr;  // drops the shared_ptr<CrateDataSource> reference
    type_id_ = TypeId::Invalid;
    is_array_ = false;
    is_lazy_ = false;
    dirty_ = false;
    array_size_ = 0;
    return;
  }

  if (type_id_ == TypeId::Invalid) {
    is_block_ = false;  // a value block holds no heap; just clear the marker.
    return;
  }

  if (is_array_) {
    // Drop the shared_ptr reference (frees the buffer iff this was the last
    // owner — the copy-on-write release).
    ArraySlot(storage_)->~ArrayHandle();
  } else if (type_id_ == TypeId::Dictionary) {
    DictSlot(storage_)->~DictHandle();
  } else if (UsesStringStorage(type_id_)) {
    reinterpret_cast<StringStorage*>(storage_)->~StringStorage();
  } else if (IsBoxedScalar(type_id_)) {
    scalar_box_release(ScalarSlot(storage_));  // COW release of the boxed matrix
  }

  type_id_ = TypeId::Invalid;
  is_array_ = false;
  dirty_ = false;
  array_size_ = 0;
}

void Value::copy_from(const Value& other) {
  type_id_ = other.type_id_;
  is_array_ = other.is_array_;
  is_lazy_ = other.is_lazy_;
  dirty_ = other.dirty_;
  is_block_ = other.is_block_;
  array_size_ = other.array_size_;

  if (other.is_lazy_) {
    LazyArrayRef* other_ptr;
    std::memcpy(&other_ptr, other.storage_, sizeof(other_ptr));
    auto* new_ptr = new LazyArrayRef(*other_ptr);  // shared_ptr refcount++
    std::memcpy(storage_, &new_ptr, sizeof(new_ptr));
    return;
  }

  if (other.is_array_) {
    // Copy-on-write: share the same buffer, just bump the refcount. The
    // element data is NOT copied; a later mutable access detaches.
    new (storage_) ArrayHandle(*ArraySlot(other.storage_));
  } else if (other.type_id_ == TypeId::Dictionary) {
    new (storage_) DictHandle(*DictSlot(other.storage_));  // refcount++ (CoW)
  } else if (UsesStringStorage(other.type_id_)) {
    new (storage_) StringStorage{reinterpret_cast<const StringStorage*>(other.storage_)->value};
  } else if (IsBoxedScalar(other.type_id_)) {
    // COW: share the boxed matrix buffer (bump refcount, no element copy).
    ScalarBox* b = ScalarSlot(other.storage_);
    scalar_box_retain(b);
    ScalarSlot(storage_) = b;
  } else {
    std::memcpy(storage_, other.storage_, kSBOSize);
  }
}

void Value::move_from(Value&& other) noexcept {
  type_id_ = other.type_id_;
  is_array_ = other.is_array_;
  is_lazy_ = other.is_lazy_;
  dirty_ = other.dirty_;
  is_block_ = other.is_block_;
  array_size_ = other.array_size_;

  if (other.is_array_ && !other.is_lazy_) {
    // Move the shared_ptr handle, then destroy the moved-from (now-empty) slot.
    new (storage_) ArrayHandle(std::move(*ArraySlot(other.storage_)));
    ArraySlot(other.storage_)->~ArrayHandle();
  } else if (other.is_lazy_) {
    // Steal the raw LazyArrayRef* (lazy arrays are not shared_ptr-backed).
    std::memcpy(storage_, other.storage_, sizeof(void*));
  } else if (other.type_id_ == TypeId::Dictionary) {
    new (storage_) DictHandle(std::move(*DictSlot(other.storage_)));
    DictSlot(other.storage_)->~DictHandle();
  } else if (UsesStringStorage(other.type_id_)) {
    new (storage_) StringStorage{std::move(reinterpret_cast<StringStorage*>(other.storage_)->value)};
    reinterpret_cast<StringStorage*>(other.storage_)->~StringStorage();
  } else {
    std::memcpy(storage_, other.storage_, kSBOSize);
  }

  other.type_id_ = TypeId::Invalid;
  other.is_array_ = false;
  other.is_lazy_ = false;
  other.dirty_ = false;
  other.is_block_ = false;
  other.array_size_ = 0;
}

// ============================================================
// Lazy array references
// ============================================================

Value Value::MakeBlock() {
  Value v;
  v.is_block_ = true;  // type_id_ stays Invalid, is_array_/is_lazy_ stay false:
  return v;            // no heap, destroy()/copy share the trivial-scalar path.
}

Value Value::MakeLazyArray(const LazyArrayRef& ref) {
  Value v;
  v.type_id_ = ref.value_type;
  v.is_array_ = true;
  v.is_lazy_ = true;
  v.dirty_ = false;
  v.array_size_ = static_cast<uint32_t>(ref.element_count);
  auto* ptr = new LazyArrayRef(ref);
  std::memcpy(v.storage_, &ptr, sizeof(ptr));
  return v;
}

const LazyArrayRef* Value::lazy_ref() const {
  if (!is_lazy_) return nullptr;
  LazyArrayRef* ptr;
  std::memcpy(&ptr, storage_, sizeof(ptr));
  return ptr;
}

void Value::materialize() {
  if (!is_lazy_) return;

  LazyArrayRef* ptr;
  std::memcpy(&ptr, storage_, sizeof(ptr));

  Value decoded;
  bool ok = (ptr && ptr->source) ? ptr->source->MaterializeArray(*ptr, &decoded) : false;

  delete ptr;  // releases the shared_ptr<CrateDataSource>

  // Reset to empty WITHOUT calling destroy() (storage_ now dangles) then adopt
  // the decoded value. If decode failed the value becomes empty.
  is_lazy_ = false;
  type_id_ = TypeId::Invalid;
  is_array_ = false;
  dirty_ = false;
  array_size_ = 0;
  if (ok) {
    move_from(std::move(decoded));
  }
}

void Value::ensure_materialized() const {
  if (is_lazy_) {
    const_cast<Value*>(this)->materialize();
  }
}

Value Value::materialized_copy() const {
  if (is_lazy_) {
    const LazyArrayRef* ref = lazy_ref();
    Value decoded;
    if (ref && ref->source && ref->source->MaterializeArray(*ref, &decoded)) {
      return decoded;  // NRVO / move: steals the decoded array handle.
    }
    return Value();  // decode failed -> empty (prints "None")
  }
  // Non-lazy: ordinary CoW copy (array refcount bump; preserves dirty_).
  return *this;
}

void* Value::data_ptr() {
  ensure_materialized();
  if (is_array_) {
    DetachArray(storage_);  // mutable raw access: privatize the buffer
    return ArraySlot(storage_)->get();
  }
  if (type_id_ == TypeId::Dictionary) {
    DetachDict(storage_);
    return DictSlot(storage_)->get();
  }
  // Boxed scalar (oversized matrix): return the box buffer, not the SBO pointer.
  if (IsBoxedScalar(type_id_)) return ScalarSlot(storage_)->data();
  return storage_;
}

const void* Value::data_ptr() const {
  ensure_materialized();
  if (is_array_) {
    return ArraySlot(storage_)->get();
  }
  if (type_id_ == TypeId::Dictionary) {
    return DictSlot(storage_)->get();
  }
  if (IsBoxedScalar(type_id_)) return ScalarSlot(storage_)->data();
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
  // Return the stable, table-owned interned string for the inline TfToken id.
  return &reinterpret_cast<const TfToken*>(storage_)->str();
}

const std::string* Value::as_asset_path() const {
  if (type_id_ != TypeId::AssetPath || is_array_) return nullptr;
  return &reinterpret_cast<const StringStorage*>(storage_)->value;
}

const Dict* Value::as_dictionary() const {
  if (type_id_ != TypeId::Dictionary) return nullptr;
  return DictSlot(storage_)->get();
}

Dict* Value::as_dictionary() {
  if (type_id_ != TypeId::Dictionary) return nullptr;
  DetachDict(storage_);
  return DictSlot(storage_)->get();
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
  return reinterpret_cast<const float*>(scalar_data(type_id_, storage_));
}

const float* Value::as_matrix4f() const {
  if (type_id_ != TypeId::Matrix4f || is_array_) return nullptr;
  return reinterpret_cast<const float*>(scalar_data(type_id_, storage_));
}

const double* Value::as_matrix2d() const {
  if (type_id_ != TypeId::Matrix2d || is_array_) return nullptr;
  return reinterpret_cast<const double*>(storage_);
}

const double* Value::as_matrix3d() const {
  if (type_id_ != TypeId::Matrix3d || is_array_) return nullptr;
  return reinterpret_cast<const double*>(scalar_data(type_id_, storage_));
}

const double* Value::as_matrix4d() const {
  if (type_id_ != TypeId::Matrix4d || is_array_) return nullptr;
  return reinterpret_cast<const double*>(scalar_data(type_id_, storage_));
}

// Array accessors
const std::vector<float>* Value::as_float_array() const {
  ensure_materialized();
  if (!is_array_ || !IsFloatBackedArray(type_id_)) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<FloatArrayStorage*>(ptr)->data;
}

std::vector<float>* Value::as_float_array() {
  ensure_materialized();
  dirty_ = true;
  if (!is_array_ || !IsFloatBackedArray(type_id_)) return nullptr;
  DetachArray(storage_);
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<FloatArrayStorage*>(ptr)->data;
}

const std::vector<int32_t>* Value::as_int_array() const {
  ensure_materialized();
  if (type_id_ != TypeId::Int || !is_array_) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<IntArrayStorage*>(ptr)->data;
}

std::vector<int32_t>* Value::as_int_array() {
  ensure_materialized();
  dirty_ = true;
  if (type_id_ != TypeId::Int || !is_array_) return nullptr;
  DetachArray(storage_);
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<IntArrayStorage*>(ptr)->data;
}

const std::vector<double>* Value::as_double_array() const {
  ensure_materialized();
  if (!is_array_ || !IsDoubleBackedArray(type_id_)) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<DoubleArrayStorage*>(ptr)->data;
}
std::vector<double>* Value::as_double_array() {
  ensure_materialized(); dirty_ = true;
  if (!is_array_ || !IsDoubleBackedArray(type_id_)) return nullptr;
  DetachArray(storage_);
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<DoubleArrayStorage*>(ptr)->data;
}
const std::vector<int64_t>* Value::as_int64_array() const {
  ensure_materialized();
  if (type_id_ != TypeId::Int64 || !is_array_) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<Int64ArrayStorage*>(ptr)->data;
}
std::vector<int64_t>* Value::as_int64_array() {
  ensure_materialized(); dirty_ = true;
  if (type_id_ != TypeId::Int64 || !is_array_) return nullptr;
  DetachArray(storage_);
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<Int64ArrayStorage*>(ptr)->data;
}
const std::vector<uint32_t>* Value::as_uint_array() const {
  ensure_materialized();
  if (type_id_ != TypeId::UInt || !is_array_) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<UIntArrayStorage*>(ptr)->data;
}
std::vector<uint32_t>* Value::as_uint_array() {
  ensure_materialized(); dirty_ = true;
  if (type_id_ != TypeId::UInt || !is_array_) return nullptr;
  DetachArray(storage_);
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<UIntArrayStorage*>(ptr)->data;
}
const std::vector<uint64_t>* Value::as_uint64_array() const {
  ensure_materialized();
  if (type_id_ != TypeId::UInt64 || !is_array_) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<UInt64ArrayStorage*>(ptr)->data;
}
std::vector<uint64_t>* Value::as_uint64_array() {
  ensure_materialized(); dirty_ = true;
  if (type_id_ != TypeId::UInt64 || !is_array_) return nullptr;
  DetachArray(storage_);
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<UInt64ArrayStorage*>(ptr)->data;
}
const std::vector<uint8_t>* Value::as_bool_array() const {
  ensure_materialized();
  if (type_id_ != TypeId::Bool || !is_array_) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<BoolArrayStorage*>(ptr)->data;
}
const std::vector<TfToken>* Value::as_token_array() const {
  ensure_materialized();
  if (type_id_ != TypeId::Token || !is_array_) return nullptr;
  ArrayBox* ptr = ArraySlot(storage_)->get();
  return &static_cast<TokenArrayStorage*>(ptr)->data;
}

// ============================================================
// Comparison
// ============================================================

bool Value::operator==(const Value& other) const {
  ensure_materialized();
  other.ensure_materialized();
  if (type_id_ != other.type_id_) return false;
  if (is_array_ != other.is_array_) return false;
  if (is_block_ != other.is_block_) return false;  // block != declared-only/empty
  if (is_array_ && array_size_ != other.array_size_) return false;
  if (type_id_ == TypeId::Invalid) return true;

  if (is_array_) {
    if (IsFloatBackedArray(type_id_)) {
      return *as_float_array() == *other.as_float_array();
    } else if (IsDoubleBackedArray(type_id_)) {
      return *as_double_array() == *other.as_double_array();
    } else if (type_id_ == TypeId::Int) {
      return *as_int_array() == *other.as_int_array();
    } else if (type_id_ == TypeId::Int64) {
      return *as_int64_array() == *other.as_int64_array();
    } else if (type_id_ == TypeId::UInt) {
      return *as_uint_array() == *other.as_uint_array();
    } else if (type_id_ == TypeId::UInt64) {
      return *as_uint64_array() == *other.as_uint64_array();
    } else if (type_id_ == TypeId::Bool) {
      return *as_bool_array() == *other.as_bool_array();
    } else if (type_id_ == TypeId::Token) {
      return *as_token_array() == *other.as_token_array();
    }
    return false;
  }

  if (type_id_ == TypeId::Dictionary) {
    const Dict* a = as_dictionary();
    const Dict* b = other.as_dictionary();
    if (!a || !b) return a == b;
    if (a == b) return true;  // same shared buffer
    if (a->entries.size() != b->entries.size()) return false;
    for (size_t i = 0; i < a->entries.size(); ++i) {
      if (a->entries[i].first != b->entries[i].first) return false;
      if (!(a->entries[i].second == b->entries[i].second)) return false;
    }
    return true;
  }

  if (type_id_ == TypeId::Token) {
    // Interned: equal ids iff equal strings.
    return reinterpret_cast<const TfToken*>(storage_)->id() ==
           reinterpret_cast<const TfToken*>(other.storage_)->id();
  }

  if (UsesStringStorage(type_id_)) {
    return reinterpret_cast<const StringStorage*>(storage_)->value ==
           reinterpret_cast<const StringStorage*>(other.storage_)->value;
  }

  size_t size = GetTypeSize(type_id_);
  if (size > 0) {
    return std::memcmp(scalar_data(type_id_, storage_),
                       scalar_data(other.type_id_, other.storage_), size) == 0;
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
  ensure_materialized();
  if (type_id_ == TypeId::Invalid) return 0;

  // Include type in hash
  uint64_t h = static_cast<uint64_t>(type_id_) | (is_array_ ? 0x100 : 0);

  if (is_array_) {
    // Hash array contents
    if (IsFloatBackedArray(type_id_)) {
      const auto* arr = as_float_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(float));
      }
    } else if (IsDoubleBackedArray(type_id_)) {
      const auto* arr = as_double_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(double));
      }
    } else if (type_id_ == TypeId::Int) {
      const auto* arr = as_int_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(int32_t));
      }
    } else if (type_id_ == TypeId::Int64) {
      const auto* arr = as_int64_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(int64_t));
      }
    } else if (type_id_ == TypeId::UInt) {
      const auto* arr = as_uint_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(uint32_t));
      }
    } else if (type_id_ == TypeId::UInt64) {
      const auto* arr = as_uint64_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(uint64_t));
      }
    } else if (type_id_ == TypeId::Bool) {
      const auto* arr = as_bool_array();
      if (arr && !arr->empty()) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(arr->data()),
                        arr->size() * sizeof(uint8_t));
      }
    } else if (type_id_ == TypeId::Token) {
      const auto* arr = as_token_array();
      if (arr) {
        // Hash the token strings (unchanged hash vs. the former string array).
        for (const TfToken& t : *arr) {
          const std::string& s = t.str();
          h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(s.data()),
                          s.size());
          h *= 1099511628211ULL;
        }
      }
    }
    return h;
  }

  // Hash dictionary entries recursively (order-sensitive, matching operator==).
  if (type_id_ == TypeId::Dictionary) {
    if (const Dict* d = as_dictionary()) {
      for (const auto& kv : d->entries) {
        h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(kv.first.data()),
                        kv.first.size());
        h *= 1099511628211ULL;
        h ^= kv.second.hash();
        h *= 1099511628211ULL;
      }
    }
    return h;
  }

  // Scalar token: hash the interned id (self-consistent within a run).
  if (type_id_ == TypeId::Token) {
    const uint32_t id = reinterpret_cast<const TfToken*>(storage_)->id();
    h ^= fnv1a_hash(reinterpret_cast<const uint8_t*>(&id), sizeof(id));
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

  // Hash scalar/vector types from storage (box buffer if the scalar is boxed).
  size_t size = GetTypeSize(type_id_);
  if (size > 0) {
    h ^= fnv1a_hash(
        reinterpret_cast<const uint8_t*>(scalar_data(type_id_, storage_)), size);
  }

  return h;
}

const uint8_t* Value::raw_bytes(size_t* out_size) const {
  if (!out_size) return nullptr;
  *out_size = 0;

  ensure_materialized();
  if (type_id_ == TypeId::Invalid) return nullptr;

  if (is_array_) {
    if (IsFloatBackedArray(type_id_)) {
      const auto* arr = as_float_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(float);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (IsDoubleBackedArray(type_id_)) {
      const auto* arr = as_double_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(double);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (type_id_ == TypeId::Int) {
      const auto* arr = as_int_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(int32_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (type_id_ == TypeId::Int64) {
      const auto* arr = as_int64_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(int64_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (type_id_ == TypeId::UInt) {
      const auto* arr = as_uint_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(uint32_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (type_id_ == TypeId::UInt64) {
      const auto* arr = as_uint64_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(uint64_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    } else if (type_id_ == TypeId::Bool) {
      const auto* arr = as_bool_array();
      if (arr && !arr->empty()) {
        *out_size = arr->size() * sizeof(uint8_t);
        return reinterpret_cast<const uint8_t*>(arr->data());
      }
    }
    return nullptr;
  }

  if (type_id_ == TypeId::Token) {
    // Inline interned id (4 bytes); self-consistent for dedup within a run.
    *out_size = sizeof(uint32_t);
    return reinterpret_cast<const uint8_t*>(storage_);
  }

  if (UsesStringStorage(type_id_)) {
    const auto& s = reinterpret_cast<const StringStorage*>(storage_)->value;
    *out_size = s.size();
    return reinterpret_cast<const uint8_t*>(s.data());
  }

  size_t size = GetTypeSize(type_id_);
  if (size > 0) {
    *out_size = size;
    return reinterpret_cast<const uint8_t*>(scalar_data(type_id_, storage_));
  }

  return nullptr;
}

Value LerpValue(const Value& a, const Value& b, double t) {
  if (a.type_id() != b.type_id()) return a;  // held on type mismatch
  const double s = 1.0 - t;
  auto lf = [&](float x, float y) { return static_cast<float>(s * x + t * y); };
  auto ld = [&](double x, double y) { return s * x + t * y; };

  switch (a.type_id()) {
    case TypeId::Float: {
      const float* pa = a.as_float();
      const float* pb = b.as_float();
      if (pa && pb) return Value(lf(*pa, *pb));
      break;
    }
    case TypeId::Double: {
      const double* pa = a.as_double();
      const double* pb = b.as_double();
      if (pa && pb) return Value(ld(*pa, *pb));
      break;
    }
    case TypeId::Float2: {
      const float* pa = a.as_float2();
      const float* pb = b.as_float2();
      if (pa && pb) return Value::MakeFloat2(lf(pa[0], pb[0]), lf(pa[1], pb[1]));
      break;
    }
    case TypeId::Float3:
    case TypeId::Point3f:
    case TypeId::Vector3f:
    case TypeId::Normal3f:
    case TypeId::Color3f: {
      const float* pa = a.as_float3();
      const float* pb = b.as_float3();
      if (!pa || !pb) break;
      const float x = lf(pa[0], pb[0]), y = lf(pa[1], pb[1]), z = lf(pa[2], pb[2]);
      switch (a.type_id()) {
        case TypeId::Point3f: return Value::MakePoint3f(x, y, z);
        case TypeId::Vector3f: return Value::MakeVector3f(x, y, z);
        case TypeId::Normal3f: return Value::MakeNormal3f(x, y, z);
        case TypeId::Color3f: return Value::MakeColor3f(x, y, z);
        default: return Value::MakeFloat3(x, y, z);
      }
    }
    case TypeId::Float4:
    case TypeId::Color4f: {
      const float* pa = a.as_float4();
      const float* pb = b.as_float4();
      if (!pa || !pb) break;
      const float x = lf(pa[0], pb[0]), y = lf(pa[1], pb[1]);
      const float z = lf(pa[2], pb[2]), w = lf(pa[3], pb[3]);
      if (a.type_id() == TypeId::Color4f) return Value::MakeColor4f(x, y, z, w);
      return Value::MakeFloat4(x, y, z, w);
    }
    case TypeId::Double2: {
      const double* pa = a.as_double2();
      const double* pb = b.as_double2();
      if (pa && pb) return Value::MakeDouble2(ld(pa[0], pb[0]), ld(pa[1], pb[1]));
      break;
    }
    case TypeId::Double3:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d: {
      const double* pa = a.as_double3();
      const double* pb = b.as_double3();
      if (!pa || !pb) break;
      const double x = ld(pa[0], pb[0]), y = ld(pa[1], pb[1]), z = ld(pa[2], pb[2]);
      switch (a.type_id()) {
        case TypeId::Point3d: return Value::MakePoint3d(x, y, z);
        case TypeId::Vector3d: return Value::MakeVector3d(x, y, z);
        case TypeId::Normal3d: return Value::MakeNormal3d(x, y, z);
        default: return Value::MakeDouble3(x, y, z);
      }
    }
    case TypeId::Double4: {
      const double* pa = a.as_double4();
      const double* pb = b.as_double4();
      if (pa && pb)
        return Value::MakeDouble4(ld(pa[0], pb[0]), ld(pa[1], pb[1]),
                                  ld(pa[2], pb[2]), ld(pa[3], pb[3]));
      break;
    }
    case TypeId::Matrix4f: {
      const float* pa = a.as_matrix4f();
      const float* pb = b.as_matrix4f();
      if (!pa || !pb) break;
      float m[16];
      for (int i = 0; i < 16; ++i) m[i] = lf(pa[i], pb[i]);
      return Value::MakeMatrix4f(m);
    }
    case TypeId::Matrix4d: {
      const double* pa = a.as_matrix4d();
      const double* pb = b.as_matrix4d();
      if (!pa || !pb) break;
      double m[16];
      for (int i = 0; i < 16; ++i) m[i] = ld(pa[i], pb[i]);
      return Value::MakeMatrix4d(m);
    }
    default:
      break;  // non-interpolatable (int/bool/string/token/quat/...) -> held
  }
  return a;
}

}  // namespace next
}  // namespace tinyusdz
