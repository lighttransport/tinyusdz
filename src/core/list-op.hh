// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// list-op.hh - ListOp template for USD list operations
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "value-types.hh"

namespace tinyusdz {

template <typename T>
class ListOp {
 public:
  ListOp() : is_explicit(false) {}

  void ClearAndMakeExplicit() {
    explicit_items.clear();
    added_items.clear();
    prepended_items.clear();
    appended_items.clear();
    deleted_items.clear();
    ordered_items.clear();

    is_explicit = true;
  }

  bool IsExplicit() const { return is_explicit; }
  bool HasExplicitItems() const { return explicit_items.size(); }

  bool HasAddedItems() const { return added_items.size(); }

  bool HasPrependedItems() const { return prepended_items.size(); }

  bool HasAppendedItems() const { return appended_items.size(); }

  bool HasDeletedItems() const { return deleted_items.size(); }

  bool HasOrderedItems() const { return ordered_items.size(); }

  const std::vector<T> &GetExplicitItems() const { return explicit_items; }

  const std::vector<T> &GetAddedItems() const { return added_items; }

  const std::vector<T> &GetPrependedItems() const { return prepended_items; }

  const std::vector<T> &GetAppendedItems() const { return appended_items; }

  const std::vector<T> &GetDeletedItems() const { return deleted_items; }

  const std::vector<T> &GetOrderedItems() const { return ordered_items; }

  void SetExplicitItems(const std::vector<T> &v) { explicit_items = v; }

  void SetAddedItems(const std::vector<T> &v) { added_items = v; }

  void SetPrependedItems(const std::vector<T> &v) { prepended_items = v; }

  void SetAppendedItems(const std::vector<T> &v) { appended_items = v; }

  void SetDeletedItems(const std::vector<T> &v) { deleted_items = v; }

  void SetOrderedItems(const std::vector<T> &v) { ordered_items = v; }

 private:
  bool is_explicit{false};
  std::vector<T> explicit_items;
  std::vector<T> added_items;
  std::vector<T> prepended_items;
  std::vector<T> appended_items;
  std::vector<T> deleted_items;
  std::vector<T> ordered_items;
};

struct ListOpHeader {
  enum Bits {
    IsExplicitBit = 1 << 0,
    HasExplicitItemsBit = 1 << 1,
    HasAddedItemsBit = 1 << 2,
    HasDeletedItemsBit = 1 << 3,
    HasOrderedItemsBit = 1 << 4,
    HasPrependedItemsBit = 1 << 5,
    HasAppendedItemsBit = 1 << 6
  };

  ListOpHeader() : bits(0) {}

  explicit ListOpHeader(uint8_t b) : bits(b) {}

  // Implicit copy so NRVO can apply at return sites without requiring
  // explicit reconstruction. Body preserved for parity with previous
  // per-flag rebuild path.
  ListOpHeader(ListOpHeader const &op) : bits(0) {
    bits |= op.IsExplicit() ? IsExplicitBit : 0;
    bits |= op.HasExplicitItems() ? HasExplicitItemsBit : 0;
    bits |= op.HasAddedItems() ? HasAddedItemsBit : 0;
    bits |= op.HasPrependedItems() ? HasPrependedItemsBit : 0;
    bits |= op.HasAppendedItems() ? HasAppendedItemsBit : 0;
    bits |= op.HasDeletedItems() ? HasDeletedItemsBit : 0;
    bits |= op.HasOrderedItems() ? HasOrderedItemsBit : 0;
  }

  bool IsExplicit() const { return bits & IsExplicitBit; }

  bool HasExplicitItems() const { return bits & HasExplicitItemsBit; }
  bool HasAddedItems() const { return bits & HasAddedItemsBit; }
  bool HasPrependedItems() const { return bits & HasPrependedItemsBit; }
  bool HasAppendedItems() const { return bits & HasAppendedItemsBit; }
  bool HasDeletedItems() const { return bits & HasDeletedItemsBit; }
  bool HasOrderedItems() const { return bits & HasOrderedItemsBit; }

