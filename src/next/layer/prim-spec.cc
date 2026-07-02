// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - PrimSpec Implementation

#include "prim-spec.hh"
#include <algorithm>
#include <cstring>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <mutex>
#endif

namespace tinyusdz {
namespace next {

namespace {

std::string NormalizeAssetPathForRemap(std::string path) {
  for (char& c : path) {
    if (c == '\\') c = '/';
  }
  while (path.rfind("./", 0) == 0) path = path.substr(2);
  while (!path.empty() && path[0] == '/') path = path.substr(1);
  return path;
}

const std::string* FindAssetPathRemap(
    const std::map<std::string, std::string>& remap,
    const std::string& path) {
  auto it = remap.find(path);
  if (it != remap.end()) return &it->second;
  const std::string norm = NormalizeAssetPathForRemap(path);
  it = remap.find(norm);
  if (it != remap.end()) return &it->second;
  return nullptr;
}

uint16_t ApplySchemaPropertyFlags(PropNameId name_id, uint16_t flags) {
  if (!name_id.is_valid()) return flags;
  const PropNameTable& names = GetPropNameTable();
  const std::string& name = names.get(name_id);
  if (name_id == names.id_xformOpOrder || name == "info:id") {
    flags |= PropSlot::kFlagUniform;
  }
  return flags;
}

}  // namespace

// ============================================================
// TypeNameTable
// ============================================================

#if defined(TINYUSDZ_ENABLE_THREAD)
void TypeNameTable::publish_snapshot_locked() {
  auto snap = std::make_unique<Snapshot>();
  snap->map = name_to_id_;
  snap->array_map = base_to_array_type_id_;
  snap->by_id.reserve(names_.size());
  for (const auto& n : names_) snap->by_id.push_back(n.get());
  snapshot_.store(snap.get(), std::memory_order_release);
  retired_.push_back(std::move(snap));
}
#endif

TypeNameId TypeNameTable::intern(std::string_view name) {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Lock-free HIT against the latest immutable snapshot (misses fall through
  // to the locked paths — see the Snapshot note in the header).
  if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
    auto sit = snap->map.find(name);
    if (sit != snap->map.end()) return TypeNameId{sit->second};
  }
  // Read-mostly: try a shared-lock lookup first (the common-type set is
  // pre-registered, so almost every parse-time intern is a hit).
  {
    std::shared_lock<std::shared_mutex> rlk(mu_);
    auto it = name_to_id_.find(name);
    if (it != name_to_id_.end()) {
      return TypeNameId{it->second};
    }
  }
  std::unique_lock<std::shared_mutex> wlk(mu_);
#endif
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return TypeNameId{it->second};
  }

  if (names_.size() >= UINT16_MAX) {
    return TypeNameId{};  // Table full
  }

  uint16_t id = static_cast<uint16_t>(names_.size());
  names_.push_back(std::make_unique<std::string>(name));
  const std::string& key = *names_.back();
  name_to_id_[key] = id;
#if defined(TINYUSDZ_ENABLE_THREAD)
  publish_snapshot_locked();
#endif
  return TypeNameId{id};
}

TypeNameId TypeNameTable::intern_array(std::string_view base_name) {
  if (base_name.empty()) {
    return intern("");
  }

  TypeNameId base_id = intern(base_name);
  if (!base_id.is_valid()) {
    return TypeNameId{};
  }

#if defined(TINYUSDZ_ENABLE_THREAD)
  if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
    auto sit = snap->array_map.find(base_id.id);
    if (sit != snap->array_map.end()) return TypeNameId{sit->second};
  }
#endif
  {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::shared_lock<std::shared_mutex> rlk(mu_);
#endif
    auto it = base_to_array_type_id_.find(base_id.id);
    if (it != base_to_array_type_id_.end()) {
      return TypeNameId{it->second};
    }
  }

  std::string array_name(base_name);
  array_name.reserve(base_name.size() + 2);
  array_name.push_back('[');
  array_name.push_back(']');
  TypeNameId array_id = intern(array_name);
  if (array_id.is_valid()) {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::unique_lock<std::shared_mutex> wlk(mu_);
    base_to_array_type_id_[base_id.id] = array_id.id;
    publish_snapshot_locked();
#else
    base_to_array_type_id_[base_id.id] = array_id.id;
#endif
  }
  return array_id;
}

const std::string& TypeNameTable::get(TypeNameId id) const {
  static const std::string empty;
  if (!id.is_valid()) return empty;
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Lock-free only when the snapshot covers this id (fall through otherwise).
  if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
    if (id.id < snap->by_id.size()) return *snap->by_id[id.id];
  }
  std::shared_lock<std::shared_mutex> rlk(mu_);
