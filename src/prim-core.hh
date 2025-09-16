// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// Core Prim class and basic prim-related types
// This is the main Prim class definition with minimal dependencies

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "prim-forward-decl.hh"
#include "value-types.hh"  // Essential for value::Value
#include "path.hh"         // Essential for Path
#include "enum-types.hh"   // For Specifier, Purpose, etc.
#include "nonstd/optional.hpp"

namespace tinyusdz {

// Forward declarations for types defined elsewhere
class Property;
struct PrimMeta;

///
/// PrimNode represents a node in the scene graph hierarchy.
/// It provides parent-child relationships and traversal capabilities.
///
class PrimNode {
 public:
  PrimNode() = default;
  PrimNode(const int64_t parent, const Path &path) 
      : _parent(parent), _path(path) {}

  int64_t GetParent() const { return _parent; }
  const std::vector<size_t> &GetChildren() const { return _children; }

  ///
  /// Add a child node.
  /// @return false when `child_name` already exists in children.
  ///
  bool AddChildren(const std::string &child_name, size_t node_index);

  ///
  /// Get full path (e.g. `/muda/dora/bora`)
  ///
  const Path &GetPath() const { return _path; }

  ///
  /// Get local path element name
  ///
  std::string GetLocalPath() const { return _path.full_path_name(); }

 private:
  int64_t _parent{-1};  // -1 = root node
  std::vector<size_t> _children;
  std::unordered_set<std::string> _primChildren;
  Path _path;
};

///
/// PrimRange provides iteration over a prim hierarchy.
/// Supports depth-first traversal of prim trees.
///
class PrimRange {
 public:
  class iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = const Prim*;
    using difference_type = std::ptrdiff_t;
    using pointer = const Prim**;
    using reference = const Prim*&;

    iterator() = default;
    iterator(const Prim* prim, bool end = false);

    reference operator*() const { return _current; }
    pointer operator->() const { return &_current; }
    
    iterator& operator++();
    iterator operator++(int);
    
    bool operator==(const iterator& rhs) const;
    bool operator!=(const iterator& rhs) const;

   private:
    const Prim* _current{nullptr};
    std::vector<const Prim*> _stack;
  };

  PrimRange() = default;
  explicit PrimRange(const Prim* root) : _root(root) {}

  iterator begin() const { return iterator(_root); }
  iterator end() const { return iterator(_root, true); }

 private:
  const Prim* _root{nullptr};
};

///
/// Core Prim class representing a USD primitive.
/// A Prim is the fundamental scene description container in USD.
///
class Prim {
 public:
  // Constructors
  Prim() = default;
  Prim(const value::Value &rhs);
  Prim(value::Value &&rhs);
  Prim(const std::string &elementName, const value::Value &rhs);
  Prim(const std::string &elementName, value::Value &&rhs);

  template <typename T>
  Prim(const T &prim) {
    set_primdata(prim);
  }

  template <typename T>
  Prim(const std::string &elementName, const T &prim) {
    set_primdata(elementName, prim);
  }

  // Prim data management
  template <typename T>
  void set_primdata(const T &prim) {
    static_assert((value::TypeId::TYPE_ID_MODEL_BEGIN <=
                   value::TypeTraits<T>::type_id()) &&
                      (value::TypeId::TYPE_ID_MODEL_END >
                       value::TypeTraits<T>::type_id()),
                  "T is not a Prim class type");
    _data = prim;
    _elementPath = Path(prim.name, "");
  }

  template <typename T>
  void set_primdata(const std::string &elementName, const T &prim) {
    static_assert((value::TypeId::TYPE_ID_MODEL_BEGIN <=
                   value::TypeTraits<T>::type_id()) &&
                      (value::TypeId::TYPE_ID_MODEL_END >
                       value::TypeTraits<T>::type_id()),
                  "T is not a Prim class type");
    _data = prim;
    SetPrimElementName(_data, elementName);
    _elementPath = Path(elementName, "");
  }

