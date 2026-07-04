// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Property Index Implementation

#include "property-index.hh"
#include <algorithm>
#include <cstring>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <mutex>
#include <shared_mutex>
#endif

namespace tinyusdz {
namespace next {

// ============================================================
// PropNameTable
// ============================================================

PropNameTable::PropNameTable() = default;

PropNameTable::~PropNameTable() = default;

#if defined(TINYUSDZ_ENABLE_THREAD)
void PropNameTable::freeze() { frozen_.store(true, std::memory_order_release); }
void PropNameTable::unfreeze() { frozen_.store(false, std::memory_order_release); }
#else
void PropNameTable::freeze() {}
void PropNameTable::unfreeze() {}
#endif

PropNameId PropNameTable::intern(const std::string& name) {
  return intern(std::string_view(name));
}

#if defined(TINYUSDZ_ENABLE_THREAD)
void PropNameTable::publish_snapshot_locked() {
  auto snap = std::make_unique<Snapshot>();
  snap->map = name_to_id_;
  snap->by_id.reserve(names_.size());
  for (const auto& n : names_) snap->by_id.push_back(n.get());
  snapshot_.store(snap.get(), std::memory_order_release);
  retired_.push_back(std::move(snap));
}
#endif

PropNameId PropNameTable::intern(std::string_view name) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Lock-free HIT path against the latest immutable snapshot. This already
  // serves the common compose-time intern (every property name was interned at
  // parse) without a lock, regardless of frozen_. A snapshot MISS must never
  // read the mutable name_to_id_/names_ lock-free -- a concurrent intern()
  // rehash/push_back would be a data race (TSan-confirmed: the old "frozen fast
  // path" read name_to_id_ here while another thread inserted) -- so it falls
  // through to the locked path below.
  if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
    auto sit = snap->map.find(name);
    if (sit != snap->map.end()) return PropNameId{sit->second};
  }
  {
    std::shared_lock<std::shared_mutex> rlk(mu_);
    auto it = name_to_id_.find(name);
    if (it != name_to_id_.end()) {
      return PropNameId{it->second};
    }
  }
  std::unique_lock<std::shared_mutex> wlk(mu_);
  // Re-check: another thread may have inserted between the locks.
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return PropNameId{it->second};
  }
  uint32_t id = static_cast<uint32_t>(names_.size());
  auto interned_name = std::make_unique<std::string>(name);
  const std::string_view key(*interned_name);
  names_.push_back(std::move(interned_name));
  name_to_id_.emplace(key, id);
  publish_snapshot_locked();
  return PropNameId{id};
#else
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return PropNameId{it->second};
  }
  uint32_t id = static_cast<uint32_t>(names_.size());
  auto interned_name = std::make_unique<std::string>(name);
  const std::string_view key(*interned_name);
  names_.push_back(std::move(interned_name));
  name_to_id_.emplace(key, id);
  return PropNameId{id};
#endif
}

PropNameId PropNameTable::intern(const char* name) {
  if (!name) return PropNameId{};
  return intern(std::string_view(name));
}

const std::string& PropNameTable::get(PropNameId id) const {
  static const std::string empty;
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Lock-free only on a covering snapshot; an id newer than the loaded
  // snapshot falls through to the locked authoritative read.
  if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
    if (id.id < snap->by_id.size()) return *snap->by_id[id.id];
  }
  // Snapshot miss (id newer than the loaded snapshot). Read names_ under the
  // shared lock -- NEVER lock-free, even when frozen: a concurrent intern()
  // push_back reallocates names_ and racing on names_.size()/[] is UB
  // (TSan-confirmed). Out-of-snapshot ids are rare, so the lock cost is minimal.
  {
    std::shared_lock<std::shared_mutex> rlk(mu_);
    if (id.id >= names_.size()) return empty;
    return *names_[id.id];
  }
#else
  if (id.id >= names_.size()) return empty;
  return *names_[id.id];
#endif
}

PropNameId PropNameTable::find(const std::string& name) const {
#if defined(TINYUSDZ_ENABLE_THREAD)
  return find(std::string_view(name));
#else
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return PropNameId{it->second};
  }
  return PropNameId{};
#endif
}

PropNameId PropNameTable::find(std::string_view name) const {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Lock-free HIT against the latest immutable snapshot.
  if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
    auto sit = snap->map.find(name);
    if (sit != snap->map.end()) return PropNameId{sit->second};
    // Frozen read-only phase: the snapshot is authoritative, so a miss means
    // absent. Resolve it lock-free from the IMMUTABLE snapshot only -- never the
    // mutable name_to_id_, which a concurrent intern() may be rehashing (that
    // lock-free read was a data race). This keeps the rwlock-free find-miss that
    // matters for multi-thread rendering, safely: a concurrent insert during the
    // frozen phase is at worst observed as a benign stale miss, not UB.
    if (frozen_.load(std::memory_order_acquire)) return PropNameId{};
  }
  // Not frozen (or no snapshot yet): authoritative locked lookup.
  {
    std::shared_lock<std::shared_mutex> rlk(mu_);
    auto it = name_to_id_.find(name);
    return it != name_to_id_.end() ? PropNameId{it->second} : PropNameId{};
  }
#else
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return PropNameId{it->second};
  }
  return PropNameId{};
#endif
}

PropNameId PropNameTable::find(const char* name) const {
  if (!name) return PropNameId{};
  return find(std::string_view(name));
}

