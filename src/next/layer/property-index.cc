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

PropNameId PropNameTable::intern(std::string_view name) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  if (frozen_.load(std::memory_order_acquire)) {
    // Frozen fast path: the common compose-time intern is a HIT (every property
    // name was interned at parse), so resolve it lock-free against the immutable
    // map and stay frozen. Only a genuinely NEW name mutates the table, which
    // can't coexist with lock-free readers -- so it unfreezes (back to locking)
    // before inserting. Callers keep frozen lock-free reads and new interns in
    // disjoint phases, so no reader is in flight at that transition.
    auto it = name_to_id_.find(name);
    if (it != name_to_id_.end()) return PropNameId{it->second};
    frozen_.store(false, std::memory_order_release);
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
  if (!frozen_.load(std::memory_order_acquire)) {
    // Shared lock: a concurrent intern() on another thread may push_back names_
    // (parallel composition warms referenced layers on workers). When frozen the
    // name storage is immutable, so concurrent reads need no lock.
    std::shared_lock<std::shared_mutex> rlk(mu_);
    if (id.id >= names_.size()) return empty;
    return *names_[id.id];
  }
#endif
  if (id.id >= names_.size()) return empty;
  return *names_[id.id];
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
  if (!frozen_.load(std::memory_order_acquire)) {
    // Shared lock vs. a concurrent intern() rehash of name_to_id_. When frozen
    // the map is immutable, so concurrent lookups need no lock -- this removes
    // the rwlock cache-line contention that dominates multi-thread rendering.
    std::shared_lock<std::shared_mutex> rlk(mu_);
    auto it = name_to_id_.find(name);
    return it != name_to_id_.end() ? PropNameId{it->second} : PropNameId{};
  }
#endif
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return PropNameId{it->second};
  }
  return PropNameId{};
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
  static PropNameTable table;
  static bool initialized = false;
  if (!initialized) {
    table.register_common_names();
    initialized = true;
  }
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
