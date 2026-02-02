// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PrimSpec
// Unified prim specification that can serve as both spec and runtime prim
// Avoids the PrimSpec→Prim tree duplication of the old design

#pragma once

#include "property-index.hh"
#include "../types/value.hh"
#include "../types/interpolation.hh"
#include "../prim/path.hh"
#include <string>
#include <vector>
#include <memory>

namespace tinyusdz {
namespace next {

/// Prim specifier (def, over, class)
enum class PrimSpecifier : uint8_t {
  Def = 0,    // Concrete definition
  Over = 1,   // Override
  Class = 2   // Abstract class
};

/// Type name ID (interned like property names)
struct TypeNameId {
  uint16_t id = UINT16_MAX;
  bool is_valid() const { return id != UINT16_MAX; }
};

/// Type name interning table
class TypeNameTable {
public:
  TypeNameId intern(const std::string& name);
  const std::string& get(TypeNameId id) const;
  TypeNameId find(const std::string& name) const;

  // Pre-registered type IDs
  TypeNameId id_Xform;
  TypeNameId id_Scope;
  TypeNameId id_Mesh;
  TypeNameId id_Points;
  TypeNameId id_BasisCurves;
  TypeNameId id_NurbsCurves;
  TypeNameId id_Sphere;
  TypeNameId id_Cube;
  TypeNameId id_Cylinder;
  TypeNameId id_Cone;
  TypeNameId id_Capsule;
  TypeNameId id_Camera;
  TypeNameId id_Material;
  TypeNameId id_Shader;
  TypeNameId id_SkelRoot;
  TypeNameId id_Skeleton;
  TypeNameId id_SkelAnimation;
  TypeNameId id_BlendShape;
  TypeNameId id_PointInstancer;
  TypeNameId id_GeomSubset;

  void register_common_types();

private:
  std::vector<std::string> names_;
  std::unordered_map<std::string, uint16_t> name_to_id_;
};

TypeNameTable& GetTypeNameTable();

/// PrimSpec metadata
struct PrimSpecMeta {
  bool active = true;
  bool hidden = false;
  std::string doc;
  std::string comment;
  std::vector<std::string> apiSchemas;

  // Composition arcs (stored as paths for lazy resolution)
  std::vector<std::string> references;
  std::vector<std::string> payloads;
  std::vector<std::string> inherits;
  std::vector<std::string> specializes;
  std::string variantSelection;  // "variantSet=selection"
};

/// Value storage block
/// Stores property values contiguously for cache efficiency
class ValueStorage {
public:
  ValueStorage();
  ~ValueStorage();

  /// Allocate space and return offset
  uint32_t allocate(size_t size, size_t alignment = 8);

  /// Store a value and return its offset
  uint32_t store(const Value& value);

  /// Get value at offset
  const Value* get(uint32_t offset) const;
  Value* get(uint32_t offset);

  /// Get raw pointer at offset
  const void* raw(uint32_t offset) const;
  void* raw(uint32_t offset);

  /// Current used size
  size_t size() const { return used_; }

  /// Total capacity
  size_t capacity() const { return data_.size(); }

  /// Reserve capacity
  void reserve(size_t bytes);

  /// Clear all storage
  void clear();

private:
  std::vector<uint8_t> data_;
  size_t used_ = 0;
  std::vector<Value> values_;  // Value objects (may reference data_)
};

/// TimeSampleStorage - stores time samples with value deduplication
/// Design goals:
/// - Deduplicate identical array values across time samples
/// - Use hash + equality check for efficient lookup
/// - Store (time, value_offset) pairs per property
class TimeSampleStorage {
public:
  TimeSampleStorage();
  ~TimeSampleStorage();

  /// Add a time sample for a property
  /// Returns the value offset (may be shared if duplicate)
  uint32_t add(PropNameId name_id, double time, Value value);

  /// Add a time sample with explicit deduplication check
  /// If an identical value exists, reuses its offset
  uint32_t add_dedup(PropNameId name_id, double time, Value value);

  /// Get time samples for a property
  /// Returns vector of (time, value_offset) pairs, sorted by time
  const std::vector<std::pair<double, uint32_t>>* get(PropNameId name_id) const;

  /// Get value at offset
  const Value* value(uint32_t offset) const;

  /// Check if property has time samples
  bool has(PropNameId name_id) const;

  /// Get interpolated value at a given time
  /// @param name_id Property name ID
  /// @param time Time to sample at
  /// @param mode Interpolation mode (default: Linear)
  /// @return SampleResult with interpolated value
  SampleResult interpolate(PropNameId name_id, double time,
                           TimeInterpolation mode = TimeInterpolation::Linear) const;

  /// Get all property IDs with time samples
  std::vector<PropNameId> properties() const;

  /// Check if empty
  bool empty() const { return samples_.empty(); }

  /// Memory usage in bytes
  size_t memory_usage() const;

  /// Clear all storage
  void clear();

  /// Statistics
  struct Stats {
    size_t property_count;     // Properties with time samples
    size_t total_samples;      // Total (time, offset) pairs
    size_t unique_values;      // Unique values stored
    size_t dedup_count;        // Values deduplicated (savings)
    size_t memory_bytes;
  };
  Stats stats() const;

private:
  // Property -> vector of (time, value_offset)
  std::unordered_map<uint32_t, std::vector<std::pair<double, uint32_t>>> samples_;

  // Value storage
  std::vector<Value> values_;

