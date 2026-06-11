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

// Forward declarations
struct VariantData;
struct VariantSetData;
class Layer;  // for VariantData::content (variant subtree)

/// Variant data - properties and prims inside a single variant option
struct VariantData {
  std::string name;
  bool active = true;
  bool hidden = false;
  std::string doc;
  std::vector<std::pair<std::string, Value>> properties;
  std::unordered_map<std::string, std::vector<Path>> relationships;
  // Optional subtree for variants that add prim-level opinions and/or child
  // prims: a Layer whose root prim "__self__" carries the host opinions and
  // whose descendants become the host prim's children when this variant is
  // selected. (Composed reference-style, so it supports child prims.)
  std::shared_ptr<Layer> content;
};

/// Variant set - a named set of variant options
struct VariantSetData {
  std::string name;
  std::string selected;  // selected variant name for this set ("" = none)
  std::vector<VariantData> variants;
};

/// One composition-arc field's list-op edits, exactly as authored (Phase 7 S5,
/// AOUSD §10.3.2). `is_explicit` marks a bare `references = [...]` (or an
/// explicit op), in which case the effective items are the inline arc vector on
/// PrimSpecMeta; otherwise the effective list is composed from the prepend /
/// append / delete / order lists. Kept so composition can merge a site's specs
/// across the layer stack and the writer can re-emit the original qualifier.
struct ArcEdit {
  bool authored = false;     // true when this arc field authored list-op edits.
  bool is_explicit = true;  // bare list (the common case)
  std::vector<std::string> prepended;
  std::vector<std::string> appended;
  std::vector<std::string> deleted;
  std::vector<std::string> ordered;

  bool has_qualifiers() const {
    return authored && (!is_explicit || !prepended.empty() || !appended.empty() ||
           !deleted.empty() || !ordered.empty());
  }
  bool has_authored_opinion() const {
    return authored;
  }
};

/// Per-arc-type list-op edits for one PrimSpec (lazily allocated; only prims
/// that author a non-bare arc qualifier pay for it).
struct ArcListOpEdits {
  ArcEdit references;
  ArcEdit payloads;
  ArcEdit inherits;
  ArcEdit specializes;
};

/// Cold PrimSpec metadata: fields that are empty on the vast majority of prims
/// (no docs / variants / relocates / apiSchemas / instancing / list-op edits).
/// Held behind a lazily-allocated unique_ptr on PrimSpecMeta so an ordinary
/// prim pays only 8 pointer bytes instead of ~190 bytes of empty std::string /
/// std::vector heads (Phase 8.1 footprint split).
struct PrimSpecMetaExt {
  std::string doc;
  std::string comment;
  // When non-empty (set by composition), this prim is an instance whose
  // children come from the prototype prim at this path (no duplicated subtree).
  std::string instance_prototype;
  std::vector<std::string> apiSchemas;
  // Variant set definitions.
  std::vector<VariantSetData> variantSets;
  // Multiple variant selections (set -> selection); composed in addition to the
  // legacy single `variantSelection` field on PrimSpecMeta.
  std::vector<std::pair<std::string, std::string>> variantSelections;
  // Relocates: namespace renames (absolute source path -> absolute target path).
  std::vector<std::pair<std::string, std::string>> relocates;
  // Arc list-op qualifiers (Phase 7 S5); null unless authored.
  std::unique_ptr<ArcListOpEdits> arc_edits;

  PrimSpecMetaExt() = default;
  PrimSpecMetaExt(const PrimSpecMetaExt &o)
      : doc(o.doc),
        comment(o.comment),
        instance_prototype(o.instance_prototype),
        apiSchemas(o.apiSchemas),
        variantSets(o.variantSets),
        variantSelections(o.variantSelections),
        relocates(o.relocates),
        arc_edits(o.arc_edits ? new ArcListOpEdits(*o.arc_edits) : nullptr) {}
  PrimSpecMetaExt &operator=(const PrimSpecMetaExt &) = delete;
};

/// PrimSpec metadata. Hot fields (flags, the four composition-arc lists, the
/// legacy single variant selection, and the layer offset) are inline; cold
/// fields live in a lazily-allocated PrimSpecMetaExt. The cold-field accessors
/// keep the old names, so call sites change only by gaining `()`. Const reads
/// never allocate (return a shared empty); mutable access allocates the ext on
/// first touch. Copyable with a deep copy of the ext.
struct PrimSpecMeta {
  bool active = true;
  bool hidden = false;
  bool instanceable = false;  // when true (with arcs), the prim is an instance

  // Composition arcs (stored as paths for lazy resolution).
  std::vector<std::string> references;
  std::vector<std::string> payloads;
  std::vector<std::string> inherits;
  std::vector<std::string> specializes;

  // Legacy single "variantSet=selection" (kept inline; the plural list of
  // selections lives in the ext).
  std::string variantSelection;

  // Layer offset (applied at evaluation time). First = offset, second = scale.
  std::pair<double, double> layer_offset = {0.0, 1.0};

  PrimSpecMeta() = default;
  PrimSpecMeta(PrimSpecMeta &&) = default;
  PrimSpecMeta &operator=(PrimSpecMeta &&) = default;
  PrimSpecMeta(const PrimSpecMeta &o) { *this = o; }
  PrimSpecMeta &operator=(const PrimSpecMeta &o) {
    active = o.active;
    hidden = o.hidden;
    instanceable = o.instanceable;
    references = o.references;
    payloads = o.payloads;
    inherits = o.inherits;
    specializes = o.specializes;
    variantSelection = o.variantSelection;
    layer_offset = o.layer_offset;
    ext_ = o.ext_ ? std::unique_ptr<PrimSpecMetaExt>(
                        new PrimSpecMetaExt(*o.ext_))
                  : nullptr;
    return *this;
  }

