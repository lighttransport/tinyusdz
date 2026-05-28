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
// TimeSampleStorage
// ============================================================

TimeSampleStorage::TimeSampleStorage() {
  values_.reserve(64);
}

TimeSampleStorage::~TimeSampleStorage() = default;

uint32_t TimeSampleStorage::find_or_store(Value value) {
  // Compute hash
  uint64_t h = value.hash();

  // Check for existing value with same hash
  auto it = hash_to_offsets_.find(h);
  if (it != hash_to_offsets_.end()) {
    // Check each offset for equality (handle hash collisions)
    for (uint32_t offset : it->second) {
      if (values_[offset] == value) {
        // Found duplicate - reuse existing offset
        ++dedup_count_;
        return offset;
      }
    }
  }

  // Store new value
  uint32_t offset = static_cast<uint32_t>(values_.size());
  values_.push_back(std::move(value));

  // Add to hash table
  hash_to_offsets_[h].push_back(offset);

  return offset;
}

uint32_t TimeSampleStorage::add(PropNameId name_id, double time, Value value) {
  // Store without deduplication (faster for unique values)
  uint32_t offset = static_cast<uint32_t>(values_.size());
  values_.push_back(std::move(value));
  samples_[name_id.id].emplace_back(time, offset);
  return offset;
}

uint32_t TimeSampleStorage::add_dedup(PropNameId name_id, double time, Value value) {
  // Store with deduplication (good for repeated array values)
  uint32_t offset = find_or_store(std::move(value));
  samples_[name_id.id].emplace_back(time, offset);
  return offset;
}

const std::vector<std::pair<double, uint32_t>>* TimeSampleStorage::get(PropNameId name_id) const {
  auto it = samples_.find(name_id.id);
  if (it == samples_.end()) return nullptr;
  return &it->second;
}

const Value* TimeSampleStorage::value(uint32_t offset) const {
  if (offset >= values_.size()) return nullptr;
  return &values_[offset];
}

bool TimeSampleStorage::has(PropNameId name_id) const {
  return samples_.find(name_id.id) != samples_.end();
}

SampleResult TimeSampleStorage::interpolate(PropNameId name_id, double time,
                                            TimeInterpolation mode) const {
  const auto* samples = get(name_id);
  if (!samples || samples->empty()) {
    return SampleResult{};
  }

  // Use the template method with a lambda to get values
  return TimeInterpolator::InterpolateWithOffsets(
      *samples,
      [this](uint32_t offset) -> const Value* { return value(offset); },
      time,
      mode);
}

std::vector<PropNameId> TimeSampleStorage::properties() const {
  std::vector<PropNameId> result;
  result.reserve(samples_.size());
  for (const auto& kv : samples_) {
    result.push_back(PropNameId{kv.first});
  }
  return result;
}

size_t TimeSampleStorage::memory_usage() const {
  size_t size = sizeof(*this);

  // Values
  size += values_.capacity() * sizeof(Value);
  for (const auto& v : values_) {
    // Add array storage size if applicable
    if (v.is_array()) {
      size += v.array_size() * 4;  // Approximate
    }
  }

  // Samples map
  for (const auto& kv : samples_) {
    size += kv.second.capacity() * sizeof(std::pair<double, uint32_t>);
  }

  // Hash table
  for (const auto& kv : hash_to_offsets_) {
    size += kv.second.capacity() * sizeof(uint32_t);
  }

  return size;
}

void TimeSampleStorage::clear() {
  samples_.clear();
  values_.clear();
  hash_to_offsets_.clear();
  dedup_count_ = 0;
}

TimeSampleStorage::Stats TimeSampleStorage::stats() const {
  Stats s = {};
  s.property_count = samples_.size();
  for (const auto& kv : samples_) {
    s.total_samples += kv.second.size();
  }
  s.unique_values = values_.size();
  s.dedup_count = dedup_count_;
  s.memory_bytes = memory_usage();
  return s;
}

