// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment, Inc.
//
// MMap zero-copy array reference types for USDC reading.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace tinyusdz {

/// Lightweight descriptor for an uncompressed array in an mmap'd USDC file.
/// 24 bytes vs potentially 100+ MB for the actual data.
struct MMapArrayRef {
  uint64_t byte_offset{0};   // Offset from mmap base to first element
  uint64_t element_count{0}; // Number of elements
  uint32_t element_size{0};  // sizeof(element), e.g. 12 for float3
  uint32_t type_id{0};       // value::TypeId for validation
};

/// Maps "prim_path\0attr_name" -> MMapArrayRef
class MMapArrayTable {
 public:
  void add(const std::string &prim_path, const std::string &attr_name,
           const MMapArrayRef &ref) {
    _entries[make_key(prim_path, attr_name)] = ref;
  }

  const MMapArrayRef *find(const std::string &prim_path,
                           const std::string &attr_name) const {
    auto it = _entries.find(make_key(prim_path, attr_name));
    if (it != _entries.end()) {
      return &it->second;
    }
    return nullptr;
  }

  bool empty() const { return _entries.empty(); }
  size_t size() const { return _entries.size(); }

 private:
  static std::string make_key(const std::string &p, const std::string &a) {
    // \0 can't appear in USD paths, so this produces a unique key
    std::string key;
    key.reserve(p.size() + 1 + a.size());
    key.append(p);
    key.push_back('\0');
    key.append(a);
    return key;
  }

  std::unordered_map<std::string, MMapArrayRef> _entries;
};

/// Wraps raw mmap pointer for safe typed access.
class MMapDataSource {
 public:
  MMapDataSource() = default;
  MMapDataSource(const uint8_t *addr, uint64_t size)
      : _addr(addr), _size(size) {}

  bool is_valid() const { return _addr && _size > 0; }
  const uint8_t *addr() const { return _addr; }
  uint64_t size() const { return _size; }

  /// Get a raw pointer to `count` elements of type T at the given ref.
  /// Returns nullptr on any bounds/alignment/type-size error.
  template <typename T>
  const T *get_ptr(const MMapArrayRef &ref) const {
    if (!is_valid()) return nullptr;
    if (ref.element_size != sizeof(T)) return nullptr;
    uint64_t end = ref.byte_offset + ref.element_count * sizeof(T);
    if (end > _size) return nullptr;
    auto ptr = reinterpret_cast<const T *>(_addr + ref.byte_offset);
    // Alignment check
    if (reinterpret_cast<uintptr_t>(ptr) % alignof(T) != 0) return nullptr;
    return ptr;
  }

 private:
  const uint8_t *_addr{nullptr};
  uint64_t _size{0};
};

}  // namespace tinyusdz