  // Child management
  bool add_child(Prim &&prim, bool rename_element_name = true,
                 std::string *err = nullptr);
  bool replace_child(const std::string &child_prim_name, Prim &&prim,
                     std::string *err = nullptr);
  
  std::vector<Prim> &children() { return _children; }
  const std::vector<Prim> &children() const { return _children; }

  // Element name and path
  const std::string element_name() const;
  const Path &element_path() const { return _elementPath; }
  Path &mutable_element_path() { return _elementPath; }

  // Type identification
  bool is_model() const;
  uint32_t prim_type_id() const;
  std::string prim_type_name() const;
  bool has_data() const { return _data.type_id() != value::TypeId::TYPE_ID_NULL; }

  // Data access
  const value::Value &data() const { return _data; }
  value::Value &data() { return _data; }

  // Property access
  bool has_property(const std::string &name) const;
  const Property* get_property(const std::string &name) const;
  Property* get_property(const std::string &name);
  
  // Metadata access
  const PrimMeta* get_prim_meta() const;
  PrimMeta* get_prim_meta();

  // Type-safe accessors
  template <typename T>
  const T* as() const {
    if (_data.type_id() == value::TypeTraits<T>::type_id()) {
      return _data.as<T>();
    }
    return nullptr;
  }

  template <typename T>
  T* as() {
    if (_data.type_id() == value::TypeTraits<T>::type_id()) {
      return _data.as<T>();
    }
    return nullptr;
  }

  // Visitor pattern support
  template <typename Visitor>
  void accept(Visitor &&visitor) const {
    visitor(*this);
    for (const auto &child : _children) {
      child.accept(std::forward<Visitor>(visitor));
    }
  }

 private:
  value::Value _data;
  Path _elementPath;
  std::vector<Prim> _children;
  
  // Private helper to set element name in value::Value
  void SetPrimElementName(value::Value &data, const std::string &name);
  
#if defined(TINYUSDZ_ENABLE_THREAD)
  mutable std::mutex _mutex;
#endif
};

///
/// Model is a concrete prim type representing a model hierarchy.
/// Models can contain other prims and define a model hierarchy.
///
struct Model {
  std::string name;
  Specifier spec{Specifier::Def};
  
  int64_t parent_id{-1};  // -1 = root
  std::vector<value::token> primChildren;
  std::map<std::string, Property> props;
  PrimMeta metas;
  
  // Model-specific metadata
  nonstd::optional<Kind> kind;
  nonstd::optional<Purpose> purpose;
  nonstd::optional<Visibility> visibility;
  nonstd::optional<value::AssetPath> assetInfo;
  
  // Type identification
  static constexpr uint32_t type_id() { 
    return value::TypeId::TYPE_ID_MODEL; 
  }
  static constexpr const char* type_name() { 
    return "Model"; 
  }
};

///
/// Xformable represents a prim with transformation capabilities.
/// It can have transform operations applied to it.
///
struct Xformable {
  std::string name;
  Specifier spec{Specifier::Def};
  
  int64_t parent_id{-1};
  std::vector<value::token> primChildren;
  std::map<std::string, Property> props;
  PrimMeta metas;
  
  // Transform-specific data
  std::vector<XformOp> xformOps;
  bool resetXformStack{false};
  
  static constexpr uint32_t type_id() { 
    return value::TypeId::TYPE_ID_XFORM; 
  }
  static constexpr const char* type_name() { 
    return "Xform"; 
  }
};

///
/// Klass represents a class primitive for defining reusable templates.
///
struct Klass {
  std::string name;
  Specifier spec{Specifier::Class};
  
  int64_t parent_id{-1};
  std::vector<value::token> primChildren;
  std::map<std::string, Property> props;
  PrimMeta metas;
  
  static constexpr uint32_t type_id() { 
    return value::TypeId::TYPE_ID_CLASS; 
  }
  static constexpr const char* type_name() { 
    return "class"; 
  }
};

// Utility functions
bool ValidatePrimElementName(const std::string &name);
std::string GetPrimElementName(const Prim &prim);
std::string GetPrimTypeName(const Prim &prim);

} // namespace tinyusdz