void PropNameTable::register_common_names() {
  // Pre-register common USD property names
  // These get the lowest IDs for fastest lookup
  id_points = intern("points");
  id_normals = intern("normals");
  id_primvars_st = intern("primvars:st");
  id_extent = intern("extent");
  id_visibility = intern("visibility");
  id_purpose = intern("purpose");
  id_xformOpOrder = intern("xformOpOrder");
  id_faceVertexCounts = intern("faceVertexCounts");
  id_faceVertexIndices = intern("faceVertexIndices");
  id_subdivisionScheme = intern("subdivisionScheme");
  id_interpolateBoundary = intern("interpolateBoundary");
  id_radius = intern("radius");
  id_width = intern("width");
  id_height = intern("height");
  id_size = intern("size");

  // Additional common names
  intern("doubleSided");
  intern("orientation");
  intern("displayColor");
  intern("displayOpacity");
  intern("velocities");
  intern("accelerations");
  intern("ids");
  intern("widths");
  intern("cornerIndices");
  intern("cornerSharpnesses");
  intern("creaseIndices");
  intern("creaseLengths");
  intern("creaseSharpnesses");
  intern("holeIndices");

  // XformOp names
  intern("xformOp:translate");
  intern("xformOp:rotateX");
  intern("xformOp:rotateY");
  intern("xformOp:rotateZ");
  intern("xformOp:rotateXYZ");
  intern("xformOp:rotateXZY");
  intern("xformOp:rotateYXZ");
  intern("xformOp:rotateYZX");
  intern("xformOp:rotateZXY");
  intern("xformOp:rotateZYX");
  intern("xformOp:scale");
  intern("xformOp:orient");
  intern("xformOp:transform");

  // Material binding
  intern("material:binding");
  intern("material:binding:preview");
  intern("material:binding:full");

  // Shader inputs/outputs
  intern("inputs:diffuseColor");
  intern("inputs:emissiveColor");
  intern("inputs:metallic");
  intern("inputs:roughness");
  intern("inputs:opacity");
  intern("inputs:normal");
  intern("inputs:occlusion");
  intern("inputs:clearcoat");
  intern("inputs:clearcoatRoughness");
  intern("inputs:ior");
  intern("inputs:specularColor");
  intern("inputs:useSpecularWorkflow");
  intern("inputs:file");
  intern("inputs:st");
  intern("inputs:wrapS");
  intern("inputs:wrapT");
  intern("inputs:fallback");
  intern("inputs:varname");
  intern("outputs:surface");
  intern("outputs:displacement");
  intern("outputs:volume");
  intern("outputs:rgb");
  intern("outputs:r");
  intern("outputs:g");
  intern("outputs:b");
  intern("outputs:a");

  // Light properties
  intern("intensity");
  intern("exposure");
  intern("color");
  intern("enableColorTemperature");
  intern("colorTemperature");
  intern("diffuse");
  intern("specular");
  intern("normalize");
  intern("inputs:intensity");
  intern("inputs:exposure");
  intern("inputs:color");
  intern("inputs:radius");
  intern("inputs:angle");
  intern("inputs:softness");
  intern("inputs:length");
  intern("inputs:width");
  intern("inputs:height");
  intern("inputs:texture:file");
  intern("inputs:texture:format");

  // Skeleton properties
  intern("joints");
  intern("jointNames");
  intern("bindTransforms");
  intern("restTransforms");
  intern("rotations");
  intern("translations");
  intern("scales");
  intern("blendShapes");
  intern("blendShapeWeights");
  intern("skeleton");
  intern("animationSource");
  intern("primvars:skel:jointIndices");
  intern("primvars:skel:jointWeights");
  intern("primvars:skel:geomBindTransform");
}

// Global singleton
PropNameTable& GetPropNameTable() {
  // Registration runs inside a (thread-safe) function-local static init:
  // concurrent first callers (parallel build_stage / subtree-parse workers)
  // must not observe a half-registered table. (The former `static bool
  // initialized` pattern raced.)
  static PropNameTable table;
  static const bool initialized = [] {
    table.register_common_names();
    return true;
  }();
  (void)initialized;
  return table;
}

// ============================================================
// PropIndex
// ============================================================

void PropIndex::reserve(size_t count) {
  slots_.reserve(count);
}

void PropIndex::add(PropSlot slot) {
  slots_.push_back(slot);
  sorted_ = false;
}

const PropSlot* PropIndex::find(PropNameId name_id) const {
  if (!name_id.is_valid()) return nullptr;

  if (sorted_) {
    // Binary search
    auto it = std::lower_bound(slots_.begin(), slots_.end(), name_id,
        [](const PropSlot& slot, PropNameId id) {
          return slot.name_id < id;
        });
    if (it != slots_.end() && it->name_id == name_id) {
      return &(*it);
    }
  } else {
    // Linear search
    for (const auto& slot : slots_) {
      if (slot.name_id == name_id) {
        return &slot;
      }
    }
  }
  return nullptr;
}

PropSlot* PropIndex::find_mutable(PropNameId name_id) {
  // const_cast is safe: *this is non-const, so the slot returned by find()
  // aliases our own non-const slots_ storage.
  return const_cast<PropSlot*>(find(name_id));
}

const PropSlot* PropIndex::find(const std::string& name) const {
  PropNameId id = GetPropNameTable().find(name);
  if (!id.is_valid()) {
    // Name not in table - do linear search by string
    const auto& table = GetPropNameTable();
    for (const auto& slot : slots_) {
      if (table.get(slot.name_id) == name) {
        return &slot;
      }
    }
    return nullptr;
  }
  return find(id);
}

void PropIndex::sort() {
  std::sort(slots_.begin(), slots_.end(),
      [](const PropSlot& a, const PropSlot& b) {
        return a.name_id < b.name_id;
      });
  sorted_ = true;
}

}  // namespace next
}  // namespace tinyusdz
