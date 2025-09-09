// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.

///
/// @file list-op.hh
/// @brief USD List Operation classes
///
/// Contains ListOp template class and ListOpHeader for managing USD list operations.
/// ListOp provides functionality for list editing operations like prepend, append,
/// add, delete, and explicit list assignments used throughout USD composition.
///
#pragma once

#include <vector>
#include <cstdint>

namespace tinyusdz {

///
/// @brief Template class for USD list operations
///
/// ListOp provides a way to edit lists through various operations:
/// - Explicit: Replace entire list
/// - Prepend: Add items to beginning
/// - Append: Add items to end  
/// - Add: Add items (order unspecified)
/// - Delete: Remove items
/// - Ordered: Reorder items
///
/// This is fundamental to USD's composition system where list edits
/// from different layers are combined.
///
template <typename T>
class ListOp {
 public:
  ///
  /// Default constructor
  ///
  ListOp() : is_explicit(false) {}

  ///
  /// Clear all items and set to explicit mode
  ///
  void ClearAndMakeExplicit() {
    explicit_items.clear();
    added_items.clear();
    prepended_items.clear();
    appended_items.clear();
    deleted_items.clear();
    ordered_items.clear();

    is_explicit = true;
  }

  ///
  /// Check if this is an explicit list operation
  ///
  bool IsExplicit() const { return is_explicit; }
  
  ///
  /// Check if explicit items are present
  ///
  bool HasExplicitItems() const { return explicit_items.size(); }

  ///
  /// Check if added items are present
  ///
  bool HasAddedItems() const { return added_items.size(); }

  ///
  /// Check if prepended items are present
  ///
  bool HasPrependedItems() const { return prepended_items.size(); }

  ///
  /// Check if appended items are present
  ///
  bool HasAppendedItems() const { return appended_items.size(); }

  ///
  /// Check if deleted items are present
  ///
  bool HasDeletedItems() const { return deleted_items.size(); }

  ///
  /// Check if ordered items are present
  ///
  bool HasOrderedItems() const { return ordered_items.size(); }

  ///
  /// Get explicit items (const)
  ///
  const std::vector<T> &GetExplicitItems() const { return explicit_items; }

  ///
  /// Get added items (const)
  ///
  const std::vector<T> &GetAddedItems() const { return added_items; }

  ///
  /// Get prepended items (const)
  ///
  const std::vector<T> &GetPrependedItems() const { return prepended_items; }

  ///
  /// Get appended items (const)
  ///
  const std::vector<T> &GetAppendedItems() const { return appended_items; }

  ///
  /// Get deleted items (const)
  ///
  const std::vector<T> &GetDeletedItems() const { return deleted_items; }

  ///
  /// Get ordered items (const)
  ///
  const std::vector<T> &GetOrderedItems() const { return ordered_items; }

  ///
  /// Set explicit items
  ///
  void SetExplicitItems(const std::vector<T> &v) { explicit_items = v; }

  ///
  /// Set added items
  ///
  void SetAddedItems(const std::vector<T> &v) { added_items = v; }

  ///
  /// Set prepended items
  ///
  void SetPrependedItems(const std::vector<T> &v) { prepended_items = v; }

  ///
  /// Set appended items
  ///
  void SetAppendedItems(const std::vector<T> &v) { appended_items = v; }

  ///
  /// Set deleted items
  ///
  void SetDeletedItems(const std::vector<T> &v) { deleted_items = v; }

  ///
  /// Set ordered items
  ///
  void SetOrderedItems(const std::vector<T> &v) { ordered_items = v; }

 private:
  bool is_explicit{false};          ///< True if this is an explicit list operation
  std::vector<T> explicit_items;    ///< Items for explicit replacement
  std::vector<T> added_items;       ///< Items to add
  std::vector<T> prepended_items;   ///< Items to prepend
  std::vector<T> appended_items;    ///< Items to append
  std::vector<T> deleted_items;     ///< Items to delete
  std::vector<T> ordered_items;     ///< Items for reordering
};

///
/// @brief Header structure for ListOp serialization
///
/// ListOpHeader provides bit flags to efficiently encode which types
/// of list operations are present in a ListOp. Used for binary serialization.
///
struct ListOpHeader {
  ///
  /// Bit flags for different list operation types
  ///
  enum Bits {
    IsExplicitBit = 1 << 0,        ///< Explicit list replacement
    HasExplicitItemsBit = 1 << 1,  ///< Has explicit items
    HasAddedItemsBit = 1 << 2,     ///< Has added items
    HasDeletedItemsBit = 1 << 3,   ///< Has deleted items
    HasOrderedItemsBit = 1 << 4,   ///< Has ordered items
    HasPrependedItemsBit = 1 << 5, ///< Has prepended items
    HasAppendedItemsBit = 1 << 6   ///< Has appended items
  };

  ///
  /// Default constructor
  ///
  ListOpHeader() : bits(0) {}

  ///
  /// Constructor from bits
  ///
  explicit ListOpHeader(uint8_t b) : bits(b) {}

  ///
  /// Copy constructor from another ListOpHeader
  ///
  explicit ListOpHeader(ListOpHeader const &op) : bits(0) {
    bits |= op.IsExplicit() ? IsExplicitBit : 0;
    bits |= op.HasExplicitItems() ? HasExplicitItemsBit : 0;
    bits |= op.HasAddedItems() ? HasAddedItemsBit : 0;
    bits |= op.HasPrependedItems() ? HasPrependedItemsBit : 0;
    bits |= op.HasAppendedItems() ? HasAppendedItemsBit : 0;
    bits |= op.HasDeletedItems() ? HasDeletedItemsBit : 0;
    bits |= op.HasOrderedItems() ? HasOrderedItemsBit : 0;
  }

  ///
  /// Check if this is an explicit list operation
  ///
  bool IsExplicit() const { return bits & IsExplicitBit; }

  ///
  /// Check if explicit items are present
  ///
  bool HasExplicitItems() const { return bits & HasExplicitItemsBit; }
  
  ///
  /// Check if added items are present
  ///
  bool HasAddedItems() const { return bits & HasAddedItemsBit; }
  
  ///
  /// Check if prepended items are present
  ///
  bool HasPrependedItems() const { return bits & HasPrependedItemsBit; }
  
  ///
  /// Check if appended items are present
  ///
  bool HasAppendedItems() const { return bits & HasAppendedItemsBit; }
  
  ///
  /// Check if deleted items are present
  ///
  bool HasDeletedItems() const { return bits & HasDeletedItemsBit; }
  
  ///
  /// Check if ordered items are present
  ///
  bool HasOrderedItems() const { return bits & HasOrderedItemsBit; }

  uint8_t bits;  ///< Bit field containing operation flags
};

}  // namespace tinyusdz