#endif
  if (id.id >= names_.size()) return empty;
  return *names_[id.id];  // unique_ptr storage: stable after unlock
}

TypeNameId TypeNameTable::find(std::string_view name) const {
#if defined(TINYUSDZ_ENABLE_THREAD)
  // Lock-free HIT; a miss falls through to the locked authoritative read.
  if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
    auto sit = snap->map.find(name);
    if (sit != snap->map.end()) return TypeNameId{sit->second};
  }
  std::shared_lock<std::shared_mutex> rlk(mu_);
#endif
  auto it = name_to_id_.find(name);
  if (it != name_to_id_.end()) {
    return TypeNameId{it->second};
  }
  return TypeNameId{};
}

void TypeNameTable::register_common_types() {
  if (names_.empty()) {
    names_.reserve(128);
    name_to_id_.reserve(256);
    base_to_array_type_id_.reserve(64);
  }

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

  // Pre-register common array type names so parser/type-name hot paths
  // avoid first-touch allocations for frequently-used built-in types.
  const char* array_type_bases[] = {
      "bool", "int", "uint", "int64", "uint64", "half", "float", "double",
      "string", "token", "asset", "int2", "int3", "int4", "uint2", "uint3", "uint4",
      "half2", "half3", "half4", "float2", "float3", "float4", "double2", "double3", "double4",
      "quath", "quatf", "quatd", "point3h", "point3f", "point3d",
      "vector3h", "vector3f", "vector3d", "normal3h", "normal3f", "normal3d",
      "color3h", "color3f", "color3d", "color4h", "color4f", "color4d",
      "matrix2d", "matrix3d", "matrix4d", "texCoord2h", "texCoord2f", "texCoord2d",
      "texCoord3h", "texCoord3f", "texCoord3d", "timecode", "dictionary"};
  for (const char* base : array_type_bases) {
    (void)intern_array(base);
  }
}

TypeNameTable& GetTypeNameTable() {
  // Registration runs inside a (thread-safe) function-local static init, so
  // concurrent first callers cannot observe a half-registered table. The
  // table itself is constructed in place (shared_mutex member: immovable).
  static TypeNameTable table;
  static const bool initialized = [] {
    table.register_common_types();
    return true;
  }();
  (void)initialized;
  return table;
}

// ============================================================
// ValueStorage
// ============================================================

ValueStorage::ValueStorage() {
  // Grow on demand: eager per-prim reservations cost KBs per prim regardless
  // of property count, which is catastrophic on high-prim-count scenes.
}

ValueStorage::~ValueStorage() = default;

uint32_t ValueStorage::store(const Value& value) {
  uint32_t idx = static_cast<uint32_t>(values_.size());
  values_.push_back(value);
  return idx;
}

