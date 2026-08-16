// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Property Index
// Fast O(1) property lookup using string interning and indexed storage

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <unordered_map>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <atomic>
#include <memory>
#include <shared_mutex>
#endif

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

// Keep owned strings as map keys while allowing allocation-free read-only
// probes from string_view callers.
struct PropNameHash {
  using is_transparent = void;

  size_t operator()(const std::string& name) const noexcept {
    return std::hash<std::string_view>{}(name);
  }
  size_t operator()(std::string_view name) const noexcept {
    return std::hash<std::string_view>{}(name);
  }
};

struct PropNameEqual {
  using is_transparent = void;

  bool operator()(const std::string& lhs, const std::string& rhs) const noexcept {
    return lhs == rhs;
  }
  bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    return lhs == rhs;
  }
  bool operator()(const std::string& lhs, std::string_view rhs) const noexcept {
    return std::string_view(lhs) == rhs;
  }
  bool operator()(std::string_view lhs, const std::string& rhs) const noexcept {
    return lhs == std::string_view(rhs);
  }
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
  PropNameId find(std::string_view name) const;
  PropNameId find(const char* name) const;

  /// Get total count of interned names
  size_t size() const {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::shared_lock<std::shared_mutex> rlk(mu_);
#endif
    return names_.size();
  }

  /// Pre-register common USD property names for faster lookup
  void register_common_names();

  /// Freeze the table for lock-free concurrent reads. After composition the set
  /// of interned names is fixed; rendering does millions of find()/get() calls
  /// across many threads, and the per-call shared_lock then contends purely on
  /// the lock's own cache line (a measured ~14% of an Island render).
  ///
  /// freeze() publishes an IMMUTABLE SNAPSHOT of the table; while one is
  /// published, find()/get() answer from it with no lock at all. A later
  /// intern() of a new name mutates only the live map/deque, which the snapshot
  /// does not alias, so it can never disturb a lock-free reader. (The previous
  /// scheme flipped `frozen_` to false and then rehashed the very map that
  /// in-flight lock-free readers were walking -- a real data race, since
  /// parallel composition interns on worker threads by design.) A lookup that
  /// misses the snapshot falls back to the locked path, so a stale snapshot is
  /// never wrong, only incomplete.
  ///
  /// No-op when built without TINYUSDZ_ENABLE_THREAD.
  void freeze();
  void unfreeze();

  /// True while the table is frozen for lock-free concurrent reads. No-op
  /// (always false) in non-threaded builds, where freeze() is a no-op.
  bool is_frozen() const;

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
  // deque, not vector: get() returns a `const std::string&` that a caller may use
  // after the shared lock is released; a deque never relocates existing elements
  // on push_back, so a concurrent intern() (parallel composition) cannot dangle
  // that reference (a vector realloc would).
  std::deque<std::string> names_;
  std::unordered_map<std::string, uint32_t, PropNameHash, PropNameEqual>
      name_to_id_;
#if defined(TINYUSDZ_ENABLE_THREAD)
  // The global table is interned into concurrently when referenced layers are
  // parsed on worker threads (parallel composition pre-warm). Read-mostly: a
  // lookup takes a shared lock, the rare new-name insert an exclusive one.
  // Uncontended in the serial path (~ns) so single-threaded parse is unaffected.
  mutable std::shared_mutex mu_;

  // Immutable lookup snapshot published by freeze(). `by_name` is sorted by
  // name so a lookup is a branch-predictable binary search over flat storage
  // (friendlier than unordered_map's pointer chase), and `by_id` indexes the
  // stable deque elements. Nothing here is ever mutated after publication:
  // intern() only touches names_/name_to_id_, so a lock-free reader holding a
  // FrozenIndex is isolated from it.
  struct FrozenIndex {
    std::vector<std::pair<const std::string*, uint32_t>> by_name;
    std::vector<const std::string*> by_id;
  };
  // Published lock-free as a RAW pointer, NOT via std::atomic_load on a
  // shared_ptr. libstdc++ implements the shared_ptr atomic free functions with
  // _Sp_locker, which serializes on one of 16 global mutexes chosen by the
  // ADDRESS of the shared_ptr -- and this table is a singleton, so every thread
  // hashed to the same mutex and every property-name lookup in every worker
  // serialized on it. On Moana island isCoral that cost ~24% of load
  // (_Sp_locker ctor+dtor inclusive) and showed up as futex traffic.
  //
  // Lifetime: every snapshot ever published is retained in published_ until the
  // table dies, so a reader that loaded the pointer can never see it freed --
  // including across unfreeze()/re-freeze(). freeze() is called twice in the
  // whole codebase, so this retains a couple of MB, not an unbounded set.
  std::atomic<const FrozenIndex*> frozen_ptr_{nullptr};
  std::vector<std::shared_ptr<const FrozenIndex>> published_;  // guarded by mu_
#endif
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
  static constexpr uint16_t kFlagVariabilityAuthored = 0x0040;
  static constexpr uint16_t kFlagVarying = 0x0080;

  bool is_custom() const { return flags & kFlagCustom; }
  bool is_uniform() const { return flags & kFlagUniform; }
  bool is_time_sampled() const { return flags & kFlagTimeSampled; }
  bool is_connection() const { return flags & kFlagConnection; }
  bool is_relationship() const { return flags & kFlagRelationship; }
  bool is_array() const { return flags & kFlagArray; }
  bool variability_authored() const {
    return flags & kFlagVariabilityAuthored;
  }
  bool is_varying() const { return flags & kFlagVarying; }
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

  /// Mutable variant of find() -- used to fill a value into an existing slot.
  PropSlot* find_mutable(PropNameId name_id);

  /// Find property by name string (O(log n) after table lookup)
  const PropSlot* find(const std::string& name) const;

  /// Remove a property slot by name ID. Preserves slot order (and thus the
  /// sorted state). Returns true if a slot was removed.
  bool remove(PropNameId name_id);

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