// ============================================================
// PrimSpec
// ============================================================

PrimSpec::PrimSpec()
    : values_(std::make_unique<ValueStorage>()),
      time_samples_(std::make_unique<TimeSampleStorage>()) {}

PrimSpec::PrimSpec(const std::string& name)
    : name_(name),
      values_(std::make_unique<ValueStorage>()),
      time_samples_(std::make_unique<TimeSampleStorage>()) {}

PrimSpec::PrimSpec(const std::string& name, const std::string& type_name)
    : name_(name),
      values_(std::make_unique<ValueStorage>()),
      time_samples_(std::make_unique<TimeSampleStorage>()) {
  set_type_name(type_name);
}

PrimSpec::~PrimSpec() = default;

PrimSpec::PrimSpec(PrimSpec&& other) noexcept = default;
PrimSpec& PrimSpec::operator=(PrimSpec&& other) noexcept = default;

PrimSpec PrimSpec::Clone() const {
  PrimSpec c(name_);
  c.type_id_ = type_id_;
  c.specifier_ = specifier_;
  c.path_ = path_;

  // Deep copy properties
  c.props_ = props_;

  // Deep copy values
  if (values_) {
    c.values_ = std::make_unique<ValueStorage>();
    // Copy each property value
    for (const auto& slot : props_.slots()) {
      const Value* v = property_value(slot.name_id);
      if (v) {
        c.add_property(slot.name_id, *v, slot.flags);
      }
    }
  }

  // Deep copy time samples
  if (time_samples_) {
    c.time_samples_ = std::make_unique<TimeSampleStorage>();
    // Copy time samples per property
    for (auto prop_id : time_sampled_properties()) {
      auto* samples = time_samples(prop_id);
      if (samples) {
        for (const auto& [t, offset] : *samples) {
          const Value* v = time_sample_value(offset);
          if (v) {
            c.add_time_sample(prop_id, t, *v);
          }
        }
      }
    }
  }

  // Deep copy relationships
  c.relationships_ = relationships_;

  // Deep copy children
  c.child_indices_ = child_indices_;

  // Deep copy metadata (simple struct - copyable)
  c.meta_ = meta_;

  return c;
}

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
  // Use deduplicated storage for array values (common case for animation)
  time_samples_->add_dedup(name_id, time, std::move(value));
}

const std::vector<std::pair<double, uint32_t>>* PrimSpec::time_samples(PropNameId name_id) const {
  if (!time_samples_) return nullptr;
  return time_samples_->get(name_id);
}

bool PrimSpec::has_time_samples(PropNameId name_id) const {
  if (!time_samples_) return false;
  return time_samples_->has(name_id);
}

const Value* PrimSpec::time_sample_value(uint32_t offset) const {
  if (!time_samples_) return nullptr;
  return time_samples_->value(offset);
}

std::vector<PropNameId> PrimSpec::time_sampled_properties() const {
  if (!time_samples_) return {};
  return time_samples_->properties();
}

bool PrimSpec::has_any_time_samples() const {
  if (!time_samples_) return false;
  return !time_samples_->empty();
}

TimeSampleStorage::Stats PrimSpec::time_sample_stats() const {
  if (!time_samples_) return {};
  return time_samples_->stats();
}

SampleResult PrimSpec::interpolate_time_sample(PropNameId name_id, double time,
                                               TimeInterpolation mode) const {
  if (!time_samples_) return SampleResult{};
  return time_samples_->interpolate(name_id, time, mode);
}

SampleResult PrimSpec::interpolate_time_sample(const std::string& name, double time,
                                               TimeInterpolation mode) const {
  PropNameId name_id = GetPropNameTable().find(name);
  if (!name_id.is_valid()) return SampleResult{};
  return interpolate_time_sample(name_id, time, mode);
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

  // Time samples (use TimeSampleStorage's memory tracking)
  if (time_samples_) {
    size += time_samples_->memory_usage();
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
