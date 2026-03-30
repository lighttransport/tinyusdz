// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Property Index
// Fast O(1) property lookup using string interning and indexed storage

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace tinyusdz {
namespace next {

/// Interned property name handle
/// Uses 32-bit index for compact storage
struct PropNameId {
  uint32_t id = UINT32_MAX;

  bool is_valid() const { return id != UINT32_MAX; }
  bool operator==(PropNameId other) const { return id == other.id; }
  bool operator!=(PropNameId other) const { return id != other.id; }
  bool operator<(PropNameId other) const { return id < other.id; }
};

/// Property name interning table
/// All property names are stored once and referenced by ID
/// Thread-safe for reads after initial population
class PropNameTable {
public:
  PropNameTable();
  ~PropNameTable();

  /// Intern a property name, returning its ID
  /// If name already exists, returns existing ID
  PropNameId intern(const std::string& name);
  PropNameId intern(const char* name);

  /// Get name by ID (O(1))
  const std::string& get(PropNameId id) const;

  /// Try to find existing ID without creating (O(1) average)
  PropNameId find(const std::string& name) const;
  PropNameId find(const char* name) const;

  /// Get total count of interned names
  size_t size() const { return names_.size(); }

  /// Pre-register common USD property names for faster lookup
  void register_common_names();

  // Common property name IDs (pre-registered for O(1) access)
  PropNameId id_points;       // "points"
  PropNameId id_normals;      // "normals"
  PropNameId id_primvars_st;  // "primvars:st"
  PropNameId id_extent;       // "extent"
  PropNameId id_visibility;   // "visibility"
  PropNameId id_purpose;      // "purpose"
  PropNameId id_xformOpOrder; // "xformOpOrder"
  PropNameId id_faceVertexCounts;    // "faceVertexCounts"
  PropNameId id_faceVertexIndices;   // "faceVertexIndices"
  PropNameId id_subdivisionScheme;   // "subdivisionScheme"
  PropNameId id_interpolateBoundary; // "interpolateBoundary"
  PropNameId id_radius;       // "radius"
  PropNameId id_width;        // "width"
  PropNameId id_height;       // "height"
  PropNameId id_size;         // "size"

private:
  std::vector<std::string> names_;
  std::unordered_map<std::string, uint32_t> name_to_id_;
};

/// Global property name table (singleton)
PropNameTable& GetPropNameTable();

/// Property storage slot
/// Compact representation of a property value with metadata
struct PropSlot {
  PropNameId name_id;         // Interned property name
  uint32_t value_offset;      // Offset into value storage
  uint16_t value_type;        // TypeId of the value
  uint16_t flags;             // Property flags

  // Flag bits
  static constexpr uint16_t kFlagCustom = 0x0001;
  static constexpr uint16_t kFlagUniform = 0x0002;
  static constexpr uint16_t kFlagTimeSampled = 0x0004;
  static constexpr uint16_t kFlagConnection = 0x0008;
  static constexpr uint16_t kFlagRelationship = 0x0010;
  static constexpr uint16_t kFlagArray = 0x0020;

  bool is_custom() const { return flags & kFlagCustom; }
  bool is_uniform() const { return flags & kFlagUniform; }
  bool is_time_sampled() const { return flags & kFlagTimeSampled; }
  bool is_connection() const { return flags & kFlagConnection; }
  bool is_relationship() const { return flags & kFlagRelationship; }
  bool is_array() const { return flags & kFlagArray; }
};

/// Property index for a single prim
/// Enables O(1) property lookup by pre-registered name ID
/// or O(log n) lookup by arbitrary name
class PropIndex {
public:
  PropIndex() = default;

  /// Reserve space for expected property count
  void reserve(size_t count);

  /// Add a property slot
  void add(PropSlot slot);

  /// Find property by name ID (O(1) for sorted index, O(log n) otherwise)
  const PropSlot* find(PropNameId name_id) const;

  /// Find property by name string (O(log n) after table lookup)
  const PropSlot* find(const std::string& name) const;

  /// Get all properties
  const std::vector<PropSlot>& slots() const { return slots_; }

  /// Get property count
  size_t size() const { return slots_.size(); }

  /// Sort slots by name_id for binary search
  void sort();

  /// Check if sorted
  bool is_sorted() const { return sorted_; }

private:
  std::vector<PropSlot> slots_;
  bool sorted_ = false;
};

}  // namespace next
}  // namespace tinyusdz
