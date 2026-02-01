// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USD Prim definition
// Represents a primitive in the USD scene hierarchy

#pragma once

#include "path.hh"
#include "attribute.hh"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace tinyusdz {
namespace next {

/// Specifier for prim definition
enum class Specifier : uint8_t {
  Def,    // Definition (concrete prim)
  Over,   // Override (composition)
  Class   // Abstract class (for inheritance)
};

/// Prim - a node in the USD scene hierarchy
class Prim {
public:
  /// Default constructor
  Prim() = default;

  /// Construct with name and type
  Prim(const std::string& name, const std::string& type_name = "");
  Prim(std::string&& name, std::string&& type_name);

  /// Copy and move
  Prim(const Prim&) = default;
  Prim(Prim&&) = default;
  Prim& operator=(const Prim&) = default;
  Prim& operator=(Prim&&) = default;

  // ============================================================
  // Prim identity
  // ============================================================

  /// Get prim name
  const std::string& name() const { return name_; }

  /// Set prim name
  void set_name(const std::string& name) { name_ = name; }

  /// Get prim type name (e.g., "Mesh", "Xform", "Material")
  const std::string& type_name() const { return type_name_; }

  /// Set prim type name
  void set_type_name(const std::string& type_name) { type_name_ = type_name; }

  /// Get the specifier
  Specifier specifier() const { return specifier_; }
  void set_specifier(Specifier spec) { specifier_ = spec; }

  /// Check if prim is active
  bool is_active() const { return active_; }
  void set_active(bool active) { active_ = active; }

  // ============================================================
  // Attributes
  // ============================================================

  /// Get all attributes
  const std::unordered_map<std::string, Attribute>& attributes() const { return attributes_; }

  /// Check if attribute exists
  bool has_attribute(const std::string& name) const;

  /// Get attribute by name (returns nullptr if not found)
  const Attribute* get_attribute(const std::string& name) const;
  Attribute* get_attribute(const std::string& name);

  /// Add or set an attribute
  void set_attribute(const std::string& name, Attribute attr);
  void set_attribute(Attribute attr);  // Uses attr.name()

  /// Remove an attribute
  bool remove_attribute(const std::string& name);

  /// Get attribute names
  std::vector<std::string> attribute_names() const;

  // ============================================================
  // Relationships
  // ============================================================

  /// Add a relationship target
  void add_relationship(const std::string& name, const Path& target);

  /// Get relationship targets
  const std::vector<Path>* get_relationship(const std::string& name) const;

  /// Get all relationship names
  std::vector<std::string> relationship_names() const;

  // ============================================================
  // Hierarchy
  // ============================================================

  /// Get children
  const std::vector<Prim>& children() const { return children_; }
  std::vector<Prim>& children() { return children_; }

  /// Add a child prim
  void add_child(Prim child);

  /// Find child by name (returns nullptr if not found)
  const Prim* find_child(const std::string& name) const;
  Prim* find_child(const std::string& name);

  /// Get child count
  size_t child_count() const { return children_.size(); }

  // ============================================================
  // Metadata
  // ============================================================

  /// Get metadata value
  const Value* get_metadata(const std::string& key) const;

  /// Set metadata value
  void set_metadata(const std::string& key, Value value);

  /// Check if metadata exists
  bool has_metadata(const std::string& key) const;

  /// Get all metadata keys
  std::vector<std::string> metadata_keys() const;

  // ============================================================
  // API schemas
  // ============================================================

  /// Get applied API schemas
  const std::vector<std::string>& api_schemas() const { return api_schemas_; }

  /// Add an API schema
  void add_api_schema(const std::string& schema_name);

  /// Check if API schema is applied
  bool has_api_schema(const std::string& schema_name) const;

private:
  std::string name_;
  std::string type_name_;
  Specifier specifier_ = Specifier::Def;
  bool active_ = true;

  std::unordered_map<std::string, Attribute> attributes_;
  std::unordered_map<std::string, std::vector<Path>> relationships_;
  std::vector<Prim> children_;
  std::unordered_map<std::string, Value> metadata_;
  std::vector<std::string> api_schemas_;
};

}  // namespace next
}  // namespace tinyusdz
