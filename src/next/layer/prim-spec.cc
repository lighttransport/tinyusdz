// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PrimSpec Implementation

#include "prim-spec.hh"
#include <algorithm>
#include <cstring>

namespace tinyusdz {
namespace next {

// ============================================================
// TypeNameTable
// ============================================================

TypeNameId TypeNameTable::intern(const std::string& name) {
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return TypeNameId{it->second};
  }

  if (names_.size() >= UINT16_MAX) {
    return TypeNameId{};  // Table full
  }

  uint16_t id = static_cast<uint16_t>(names_.size());
  names_.push_back(name);
  name_to_id_[name] = id;
  return TypeNameId{id};
}

const std::string& TypeNameTable::get(TypeNameId id) const {
  static const std::string empty;
  if (!id.is_valid() || id.id >= names_.size()) return empty;
  return names_[id.id];
}

TypeNameId TypeNameTable::find(const std::string& name) const {
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return TypeNameId{it->second};
  }
  return TypeNameId{};
}

void TypeNameTable::register_common_types() {
  // Empty string for typeless prims
  intern("");

  // Core types
  id_Xform = intern("Xform");
  id_Scope = intern("Scope");

  // Geometry
  id_Mesh = intern("Mesh");
  id_Points = intern("Points");
  id_BasisCurves = intern("BasisCurves");
  id_NurbsCurves = intern("NurbsCurves");
  id_Sphere = intern("Sphere");
  id_Cube = intern("Cube");
  id_Cylinder = intern("Cylinder");
  id_Cone = intern("Cone");
  id_Capsule = intern("Capsule");
  id_Camera = intern("Camera");
  id_PointInstancer = intern("PointInstancer");
  id_GeomSubset = intern("GeomSubset");

  // Materials
  id_Material = intern("Material");
  id_Shader = intern("Shader");

  // Skeleton
  id_SkelRoot = intern("SkelRoot");
  id_Skeleton = intern("Skeleton");
  id_SkelAnimation = intern("SkelAnimation");
  id_BlendShape = intern("BlendShape");

  // Lights
  intern("SphereLight");
  intern("DomeLight");
  intern("DiskLight");
  intern("RectLight");
  intern("CylinderLight");
  intern("DistantLight");
  intern("PortalLight");
  intern("PluginLight");

  // Volume
  intern("Volume");
  intern("OpenVDBAsset");

  // Render
  intern("RenderSettings");
  intern("RenderProduct");
  intern("RenderVar");

  // Other
  intern("Model");
  intern("Group");
  intern("Assembly");
  intern("Component");
}

TypeNameTable& GetTypeNameTable() {
  static TypeNameTable table;
  static bool initialized = false;
  if (!initialized) {
    table.register_common_types();
    initialized = true;
  }
  return table;
}

// ============================================================
// ValueStorage
// ============================================================

ValueStorage::ValueStorage() {
  data_.reserve(4096);  // Initial 4KB
  values_.reserve(64);
}

ValueStorage::~ValueStorage() = default;

uint32_t ValueStorage::allocate(size_t size, size_t alignment) {
  // Align current position
  size_t aligned = (used_ + alignment - 1) & ~(alignment - 1);

  // Grow if needed
  if (aligned + size > data_.size()) {
    size_t new_size = std::max(data_.size() * 2, aligned + size + 1024);
    data_.resize(new_size);
  }

  uint32_t offset = static_cast<uint32_t>(aligned);
  used_ = aligned + size;
  return offset;
}

uint32_t ValueStorage::store(const Value& value) {
  uint32_t idx = static_cast<uint32_t>(values_.size());
  values_.push_back(value);
  return idx;
}

const Value* ValueStorage::get(uint32_t offset) const {
  if (offset >= values_.size()) return nullptr;
  return &values_[offset];
}

Value* ValueStorage::get(uint32_t offset) {
  if (offset >= values_.size()) return nullptr;
  return &values_[offset];
}

const void* ValueStorage::raw(uint32_t offset) const {
  if (offset >= data_.size()) return nullptr;
  return data_.data() + offset;
}

void* ValueStorage::raw(uint32_t offset) {
  if (offset >= data_.size()) return nullptr;
  return data_.data() + offset;
}

void ValueStorage::reserve(size_t bytes) {
  if (bytes > data_.size()) {
    data_.resize(bytes);
  }
}

void ValueStorage::clear() {
  used_ = 0;
  values_.clear();
}

// ============================================================
// PrimSpec
// ============================================================

PrimSpec::PrimSpec()
    : values_(std::make_unique<ValueStorage>()) {}

PrimSpec::PrimSpec(const std::string& name)
    : name_(name), values_(std::make_unique<ValueStorage>()) {}

PrimSpec::PrimSpec(const std::string& name, const std::string& type_name)
    : name_(name), values_(std::make_unique<ValueStorage>()) {
  set_type_name(type_name);
}

PrimSpec::~PrimSpec() = default;

