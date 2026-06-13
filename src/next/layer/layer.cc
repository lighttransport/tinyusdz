// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Layer Implementation

#include "layer.hh"
#include <algorithm>

namespace tinyusdz {
namespace next {

// ============================================================
// Layer
// ============================================================

Layer::Layer() = default;
Layer::~Layer() = default;

Layer::Layer(Layer&&) noexcept = default;
Layer& Layer::operator=(Layer&&) noexcept = default;

void Layer::reserve(size_t count) {
  prims_.reserve(count);
  root_indices_.reserve(count / 4);  // Estimate ~25% are roots
}

uint32_t Layer::add_prim(PrimSpec&& spec) {
  uint32_t index = static_cast<uint32_t>(prims_.size());
  prims_.push_back(std::move(spec));
  return index;
}

void Layer::set_parent(uint32_t child_index, uint32_t parent_index) {
  if (parent_index < prims_.size() && child_index < prims_.size()) {
    prims_[parent_index].add_child_index(child_index);
  }
}

void Layer::add_root(uint32_t index) {
  if (index < prims_.size()) {
    root_indices_.push_back(index);
  }
}

void Layer::build_path_index() {
  path_to_index_.clear();
  path_to_index_.reserve(prims_.size());
  for (size_t i = 0; i < prims_.size(); ++i) {
    const std::string& path_str = prims_[i].path().str();
    if (!path_str.empty()) {
      path_to_index_[path_str] = static_cast<uint32_t>(i);
    }
  }
}

void Layer::sort_prims_by_path() {
  std::stable_sort(prims_.begin(), prims_.end(),
                   [](const PrimSpec& a, const PrimSpec& b) {
                     return a.path().str() < b.path().str();
                   });
  path_to_index_.clear();
}

Layer Layer::Clone() const {
  Layer out;
  out.prims_.reserve(prims_.size());
  for (const PrimSpec& prim : prims_) {
    out.prims_.push_back(prim.Clone());
  }
  out.root_indices_ = root_indices_;
  out.meta_ = meta_;
  out.finalized_ = finalized_;
  out.build_path_index();
  return out;
}

void Layer::finalize() {
  if (finalized_) return;

  // Build path-to-index map
  build_path_index();

  // Finalize each prim's properties (sort for binary search)
  for (auto& prim : prims_) {
    prim.finalize_properties();
  }

  // The prim array is append-only during build and immutable after finalize:
  // release any over-reservation so the per-layer fixed cost matches its
  // contents (Phase 8.1 footprint trim).
  prims_.shrink_to_fit();

  finalized_ = true;
}

const PrimSpec* Layer::prim(uint32_t index) const {
  if (index >= prims_.size()) return nullptr;
  return &prims_[index];
}

PrimSpec* Layer::prim(uint32_t index) {
  if (index >= prims_.size()) return nullptr;
  return &prims_[index];
}

const PrimSpec* Layer::prim_at_path(const Path& path) const {
  return prim_at_path(path.str());
}

const PrimSpec* Layer::prim_at_path(const std::string& path) const {
  auto it = path_to_index_.find(path);
  if (it == path_to_index_.end()) return nullptr;
  return prim(it->second);
}

PrimSpec* Layer::prim_at_path_mutable(const std::string& path) {
  auto it = path_to_index_.find(path);
  if (it == path_to_index_.end()) return nullptr;
  return prim(it->second);
}

PrimSpec* Layer::prim_mutable(uint32_t index) {
  if (index >= prims_.size()) return nullptr;
  return &prims_[index];
}

std::vector<const PrimSpec*> Layer::children(uint32_t prim_index) const {
  std::vector<const PrimSpec*> result;
  const PrimSpec* p = prim(prim_index);
  if (!p) return result;

  const auto& indices = p->child_indices();
  result.reserve(indices.size());
  for (uint32_t idx : indices) {
    if (const PrimSpec* child = prim(idx)) {
      result.push_back(child);
    }
  }
  return result;
}

size_t Layer::memory_usage() const {
  size_t size = sizeof(Layer);

  // Prims vector
  size += prims_.capacity() * sizeof(PrimSpec);
  for (const auto& prim : prims_) {
    size += prim.memory_usage() - sizeof(PrimSpec);  // Don't double count
  }

  // Root indices
  size += root_indices_.capacity() * sizeof(uint32_t);

  // Path index map (estimate)
  for (const auto& kv : path_to_index_) {
    size += kv.first.capacity() + sizeof(uint32_t) + sizeof(void*) * 2;  // Rough hash map overhead
  }

  // Metadata
  size += meta_.defaultPrim.capacity();
  size += meta_.upAxis.capacity();
  size += meta_.doc.capacity();
  size += meta_.comment.capacity();
  for (const auto& s : meta_.subLayers) {
    size += s.capacity();
  }

  return size;
}

Layer::Stats Layer::stats() const {
  Stats s{};
  s.prim_count = prims_.size();
  s.root_count = root_indices_.size();

  for (const auto& prim : prims_) {
    s.total_properties += prim.properties().size();
    // Count time samples (simplified)
  }

  s.memory_bytes = memory_usage();
  return s;
}

// ============================================================
// LayerBuilder
// ============================================================

LayerBuilder::LayerBuilder(Layer& layer)
    : layer_(layer) {}

uint32_t LayerBuilder::begin_prim(const std::string& name, const std::string& type_name,
                                   PrimSpecifier specifier) {
  PrimSpec spec(name, type_name);
  spec.set_specifier(specifier);

  // Build path based on parent stack
  std::string path_str;
  if (prim_stack_.empty()) {
    path_str = "/" + name;
  } else {
    // Get parent path
    const PrimSpec* parent = layer_.prim(prim_stack_.back());
    if (parent) {
      path_str = parent->path().str() + "/" + name;
    } else {
      path_str = "/" + name;
    }
  }
  spec.set_path(Path(path_str));

  current_index_ = layer_.add_prim(std::move(spec));

  // Set up parent-child relationship
  if (!prim_stack_.empty()) {
    layer_.set_parent(current_index_, prim_stack_.back());
  } else {
    layer_.add_root(current_index_);
  }

  // Push to stack
  prim_stack_.push_back(current_index_);

  return current_index_;
}

void LayerBuilder::end_prim() {
  if (!prim_stack_.empty()) {
    prim_stack_.pop_back();
  }
  current_index_ = prim_stack_.empty() ? UINT32_MAX : prim_stack_.back();
}

PrimSpec* LayerBuilder::current() {
  if (current_index_ == UINT32_MAX) return nullptr;
  return layer_.prim(current_index_);
}

void LayerBuilder::add_property(const std::string& name, Value value, uint16_t flags) {
  if (PrimSpec* p = current()) {
    p->add_property(name, std::move(value), flags);
  }
}

void LayerBuilder::add_time_sample(const std::string& prop_name, double time, Value value) {
  if (PrimSpec* p = current()) {
    PropNameId name_id = GetPropNameTable().intern(prop_name);

    // First, add the time sample (stores the value and records time)
    TypeId type_id = value.type_id();
    bool is_array = value.is_array();
    p->add_time_sample(name_id, time, std::move(value));

    // Ensure property slot exists with time sampled flag
    // We create/update slot after adding sample so we have type info
    const PropSlot* existing = p->property(name_id);
    if (!existing) {
      // Create property slot for this time-sampled property
      uint16_t flags = PropSlot::kFlagTimeSampled;
      if (is_array) {
        flags |= PropSlot::kFlagArray;
      }
      // Add slot with type info but no default value
      p->add_property_slot(name_id, type_id, flags);
    }
  }
}

void LayerBuilder::add_relationship(const std::string& name, const Path& target) {
  if (PrimSpec* p = current()) {
    p->add_relationship(name, target);
  }
}

void LayerBuilder::set_active(bool active) {
  if (PrimSpec* p = current()) {
    p->meta().active = active;
  }
}

void LayerBuilder::set_hidden(bool hidden) {
  if (PrimSpec* p = current()) {
    p->meta().hidden = hidden;
  }
}

void LayerBuilder::finalize() {
  layer_.finalize();
}

}  // namespace next
}  // namespace tinyusdz
