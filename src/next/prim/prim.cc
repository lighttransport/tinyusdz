// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Prim implementation

#include "prim.hh"
#include <algorithm>

namespace tinyusdz {
namespace next {

Prim::Prim(const std::string& name, const std::string& type_name)
    : name_(name), type_name_(type_name) {}

Prim::Prim(std::string&& name, std::string&& type_name)
    : name_(std::move(name)), type_name_(std::move(type_name)) {}

// ============================================================
// Attributes
// ============================================================

bool Prim::has_attribute(const std::string& name) const {
  return attributes_.find(name) != attributes_.end();
}

const Attribute* Prim::get_attribute(const std::string& name) const {
  auto it = attributes_.find(name);
  return (it != attributes_.end()) ? &it->second : nullptr;
}

Attribute* Prim::get_attribute(const std::string& name) {
  auto it = attributes_.find(name);
  return (it != attributes_.end()) ? &it->second : nullptr;
}

void Prim::set_attribute(const std::string& name, Attribute attr) {
  attributes_[name] = std::move(attr);
}

void Prim::set_attribute(Attribute attr) {
  std::string name = attr.name();
  attributes_[name] = std::move(attr);
}

bool Prim::remove_attribute(const std::string& name) {
  return attributes_.erase(name) > 0;
}

std::vector<std::string> Prim::attribute_names() const {
  std::vector<std::string> names;
  names.reserve(attributes_.size());
  for (const auto& pair : attributes_) {
    names.push_back(pair.first);
  }
  return names;
}

// ============================================================
// Relationships
// ============================================================

void Prim::add_relationship(const std::string& name, const Path& target) {
  relationships_[name].push_back(target);
}

const std::vector<Path>* Prim::get_relationship(const std::string& name) const {
  auto it = relationships_.find(name);
  return (it != relationships_.end()) ? &it->second : nullptr;
}

std::vector<std::string> Prim::relationship_names() const {
  std::vector<std::string> names;
  names.reserve(relationships_.size());
  for (const auto& pair : relationships_) {
    names.push_back(pair.first);
  }
  return names;
}

// ============================================================
// Hierarchy
// ============================================================

void Prim::add_child(Prim child) {
  children_.push_back(std::move(child));
}

const Prim* Prim::find_child(const std::string& name) const {
  for (const auto& child : children_) {
    if (child.name() == name) {
      return &child;
    }
  }
  return nullptr;
}

Prim* Prim::find_child(const std::string& name) {
  for (auto& child : children_) {
    if (child.name() == name) {
      return &child;
    }
  }
  return nullptr;
}

// ============================================================
// Metadata
// ============================================================

const Value* Prim::get_metadata(const std::string& key) const {
  auto it = metadata_.find(key);
  return (it != metadata_.end()) ? &it->second : nullptr;
}

void Prim::set_metadata(const std::string& key, Value value) {
  metadata_[key] = std::move(value);
}

bool Prim::has_metadata(const std::string& key) const {
  return metadata_.find(key) != metadata_.end();
}

std::vector<std::string> Prim::metadata_keys() const {
  std::vector<std::string> keys;
  keys.reserve(metadata_.size());
  for (const auto& pair : metadata_) {
    keys.push_back(pair.first);
  }
  return keys;
}

// ============================================================
// API schemas
// ============================================================

void Prim::add_api_schema(const std::string& schema_name) {
  if (!has_api_schema(schema_name)) {
    api_schemas_.push_back(schema_name);
  }
}

bool Prim::has_api_schema(const std::string& schema_name) const {
  return std::find(api_schemas_.begin(), api_schemas_.end(), schema_name) != api_schemas_.end();
}

}  // namespace next
}  // namespace tinyusdz