PrimSpec::PrimSpec(PrimSpec&& other) noexcept = default;
PrimSpec& PrimSpec::operator=(PrimSpec&& other) noexcept = default;

const std::string& PrimSpec::type_name() const {
  return GetTypeNameTable().get(type_id_);
}

void PrimSpec::set_type_name(const std::string& name) {
  type_id_ = GetTypeNameTable().intern(name);
}

const PropSlot* PrimSpec::property(PropNameId name_id) const {
  return props_.find(name_id);
}

const PropSlot* PrimSpec::property(const std::string& name) const {
  return props_.find(name);
}

const Value* PrimSpec::property_value(PropNameId name_id) const {
  const PropSlot* slot = property(name_id);
  if (!slot) return nullptr;
  return values_->get(slot->value_offset);
}

const Value* PrimSpec::property_value(const std::string& name) const {
  const PropSlot* slot = property(name);
  if (!slot) return nullptr;
  return values_->get(slot->value_offset);
}

void PrimSpec::add_property(const std::string& name, Value value, uint16_t flags) {
  PropNameId name_id = GetPropNameTable().intern(name);
  add_property(name_id, std::move(value), flags);
}

void PrimSpec::add_property(PropNameId name_id, Value value, uint16_t flags) {
  // Store value
  uint32_t offset = values_->store(std::move(value));

  // Get type from stored value
  const Value* stored = values_->get(offset);
  uint16_t type = static_cast<uint16_t>(stored ? stored->type_id() : TypeId::Invalid);

  // Add slot
  PropSlot slot;
  slot.name_id = name_id;
  slot.value_offset = offset;
  slot.value_type = type;
  slot.flags = flags;

  if (stored && stored->is_array()) {
    slot.flags |= PropSlot::kFlagArray;
  }

  props_.add(slot);
}

void PrimSpec::add_property_slot(PropNameId name_id, TypeId type_id, uint16_t flags) {
  PropSlot slot;
  slot.name_id = name_id;
  slot.value_offset = UINT32_MAX;  // No default value
  slot.value_type = static_cast<uint16_t>(type_id);
  slot.flags = flags;
  props_.add(slot);
}

void PrimSpec::reserve_properties(size_t count) {
  props_.reserve(count);
}

void PrimSpec::finalize_properties() {
  props_.sort();
}

void PrimSpec::add_time_sample(PropNameId name_id, double time, Value value) {
  uint32_t offset = values_->store(std::move(value));
  time_samples_[name_id.id].emplace_back(time, offset);
}

const std::vector<std::pair<double, uint32_t>>* PrimSpec::time_samples(PropNameId name_id) const {
  auto it = time_samples_.find(name_id.id);
  if (it == time_samples_.end()) return nullptr;
  return &it->second;
}

bool PrimSpec::has_time_samples(PropNameId name_id) const {
  return time_samples_.find(name_id.id) != time_samples_.end();
}

const Value* PrimSpec::time_sample_value(uint32_t offset) const {
  if (!values_) return nullptr;
  return values_->get(offset);
}

std::vector<PropNameId> PrimSpec::time_sampled_properties() const {
  std::vector<PropNameId> result;
  result.reserve(time_samples_.size());
  for (const auto& kv : time_samples_) {
    result.push_back(PropNameId{kv.first});
  }
  return result;
}

void PrimSpec::add_relationship(const std::string& name, const Path& target) {
  relationships_[name].push_back(target);
}

const std::vector<Path>* PrimSpec::relationship(const std::string& name) const {
  auto it = relationships_.find(name);
  if (it == relationships_.end()) return nullptr;
  return &it->second;
}

void PrimSpec::add_child_index(uint32_t index) {
  child_indices_.push_back(index);
}

size_t PrimSpec::memory_usage() const {
  size_t size = sizeof(PrimSpec);
  size += name_.capacity();
  size += path_.str().capacity();

  // Properties
  size += props_.slots().capacity() * sizeof(PropSlot);
  if (values_) {
    size += values_->capacity();
    // Estimate Value objects
    size += values_->size() * sizeof(Value);
  }

  // Time samples
  for (const auto& ts : time_samples_) {
    size += ts.second.capacity() * sizeof(std::pair<double, uint32_t>);
  }

  // Relationships
  for (const auto& rel : relationships_) {
    size += rel.first.capacity();
    size += rel.second.capacity() * sizeof(Path);
  }

  // Children
  size += child_indices_.capacity() * sizeof(uint32_t);

  // Metadata
  size += meta_.doc.capacity();
  size += meta_.comment.capacity();
  for (const auto& s : meta_.apiSchemas) size += s.capacity();
  for (const auto& s : meta_.references) size += s.capacity();
  for (const auto& s : meta_.payloads) size += s.capacity();
  for (const auto& s : meta_.inherits) size += s.capacity();
  for (const auto& s : meta_.specializes) size += s.capacity();
  size += meta_.variantSelection.capacity();

  return size;
}

}  // namespace next
}  // namespace tinyusdz