  // Deduplication: hash -> list of (offset, hash) for collision handling
  std::unordered_map<uint64_t, std::vector<uint32_t>> hash_to_offsets_;

  // Stats
  size_t dedup_count_ = 0;

  // Find existing value or store new one
  uint32_t find_or_store(Value value);
};

/// PrimSpec - unified spec/prim representation
/// Design goals:
/// - No separate Prim type needed (PrimSpec IS the prim)
/// - Properties accessed via O(1) indexed lookup
/// - Values stored in contiguous memory
/// - Children stored as indices, not pointers
class PrimSpec {
public:
  PrimSpec();
  explicit PrimSpec(const std::string& name);
  PrimSpec(const std::string& name, const std::string& type_name);
  ~PrimSpec();

  // Move only (for efficiency)
  PrimSpec(PrimSpec&& other) noexcept;
  PrimSpec& operator=(PrimSpec&& other) noexcept;
  PrimSpec(const PrimSpec&) = delete;
  PrimSpec& operator=(const PrimSpec&) = delete;

  // ============================================================
  // Identity
  // ============================================================

  /// Get prim name
  const std::string& name() const { return name_; }
  void set_name(const std::string& name) { name_ = name; }

  /// Get type name
  const std::string& type_name() const;
  TypeNameId type_id() const { return type_id_; }
  void set_type_name(const std::string& name);

  /// Get specifier
  PrimSpecifier specifier() const { return specifier_; }
  void set_specifier(PrimSpecifier spec) { specifier_ = spec; }

  /// Get path (set during layer building)
  const Path& path() const { return path_; }
  void set_path(const Path& path) { path_ = path; }

  // ============================================================
  // Properties (O(1) lookup for common names)
  // ============================================================

  /// Get property by name ID (fastest)
  const PropSlot* property(PropNameId name_id) const;

  /// Get property by name string
  const PropSlot* property(const std::string& name) const;

  /// Get property value
  const Value* property_value(PropNameId name_id) const;
  const Value* property_value(const std::string& name) const;

  /// Add a property
  void add_property(const std::string& name, Value value, uint16_t flags = 0);
  void add_property(PropNameId name_id, Value value, uint16_t flags = 0);

  /// Add a property slot without value (for time-sampled properties)
  void add_property_slot(PropNameId name_id, TypeId type_id, uint16_t flags);

  /// Get property index (for iteration)
  const PropIndex& properties() const { return props_; }

  /// Reserve property storage
  void reserve_properties(size_t count);

  /// Finalize properties (sort for binary search)
  void finalize_properties();

  // ============================================================
  // TimeSamples (stored separately for efficiency)
  // ============================================================

  /// Add a time sample for a property
  void add_time_sample(PropNameId name_id, double time, Value value);

  /// Get time samples for a property (returns vector of (time, value_offset))
  const std::vector<std::pair<double, uint32_t>>* time_samples(PropNameId name_id) const;

  /// Get value at a specific time sample offset
  const Value* time_sample_value(uint32_t offset) const;

  /// Get all property names that have time samples
  std::vector<PropNameId> time_sampled_properties() const;

  /// Check if property has time samples
  bool has_time_samples(PropNameId name_id) const;

  /// Check if any property has time samples
  bool has_any_time_samples() const;

  /// Get time sample statistics (for debugging/profiling)
  TimeSampleStorage::Stats time_sample_stats() const;

  /// Get interpolated value at a given time
  /// @param name_id Property name ID
  /// @param time Time to sample at
  /// @param mode Interpolation mode (default: Linear)
  /// @return SampleResult with interpolated value
  SampleResult interpolate_time_sample(PropNameId name_id, double time,
                                       TimeInterpolation mode = TimeInterpolation::Linear) const;

  /// Get interpolated value at a given time (by property name)
  SampleResult interpolate_time_sample(const std::string& name, double time,
                                       TimeInterpolation mode = TimeInterpolation::Linear) const;

  // ============================================================
  // Relationships
  // ============================================================

  /// Add a relationship target
  void add_relationship(const std::string& name, const Path& target);

  /// Get relationship targets
  const std::vector<Path>* relationship(const std::string& name) const;

  // ============================================================
  // Children (stored as indices into Layer's prim array)
  // ============================================================

  /// Get child indices
  const std::vector<uint32_t>& child_indices() const { return child_indices_; }

  /// Add child index
  void add_child_index(uint32_t index);

  /// Get child count
  size_t child_count() const { return child_indices_.size(); }

  // ============================================================
  // Metadata
  // ============================================================

  /// Get metadata
  const PrimSpecMeta& meta() const { return meta_; }
  PrimSpecMeta& meta() { return meta_; }

  // ============================================================
  // Memory
  // ============================================================

  /// Get approximate memory usage
  size_t memory_usage() const;

private:
  std::string name_;
  TypeNameId type_id_;
  PrimSpecifier specifier_ = PrimSpecifier::Def;
  Path path_;

  PropIndex props_;
  std::unique_ptr<ValueStorage> values_;

  // TimeSamples storage with deduplication
  std::unique_ptr<TimeSampleStorage> time_samples_;

  // Relationships: name -> targets
  std::unordered_map<std::string, std::vector<Path>> relationships_;

  // Children stored as indices into parent Layer's prim array
  std::vector<uint32_t> child_indices_;

  PrimSpecMeta meta_;
};

}  // namespace next
}  // namespace tinyusdz