  uint8_t bits;
};

///
/// Compose (reduce) a ListOp into a flat result vector.
///
/// Per AOUSD Core Spec 6.6.3:
///   - If explicit: result = explicit_items
///   - Otherwise: result = prepended + added + (existing minus deleted) + appended
///   - Then apply ordering if ordered_items present
///
/// @param[in] op The ListOp to reduce
/// @param[in] existing Existing items from weaker opinion (default: empty)
/// @return The composed flat list
///
template <typename T>
std::vector<T> ComposeListOp(const ListOp<T> &op,
                              const std::vector<T> &existing = {}) {
  if (op.IsExplicit()) {
    return op.GetExplicitItems();
  }

  std::vector<T> result;

  // Prepend
  for (const auto &item : op.GetPrependedItems()) {
    result.push_back(item);
  }

  // Added (treat as appended per spec deprecation 6.6.3.10)
  // and existing items minus deleted
  auto is_deleted = [&op](const T &item) {
    for (const auto &d : op.GetDeletedItems()) {
      if (d == item) return true;
    }
    return false;
  };

  for (const auto &item : existing) {
    if (!is_deleted(item)) {
      // Check not already in prepended
      bool already_prepended = false;
      for (const auto &p : op.GetPrependedItems()) {
        if (p == item) { already_prepended = true; break; }
      }
      if (!already_prepended) {
        result.push_back(item);
      }
    }
  }

  for (const auto &item : op.GetAddedItems()) {
    // Add only if not already present
    bool found = false;
    for (const auto &r : result) {
      if (r == item) { found = true; break; }
    }
    if (!found) {
      result.push_back(item);
    }
  }

  // Append
  for (const auto &item : op.GetAppendedItems()) {
    // Remove from current position if exists, then append
    auto it = result.begin();
    while (it != result.end()) {
      if (*it == item) { it = result.erase(it); } else { ++it; }
    }
    result.push_back(item);
  }

  // Ordering (reorder items to match ordered_items order)
  if (op.HasOrderedItems()) {
    const auto &order = op.GetOrderedItems();
    std::vector<T> ordered;

    // First add items in the specified order
    for (const auto &o : order) {
      for (const auto &r : result) {
        if (r == o) { ordered.push_back(r); break; }
      }
    }
    // Then add remaining items not in the order list
    for (const auto &r : result) {
      bool in_order = false;
      for (const auto &o : order) {
        if (r == o) { in_order = true; break; }
      }
      if (!in_order) { ordered.push_back(r); }
    }
    result = std::move(ordered);
  }

  return result;
}

// Forward declarations needed for ListOp type traits
class Path;
struct Reference;
struct Payload;

namespace value {

#include "define-type-trait.inc"

DEFINE_TYPE_TRAIT(ListOp<value::token>, "ListOpToken", TYPE_ID_LIST_OP_TOKEN,
                  1);
DEFINE_TYPE_TRAIT(ListOp<std::string>, "ListOpString", TYPE_ID_LIST_OP_STRING,
                  1);
DEFINE_TYPE_TRAIT(ListOp<Path>, "ListOpPath", TYPE_ID_LIST_OP_PATH, 1);
DEFINE_TYPE_TRAIT(ListOp<Reference>, "ListOpReference",
                  TYPE_ID_LIST_OP_REFERENCE, 1);
DEFINE_TYPE_TRAIT(ListOp<int32_t>, "ListOpInt", TYPE_ID_LIST_OP_INT, 1);
DEFINE_TYPE_TRAIT(ListOp<uint32_t>, "ListOpUInt", TYPE_ID_LIST_OP_UINT, 1);
DEFINE_TYPE_TRAIT(ListOp<int64_t>, "ListOpInt64", TYPE_ID_LIST_OP_INT64, 1);
DEFINE_TYPE_TRAIT(ListOp<uint64_t>, "ListOpUInt64", TYPE_ID_LIST_OP_UINT64, 1);
DEFINE_TYPE_TRAIT(ListOp<Payload>, "ListOpPayload", TYPE_ID_LIST_OP_PAYLOAD, 1);

#undef DEFINE_TYPE_TRAIT
#undef DEFINE_ROLE_TYPE_TRAIT

}  // namespace value

}  // namespace tinyusdz