uint32_t ValueStorage::store(Value&& value) {
  uint32_t idx = static_cast<uint32_t>(values_.size());
  values_.push_back(std::move(value));
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

size_t ValueStorage::memory_usage() const {
  size_t size = values_.capacity() * sizeof(Value);
  for (const auto& v : values_) {
    if (v.is_array()) {
      size += v.array_size() * 4;  // Approximate (element payload)
    }
  }
  return size;
}

void ValueStorage::clear() {
  values_.clear();
}

// ============================================================
// TimeSampleStorage
// ============================================================

TimeSampleStorage::TimeSampleStorage() {
  // Grow on demand (was reserve(64) ~= 8.7 KB per prim with time samples).
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
  // Lazy (crate-backed) arrays: skip content dedup. find_or_store() hashes the
  // value, and Value::hash() forces an in-place materialize — decoding every
  // array-valued time sample into the heap at parse time, which is the dominant
  // peak-RSS cost on animation-heavy scenes (e.g. Caldera). Store the lazy ref
  // as-is via the no-dedup path so it stays a cheap byte-range reference until
  // the writer decodes it transiently. (Scalar samples are never lazy, so they
  // keep deduping through the path below.)
  if (value.is_lazy()) {
    return add(name_id, time, std::move(value));
  }
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

size_t TimeSampleStorage::remap_asset_paths(
    const std::map<std::string, std::string>& remap) {
  if (remap.empty()) return 0;
  size_t count = 0;
  for (Value& value : values_) {
    const std::string* path = value.as_asset_path();
    if (!path) continue;
    const std::string* mapped = FindAssetPathRemap(remap, *path);
    if (!mapped) continue;
    value = Value::MakeAssetPath(*mapped);
    ++count;
  }
  return count;
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

// values_ / time_samples_ are allocated lazily on first use: most prims in
// composed scenes carry no local values or samples, and two eager heap blocks
// per prim dominated the per-prim fixed cost on high-prim-count layers.
PrimSpec::PrimSpec() {}

PrimSpec::PrimSpec(std::string_view name) : name_(name) {}

PrimSpec::PrimSpec(std::string_view name, std::string_view type_name)
    : name_(name) {
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

  // Deep copy properties (slots carry value-storage indices + flags).
  c.props_ = props_;

  // Deep copy the value storage. A slot's value_offset is a position in the
  // values_ vector, so copying the vector keeps every offset valid — no need to
  // re-add (the old code did both `c.props_ = props_` AND a re-add loop, which
  // duplicated every property). Lazy array Values copy as a shared_ptr bump
  // (no payload copy), so cloning a crate-backed prim stays cheap.
  if (values_) {
    c.values_ = std::make_unique<ValueStorage>(*values_);
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

  // Deep copy attribute connections + declared type names
  c.connections_ = connections_;
  c.prop_type_names_ = prop_type_names_;

  // Deep copy per-property metadata (unique_ptr side table).
  for (const auto& kv : prop_metas_) {
    if (kv.second) c.prop_metas_[kv.first] = std::make_unique<PropMeta>(*kv.second);
  }

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
  set_type_name(std::string_view(name));
}

void PrimSpec::set_type_name(std::string_view name) {
  type_id_ = GetTypeNameTable().intern(name);
}

const PropSlot* PrimSpec::property(PropNameId name_id) const {
  return props_.find(name_id);
}

const PropSlot* PrimSpec::property(std::string_view name) const {
  return props_.find(GetPropNameTable().find(name));
}

const Value* PrimSpec::property_value(PropNameId name_id) const {
  const PropSlot* slot = property(name_id);
  if (!slot || !values_) return nullptr;
  return values_->get(slot->value_offset);
}

const Value* PrimSpec::property_value(std::string_view name) const {
  const PropSlot* slot = property(name);
  if (!slot || !values_) return nullptr;
  return values_->get(slot->value_offset);
}

void PrimSpec::add_property(std::string_view name, Value value, uint16_t flags) {
  add_property(GetPropNameTable().intern(name), std::move(value), flags);
}

void PrimSpec::add_property(PropNameId name_id, Value value, uint16_t flags) {
  if (!name_id.is_valid()) return;
  flags = ApplySchemaPropertyFlags(name_id, flags);
  // Store value
  if (!values_) values_ = std::make_unique<ValueStorage>();
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

void PrimSpec::add_relationship(std::string_view name, const Path& target) {
  if (name.empty()) return;
  relationships_[std::string(name)].push_back(target);
}

void PrimSpec::add_relationship(std::string_view name, Path&& target) {
  if (name.empty()) return;
  relationships_[std::string(name)].push_back(std::move(target));
}

void PrimSpec::add_connection(std::string_view prop_name, const Path& target) {
  if (prop_name.empty()) return;
  PropNameId id = GetPropNameTable().intern(prop_name);
  if (!id.is_valid()) return;
  connections_[id.id].push_back(target);
}

void PrimSpec::add_connection(std::string_view prop_name, Path&& target) {
  if (prop_name.empty()) return;
  PropNameId id = GetPropNameTable().intern(prop_name);
  if (!id.is_valid()) return;
  connections_[id.id].push_back(std::move(target));
}

void PrimSpec::add_connection(PropNameId prop_name_id, const Path& target) {
  if (!prop_name_id.is_valid()) return;
  connections_[prop_name_id.id].push_back(target);
}

void PrimSpec::add_connection(PropNameId prop_name_id, Path&& target) {
  if (!prop_name_id.is_valid()) return;
  connections_[prop_name_id.id].push_back(std::move(target));
}

const PropMeta* PrimSpec::property_meta(std::string_view prop_name) const {
  return property_meta(GetPropNameTable().find(prop_name));
}

const std::string* PrimSpec::property_type_name(std::string_view prop_name) const {
  PropNameId id = GetPropNameTable().find(prop_name);
  if (!id.is_valid()) return nullptr;
  return property_type_name(id);
}

const std::string* PrimSpec::property_type_name(PropNameId prop_name_id) const {
  if (!prop_name_id.is_valid()) return nullptr;
  auto it = prop_type_names_.find(prop_name_id.id);
  if (it == prop_type_names_.end()) return nullptr;
  PropNameId tn_id;
  tn_id.id = it->second;
  return &GetPropNameTable().get(tn_id);
}

void PrimSpec::set_property_type_name(std::string_view prop_name,
                                     std::string_view type_name) {
  // Intern both name and typeName; typeNames are few and highly shared.
  PropNameId prop_id = GetPropNameTable().intern(prop_name);
  if (!prop_id.is_valid()) return;
  set_property_type_name(prop_id, type_name);
}

void PrimSpec::set_property_type_name(PropNameId prop_name_id,
                                     std::string_view type_name) {
  if (!prop_name_id.is_valid()) return;
  PropNameId tn_id = GetPropNameTable().intern(type_name);
  if (!tn_id.is_valid()) return;
  prop_type_names_[prop_name_id.id] = tn_id.id;
}

PropMeta& PrimSpec::ensure_property_meta(std::string_view prop_name) {
  return ensure_property_meta(GetPropNameTable().intern(prop_name));
}

void PrimSpec::add_property_slot(PropNameId name_id, TypeId type_id, uint16_t flags) {
  flags = ApplySchemaPropertyFlags(name_id, flags);

  PropSlot slot;
  slot.name_id = name_id;
  slot.value_offset = UINT32_MAX;  // No default value
  slot.value_type = static_cast<uint16_t>(type_id);
  slot.flags = flags;
  props_.add(slot);
}

bool PrimSpec::fill_property_value_if_absent(PropNameId name_id, Value value) {
  PropSlot* slot = props_.find_mutable(name_id);
  if (!slot) return false;
  if (slot->value_offset != UINT32_MAX) return false;  // already has a value
  if (!values_) values_ = std::make_unique<ValueStorage>();
  uint32_t offset = values_->store(std::move(value));
  const Value* stored = values_->get(offset);
  slot->value_offset = offset;
  // Bind the concrete stored type (and array flag); keep the declared slot type
  // if the incoming value is Invalid.
  if (stored && stored->type_id() != TypeId::Invalid) {
    slot->value_type = static_cast<uint16_t>(stored->type_id());
    if (stored->is_array()) slot->flags |= PropSlot::kFlagArray;
  }
  return true;
}

bool PrimSpec::set_property_value(PropNameId name_id, Value value) {
  PropSlot* slot = props_.find_mutable(name_id);
  if (!slot || slot->value_offset == UINT32_MAX) return false;
  if (!values_) return false;
  uint32_t offset = values_->store(std::move(value));
  const Value* stored = values_->get(offset);
  slot->value_offset = offset;
  if (stored && stored->type_id() != TypeId::Invalid) {
    slot->value_type = static_cast<uint16_t>(stored->type_id());
    if (stored->is_array()) slot->flags |= PropSlot::kFlagArray;
    else slot->flags &= static_cast<uint16_t>(~PropSlot::kFlagArray);
  }
  return true;
}

size_t PrimSpec::remap_asset_paths(
    const std::map<std::string, std::string>& remap) {
  if (remap.empty()) return 0;
  size_t count = 0;
  for (const PropSlot& slot : props_.slots()) {
    const Value* value = property_value(slot.name_id);
    if (!value) continue;
    const std::string* path = value->as_asset_path();
    if (!path) continue;
    const std::string* mapped = FindAssetPathRemap(remap, *path);
    if (!mapped) continue;
    if (set_property_value(slot.name_id, Value::MakeAssetPath(*mapped))) {
      ++count;
    }
  }
  if (time_samples_) {
    count += time_samples_->remap_asset_paths(remap);
  }
  return count;
}

void PrimSpec::reserve_properties(size_t count) {
  props_.reserve(count);
}

void PrimSpec::finalize_properties() {
  props_.sort();
}

void PrimSpec::mark_property_time_sampled(PropNameId name_id) {
  if (PropSlot* slot = props_.find_mutable(name_id)) {
    slot->flags |= PropSlot::kFlagTimeSampled;
  }
}

void PrimSpec::add_time_sample(PropNameId name_id, double time, Value value,
                               bool dedup) {
  // Use deduplicated storage for array values (common case for animation).
  // `dedup=false` is for deferred-fill values from the async USDA array parser:
  // their payload is not filled yet, so find_or_store()'s content hash would be
  // meaningless (same reasoning as the is_lazy() bypass in add_dedup).
  if (!time_samples_) time_samples_ = std::make_unique<TimeSampleStorage>();
  if (dedup) {
    time_samples_->add_dedup(name_id, time, std::move(value));
  } else {
    time_samples_->add(name_id, time, std::move(value));
  }
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

void PrimSpec::apply_relationship_list_op(const std::string& name,
                                          const std::vector<Path>& targets,
                                          RelationshipListOp op) {
  if (targets.empty()) return;
  std::vector<Path>& dst = relationships_[name];

  switch (op) {
    case RelationshipListOp::Prepend:
      dst.insert(dst.begin(), targets.begin(), targets.end());
      break;
    case RelationshipListOp::Add:
      for (const Path& t : targets) {
        if (std::find(dst.begin(), dst.end(), t) == dst.end()) {
          dst.push_back(t);
        }
      }
      break;
    case RelationshipListOp::Delete:
      dst.erase(std::remove_if(dst.begin(), dst.end(),
                               [&](const Path& p) {
                                 return std::find(targets.begin(), targets.end(),
                                                  p) != targets.end();
                               }),
                dst.end());
      if (dst.empty()) relationships_.erase(name);
      break;
    case RelationshipListOp::Append:
    default:
      dst.insert(dst.end(), targets.begin(), targets.end());
      break;
  }
}

const std::vector<Path>* PrimSpec::relationship(const std::string& name) const {
  auto it = relationships_.find(name);
  if (it == relationships_.end()) return nullptr;
  return &it->second;
}

const std::vector<Path>* PrimSpec::relationship(PropNameId name_id) const {
  if (!name_id.is_valid()) return nullptr;
  return PrimSpec::relationship(GetPropNameTable().get(name_id));
}

std::vector<std::string> PrimSpec::relationship_names() const {
  std::vector<std::string> names;
  names.reserve(relationships_.size());
  for (const auto& [name, _] : relationships_) {
    names.push_back(name);
  }
  // relationships_ is an unordered_map; sort for a deterministic, stable order
  // across runs/platforms (callers and tests rely on consistent ordering).
  std::sort(names.begin(), names.end());
  return names;
}

const std::vector<Path>* PrimSpec::connection(const std::string& prop_name) const {
  PropNameId id = GetPropNameTable().find(prop_name);
  if (!id.is_valid()) return nullptr;
  auto it = connections_.find(id.id);
  if (it == connections_.end()) return nullptr;
  return &it->second;
}

const std::vector<Path>* PrimSpec::connection(PropNameId name_id) const {
  if (!name_id.is_valid()) return nullptr;
  auto it = connections_.find(name_id.id);
  if (it == connections_.end()) return nullptr;
  return &it->second;
}

void PrimSpec::remap_target_prefix(const std::string& old_prefix,
                                   const std::string& new_prefix) {
  auto remap = [&](std::vector<Path>& targets) {
    for (Path& t : targets) {
      const std::string s = t.str();
      if (s == old_prefix) {
        t = Path(new_prefix);
      } else if (s.size() > old_prefix.size() &&
                 s.compare(0, old_prefix.size(), old_prefix) == 0 &&
                 s[old_prefix.size()] == '/') {
        t = Path(new_prefix + s.substr(old_prefix.size()));
      }
    }
  };
  for (auto& kv : relationships_) remap(kv.second);
  for (auto& kv : connections_) remap(kv.second);
}

const PropMeta* PrimSpec::property_meta(PropNameId name_id) const {
  if (!name_id.is_valid()) return nullptr;
  auto it = prop_metas_.find(name_id.id);
  if (it == prop_metas_.end()) return nullptr;
  return it->second.get();
}

PropMeta& PrimSpec::ensure_property_meta(PropNameId name_id) {
  std::unique_ptr<PropMeta>& slot = prop_metas_[name_id.id];
  if (!slot) slot = std::make_unique<PropMeta>();
  return *slot;
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
    size += values_->memory_usage();
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

  // Metadata (inline hot fields)
  for (const auto& s : meta_.references) size += s.capacity();
  for (const auto& s : meta_.payloads) size += s.capacity();
  for (const auto& s : meta_.inherits) size += s.capacity();
  for (const auto& s : meta_.specializes) size += s.capacity();
  size += meta_.variantSelection.capacity();
  // Cold fields only cost anything when the ext was allocated.
  if (const PrimSpecMetaExt* ext = meta_.ext()) {
    size += sizeof(PrimSpecMetaExt);
    size += ext->doc.capacity();
    size += ext->comment.capacity();
    size += ext->instance_prototype.capacity();
    size += ext->apiSchemas.size() * sizeof(TfToken);  // interned: id only
  }

  return size;
}

}  // namespace next
}  // namespace tinyusdz