  bool has_ext() const { return ext_ != nullptr; }
  void ensure_ext() {
    if (!ext_) ext_.reset(new PrimSpecMetaExt());
  }
  const PrimSpecMetaExt *ext() const { return ext_.get(); }

  // Arc list-op edits (Phase 7 S5). Const returns null when none authored (the
  // inline arc vectors are then implicit explicit lists); mutable allocates.
  const ArcListOpEdits *arc_edits() const {
    return ext_ ? ext_->arc_edits.get() : nullptr;
  }
  ArcListOpEdits &ensure_arc_edits() {
    ensure_ext();
    if (!ext_->arc_edits) ext_->arc_edits.reset(new ArcListOpEdits());
    return *ext_->arc_edits;
  }

  const std::string &doc() const {
    static const std::string kEmpty;
    return ext_ ? ext_->doc : kEmpty;
  }
  std::string &doc() {
    ensure_ext();
    return ext_->doc;
  }
  const std::string &comment() const {
    static const std::string kEmpty;
    return ext_ ? ext_->comment : kEmpty;
  }
  std::string &comment() {
    ensure_ext();
    return ext_->comment;
  }
  const std::string &instance_prototype() const {
    static const std::string kEmpty;
    return ext_ ? ext_->instance_prototype : kEmpty;
  }
  std::string &instance_prototype() {
    ensure_ext();
    return ext_->instance_prototype;
  }
  const std::vector<std::string> &apiSchemas() const {
    static const std::vector<std::string> kEmpty;
    return ext_ ? ext_->apiSchemas : kEmpty;
  }
  std::vector<std::string> &apiSchemas() {
    ensure_ext();
    return ext_->apiSchemas;
  }
  const std::vector<VariantSetData> &variantSets() const {
    static const std::vector<VariantSetData> kEmpty;
    return ext_ ? ext_->variantSets : kEmpty;
  }
  std::vector<VariantSetData> &variantSets() {
    ensure_ext();
    return ext_->variantSets;
  }
  const std::vector<std::pair<std::string, std::string>> &variantSelections()
      const {
    static const std::vector<std::pair<std::string, std::string>> kEmpty;
    return ext_ ? ext_->variantSelections : kEmpty;
  }
  std::vector<std::pair<std::string, std::string>> &variantSelections() {
    ensure_ext();
    return ext_->variantSelections;
  }
  const std::vector<std::pair<std::string, std::string>> &relocates() const {
    static const std::vector<std::pair<std::string, std::string>> kEmpty;
    return ext_ ? ext_->relocates : kEmpty;
  }
  std::vector<std::pair<std::string, std::string>> &relocates() {
    ensure_ext();
    return ext_->relocates;
  }

 private:
  std::unique_ptr<PrimSpecMetaExt> ext_;
};

/// Value storage block
/// Stores property values contiguously for cache efficiency
class ValueStorage {
public:
  ValueStorage();
  ~ValueStorage();

  /// Store a value and return its offset
  uint32_t store(const Value& value);
  uint32_t store(Value&& value);

  /// Get value at offset
  const Value* get(uint32_t offset) const;
  Value* get(uint32_t offset);

  /// Number of stored values
  size_t count() const { return values_.size(); }

  /// Approximate memory usage in bytes
  size_t memory_usage() const;

  /// Clear all storage
  void clear();

private:
  std::vector<Value> values_;
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

  /// Clone this PrimSpec (deep copy)
  PrimSpec Clone() const;

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

  /// Get all relationship names
  std::vector<std::string> relationship_names() const;

  // ============================================================
  // Attribute connections / declared type names (USDC fidelity)
  // ============================================================

  /// Add an attribute connection target (e.g. inputs:x.connect = </path>).
  /// The property itself should also exist as a slot (kFlagConnection).
  void add_connection(const std::string& prop_name, const Path& target);

  /// Get connection targets for an attribute (nullptr if none)
  const std::vector<Path>* connection(const std::string& prop_name) const;

  /// Record the declared USD type name of a property (e.g. "color3f",
  /// "token", "float[]"). Needed to faithfully re-emit attributes that have
  /// no authored default value (connection-only / declared-only) and to
  /// round-trip the exact role type for valued attributes.
  void set_property_type_name(const std::string& prop_name,
                              const std::string& type_name);

  /// Get the declared type name of a property (nullptr if not recorded)
  const std::string* property_type_name(const std::string& prop_name) const;

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

  // Attribute connections: interned property-name id -> connection targets.
  // (Keyed by PropNameId.id rather than a string to avoid a key string per
  // connected property on shader-heavy scenes.)
  std::unordered_map<uint32_t, std::vector<Path>> connections_;

  // Declared USD type names: interned property-name id -> interned typeName id
  // (both interned in the global PropNameTable). Lets the writer re-emit the
  // exact `typeName` (incl. role types and value-less / connection-only attrs)
  // while storing only two uint32s per property instead of two strings — the
  // "string pooling" of the original low-memory plan (typeNames are highly
  // repeated, so interning collapses ~150k strings to a few dozen).
  std::unordered_map<uint32_t, uint32_t> prop_type_names_;

  // Children stored as indices into parent Layer's prim array
  std::vector<uint32_t> child_indices_;

  PrimSpecMeta meta_;
};

}  // namespace next
}  // namespace tinyusdz
