// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Stage Implementation

#include "stage.hh"
#include "../composition/composition.hh"
#include <algorithm>

namespace tinyusdz {
namespace next {

// ============================================================
// UsdPrim
// ============================================================

static const std::string kEmptyString;
static const Path kEmptyPath;
static const PrimSpecMeta kDefaultMeta;

const std::string& UsdPrim::GetName() const {
  if (!spec_) return kEmptyString;
  return spec_->name();
}

const std::string& UsdPrim::GetTypeName() const {
  if (!spec_) return kEmptyString;
  return spec_->type_name();
}

const Path& UsdPrim::GetPath() const {
  if (!spec_) return kEmptyPath;
  return spec_->path();
}

PrimSpecifier UsdPrim::GetSpecifier() const {
  if (!spec_) return PrimSpecifier::Def;
  return spec_->specifier();
}

bool UsdPrim::IsActive() const {
  if (!spec_) return false;
  return spec_->meta().active;
}

bool UsdPrim::IsDefined() const {
  if (!spec_) return false;
  return spec_->specifier() == PrimSpecifier::Def;
}

bool UsdPrim::HasProperty(const std::string& name) const {
  if (!spec_) return false;
  return spec_->property(name) != nullptr;
}

const Value* UsdPrim::GetPropertyValue(const std::string& name) const {
  if (!spec_) return nullptr;
  return spec_->property_value(name);
}

const Value* UsdPrim::GetPropertyValue(PropNameId name_id) const {
  if (!spec_) return nullptr;
  return spec_->property_value(name_id);
}

std::vector<std::string> UsdPrim::GetPropertyNames() const {
  std::vector<std::string> names;
  if (!spec_) return names;

  const auto& props = spec_->properties();
  names.reserve(props.size());

  PropNameTable& table = GetPropNameTable();
  for (const auto& slot : props.slots()) {
    names.push_back(table.get(slot.name_id));
  }
  return names;
}

bool UsdPrim::HasTimeSamples(const std::string& name) const {
  if (!spec_) return false;
  PropNameId name_id = GetPropNameTable().find(name);
  if (!name_id.is_valid()) return false;
  return spec_->has_time_samples(name_id);
}

std::vector<double> UsdPrim::GetTimeSampleTimes(const std::string& name) const {
  std::vector<double> times;
  if (!spec_) return times;

  PropNameId name_id = GetPropNameTable().find(name);
  if (!name_id.is_valid()) return times;

  const auto* samples = spec_->time_samples(name_id);
  if (!samples) return times;

  times.reserve(samples->size());
  for (const auto& sample : *samples) {
    times.push_back(sample.first);
  }
  return times;
}

const Value* UsdPrim::GetValueAtTime(const std::string& name, double time) const {
  if (!spec_) return nullptr;

  PropNameId name_id = GetPropNameTable().find(name);
  if (!name_id.is_valid()) return nullptr;

  const auto* samples = spec_->time_samples(name_id);
  if (!samples || samples->empty()) {
    // Fall back to default value
    return spec_->property_value(name_id);
  }

  // Binary search for closest time
  auto it = std::lower_bound(samples->begin(), samples->end(), time,
                              [](const std::pair<double, uint32_t>& p, double t) {
                                return p.first < t;
                              });

  if (it == samples->end()) {
    // Time is past all samples, use last
    return spec_->time_sample_value(samples->back().second);
  }
  if (it == samples->begin()) {
    // Time is before all samples, use first
    return spec_->time_sample_value(it->second);
  }

  // Check if exact match or use previous
  if (it->first == time) {
    return spec_->time_sample_value(it->second);
  }

  // Return the previous sample (held interpolation)
  --it;
  return spec_->time_sample_value(it->second);
}

Value UsdPrim::GetInterpolatedValue(const std::string& name, double time) const {
  // For now, just return the held value (no interpolation)
  // TODO: Implement linear interpolation for numeric types
  const Value* val = GetValueAtTime(name, time);
  if (val) return *val;
  return Value();
}

const std::vector<Path>* UsdPrim::GetRelationship(const std::string& name) const {
  if (!spec_) return nullptr;
  return spec_->relationship(name);
}

std::vector<std::string> UsdPrim::GetRelationshipNames() const {
  if (!spec_) return {};
  return spec_->relationship_names();
}

UsdPrim UsdPrim::GetParent() const {
  if (!spec_ || !layer_) return UsdPrim();

  // Parse parent path from current path
  const std::string& path_str = spec_->path().str();
  if (path_str.empty() || path_str == "/") return UsdPrim();

  size_t last_slash = path_str.rfind('/');
  if (last_slash == 0) {
    // Parent is pseudo-root, not a real prim
    return UsdPrim();
  }
  if (last_slash == std::string::npos) return UsdPrim();

  std::string parent_path = path_str.substr(0, last_slash);
  const PrimSpec* parent = layer_->prim_at_path(parent_path);
  if (!parent) return UsdPrim();

  // Find parent index (inefficient but works)
  for (size_t i = 0; i < layer_->prim_count(); ++i) {
    if (layer_->prim(static_cast<uint32_t>(i)) == parent) {
      return UsdPrim(parent, layer_, static_cast<uint32_t>(i));
    }
  }
  return UsdPrim();
}

// For instance proxies, children are provided by the prototype prim's subtree.
const PrimSpec* UsdPrim::ChildSourceSpec() const {
  if (spec_ && layer_ && !spec_->meta().instance_prototype().empty()) {
    const PrimSpec* proto =
        layer_->prim_at_path(Path(spec_->meta().instance_prototype()));
    if (proto) return proto;
  }
  return spec_;
}

std::vector<UsdPrim> UsdPrim::GetChildren() const {
  std::vector<UsdPrim> children;
  if (!spec_ || !layer_) return children;

  const PrimSpec* src = ChildSourceSpec();
  const auto& indices = src->child_indices();
  children.reserve(indices.size());

  for (uint32_t idx : indices) {
    const PrimSpec* child = layer_->prim(idx);
    if (child) {
      children.emplace_back(child, layer_, idx);
    }
  }
  return children;
}

size_t UsdPrim::GetChildCount() const {
  if (!spec_) return 0;
  return ChildSourceSpec()->child_count();
}

UsdPrim UsdPrim::GetChild(const std::string& name) const {
  if (!spec_ || !layer_) return UsdPrim();

  for (uint32_t idx : ChildSourceSpec()->child_indices()) {
    const PrimSpec* child = layer_->prim(idx);
    if (child && child->name() == name) {
      return UsdPrim(child, layer_, idx);
    }
  }
  return UsdPrim();
}

const PrimSpecMeta& UsdPrim::GetMeta() const {
  if (!spec_) return kDefaultMeta;
  return spec_->meta();
}

// ============================================================
// Stage
// ============================================================

Stage::Stage() = default;
Stage::~Stage() = default;

Stage::Stage(Stage&&) noexcept = default;
Stage& Stage::operator=(Stage&&) noexcept = default;

void Stage::SetRootLayer(Layer&& layer) {
  root_layer_ = std::make_unique<Layer>(std::move(layer));
  UpdateMetaFromRootLayer();
}

void Stage::AddSubLayer(Layer&& layer) {
  sub_layers_.push_back(std::make_unique<Layer>(std::move(layer)));
}

const Layer* Stage::GetSubLayer(size_t index) const {
  if (index >= sub_layers_.size()) return nullptr;
  return sub_layers_[index].get();
}

void Stage::UpdateMetaFromRootLayer() {
  if (!root_layer_) return;

  const LayerMeta& lm = root_layer_->meta();
  meta_.defaultPrim = lm.defaultPrim;
  meta_.upAxis = lm.upAxis;
  meta_.metersPerUnit = lm.metersPerUnit;
  meta_.timeCodesPerSecond = lm.timeCodesPerSecond;
  meta_.startTimeCode = lm.startTimeCode;
  meta_.endTimeCode = lm.endTimeCode;
  meta_.framesPerSecond = lm.framesPerSecond;
  meta_.framesPerSecond_set = lm.framesPerSecond_set;
  meta_.kilogramsPerUnit = lm.kilogramsPerUnit;
  meta_.kilogramsPerUnit_set = lm.kilogramsPerUnit_set;
  meta_.colorConfiguration = lm.colorConfiguration;
  meta_.colorManagementSystem = lm.colorManagementSystem;
  meta_.doc = lm.doc;
  meta_.comment = lm.comment;
}

UsdPrim Stage::GetPseudoRoot() const {
  // Pseudo-root is a virtual prim, we return an invalid prim
  // Users should use GetRootPrims() instead
  return UsdPrim();
}

UsdPrim Stage::GetPrimAtPath(const Path& path) const {
  return GetPrimAtPath(path.str());
}

UsdPrim Stage::GetPrimAtPath(const std::string& path) const {
  if (!root_layer_) return UsdPrim();

  const PrimSpec* spec = root_layer_->prim_at_path(path);
  if (!spec) return UsdPrim();

  // Find index (could be optimized with reverse map)
  for (size_t i = 0; i < root_layer_->prim_count(); ++i) {
    if (root_layer_->prim(static_cast<uint32_t>(i)) == spec) {
      return UsdPrim(spec, root_layer_.get(), static_cast<uint32_t>(i));
    }
  }
  return UsdPrim();
}

UsdPrim Stage::GetDefaultPrim() const {
  if (meta_.defaultPrim.empty()) return UsdPrim();
  return GetPrimAtPath("/" + meta_.defaultPrim);
}

std::vector<UsdPrim> Stage::GetRootPrims() const {
  std::vector<UsdPrim> prims;
  if (!root_layer_) return prims;

  const auto& indices = root_layer_->root_indices();
  prims.reserve(indices.size());

  for (uint32_t idx : indices) {
    const PrimSpec* spec = root_layer_->prim(idx);
    if (spec) {
      prims.emplace_back(spec, root_layer_.get(), idx);
    }
  }
  return prims;
}

bool Stage::HasPrimAtPath(const Path& path) const {
  return HasPrimAtPath(path.str());
}

bool Stage::HasPrimAtPath(const std::string& path) const {
  if (!root_layer_) return false;
  return root_layer_->prim_at_path(path) != nullptr;
}

bool Stage::TraverseImpl(uint32_t prim_index, const Layer* layer,
                          const std::function<bool(const UsdPrim&)>& callback) const {
  const PrimSpec* spec = layer->prim(prim_index);
  if (!spec) return true;

  UsdPrim prim(spec, layer, prim_index);

  // Call callback, stop if returns false
  if (!callback(prim)) return false;

  // Recurse to children, propagate stop signal
  for (uint32_t child_idx : spec->child_indices()) {
    if (!TraverseImpl(child_idx, layer, callback)) {
      return false;
    }
  }
  return true;
}

std::vector<UsdPrim> Stage::GetPrimsOfType(const std::string& typeName) const {
  std::vector<UsdPrim> result;

  Traverse([&](const UsdPrim& prim) {
    if (prim.GetTypeName() == typeName) {
      result.push_back(prim);
    }
    return true;  // Continue traversal
  });

  return result;
}

Layer Stage::Flatten() const {
  if (!root_layer_) return {};

  // Use the Compositor to resolve sublayers, references, payloads, etc.
  Compositor compositor;
  CompositionOptions opts;
  opts.load_payloads = true;
  compositor.SetOptions(opts);

  auto result = compositor.Compose(*root_layer_);
  if (!result) return {};

  return std::move(*result);
}

double Stage::GetStartTimeCode() const {
  return meta_.startTimeCode;
}

double Stage::GetEndTimeCode() const {
  return meta_.endTimeCode;
}

double Stage::GetTimeCodesPerSecond() const {
  return meta_.timeCodesPerSecond;
}

bool Stage::HasTimeSamples() const {
  // Check if any prim has time samples
  bool has_samples = false;
  Traverse([&](const UsdPrim& prim) {
    // Check all properties for time samples
    // Simplified: just check if endTimeCode > startTimeCode
    if (meta_.endTimeCode > meta_.startTimeCode) {
      has_samples = true;
      return false;  // Stop traversal
    }
    return true;
  });
  return has_samples;
}

size_t Stage::GetPrimCount() const {
  if (!root_layer_) return 0;
  return root_layer_->prim_count();
}

size_t Stage::GetMemoryUsage() const {
  size_t size = sizeof(Stage);
  size += sizeof(StageMeta);
  size += meta_.defaultPrim.capacity();
  size += meta_.upAxis.capacity();
  size += meta_.doc.capacity();

  if (root_layer_) {
    size += root_layer_->memory_usage();
  }

  for (const auto& sub : sub_layers_) {
    if (sub) {
      size += sub->memory_usage();
    }
  }

  return size;
}

Stage::Stats Stage::GetStats() const {
  Stats s{};
  s.layer_count = 1 + sub_layers_.size();

  if (root_layer_) {
    auto layer_stats = root_layer_->stats();
    s.prim_count = layer_stats.prim_count;
    s.total_properties = layer_stats.total_properties;
  }

  s.memory_bytes = GetMemoryUsage();
  return s;
}

// ============================================================
// StageBuilder
// ============================================================

StageBuilder::StageBuilder()
    : layer_(std::make_unique<Layer>()),
      layer_builder_(std::make_unique<LayerBuilder>(*layer_)) {}

StageBuilder::~StageBuilder() = default;

void StageBuilder::SetDefaultPrim(const std::string& primName) {
  meta_.defaultPrim = primName;
  layer_->meta().defaultPrim = primName;
}

void StageBuilder::SetUpAxis(const std::string& axis) {
  meta_.upAxis = axis;
  layer_->meta().upAxis = axis;
}

void StageBuilder::SetMetersPerUnit(double value) {
  meta_.metersPerUnit = value;
  layer_->meta().metersPerUnit = value;
}

void StageBuilder::SetTimeCodesPerSecond(double fps) {
  meta_.timeCodesPerSecond = fps;
  layer_->meta().timeCodesPerSecond = fps;
}

void StageBuilder::SetStartTimeCode(double time) {
  meta_.startTimeCode = time;
  layer_->meta().startTimeCode = time;
}

void StageBuilder::SetEndTimeCode(double time) {
  meta_.endTimeCode = time;
  layer_->meta().endTimeCode = time;
}

Stage StageBuilder::Build() {
  layer_builder_->finalize();

  Stage stage;
  stage.SetRootLayer(std::move(*layer_));

  // Reset for potential reuse
  layer_ = std::make_unique<Layer>();
  layer_builder_ = std::make_unique<LayerBuilder>(*layer_);

  return stage;
}

}  // namespace next
}  // namespace tinyusdz
